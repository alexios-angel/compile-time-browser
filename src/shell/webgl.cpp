#include <ctbrowser/shell/webgl.hpp>

#include <algorithm>

namespace ctbrowser::shell {

namespace {

// THE FEW GL NUMBERS THIS FILE NEEDS BY NAME. `gl.hpp` names no GLES type, so
// the constants a page passes stay plain integers all the way down - they came
// from JavaScript as numbers and there is nothing to translate.
constexpr std::uint32_t gl_no_error = 0;
constexpr std::uint32_t gl_invalid_value = 0x0501;

} // namespace

webgl_context::webgl_context(int width, int height, raster::gl::driver which)
    : device_{width, height, which} {
    surface_ = paint::bitmap{width, height};
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

paint::bitmap & webgl_context::surface() {
    return surface_;
}

void webgl_context::present() {
    // ONCE PER FRAME, NOT ONCE PER DRAW. The context this replaces read the
    // whole surface back after every drawArrays and drawElements - a full copy
    // per mesh, which for Babylon's scene meant 267 of them for one frame.
    if (!device_.ok()) { return; }
    (void)device_.read_pixels(surface_);
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
    device_.shader_source(shader, std::string{source});
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
    return device_.uniform_block_index(program, std::string{name});
}

void webgl_context::uniform_block_binding(std::uint32_t program, std::uint32_t index,
                                          std::uint32_t binding) {
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
