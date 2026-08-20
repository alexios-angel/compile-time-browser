#include <ctbrowser/gpu/device.hpp>

// device: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::gpu {

std::string sdl_gpu_backend::driver() const {
    return device_ == nullptr ? std::string{} : std::string{SDL_GetGPUDeviceDriver(device_)};
}

std::string sdl_gpu_backend::adapter() const {
    if (device_ == nullptr) { return {}; }
    const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(device_);
    const char * name = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "");
    return std::string{name};
}

bool sdl_gpu_backend::adapter_is_software() const {
    const std::string name = adapter();
    for (const std::string_view needle : {"llvmpipe", "lavapipe", "swiftshader", "software",
                                          "SwiftShader", "Microsoft Basic Render"}) {
        if (name.find(needle) != std::string::npos) { return true; }
    }
    return false;
}

std::expected<frame_token, gpu_error> sdl_gpu_backend::begin_frame() {
    if (in_frame_) { return std::unexpected(gpu_error::no_frame); }
    in_frame_ = true;
    return frame_token{++frame_};
}

std::expected<void, gpu_error> sdl_gpu_backend::reserve_tiles(std::span<const tile> tiles) {
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

bool sdl_gpu_backend::needs_raster(tile_id id) const {
    if (id.layer >= layers_.size()) { return true; }
    const auto it = layers_[id.layer].find(key_of(id));
    return it == layers_[id.layer].end() || !it->second.valid;
}

std::expected<void, gpu_error> sdl_gpu_backend::raster(tile_id id, const display_list & list) {
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

void sdl_gpu_backend::tile_ready(tile_id id) {
    if (id.layer >= layers_.size()) { return; }
    const auto it = layers_[id.layer].find(key_of(id));
    if (it == layers_[id.layer].end() || it->second.uploaded) { return; }
    upload(it->second);
}

std::expected<void, gpu_error> sdl_gpu_backend::composite(std::span<const layer> layers) {
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

std::expected<void, gpu_error> sdl_gpu_backend::end_frame() {
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

std::expected<surface, gpu_error> sdl_gpu_backend::read_target() {
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

void sdl_gpu_backend::resize(int width, int height) {
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

void sdl_gpu_backend::discard_layer(std::uint32_t layer) {
    if (layer >= layers_.size()) { return; }
    for (auto & [key, s] : layers_[layer]) {
        if (s.texture != nullptr) { SDL_ReleaseGPUTexture(device_, s.texture); }
    }
    layers_[layer].clear();
}

void sdl_gpu_backend::discard() {
    for (auto & per_layer : layers_) {
        for (auto & [key, s] : per_layer) {
            if (s.texture != nullptr) { SDL_ReleaseGPUTexture(device_, s.texture); }
        }
    }
    layers_.clear();
}

constexpr std::uint64_t sdl_gpu_backend::key_of(tile_id id) noexcept {
    return (static_cast<std::uint64_t>(id.column) << 32) | id.row;
}

constexpr float sdl_gpu_backend::channel(std::uint8_t v) noexcept {
    return static_cast<float>(v) / 255.0f;
}

bool sdl_gpu_backend::offscreen(float x, float y) const noexcept {
    return x + static_cast<float>(extent_) <= 0 || y + static_cast<float>(extent_) <= 0 ||
           x >= static_cast<float>(width_) || y >= static_cast<float>(height_);
}

SDL_GPUTexture * sdl_gpu_backend::create_tile_texture() {
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

bool sdl_gpu_backend::build_target() {
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

bool sdl_gpu_backend::build_pipeline() {
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

void sdl_gpu_backend::upload(slot & s) {
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

void sdl_gpu_backend::steal(sdl_gpu_backend && other) noexcept {
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

void sdl_gpu_backend::release() {
    if (device_ == nullptr) { return; }
    discard();
    if (sampler_ != nullptr) { SDL_ReleaseGPUSampler(device_, sampler_); }
    if (pipeline_ != nullptr) { SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_); }
    if (target_ != nullptr) { SDL_ReleaseGPUTexture(device_, target_); }
    if (window_ != nullptr) { SDL_ReleaseWindowFromGPUDevice(device_, window_); }
    SDL_DestroyGPUDevice(device_);
    device_ = nullptr;
}

} // namespace ctbrowser::gpu
