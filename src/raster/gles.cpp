#include <ctbrowser/raster/gles.hpp>

#include <string>
#include <utility>

// EGL AND GLES LIVE HERE AND NOWHERE ELSE. See gles.hpp: `tests/api_surface`
// lints that no third-party header reaches a public one, so the whole of ANGLE
// is behind this translation unit's pimpl.

#if CTBROWSER_WITH_ANGLE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#endif

namespace ctbrowser::raster::gles {

#if CTBROWSER_WITH_ANGLE

struct device::impl {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0;
    int height = 0;
    std::string error;

    ~impl() {
        if (display == EGL_NO_DISPLAY) { return; }
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) { eglDestroyContext(display, context); }
        if (surface != EGL_NO_SURFACE) { eglDestroySurface(display, surface); }
        eglTerminate(display);
    }
};

namespace {

// THE DEVICE IS ASKED FOR BY NAME rather than left to ANGLE's own choice.
//
// `eglGetDisplay(EGL_DEFAULT_DISPLAY)` lets ANGLE pick, and on a headless Linux
// box it picks the host's Vulkan - which here has no VK_EXT_headless_surface, so
// initialisation fails with an error naming an extension rather than a decision.
// SwiftShader is ANGLE's own software Vulkan, it ships beside it, and it is
// DETERMINISTIC ACROSS PLATFORMS - measured, the same shader gives the identical
// pixel on Linux and Windows. That is what lets a byte-compared golden survive
// this path at all, so it is the default rather than a fallback.
[[nodiscard]] EGLDisplay open_display(std::string & error) {
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (get_platform_display == nullptr) {
        error = "this EGL has no eglGetPlatformDisplayEXT";
        return EGL_NO_DISPLAY;
    }
    // EGLint, NOT EGLAttrib. eglGetPlatformDisplayEXT takes 32-bit attributes;
    // building the 64-bit array and casting makes every second word read as
    // zero, and the only symptom is EGL_NO_DISPLAY with nothing logged.
    const EGLint attrs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE,
                            EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
                            EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
                            EGL_PLATFORM_ANGLE_DEVICE_TYPE_SWIFTSHADER_ANGLE,
                            EGL_NONE};
    EGLDisplay display = get_platform_display(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attrs);
    if (display == EGL_NO_DISPLAY) { error = "no ANGLE display for a SwiftShader Vulkan device"; }
    return display;
}

} // namespace

bool available() {
    return unavailable_because().empty();
}

std::string unavailable_because() {
    // MADE AND DESTROYED, rather than a flag consulted. Whether the libraries
    // load, the loader finds an ICD and a context can be created are three
    // separate ways to fail and none of them is visible at build time.
    std::string error;
    EGLDisplay display = open_display(error);
    if (display == EGL_NO_DISPLAY) { return error.empty() ? "no ANGLE display" : error; }
    EGLint major = 0;
    EGLint minor = 0;
    const bool ok = eglInitialize(display, &major, &minor) == EGL_TRUE;
    if (!ok) {
        error = "eglInitialize failed - is vk_swiftshader_icd.json beside the libraries?";
    }
    eglTerminate(display);
    return ok ? std::string{} : error;
}

device::device(int width, int height) : impl_(std::make_unique<impl>()) {
    impl_->width = width > 0 ? width : 0;
    impl_->height = height > 0 ? height : 0;
    if (impl_->width == 0 || impl_->height == 0) {
        impl_->error = "a device needs a positive size";
        return;
    }

    impl_->display = open_display(impl_->error);
    if (impl_->display == EGL_NO_DISPLAY) { return; }
    EGLint major = 0;
    EGLint minor = 0;
    if (eglInitialize(impl_->display, &major, &minor) != EGL_TRUE) {
        impl_->error = "eglInitialize failed";
        impl_->display = EGL_NO_DISPLAY;
        return;
    }

    const EGLint config_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                   EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
                                   EGL_NONE};
    EGLConfig config{};
    EGLint found = 0;
    if (eglChooseConfig(impl_->display, config_attrs, &config, 1, &found) != EGL_TRUE ||
        found == 0) {
        impl_->error = "no EGL config with a 24-bit depth and 8-bit stencil";
        return;
    }

    const EGLint surface_attrs[] = {EGL_WIDTH, impl_->width, EGL_HEIGHT, impl_->height, EGL_NONE};
    impl_->surface = eglCreatePbufferSurface(impl_->display, config, surface_attrs);
    if (impl_->surface == EGL_NO_SURFACE) {
        impl_->error = "could not create a pbuffer surface";
        return;
    }

    const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    impl_->context = eglCreateContext(impl_->display, config, EGL_NO_CONTEXT, context_attrs);
    if (impl_->context == EGL_NO_CONTEXT) {
        impl_->error = "could not create an ES 3 context";
        return;
    }
    if (!make_current()) { return; }
}

device::~device() = default;
device::device(device &&) noexcept = default;
device & device::operator=(device &&) noexcept = default;

bool device::ok() const noexcept {
    return impl_ != nullptr && impl_->error.empty() && impl_->context != EGL_NO_CONTEXT;
}

const std::string & device::error() const noexcept {
    static const std::string moved_from = "the device was moved from";
    return impl_ != nullptr ? impl_->error : moved_from;
}

int device::width() const noexcept {
    return impl_ != nullptr ? impl_->width : 0;
}
int device::height() const noexcept {
    return impl_ != nullptr ? impl_->height : 0;
}

bool device::make_current() {
    if (impl_ == nullptr || impl_->context == EGL_NO_CONTEXT) { return false; }
    if (eglMakeCurrent(impl_->display, impl_->surface, impl_->surface, impl_->context) != EGL_TRUE) {
        impl_->error = "eglMakeCurrent failed";
        return false;
    }
    return true;
}

std::string device::renderer() const {
    if (!ok()) { return {}; }
    const auto * said = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    return said != nullptr ? std::string{said} : std::string{};
}

std::string device::version() const {
    if (!ok()) { return {}; }
    const auto * said = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    return said != nullptr ? std::string{said} : std::string{};
}

void device::clear(float red, float green, float blue, float alpha) {
    if (!ok()) { return; }
    glViewport(0, 0, impl_->width, impl_->height);
    glClearColor(red, green, blue, alpha);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

bool device::read_pixels(paint::bitmap & into) const {
    if (!ok()) { return false; }
    if (into.width != impl_->width || into.height != impl_->height) {
        into = paint::bitmap{impl_->width, impl_->height};
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(impl_->width) *
                                     static_cast<std::size_t>(impl_->height) * 4);
    glFinish();
    glReadPixels(0, 0, impl_->width, impl_->height, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());

    // RGBA IN, ARGB OUT, AND FLIPPED. GL's origin is the bottom left and a
    // bitmap's is the top left; handing back the buffer as it arrives gives a
    // picture that is upside down and otherwise perfect, which survives any
    // test that only counts pixels.
    for (int y = 0; y < impl_->height; ++y) {
        const int source_row = impl_->height - 1 - y;
        for (int x = 0; x < impl_->width; ++x) {
            const std::size_t at =
                (static_cast<std::size_t>(source_row) * static_cast<std::size_t>(impl_->width) +
                 static_cast<std::size_t>(x)) *
                4;
            into.put(x, y,
                     (static_cast<std::uint32_t>(bytes[at + 3]) << 24) |
                         (static_cast<std::uint32_t>(bytes[at + 0]) << 16) |
                         (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
                         static_cast<std::uint32_t>(bytes[at + 2]));
        }
    }
    return true;
}

#else // CTBROWSER_WITH_ANGLE

// BUILT WITHOUT IT, and saying so rather than pretending. Same shape as
// `allocator_name()` reporting "system": a build that did not include something
// should report that, not fail in a way that looks like the thing is broken.
struct device::impl {
    std::string error = "built without ANGLE (-DCTBROWSER_WITH_ANGLE=OFF)";
};

bool available() {
    return false;
}
std::string unavailable_because() {
    return "built without ANGLE (-DCTBROWSER_WITH_ANGLE=OFF)";
}

device::device(int, int) : impl_(std::make_unique<impl>()) {}
device::~device() = default;
device::device(device &&) noexcept = default;
device & device::operator=(device &&) noexcept = default;
bool device::ok() const noexcept {
    return false;
}
const std::string & device::error() const noexcept {
    static const std::string moved_from = "the device was moved from";
    return impl_ != nullptr ? impl_->error : moved_from;
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
bool device::read_pixels(paint::bitmap &) const {
    return false;
}

#endif // CTBROWSER_WITH_ANGLE

} // namespace ctbrowser::raster::gles
