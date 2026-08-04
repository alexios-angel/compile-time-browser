#include <ctbrowser/raster/gl.hpp>

#include <string>
#include <utility>
#include <vector>

#if CTBROWSER_WITH_ANGLE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#endif

namespace ctbrowser::raster::gl {

#if CTBROWSER_WITH_ANGLE

struct device::impl {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    int width = 0;
    int height = 0;
    std::string error;

    ~impl() {
        if (display == EGL_NO_DISPLAY) { return; }
        if (context != EGL_NO_CONTEXT) { eglDestroyContext(display, context); }
        if (surface != EGL_NO_SURFACE) { eglDestroySurface(display, surface); }
        eglTerminate(display);
    }
};

namespace {

// THE DRIVER, ASKED FOR BY NAME. The stack this replaces hard-coded SwiftShader
// here with no way to select anything else, so "safe mode" was a rewrite rather
// than an argument.
[[nodiscard]] EGLDisplay open_display(driver which) {
    const auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (get_platform_display == nullptr) { return EGL_NO_DISPLAY; }

    // EGLint, NOT EGLAttrib. eglGetPlatformDisplayEXT takes the narrower type
    // and passing the wider one compiles, then reads the attribute list wrong.
    std::vector<EGLint> attrs{EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE};
    if (which == driver::deterministic) {
        attrs.push_back(EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE);
        attrs.push_back(EGL_PLATFORM_ANGLE_DEVICE_TYPE_SWIFTSHADER_ANGLE);
    }
    attrs.push_back(EGL_NONE);
    return get_platform_display(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attrs.data());
}

} // namespace

bool available() {
    return unavailable_because().empty();
}

std::string unavailable_because() {
    const EGLDisplay display = open_display(driver::fastest);
    if (display == EGL_NO_DISPLAY) { return "no ANGLE platform display"; }
    EGLint major = 0;
    EGLint minor = 0;
    if (eglInitialize(display, &major, &minor) != EGL_TRUE) {
        // THE LIKELIEST CAUSE NAMED, because "eglInitialize failed" sends the
        // next reader nowhere and this exact omission cost an hour when the
        // release was packaged.
        return "eglInitialize failed - is vk_swiftshader_icd.json beside the libraries?";
    }
    eglTerminate(display);
    return {};
}

device::device(int width, int height, driver which) : impl_{std::make_unique<impl>()} {
    impl_->width = width;
    impl_->height = height;

    impl_->display = open_display(which);
    if (impl_->display == EGL_NO_DISPLAY) {
        impl_->error = "no ANGLE platform display";
        return;
    }
    if (eglInitialize(impl_->display, nullptr, nullptr) != EGL_TRUE) {
        impl_->error = "eglInitialize failed";
        return;
    }

    const EGLint config_attrs[] = {EGL_SURFACE_TYPE,
                                   EGL_PBUFFER_BIT,
                                   EGL_RENDERABLE_TYPE,
                                   EGL_OPENGL_ES3_BIT,
                                   EGL_RED_SIZE,
                                   8,
                                   EGL_GREEN_SIZE,
                                   8,
                                   EGL_BLUE_SIZE,
                                   8,
                                   EGL_ALPHA_SIZE,
                                   8,
                                   EGL_DEPTH_SIZE,
                                   24,
                                   EGL_STENCIL_SIZE,
                                   8,
                                   EGL_NONE};
    EGLConfig config{};
    EGLint configs = 0;
    if (eglChooseConfig(impl_->display, config_attrs, &config, 1, &configs) != EGL_TRUE ||
        configs == 0) {
        impl_->error = "no EGL config with depth and stencil";
        return;
    }

    const EGLint surface_attrs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    impl_->surface = eglCreatePbufferSurface(impl_->display, config, surface_attrs);
    if (impl_->surface == EGL_NO_SURFACE) {
        impl_->error = "eglCreatePbufferSurface failed";
        return;
    }

    const EGLint context_attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1,
                                    EGL_NONE};
    impl_->context = eglCreateContext(impl_->display, config, EGL_NO_CONTEXT, context_attrs);
    if (impl_->context == EGL_NO_CONTEXT) {
        impl_->error = "eglCreateContext failed for ES 3.1";
        return;
    }
    if (!make_current()) { impl_->error = "eglMakeCurrent failed"; }
}

device::~device() = default;
device::device(device &&) noexcept = default;
device & device::operator=(device &&) noexcept = default;

bool device::ok() const noexcept {
    return impl_ != nullptr && impl_->error.empty() && impl_->context != EGL_NO_CONTEXT;
}

const std::string & device::error() const noexcept {
    return impl_->error;
}

int device::width() const noexcept {
    return impl_->width;
}

int device::height() const noexcept {
    return impl_->height;
}

bool device::make_current() {
    return eglMakeCurrent(impl_->display, impl_->surface, impl_->surface, impl_->context) ==
           EGL_TRUE;
}

namespace {

[[nodiscard]] std::string ask(GLenum name) {
    const GLubyte * text = glGetString(name);
    return text == nullptr ? std::string{} : std::string{reinterpret_cast<const char *>(text)};
}

} // namespace

std::string device::renderer() const {
    return ok() ? ask(GL_RENDERER) : std::string{};
}

std::string device::version() const {
    return ok() ? ask(GL_VERSION) : std::string{};
}

void device::viewport(int x, int y, int w, int h) {
    if (!ok()) { return; }
    glViewport(x, y, w, h);
}

void device::scissor(int x, int y, int w, int h) {
    if (!ok()) { return; }
    glScissor(x, y, w, h);
}

void device::clear_buffers(std::uint32_t mask) {
    if (!ok()) { return; }
    glClear(static_cast<GLbitfield>(mask));
}

void device::clear_color(float red, float green, float blue, float alpha) {
    if (!ok()) { return; }
    glClearColor(red, green, blue, alpha);
}

void device::clear_depth(float depth) {
    if (!ok()) { return; }
    glClearDepthf(depth);
}

void device::set_capability(int capability, bool on) {
    if (!ok()) { return; }
    if (on) {
        glEnable(static_cast<GLenum>(capability));
    } else {
        glDisable(static_cast<GLenum>(capability));
    }
}

std::uint32_t device::take_error() {
    return ok() ? static_cast<std::uint32_t>(glGetError()) : 0u;
}

void device::clear(float red, float green, float blue, float alpha) {
    if (!ok()) { return; }
    glClearColor(red, green, blue, alpha);
    glClear(static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT) | GL_DEPTH_BUFFER_BIT |
            GL_STENCIL_BUFFER_BIT);
}

bool device::read_pixels(paint::bitmap & into) const {
    if (!ok()) { return false; }
    const int w = impl_->width;
    const int h = impl_->height;
    if (into.width != w || into.height != h) { into = paint::bitmap{w, h}; }

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    for (int y = 0; y < h; ++y) {
        // BOTTOM-LEFT TO TOP-LEFT. GL's origin is not a bitmap's.
        const std::size_t row = static_cast<std::size_t>(h - 1 - y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x) {
            const std::size_t at = (row + static_cast<std::size_t>(x)) * 4;
            // RGBA IN, ARGB OUT.
            into.put(x, y,
                     (static_cast<std::uint32_t>(rgba[at + 3]) << 24) |
                         (static_cast<std::uint32_t>(rgba[at + 0]) << 16) |
                         (static_cast<std::uint32_t>(rgba[at + 1]) << 8) |
                         static_cast<std::uint32_t>(rgba[at + 2]));
        }
    }
    return true;
}

#else

// WITHOUT ANGLE the device does not exist, and says so rather than pretending.
struct device::impl {
    std::string error = "built without ANGLE - run tools/fetch-angle.sh and set "
                        "-DCTBROWSER_WITH_ANGLE=ON";
};

bool available() {
    return false;
}
std::string unavailable_because() {
    return "built without ANGLE - run tools/fetch-angle.sh and set -DCTBROWSER_WITH_ANGLE=ON";
}
device::device(int, int, driver) : impl_{std::make_unique<impl>()} {}
device::~device() = default;
device::device(device &&) noexcept = default;
device & device::operator=(device &&) noexcept = default;
bool device::ok() const noexcept {
    return false;
}
const std::string & device::error() const noexcept {
    return impl_->error;
}
std::string device::renderer() const {
    return {};
}
std::string device::version() const {
    return {};
}
int device::width() const noexcept {
    return 0;
}
int device::height() const noexcept {
    return 0;
}
bool device::make_current() {
    return false;
}
void device::clear(float, float, float, float) {}
void device::viewport(int, int, int, int) {}
void device::scissor(int, int, int, int) {}
void device::clear_buffers(std::uint32_t) {}
void device::clear_color(float, float, float, float) {}
void device::clear_depth(float) {}
void device::set_capability(int, bool) {}
std::uint32_t device::take_error() {
    return 0u;
}
bool device::read_pixels(paint::bitmap &) const {
    return false;
}

#endif

} // namespace ctbrowser::raster::gl
