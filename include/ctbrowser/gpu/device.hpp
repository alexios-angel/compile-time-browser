#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <ctbrowser/gpu/shaders/tile_spv.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>

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

namespace ctbrowser::gpu {

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
    [[nodiscard]] std::string driver() const;

    // The ADAPTER, which is the one that actually answers "is this hardware".
    // SDL's driver name says which API is in use and nothing about what is
    // behind it: a Vulkan device may be a real GPU or a CPU implementation such
    // as lavapipe, and those differ by orders of magnitude.
    [[nodiscard]] std::string adapter() const;

    // Whether the "GPU" is really a software implementation of the graphics API.
    //
    // This matters enough to detect rather than assume. Under WSL2 without GPU
    // passthrough, and in most containers and CI images, the only Vulkan driver
    // present is lavapipe - so the GPU path RUNS, and every correctness test
    // passes, while the performance is a CPU's. A benchmark that reported that
    // as a GPU number would be worse than no benchmark.
    [[nodiscard]] bool adapter_is_software() const;

    // --- RasterBackend ---------------------------------------------------

    [[nodiscard]] std::expected<frame_token, gpu_error> begin_frame();

    [[nodiscard]] std::expected<void, gpu_error> reserve_tiles(std::span<const tile> tiles);

    [[nodiscard]] bool needs_raster(tile_id id) const;

    // RASTER POOL. CPU only - no device call here, by design and by necessity:
    // this runs on many threads and the device may be used from one.
    [[nodiscard]] std::expected<void, gpu_error> raster(tile_id id, const display_list & list);

    // COMPOSITOR THREAD. The tile has finished rastering, so its pixels can go
    // to the device now rather than in one burst at composite time - which is
    // the point of being told about it at all.
    void tile_ready(tile_id id);

    [[nodiscard]] std::expected<void, gpu_error> composite(std::span<const layer> layers);

    [[nodiscard]] std::expected<void, gpu_error> end_frame();

    // --- readback, so the GPU result can be checked against the software one --

    [[nodiscard]] std::expected<surface, gpu_error> read_target();

    // See the software backend's resize for why this exists rather than the
    // caller building a new renderer.
    void resize(int width, int height);

    // ONE layer's tiles - see the note on the software backend.
    void discard_layer(std::uint32_t layer);

    void discard();

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

    [[nodiscard]] static constexpr std::uint64_t key_of(tile_id id) noexcept;
    [[nodiscard]] static constexpr float channel(std::uint8_t v) noexcept;
    [[nodiscard]] bool offscreen(float x, float y) const noexcept;

    [[nodiscard]] SDL_GPUTexture * create_tile_texture();

    bool build_target();

    bool build_pipeline();

    void upload(slot & s);

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

    void steal(sdl_gpu_backend && other) noexcept;

    void release();

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
