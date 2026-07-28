module;
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include "shaders/tile_spv.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <vector>

export module ctbrowser.gpu:device;

import ctbrowser.core;
import ctbrowser.paint;
import ctbrowser.raster;

// The SDL3 GPU backend: hardware-accelerated COMPOSITION.
//
// What is and is not on the GPU here matters, so it is worth stating plainly.
// Rasterization stays on the CPU: display lists are drawn into tile pixels by
// the same code the software backend uses, on the raster pool. The GPU's job is
// to take those tiles and put them on screen at the layer's current offset,
// which is a textured quad per tile and nothing else.
//
// That split is not a shortcut - it is what Chrome's software-raster path does,
// and it is where the win actually is. A scroll does not re-raster anything, so
// the per-frame work is a few dozen quads; moving raster itself onto the GPU
// changes the FIRST paint, not the scroll, and needs a shader per paint
// operation rather than one.
//
// PORTABILITY. The shaders ship as SPIR-V, so this runs on SDL's Vulkan driver.
// D3D12 wants DXIL and Metal wants MSL; both need their own compiler in
// tools/gen-shaders.py and belong with the Windows and macOS platform work.
// SDL_GPUSupportsShaderFormats is checked at construction rather than assumed.

export namespace ctbrowser::gpu {

using ctbrowser::color;
using ctbrowser::rect;
using ctbrowser::paint::display_list;
using ctbrowser::paint::layer;
using ctbrowser::raster::default_tile_extent;
using ctbrowser::raster::frame_token;
using ctbrowser::raster::gpu_error;
using ctbrowser::raster::surface;
using ctbrowser::raster::tile;
using ctbrowser::raster::tile_id;

// Why a device could not be created. Distinct from gpu_error because this is a
// startup question with a fallback - the caller drops to the software backend -
// rather than a frame that went wrong.
enum class device_error : std::uint8_t {
	no_sdl_video,
	no_supported_driver,
	device_creation_failed,
	pipeline_creation_failed,
	window_claim_failed,
};

[[nodiscard]] inline std::string_view describe(device_error e) noexcept {
	switch (e) {
	case device_error::no_sdl_video: return "SDL video subsystem is not initialised";
	case device_error::no_supported_driver: return "no SDL_GPU driver supports SPIR-V here";
	case device_error::device_creation_failed: return "SDL_CreateGPUDevice failed";
	case device_error::pipeline_creation_failed: return "the tile pipeline could not be built";
	case device_error::window_claim_failed: return "SDL_ClaimWindowForGPUDevice failed";
	}
	return "unknown";
}

class sdl_gpu_backend {
public:
	static constexpr bool is_hardware = true;

	// Offscreen when `window` is null - which is how the tests run it, and what
	// makes the result downloadable and comparable against the software backend.
	[[nodiscard]] static std::expected<sdl_gpu_backend, device_error> create(
	    int width, int height, SDL_Window * window = nullptr, int extent = default_tile_extent) {
		if (!SDL_WasInit(SDL_INIT_VIDEO)) { return std::unexpected(device_error::no_sdl_video); }
		if (!SDL_GPUSupportsShaderFormats(SDL_GPU_SHADERFORMAT_SPIRV, nullptr)) {
			return std::unexpected(device_error::no_supported_driver);
		}
		SDL_GPUDevice * device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
		if (device == nullptr) { return std::unexpected(device_error::device_creation_failed); }

		sdl_gpu_backend out{device, width, height, window, extent};
		if (window != nullptr && !SDL_ClaimWindowForGPUDevice(device, window)) {
			return std::unexpected(device_error::window_claim_failed);
		}
		if (!out.build_pipeline()) {
			return std::unexpected(device_error::pipeline_creation_failed);
		}
		if (!out.build_target()) { return std::unexpected(device_error::device_creation_failed); }
		return out;
	}

	sdl_gpu_backend(sdl_gpu_backend && other) noexcept { steal(std::move(other)); }
	sdl_gpu_backend & operator=(sdl_gpu_backend && other) noexcept {
		if (this != &other) {
			release();
			steal(std::move(other));
		}
		return *this;
	}
	sdl_gpu_backend(const sdl_gpu_backend &) = delete;
	sdl_gpu_backend & operator=(const sdl_gpu_backend &) = delete;
	~sdl_gpu_backend() { release(); }

	color clear_color = color::rgba(255, 255, 255);
	// Null means font8x8. Set through renderer::set_fonts by whoever owns the
	// faces - the browser - because a glyph cache outlives any one frame.
	const raster::font_backend * fonts = nullptr;

	// The SDL backend name: "vulkan", "direct3d12", "metal".
	[[nodiscard]] std::string driver() const {
		return device_ == nullptr ? std::string{} : std::string{SDL_GetGPUDeviceDriver(device_)};
	}

	// The ADAPTER, which is the one that actually answers "is this hardware".
	// SDL's driver name says which API is in use and nothing about what is
	// behind it: a Vulkan device may be a real GPU or a CPU implementation such
	// as lavapipe, and those differ by orders of magnitude.
	[[nodiscard]] std::string adapter() const {
		if (device_ == nullptr) { return {}; }
		const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(device_);
		const char * name = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "");
		return std::string{name};
	}

	// Whether the "GPU" is really a software implementation of the graphics API.
	//
	// This matters enough to detect rather than assume. Under WSL2 without GPU
	// passthrough, and in most containers and CI images, the only Vulkan driver
	// present is lavapipe - so the GPU path RUNS, and every correctness test
	// passes, while the performance is a CPU's. A benchmark that reported that
	// as a GPU number would be worse than no benchmark.
	[[nodiscard]] bool adapter_is_software() const {
		const std::string name = adapter();
		for (const std::string_view needle : {"llvmpipe", "lavapipe", "swiftshader", "software",
		                                      "SwiftShader", "Microsoft Basic Render"}) {
			if (name.find(needle) != std::string::npos) { return true; }
		}
		return false;
	}

	// --- RasterBackend ---------------------------------------------------

	[[nodiscard]] std::expected<frame_token, gpu_error> begin_frame() {
		if (in_frame_) { return std::unexpected(gpu_error::no_frame); }
		in_frame_ = true;
		return frame_token{++frame_};
	}

	[[nodiscard]] std::expected<void, gpu_error> reserve_tiles(std::span<const tile> tiles) {
		if (!in_frame_) { return std::unexpected(gpu_error::no_frame); }
		for (const tile & t : tiles) {
			if (t.id.layer >= layers_.size()) { layers_.resize(t.id.layer + 1); }
			slot & s = layers_[t.id.layer][key_of(t.id)];
			if (s.texture == nullptr) {
				s.texture = create_tile_texture();
				if (s.texture == nullptr) { return std::unexpected(gpu_error::out_of_memory); }
			}
			if (s.staging.width() != extent_ || !(s.area == t.area)) {
				s.staging = surface{extent_, extent_};
				s.area = t.area;
				s.valid = false;
			}
		}
		return {};
	}

	[[nodiscard]] bool needs_raster(tile_id id) const {
		if (id.layer >= layers_.size()) { return true; }
		const auto it = layers_[id.layer].find(key_of(id));
		return it == layers_[id.layer].end() || !it->second.valid;
	}

	// RASTER POOL. CPU only - no device call here, by design and by necessity:
	// this runs on many threads and the device may be used from one.
	[[nodiscard]] std::expected<void, gpu_error> raster(tile_id id, const display_list & list) {
		if (!in_frame_) { return std::unexpected(gpu_error::no_frame); }
		if (id.layer >= layers_.size()) { return std::unexpected(gpu_error::bad_tile); }
		const auto it = layers_[id.layer].find(key_of(id));
		if (it == layers_[id.layer].end()) { return std::unexpected(gpu_error::bad_tile); }
		slot & s = it->second;
		raster::draw_into(s.staging, list, s.area, fonts);
		s.valid = true;
		s.uploaded = false;
		return {};
	}

	// COMPOSITOR THREAD. The tile has finished rastering, so its pixels can go
	// to the device now rather than in one burst at composite time - which is
	// the point of being told about it at all.
	void tile_ready(tile_id id) {
		if (id.layer >= layers_.size()) { return; }
		const auto it = layers_[id.layer].find(key_of(id));
		if (it == layers_[id.layer].end() || it->second.uploaded) { return; }
		upload(it->second);
	}

	[[nodiscard]] std::expected<void, gpu_error> composite(std::span<const layer> layers) {
		if (!in_frame_) { return std::unexpected(gpu_error::no_frame); }
		SDL_GPUCommandBuffer * cmd = SDL_AcquireGPUCommandBuffer(device_);
		if (cmd == nullptr) { return std::unexpected(gpu_error::device_lost); }

		// Anything tile_ready did not get to - a tile still valid from an
		// earlier frame, or one whose upload was skipped - is caught here.
		for (auto & per_layer : layers_) {
			for (auto & [key, s] : per_layer) {
				if (s.valid && !s.uploaded) { upload(s); }
			}
		}

		SDL_GPUTexture * target = target_;
		if (window_ != nullptr) {
			if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &target, nullptr, nullptr) ||
			    target == nullptr) {
				SDL_CancelGPUCommandBuffer(cmd);
				return std::unexpected(gpu_error::device_lost);
			}
		}

		SDL_GPUColorTargetInfo info{};
		info.texture = target;
		info.clear_color = SDL_FColor{channel(clear_color.red()), channel(clear_color.green()),
		                              channel(clear_color.blue()), channel(clear_color.alpha())};
		info.load_op = SDL_GPU_LOADOP_CLEAR;
		info.store_op = SDL_GPU_STOREOP_STORE;

		SDL_GPURenderPass * pass = SDL_BeginGPURenderPass(cmd, &info, 1, nullptr);
		if (pass == nullptr) {
			SDL_CancelGPUCommandBuffer(cmd);
			return std::unexpected(gpu_error::device_lost);
		}
		SDL_BindGPUGraphicsPipeline(pass, pipeline_);

		// Layers composite back to front. Order within a layer does not matter:
		// a layer's tiles never overlap.
		for (std::size_t li = 0; li < layers.size() && li < layers_.size(); ++li) {
			const layer & l = layers[li];
			for (const auto & [key, s] : layers_[li]) {
				if (!s.valid || !s.uploaded) { continue; }
				const float at_x = s.area.x + l.offset.x;
				const float at_y = s.area.y + l.offset.y;
				if (offscreen(at_x, at_y)) { continue; }
				draw_tile(cmd, pass, s, at_x, at_y);
			}
		}
		SDL_EndGPURenderPass(pass);
		pending_ = cmd;
		return {};
	}

	[[nodiscard]] std::expected<void, gpu_error> end_frame() {
		if (!in_frame_) { return std::unexpected(gpu_error::no_frame); }
		in_frame_ = false;
		if (pending_ != nullptr) {
			SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(pending_);
			pending_ = nullptr;
			if (fence == nullptr) { return std::unexpected(gpu_error::device_lost); }
			SDL_WaitForGPUFences(device_, true, &fence, 1);
			SDL_ReleaseGPUFence(device_, fence);
		}
		return {};
	}

	// --- readback, so the GPU result can be checked against the software one --

	[[nodiscard]] std::expected<surface, gpu_error> read_target() {
		if (window_ != nullptr) { return std::unexpected(gpu_error::unsupported); }
		const std::size_t bytes =
		    static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4;
		SDL_GPUTransferBufferCreateInfo tb{};
		tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
		tb.size = static_cast<Uint32>(bytes);
		SDL_GPUTransferBuffer * buffer = SDL_CreateGPUTransferBuffer(device_, &tb);
		if (buffer == nullptr) { return std::unexpected(gpu_error::out_of_memory); }

		SDL_GPUCommandBuffer * cmd = SDL_AcquireGPUCommandBuffer(device_);
		SDL_GPUCopyPass * pass = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTextureRegion region{};
		region.texture = target_;
		region.w = static_cast<Uint32>(width_);
		region.h = static_cast<Uint32>(height_);
		region.d = 1;
		SDL_GPUTextureTransferInfo dst{};
		dst.transfer_buffer = buffer;
		dst.pixels_per_row = static_cast<Uint32>(width_);
		dst.rows_per_layer = static_cast<Uint32>(height_);
		SDL_DownloadFromGPUTexture(pass, &region, &dst);
		SDL_EndGPUCopyPass(pass);
		SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
		SDL_WaitForGPUFences(device_, true, &fence, 1);
		SDL_ReleaseGPUFence(device_, fence);

		void * mapped = SDL_MapGPUTransferBuffer(device_, buffer, false);
		if (mapped == nullptr) {
			SDL_ReleaseGPUTransferBuffer(device_, buffer);
			return std::unexpected(gpu_error::device_lost);
		}
		surface out{width_, height_};
		for (int y = 0; y < height_; ++y) {
			std::memcpy(out.row(y).data(),
			            static_cast<const std::byte *>(mapped) +
			                static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) * 4,
			            static_cast<std::size_t>(width_) * 4);
		}
		SDL_UnmapGPUTransferBuffer(device_, buffer);
		SDL_ReleaseGPUTransferBuffer(device_, buffer);
		return out;
	}

	// See the software backend's resize for why this exists rather than the
	// caller building a new renderer.
	void resize(int width, int height) {
		if (width == width_ && height == height_) { return; }
		width_ = width;
		height_ = height;
		discard();
		if (target_ != nullptr) {
			SDL_ReleaseGPUTexture(device_, target_);
			target_ = nullptr;
		}
		// A windowed backend renders to the swapchain, which SDL resizes for us;
		// only the offscreen target is ours to rebuild.
		if (window_ == nullptr) { (void)build_target(); }
	}

	// ONE layer's tiles - see the note on the software backend.
	void discard_layer(std::uint32_t layer) {
		if (layer >= layers_.size()) { return; }
		for (auto & [key, s] : layers_[layer]) {
			if (s.texture != nullptr) { SDL_ReleaseGPUTexture(device_, s.texture); }
		}
		layers_[layer].clear();
	}

	void discard() {
		for (auto & per_layer : layers_) {
			for (auto & [key, s] : per_layer) {
				if (s.texture != nullptr) { SDL_ReleaseGPUTexture(device_, s.texture); }
			}
		}
		layers_.clear();
	}

private:
	struct slot {
		rect area;
		surface staging;
		SDL_GPUTexture * texture = nullptr;
		bool valid = false;
		bool uploaded = false;
	};

	sdl_gpu_backend(SDL_GPUDevice * device, int width, int height, SDL_Window * window, int extent)
	    : device_(device), window_(window), width_(width), height_(height), extent_(extent) {}

	[[nodiscard]] static constexpr std::uint64_t key_of(tile_id id) noexcept {
		return (static_cast<std::uint64_t>(id.column) << 32) | id.row;
	}
	[[nodiscard]] static constexpr float channel(std::uint8_t v) noexcept {
		return static_cast<float>(v) / 255.0f;
	}
	[[nodiscard]] bool offscreen(float x, float y) const noexcept {
		return x + static_cast<float>(extent_) <= 0 || y + static_cast<float>(extent_) <= 0 ||
		       x >= static_cast<float>(width_) || y >= static_cast<float>(height_);
	}

	[[nodiscard]] SDL_GPUTexture * create_tile_texture() {
		SDL_GPUTextureCreateInfo info{};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = static_cast<Uint32>(extent_);
		info.height = static_cast<Uint32>(extent_);
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		return SDL_CreateGPUTexture(device_, &info);
	}

	bool build_target() {
		if (window_ != nullptr) { return true; }
		SDL_GPUTextureCreateInfo info{};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
		info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = static_cast<Uint32>(width_);
		info.height = static_cast<Uint32>(height_);
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		target_ = SDL_CreateGPUTexture(device_, &info);
		return target_ != nullptr;
	}

	bool build_pipeline() {
		SDL_GPUShaderCreateInfo vs{};
		vs.code = shaders::tile_vert_spv;
		vs.code_size = shaders::tile_vert_spv_size;
		vs.entrypoint = "main";
		vs.format = SDL_GPU_SHADERFORMAT_SPIRV;
		vs.stage = SDL_GPU_SHADERSTAGE_VERTEX;
		vs.num_uniform_buffers = 1;
		SDL_GPUShader * vertex = SDL_CreateGPUShader(device_, &vs);

		SDL_GPUShaderCreateInfo fs{};
		fs.code = shaders::tile_frag_spv;
		fs.code_size = shaders::tile_frag_spv_size;
		fs.entrypoint = "main";
		fs.format = SDL_GPU_SHADERFORMAT_SPIRV;
		fs.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
		fs.num_samplers = 1;
		SDL_GPUShader * fragment = SDL_CreateGPUShader(device_, &fs);
		if (vertex == nullptr || fragment == nullptr) { return false; }

		SDL_GPUColorTargetBlendState blend{};
		blend.enable_blend = true;
		// Source-over with STRAIGHT alpha, matching the software backend's
		// blend_over exactly. Premultiplied would need the rasterizer to
		// premultiply too, and then the two backends would disagree.
		blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
		blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
		blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

		SDL_GPUColorTargetDescription target{};
		target.format = window_ != nullptr ? SDL_GetGPUSwapchainTextureFormat(device_, window_)
		                                   : SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
		target.blend_state = blend;

		SDL_GPUGraphicsPipelineCreateInfo info{};
		info.vertex_shader = vertex;
		info.fragment_shader = fragment;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.target_info.num_color_targets = 1;
		info.target_info.color_target_descriptions = &target;
		pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &info);

		// The pipeline holds its own references; ours are done.
		SDL_ReleaseGPUShader(device_, vertex);
		SDL_ReleaseGPUShader(device_, fragment);

		SDL_GPUSamplerCreateInfo sampler{};
		// NEAREST, and it is not a preference: a tile is blitted at exactly 1:1,
		// so any filtering can only introduce error - and would put the GPU
		// image a fraction off the software one for no benefit.
		sampler.min_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
		sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler_ = SDL_CreateGPUSampler(device_, &sampler);
		return pipeline_ != nullptr && sampler_ != nullptr;
	}

	void upload(slot & s) {
		const std::size_t bytes =
		    static_cast<std::size_t>(extent_) * static_cast<std::size_t>(extent_) * 4;
		SDL_GPUTransferBufferCreateInfo tb{};
		tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		tb.size = static_cast<Uint32>(bytes);
		SDL_GPUTransferBuffer * buffer = SDL_CreateGPUTransferBuffer(device_, &tb);
		if (buffer == nullptr) { return; }
		void * mapped = SDL_MapGPUTransferBuffer(device_, buffer, false);
		if (mapped != nullptr) {
			std::memcpy(mapped, s.staging.pixels().data(), bytes);
			SDL_UnmapGPUTransferBuffer(device_, buffer);

			SDL_GPUCommandBuffer * cmd = SDL_AcquireGPUCommandBuffer(device_);
			SDL_GPUCopyPass * pass = SDL_BeginGPUCopyPass(cmd);
			SDL_GPUTextureTransferInfo src{};
			src.transfer_buffer = buffer;
			src.pixels_per_row = static_cast<Uint32>(extent_);
			src.rows_per_layer = static_cast<Uint32>(extent_);
			SDL_GPUTextureRegion dst{};
			dst.texture = s.texture;
			dst.w = static_cast<Uint32>(extent_);
			dst.h = static_cast<Uint32>(extent_);
			dst.d = 1;
			SDL_UploadToGPUTexture(pass, &src, &dst, false);
			SDL_EndGPUCopyPass(pass);
			SDL_SubmitGPUCommandBuffer(cmd);
			s.uploaded = true;
		}
		SDL_ReleaseGPUTransferBuffer(device_, buffer);
	}

	void draw_tile(SDL_GPUCommandBuffer * cmd, SDL_GPURenderPass * pass, const slot & s, float x,
	               float y) {
		// Pixels to normalised device coordinates. y flips because NDC is
		// bottom-up and every coordinate above this point is top-down.
		const float w = static_cast<float>(width_);
		const float h = static_cast<float>(height_);
		const float e = static_cast<float>(extent_);
		const float quad[4] = {x / w * 2.0f - 1.0f, 1.0f - (y + e) / h * 2.0f, e / w * 2.0f,
		                       e / h * 2.0f};
		SDL_PushGPUVertexUniformData(cmd, 0, quad, sizeof quad);

		SDL_GPUTextureSamplerBinding binding{};
		binding.texture = s.texture;
		binding.sampler = sampler_;
		SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
		SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
	}

	void steal(sdl_gpu_backend && other) noexcept {
		device_ = other.device_;
		window_ = other.window_;
		target_ = other.target_;
		pipeline_ = other.pipeline_;
		sampler_ = other.sampler_;
		layers_ = std::move(other.layers_);
		clear_color = other.clear_color;
		width_ = other.width_;
		height_ = other.height_;
		extent_ = other.extent_;
		frame_ = other.frame_;
		other.device_ = nullptr;
		other.window_ = nullptr;
		other.target_ = nullptr;
		other.pipeline_ = nullptr;
		other.sampler_ = nullptr;
	}

	void release() {
		if (device_ == nullptr) { return; }
		discard();
		if (sampler_ != nullptr) { SDL_ReleaseGPUSampler(device_, sampler_); }
		if (pipeline_ != nullptr) { SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_); }
		if (target_ != nullptr) { SDL_ReleaseGPUTexture(device_, target_); }
		if (window_ != nullptr) { SDL_ReleaseWindowFromGPUDevice(device_, window_); }
		SDL_DestroyGPUDevice(device_);
		device_ = nullptr;
	}

	SDL_GPUDevice * device_ = nullptr;
	SDL_Window * window_ = nullptr;
	SDL_GPUTexture * target_ = nullptr;
	SDL_GPUGraphicsPipeline * pipeline_ = nullptr;
	SDL_GPUSampler * sampler_ = nullptr;
	SDL_GPUCommandBuffer * pending_ = nullptr;
	std::vector<flat_map<std::uint64_t, slot>> layers_;
	int width_ = 0;
	int height_ = 0;
	int extent_ = default_tile_extent;
	std::uint64_t frame_ = 0;
	bool in_frame_ = false;
};

static_assert(ctbrowser::raster::RasterBackend<sdl_gpu_backend>);

} // namespace ctbrowser::gpu
