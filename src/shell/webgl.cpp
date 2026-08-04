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
