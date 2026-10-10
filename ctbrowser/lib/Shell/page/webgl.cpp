//===- ctbrowser/lib/Shell/page/webgl.cpp - WebGL context over GL device --===//
//
// webgl_context translates WebGL calls from the bindings into calls on the GL
// device (raster::gl::device), validating arguments the way WebGL specifies,
// keeping WebGL's first-error-wins error flag, and copying the rendered
// surface into the canvas bitmap once per frame.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/page/webgl.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace ctbrowser::shell {

namespace {

// GL error codes reported through fail(). They duplicate gl_enum's no_error,
// invalid_value and out_of_memory in webgl.hpp. Other constants a page passes
// stay plain integers all the way down: they arrive from JavaScript as numbers.
constexpr std::uint32_t gl_no_error = 0;
constexpr std::uint32_t gl_invalid_value = 0x0501;
constexpr std::uint32_t gl_out_of_memory = 0x0505;

} // namespace

webgl_context::webgl_context(paint::bitmap * surface, int width, int height)
    : device_{width, height}, surface_{surface} {}

void webgl_context::resize(paint::bitmap * surface, int width, int height) {
    surface_ = surface;
    if (width == device_.width() && height == device_.height()) { return; }
    // A canvas resize reallocates the drawing buffer only. It is not a context
    // loss: programs, buffers, textures and pipeline state must survive (p5 sets
    // DEPTH_TEST before resizing its canvas, for example).
    device_.resize(width, height);
}

paint::bitmap * webgl_context::surface() {
    return surface_;
}

void webgl_context::present() {
    // Copies the surface once per frame, not once per draw: a copy per mesh
    // would mean hundreds of full-surface readbacks for one Babylon frame.
    if (!device_.ok()) { return; }
    if (surface_ == nullptr) { return; }
    // A borrowed bitmap keeps its size. read_pixels re-makes a bitmap of the
    // wrong size, and this one is the canvas's: a 2D clip mask or the painter
    // sized against it would then index past its end.
    if (surface_->width != device_.width() || surface_->height != device_.height()) { return; }
    (void)device_.read_pixels(*surface_);
}

void webgl_context::set_version(int version) {
    version_ = version;
}

int webgl_context::version() const {
    return version_;
}

void webgl_context::viewport(int x, int y, int width, int height) {
    if (width < 0 || height < 0) {
        fail(gl_invalid_value);
        return;
    }
    device_.viewport(x, y, width, height);
}

void webgl_context::scissor(int x, int y, int width, int height) {
    if (width < 0 || height < 0) {
        fail(gl_invalid_value);
        return;
    }
    device_.scissor(x, y, width, height);
}

// --- shaders and programs ---------------------------------------------------
//
// Each of these is a translation and a return. Nothing is remembered: which
// program is linked, what a shader's source was and what a program declares are
// all questions GL answers, and a cached answer is the thing that drifts.

void webgl_context::shader_source(std::uint32_t shader, std::string_view source) {
    // The page's shader source is passed through unchanged; browsers prepend
    // nothing. (A `#version 300 es` repair once lived here; it only masked a
    // missing TEXTURE_BINDING_3D constant and fired zero times once that was
    // fixed - see docs/history/webgl2.md.)
    if (std::getenv("CTBROWSER_GL_SRC") != nullptr) {
        // Logs what actually reached the compiler. A body that arrives empty
        // or truncated fails in a way that looks like a draw problem. The
        // first line is printed too: that is where a `#version` has to be, and
        // where a body made only of `#define`s is visible at a glance.
        const std::size_t line = std::min(source.find('\n'), source.size());
        const std::string first{source.substr(0, std::min<std::size_t>(line, 60))};
        std::fprintf(stderr, "[src] shader=%u bytes=%zu page_version=%d | %s\n", shader,
                     source.size(), source.find("#version") != std::string_view::npos ? 1 : 0,
                     first.c_str());
    }
    device_.shader_source(shader, std::string{source});
}

void webgl_context::compile_shader(std::uint32_t shader) {
    device_.compile_shader(shader);
    // Keep the compile log for shader_error(). A shader that fails to compile
    // draws nothing and raises no GL error, so when the page does not log the
    // failure itself (Babylon does), this is the only record of the reason.
    if (!device_.shader_compiled(shader)) { shader_error_ = device_.shader_log(shader); }
}

void webgl_context::link_program(std::uint32_t program) {
    device_.link_program(program);
    if (!device_.program_linked(program)) { shader_error_ = device_.program_log(program); }
}

namespace {

[[nodiscard]] std::vector<active_variable> as_variables(
    const std::vector<raster::gl::device::active> & from) {
    std::vector<active_variable> out;
    out.reserve(from.size());
    for (const auto & each : from) { out.push_back({each.name, each.type, each.size}); }
    return out;
}

} // namespace

std::vector<active_variable> webgl_context::active_attributes(std::uint32_t program) const {
    return as_variables(device_.active_attributes(program));
}

std::vector<active_variable> webgl_context::active_uniforms(std::uint32_t program) const {
    return as_variables(device_.active_uniforms(program));
}

int webgl_context::get_uniform_block_index(std::uint32_t program, std::string_view name) const {
    const int index = device_.uniform_block_index(program, std::string{name});
    // `CTBROWSER_GL_UBO=1` prints the block index, binding and buffer together
    // so they can be checked against each other. -1 is normal for a block the
    // shader never reads (GL drops it and answers GL_INVALID_INDEX; Babylon's
    // `Mesh` block does this).
    if (std::getenv("CTBROWSER_GL_UBO") != nullptr) {
        std::fprintf(stderr, "[ubo] index program=%u name=%s -> %d\n", program,
                     std::string{name}.c_str(), index);
    }
    return index;
}

void webgl_context::uniform_block_binding(std::uint32_t program, std::uint32_t index,
                                          std::uint32_t binding) {
    if (std::getenv("CTBROWSER_GL_UBO") != nullptr) {
        std::fprintf(stderr, "[ubo] bind  program=%u index=%u -> binding=%u\n", program, index,
                     binding);
    }
    device_.uniform_block_binding(program, index, binding);
}

void webgl_context::set_uniform(std::string_view name, const uniform_value & value) {
    // The location is looked up here, from the program GL says is in use. A
    // page holds an opaque location object that carries the name, so there is
    // nothing to cache and nothing to invalidate when it relinks.
    const unsigned program = device_.program_in_use();
    if (program == 0 || value.data.empty()) { return; }
    const int location = device_.uniform_location(program, std::string{name});
    if (location < 0) { return; } // A uniform the linker dropped is not an error.
    const int per = value.rows * value.cols;
    const int count = per > 0 ? static_cast<int>(value.data.size()) / per : 0;
    device_.set_uniform(location, value.data.data(), count, value.rows, value.cols, value.integer);
}

// --- buffers, attributes and vertex arrays ----------------------------------

void webgl_context::buffer_data(std::uint32_t target, std::span<const std::byte> bytes,
                                std::uint32_t usage) {
    note_storage(target, bytes.size());
    device_.buffer_data(static_cast<int>(target), bytes.data(), bytes.size(),
                        static_cast<int>(usage));
}

void webgl_context::note_storage(std::uint32_t target, std::size_t size) {
    // Logs which buffer got storage, and when. Printed beside the `[ubo] base`
    // lines so the order of the two is readable in one run: a binding point with a
    // buffer bound and no storage behind it is the shape that leaves ANGLE a
    // null BufferHelper to dereference at draw time.
    if (std::getenv("CTBROWSER_GL_UBO") == nullptr) { return; }
    std::fprintf(stderr, "[ubo] data  target=%u buffer=%u bytes=%zu\n", target,
                 device_.bound_buffer(static_cast<int>(target)), size);
}

void webgl_context::buffer_data(std::uint32_t target, int size, std::uint32_t usage) {
    // Size without data, which is `gl.bufferData(target, 1024, usage)` - a page
    // reserving room it will fill with bufferSubData.
    if (size < 0) {
        fail(gl_invalid_value);
        return;
    }
    if (size > (1 << 30)) { // Over 1 GiB: OUT_OF_MEMORY, never the allocator.
        fail(gl_out_of_memory);
        return;
    }
    note_storage(target, static_cast<std::size_t>(size));
    device_.buffer_data(static_cast<int>(target), nullptr, static_cast<std::size_t>(size),
                        static_cast<int>(usage));
}

void webgl_context::buffer_sub_data(std::uint32_t target, int offset,
                                    std::span<const std::byte> bytes) {
    if (offset < 0) {
        fail(gl_invalid_value);
        return;
    }
    device_.buffer_sub_data(static_cast<int>(target), static_cast<std::size_t>(offset),
                            bytes.data(), bytes.size());
}

void webgl_context::bind_buffer_base(std::uint32_t target, std::uint32_t index,
                                     std::uint32_t buffer) {
    // Printed beside the `[ubo] data` line `buffer_data` writes, so the order
    // of a binding and its storage is readable in one run.
    if (std::getenv("CTBROWSER_GL_UBO") != nullptr) {
        std::fprintf(stderr, "[ubo] base  target=%u index=%u buffer=%u\n", target, index, buffer);
    }
    device_.bind_buffer_base(static_cast<int>(target), index, buffer);
}

void webgl_context::enable_attribute(int location, bool on) {
    if (location < 0) { return; }
    device_.enable_attribute(static_cast<unsigned>(location), on);
}

void webgl_context::attribute_pointer(int location, int size, std::uint32_t type, bool normalised,
                                      int stride, int offset) {
    if (location < 0 || offset < 0) {
        if (offset < 0) { fail(gl_invalid_value); }
        return;
    }
    device_.attribute_pointer(static_cast<unsigned>(location), size, static_cast<int>(type),
                              normalised, stride, static_cast<std::size_t>(offset));
}

void webgl_context::attribute_divisor(int location, int divisor) {
    if (location < 0 || divisor < 0) { return; }
    device_.attribute_divisor(static_cast<unsigned>(location), static_cast<unsigned>(divisor));
}

const vertex_attribute * webgl_context::attribute_at(int location) const {
    if (location < 0) { return nullptr; }
    const auto got = device_.attribute_at(static_cast<unsigned>(location));
    scratch_ =
        vertex_attribute{got.enabled, got.size, got.stride, got.type, got.normalised, got.divisor};
    return &scratch_;
}

// --- textures and framebuffers ----------------------------------------------

void webgl_context::texture_image(std::uint32_t target, int width, int height,
                                  std::span<const std::byte> rgba) {
    if (width < 0 || height < 0) {
        fail(gl_invalid_value);
        return;
    }
    const auto wanted = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    // Short data is an error, not a partial upload. Handing GL a buffer smaller
    // than the level it was told to make reads past the end.
    if (!rgba.empty() && rgba.size() < wanted) {
        fail(gl_invalid_value);
        return;
    }
    device_.texture_image(static_cast<int>(target), width, height,
                          rgba.empty() ? nullptr : rgba.data());
}

void webgl_context::texture_from_bitmap(std::uint32_t target, const paint::bitmap & image) {
    // ARGB in, RGBA out: the same swap read_pixels does in the other direction,
    // and getting it wrong gives a texture that looks right until something in
    // it is red.
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(image.width) *
                                   static_cast<std::size_t>(image.height) * 4);
    std::size_t at = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::uint32_t argb = image.at(x, y);
            rgba[at++] = static_cast<std::uint8_t>((argb >> 16) & 0xFF);
            rgba[at++] = static_cast<std::uint8_t>((argb >> 8) & 0xFF);
            rgba[at++] = static_cast<std::uint8_t>(argb & 0xFF);
            rgba[at++] = static_cast<std::uint8_t>((argb >> 24) & 0xFF);
        }
    }
    device_.texture_image(static_cast<int>(target), image.width, image.height, rgba.data());
}

// --- draws -------------------------------------------------------------------
//
// No readback here: `present()` copies the surface once per frame, not once
// per draw.

void webgl_context::draw_arrays(std::uint32_t mode, int first, int count) {
    if (count < 0 || first < 0) {
        fail(gl_invalid_value);
        return;
    }
    device_.draw_arrays(static_cast<int>(mode), first, count);
}

void webgl_context::draw_elements(std::uint32_t mode, int count, std::uint32_t type, int offset) {
    if (count < 0 || offset < 0) {
        fail(gl_invalid_value);
        return;
    }
    device_.draw_elements(static_cast<int>(mode), count, static_cast<int>(type),
                          static_cast<std::size_t>(offset));
}

void webgl_context::draw_arrays_instanced(std::uint32_t mode, int first, int count, int instances) {
    if (count < 0 || first < 0 || instances < 0) {
        fail(gl_invalid_value);
        return;
    }
    device_.draw_arrays_instanced(static_cast<int>(mode), first, count, instances);
}

void webgl_context::draw_elements_instanced(std::uint32_t mode, int count, std::uint32_t type,
                                            int offset, int instances) {
    if (count < 0 || offset < 0 || instances < 0) {
        fail(gl_invalid_value);
        return;
    }
    device_.draw_elements_instanced(static_cast<int>(mode), count, static_cast<int>(type),
                                    static_cast<std::size_t>(offset), instances);
}

std::uint32_t webgl_context::take_error() {
    // WebGL's getError reports and clears, and asking the device as well means
    // an error raised inside GL is not lost behind one raised here.
    const std::uint32_t mine = error_;
    error_ = gl_no_error;
    const std::uint32_t theirs = device_.take_error();
    return mine != gl_no_error ? mine : theirs;
}

void webgl_context::fail(std::uint32_t error) {
    // The first error wins, as GL specifies: a page that checks once
    // after several calls should see the first thing that went wrong.
    if (error_ == gl_no_error) { error_ = error; }
}

void webgl_context::refuse(std::string_view call) {
    // A refused entry point raises INVALID_OPERATION, the error the bindings
    // tell the page about, and records its name for developers (refusing by
    // name is this engine's contract; see docs/history/webgl2.md).
    fail(gl_enum::invalid_operation);
    const std::string name{call};
    if (std::find(refused_.begin(), refused_.end(), name) == refused_.end()) {
        refused_.push_back(name);
    }
}

const std::vector<std::string> & webgl_context::refused() const {
    return refused_;
}

const std::string & webgl_context::shader_error() const {
    return shader_error_;
}

} // namespace ctbrowser::shell
