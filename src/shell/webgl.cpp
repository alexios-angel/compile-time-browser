#include <ctbrowser/shell/webgl.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace ctbrowser::shell {

namespace {

// THE FEW GL NUMBERS THIS FILE NEEDS BY NAME. `gl.hpp` names no GLES type, so
// the constants a page passes stay plain integers all the way down - they came
// from JavaScript as numbers and there is nothing to translate.
constexpr std::uint32_t gl_no_error = 0;
constexpr std::uint32_t gl_invalid_value = 0x0501;

} // namespace

webgl_context::webgl_context(paint::bitmap * surface, int width, int height,
                             raster::gl::driver which)
    : device_{width, height, which}, surface_{surface} {}

void webgl_context::resize(paint::bitmap * surface, int width, int height) {
    surface_ = surface;
    if (width == device_.width() && height == device_.height()) { return; }
    // A NEW DEVICE, and the page's GL objects go with the old one. That is what
    // a real browser does on a context loss, and pretending otherwise would hand
    // a page handles into a surface that no longer exists.
    device_ = raster::gl::device{width, height};
}

bool webgl_context::ok() const {
    return device_.ok();
}

const std::string & webgl_context::device_error() const {
    return device_.error();
}

int webgl_context::width() const {
    return device_.width();
}

int webgl_context::height() const {
    return device_.height();
}

paint::bitmap * webgl_context::surface() {
    return surface_;
}

void webgl_context::present() {
    // ONCE PER FRAME, NOT ONCE PER DRAW. The context this replaces read the
    // whole surface back after every drawArrays and drawElements - a full copy
    // per mesh, which for Babylon's scene meant 267 of them for one frame.
    if (!device_.ok()) { return; }
    if (surface_ == nullptr) { return; }
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

void webgl_context::clear(std::uint32_t mask) {
    device_.clear_buffers(mask);
}

void webgl_context::clear_color(float red, float green, float blue, float alpha) {
    device_.clear_color(red, green, blue, alpha);
}

void webgl_context::clear_depth(float depth) {
    device_.clear_depth(depth);
}

void webgl_context::set_enabled(std::uint32_t capability, bool on) {
    device_.set_capability(static_cast<int>(capability), on);
}

// --- shaders and programs ---------------------------------------------------
//
// Each of these is a translation and a return. Nothing is remembered: which
// program is linked, what a shader's source was and what a program declares are
// all questions GL answers, and a cached answer is the thing that drifts.

std::uint32_t webgl_context::create_shader(std::uint32_t kind) {
    return device_.create_shader(static_cast<int>(kind));
}

void webgl_context::shader_source(std::uint32_t shader, std::string_view source) {
    std::string sent{source};
    // MEASURED BEFORE ANYTHING IS ADDED. The previous reading of this could not
    // tell two different facts apart: a run with the preamble on printed
    // `version=1` whether the PAGE supplied the directive or this code did, and
    // "Babylon assembles its bodies without one" was inferred from that number.
    const bool page_version = sent.find("#version") != std::string::npos;
    const bool has_layout = sent.find("layout(") != std::string::npos;

    // A shader using `layout(...)` is GLSL ES 3.00 and the `#version 300 es`
    // directive is then mandatory. Supplying it is the PAGE'S job - a real
    // browser adds nothing - so this is a repair for a bundle that did not, and
    // it fires only when the page did not and the source plainly needs it.
    //
    // TEMPORARY KNOB, and it comes out with the one in raster/gl.cpp:
    // `CTBROWSER_GL_PREAMBLE=0` turns it off, so ONE build can be measured both
    // ways rather than rebuilt between two runs.
    const char * knob = std::getenv("CTBROWSER_GL_PREAMBLE");
    const bool add =
        (knob == nullptr || std::string_view{knob} != "0") && !page_version && has_layout;
    if (add) { sent.insert(0, "#version 300 es\n"); }

    // WHAT ACTUALLY REACHED THE COMPILER. `CTBROWSER_GL_SRC=1`. A shader body
    // that arrives empty or truncated fails in a way that looks like a draw
    // problem three layers away - which has happened twice on this page. The
    // FIRST LINE comes too, because that is where a `#version` has to be and
    // where a body that arrived as nothing but `#define`s is visible at a glance.
    if (std::getenv("CTBROWSER_GL_SRC") != nullptr) {
        const std::size_t line = std::min(sent.find('\n'), sent.size());
        const std::string first = sent.substr(0, std::min<std::size_t>(line, 60));
        std::fprintf(stderr, "[src] shader=%u bytes=%zu page_version=%d layout=%d added=%d | %s\n",
                     shader, sent.size(), page_version ? 1 : 0, has_layout ? 1 : 0, add ? 1 : 0,
                     first.c_str());
    }
    device_.shader_source(shader, sent);
}

void webgl_context::compile_shader(std::uint32_t shader) {
    device_.compile_shader(shader);
    // THE PAGE'S OWN DIAGNOSIS, kept for a report. Babylon logs its compile
    // failures itself, and when it does not, this is the only place the reason
    // exists - a shader that fails to compile draws nothing and raises no GL
    // error, which is silence in both directions.
    if (!device_.shader_compiled(shader)) { shader_error_ = device_.shader_log(shader); }
}

bool webgl_context::shader_compiled(std::uint32_t shader) const {
    return device_.shader_compiled(shader);
}

std::string webgl_context::shader_log(std::uint32_t shader) const {
    return device_.shader_log(shader);
}

std::uint32_t webgl_context::create_program() {
    return device_.create_program();
}

void webgl_context::attach_shader(std::uint32_t program, std::uint32_t shader) {
    device_.attach_shader(program, shader);
}

void webgl_context::link_program(std::uint32_t program) {
    device_.link_program(program);
    if (!device_.program_linked(program)) { shader_error_ = device_.program_log(program); }
}

bool webgl_context::program_linked(std::uint32_t program) const {
    return device_.program_linked(program);
}

std::string webgl_context::program_log(std::uint32_t program) const {
    return device_.program_log(program);
}

void webgl_context::use_program(std::uint32_t program) {
    device_.use_program(program);
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

int webgl_context::attribute_location(std::uint32_t program, std::string_view name) const {
    return device_.attribute_location(program, std::string{name});
}

int webgl_context::get_uniform_block_index(std::uint32_t program, std::string_view name) const {
    const int index = device_.uniform_block_index(program, std::string{name});
    // `CTBROWSER_GL_UBO=1` prints this, the binding and the buffer together.
    // THE THREE MUST AGREE, and reading them side by side in one run is what
    // refuted two hypotheses that had each looked obvious on their own.
    //
    // -1 IS NORMAL for a block the shader never reads: GL drops it and answers
    // GL_INVALID_INDEX. Babylon's `Mesh` block does this and it cost a whole
    // pass to rule out - it is not a bug, do not chase it again.
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
    // THE LOCATION IS LOOKED UP HERE, from the program GL says is in use. A page
    // holds an opaque location object that carries the NAME, so there is nothing
    // to cache and nothing to invalidate when it relinks.
    const unsigned program = device_.program_in_use();
    if (program == 0 || value.data.empty()) { return; }
    const int location = device_.uniform_location(program, std::string{name});
    if (location < 0) { return; } // a uniform the linker dropped is not an error
    const int per = value.rows * value.cols;
    const int count = per > 0 ? static_cast<int>(value.data.size()) / per : 0;
    device_.set_uniform(location, value.data.data(), count, value.rows, value.cols, value.integer);
}

// --- buffers, attributes and vertex arrays ----------------------------------

std::uint32_t webgl_context::create_buffer() {
    return device_.create_buffer();
}

void webgl_context::bind_buffer(std::uint32_t target, std::uint32_t buffer) {
    device_.bind_buffer(static_cast<int>(target), buffer);
}

void webgl_context::buffer_data(std::uint32_t target, std::span<const std::byte> bytes,
                                std::uint32_t usage) {
    note_storage(target, bytes.size());
    device_.buffer_data(static_cast<int>(target), bytes.data(), bytes.size(),
                        static_cast<int>(usage));
}

void webgl_context::note_storage(std::uint32_t target, std::size_t size) {
    // WHICH BUFFER GOT STORAGE, AND WHEN. Printed beside the `[ubo] base` lines
    // so the ORDER of the two is readable in one run: a binding point with a
    // buffer bound and no storage behind it is the shape that leaves ANGLE a
    // null BufferHelper to dereference at draw time.
    if (std::getenv("CTBROWSER_GL_UBO") == nullptr) { return; }
    std::fprintf(stderr, "[ubo] data  target=%u buffer=%u bytes=%zu\n", target,
                 device_.bound_buffer(static_cast<int>(target)), size);
}

void webgl_context::buffer_data(std::uint32_t target, int size, std::uint32_t usage) {
    // SIZE WITHOUT DATA, which is `gl.bufferData(target, 1024, usage)` - a page
    // reserving room it will fill with bufferSubData.
    if (size < 0) {
        fail(gl_invalid_value);
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
    // Printed beside the `[ubo] data` line `buffer_data` writes, so the ORDER
    // of a binding and its storage is readable in one run.
    if (std::getenv("CTBROWSER_GL_UBO") != nullptr) {
        std::fprintf(stderr, "[ubo] base  target=%u index=%u buffer=%u\n", target, index, buffer);
    }
    device_.bind_buffer_base(static_cast<int>(target), index, buffer);
}

void webgl_context::delete_object(raster::gl::device::object_kind kind, std::uint32_t name) {
    device_.delete_object(kind, name);
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

std::uint32_t webgl_context::create_vertex_array() {
    return device_.create_vertex_array();
}

void webgl_context::bind_vertex_array(std::uint32_t array) {
    device_.bind_vertex_array(array);
}

void webgl_context::delete_vertex_array(std::uint32_t array) {
    device_.delete_vertex_array(array);
}

bool webgl_context::is_vertex_array(std::uint32_t array) const {
    return device_.is_vertex_array(array);
}

std::uint32_t webgl_context::bound_vertex_array() const {
    return device_.bound_vertex_array();
}

// --- textures and framebuffers ----------------------------------------------

std::uint32_t webgl_context::create_texture() {
    return device_.create_texture();
}

void webgl_context::bind_texture(std::uint32_t target, std::uint32_t texture) {
    device_.bind_texture(static_cast<int>(target), texture);
}

void webgl_context::active_texture(std::uint32_t unit) {
    device_.active_texture(static_cast<int>(unit));
}

void webgl_context::texture_image(std::uint32_t target, int width, int height,
                                  std::span<const std::byte> rgba) {
    if (width < 0 || height < 0) {
        fail(gl_invalid_value);
        return;
    }
    const auto wanted = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    // SHORT DATA IS AN ERROR, NOT A PARTIAL UPLOAD. Handing GL a buffer smaller
    // than the level it was told to make reads past the end.
    if (!rgba.empty() && rgba.size() < wanted) {
        fail(gl_invalid_value);
        return;
    }
    device_.texture_image(static_cast<int>(target), width, height,
                          rgba.empty() ? nullptr : rgba.data());
}

void webgl_context::texture_from_bitmap(std::uint32_t target, const paint::bitmap & image) {
    // ARGB IN, RGBA OUT - the same swap read_pixels does in the other direction,
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

void webgl_context::texture_parameter(std::uint32_t target, std::uint32_t name,
                                      std::uint32_t value) {
    device_.texture_parameter(static_cast<int>(target), static_cast<int>(name),
                              static_cast<int>(value));
}

void webgl_context::bind_framebuffer(std::uint32_t framebuffer) {
    device_.bind_framebuffer(framebuffer);
}

void webgl_context::framebuffer_texture(std::uint32_t attachment, std::uint32_t texture) {
    device_.framebuffer_texture(static_cast<int>(attachment), texture);
}

std::uint32_t webgl_context::framebuffer_status() const {
    return device_.framebuffer_status();
}

// --- draws and pipeline state -----------------------------------------------
//
// NO READBACK HERE. The context this replaces read the whole surface after every
// draw; `present()` does it once per frame.

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

void webgl_context::cull_face(std::uint32_t which) {
    device_.cull_face(static_cast<int>(which));
}

void webgl_context::front_face(std::uint32_t which) {
    device_.front_face(static_cast<int>(which));
}

void webgl_context::depth_func(std::uint32_t how) {
    device_.depth_func(static_cast<int>(how));
}

void webgl_context::depth_mask(bool on) {
    device_.depth_mask(on);
}

void webgl_context::blend_func(std::uint32_t source, std::uint32_t destination) {
    device_.blend_func(static_cast<int>(source), static_cast<int>(destination));
}

std::uint32_t webgl_context::take_error() {
    // WebGL's getError REPORTS AND CLEARS, and asking the device as well means
    // an error raised inside GL is not lost behind one raised here.
    const std::uint32_t mine = error_;
    error_ = gl_no_error;
    const std::uint32_t theirs = device_.take_error();
    return mine != gl_no_error ? mine : theirs;
}

void webgl_context::fail(std::uint32_t error) {
    // FIRST ERROR WINS, which is what GL specifies: a page that checks once
    // after several calls should see the first thing that went wrong.
    if (error_ == gl_no_error) { error_ = error; }
}

void webgl_context::refuse(std::string_view call) {
    // AND IT RAISES THE ERROR IT ADVERTISES.
    //
    // This recorded the name and stopped there, while the bindings printed
    // "it raises INVALID_OPERATION" to the page's console - so all fifty-two
    // refused entry points returned null with getError() still reading
    // NO_ERROR. A page that checks for the error it was told about was told
    // nothing was wrong.
    //
    // Refusing BY NAME is this engine's contract (docs/webgl2-plan.md); the
    // point of it is to be loud. A silent refusal is the same defect as a
    // silent no-op, which is what the whole rewrite exists to eliminate. The
    // ledger records the name for a developer, `fail` answers the page.
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
