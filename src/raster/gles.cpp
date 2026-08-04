#include <ctbrowser/raster/gles.hpp>

#include <string>
#include <utility>
#include <vector>

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
    // See draw_arrays: ES 3 core needs one bound before attributes mean
    // anything, and WebGL 1 pages never make one.
    GLuint vao = 0;
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
    const EGLint attrs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
                            EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
                            EGL_PLATFORM_ANGLE_DEVICE_TYPE_SWIFTSHADER_ANGLE, EGL_NONE};
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
    if (!ok) { error = "eglInitialize failed - is vk_swiftshader_icd.json beside the libraries?"; }
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

    // A VERTEX ARRAY OBJECT, BOUND ONCE, HERE.
    //
    // It was created lazily in `draw_arrays` first, which is wrong in a way
    // that draws NOTHING and reports nothing: binding a fresh VAO discards
    // every attribute enable and pointer set before it, and a page sets all of
    // those long before it draws. The triangle came back empty with no GL error
    // anywhere.
    //
    // Bound at creation it is simply the context's array state, which is what a
    // WebGL 1 page believes it is using anyway.
    glGenVertexArrays(1, &impl_->vao);
    glBindVertexArray(impl_->vao);
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
    if (eglMakeCurrent(impl_->display, impl_->surface, impl_->surface, impl_->context) !=
        EGL_TRUE) {
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

// --- the facade ------------------------------------------------------------
//
// Thin on purpose: every one of these is a GL call with the arguments a page
// already passed as numbers. The value is not in what they do, it is in where
// the GLES types stop - which is here.

void device::viewport(int x, int y, int width, int height) {
    if (!ok()) { return; }
    glViewport(x, y, width, height);
}

void device::set_capability(int capability, bool on) {
    if (!ok()) { return; }
    if (on) {
        glEnable(static_cast<GLenum>(capability));
    } else {
        glDisable(static_cast<GLenum>(capability));
    }
}

unsigned device::create_shader(int kind) {
    return ok() ? glCreateShader(static_cast<GLenum>(kind)) : 0;
}

void device::shader_source(unsigned shader, const std::string & source) {
    if (!ok()) { return; }
    const char * text = source.c_str();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
}

void device::compile_shader(unsigned shader) {
    if (!ok()) { return; }
    glCompileShader(shader);
}

bool device::shader_compiled(unsigned shader) const {
    if (!ok()) { return false; }
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status != 0;
}

std::string device::shader_log(unsigned shader) const {
    if (!ok()) { return {}; }
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 0) { return {}; }
    std::string log(static_cast<std::size_t>(length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    // The driver counts the terminator; a std::string does not.
    if (!log.empty() && log.back() == '\0') { log.pop_back(); }
    return log;
}

unsigned device::create_program() {
    return ok() ? glCreateProgram() : 0;
}

void device::attach_shader(unsigned program, unsigned shader) {
    if (!ok()) { return; }
    glAttachShader(program, shader);
}

void device::link_program(unsigned program) {
    if (!ok()) { return; }
    glLinkProgram(program);
}

bool device::program_linked(unsigned program) const {
    if (!ok()) { return false; }
    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    return status != 0;
}

std::string device::program_log(unsigned program) const {
    if (!ok()) { return {}; }
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 0) { return {}; }
    std::string log(static_cast<std::size_t>(length), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    if (!log.empty() && log.back() == '\0') { log.pop_back(); }
    return log;
}

void device::use_program(unsigned program) {
    if (!ok()) { return; }
    glUseProgram(program);
}

int device::attribute_location(unsigned program, const std::string & name) const {
    return ok() ? glGetAttribLocation(program, name.c_str()) : -1;
}

int device::uniform_location(unsigned program, const std::string & name) const {
    return ok() ? glGetUniformLocation(program, name.c_str()) : -1;
}

unsigned device::create_buffer() {
    if (!ok()) { return 0; }
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    return buffer;
}

void device::bind_buffer(int target, unsigned buffer) {
    if (!ok()) { return; }
    glBindBuffer(static_cast<GLenum>(target), buffer);
}

void device::buffer_data(int target, const void * bytes, std::size_t size, int usage) {
    if (!ok()) { return; }
    glBufferData(static_cast<GLenum>(target), static_cast<GLsizeiptr>(size), bytes,
                 static_cast<GLenum>(usage));
}

void device::enable_attribute(unsigned location, bool on) {
    if (!ok()) { return; }
    if (on) {
        glEnableVertexAttribArray(location);
    } else {
        glDisableVertexAttribArray(location);
    }
}

void device::attribute_pointer(unsigned location, int size, int type, bool normalised, int stride,
                               std::size_t offset) {
    if (!ok()) { return; }
    glVertexAttribPointer(location, size, static_cast<GLenum>(type),
                          normalised ? GL_TRUE : GL_FALSE, stride,
                          reinterpret_cast<const void *>(offset));
}

void device::draw_arrays(int mode, int first, int count) {
    if (!ok()) { return; }
    glDrawArrays(static_cast<GLenum>(mode), first, count);
}

void device::depth_func(int how) {
    if (!ok()) { return; }
    glDepthFunc(static_cast<GLenum>(how));
}

void device::depth_mask(bool on) {
    if (!ok()) { return; }
    glDepthMask(on ? GL_TRUE : GL_FALSE);
}

void device::blend_func(int source, int destination) {
    if (!ok()) { return; }
    glBlendFunc(static_cast<GLenum>(source), static_cast<GLenum>(destination));
}

unsigned device::create_texture() {
    if (!ok()) { return 0; }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    return texture;
}

void device::bind_texture(int target, unsigned texture) {
    if (!ok()) { return; }
    glBindTexture(static_cast<GLenum>(target), texture);
}

void device::active_texture(int unit) {
    if (!ok()) { return; }
    glActiveTexture(static_cast<GLenum>(unit));
}

void device::texture_image(int target, int width, int height, const void * rgba) {
    if (!ok()) { return; }
    glTexImage2D(static_cast<GLenum>(target), 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    // NO MIPMAPS AND LINEAR FILTERING, because a WebGL 1 page's default
    // minification filter needs a full mip chain and this uploads one level.
    // Left at the default, every texture samples BLACK - which looks like a
    // missing texture and is really a missing filter setting.
    glTexParameteri(static_cast<GLenum>(target), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(static_cast<GLenum>(target), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(static_cast<GLenum>(target), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(static_cast<GLenum>(target), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void device::texture_parameter(int target, int name, int value) {
    if (!ok()) { return; }
    glTexParameteri(static_cast<GLenum>(target), static_cast<GLenum>(name), value);
}

void device::draw_elements(int mode, int count, int type, std::size_t offset) {
    if (!ok()) { return; }
    glDrawElements(static_cast<GLenum>(mode), count, static_cast<GLenum>(type),
                   reinterpret_cast<const void *>(offset));
}

void device::set_uniform(unsigned program, const std::string & name, const float * values,
                         int count, int rows, int cols, bool integer) {
    if (!ok() || values == nullptr) { return; }
    const GLint location = glGetUniformLocation(program, name.c_str());
    // -1 IS NOT AN ERROR. A uniform the shader does not use is optimised out,
    // and a page setting one is doing nothing wrong - GL ignores it, and so
    // must this, or every page with a spare uniform reports a fault.
    if (location < 0) { return; }

    if (cols > 1) {
        // A MATRIX, AND NEVER TRANSPOSED. WebGL's uniformMatrix*fv takes a
        // `transpose` flag that ES requires to be false, and the data is
        // column-major already.
        switch (cols) {
        case 2: glUniformMatrix2fv(location, count, GL_FALSE, values); break;
        case 3: glUniformMatrix3fv(location, count, GL_FALSE, values); break;
        case 4: glUniformMatrix4fv(location, count, GL_FALSE, values); break;
        default: break;
        }
        return;
    }
    if (integer) {
        // Rebuilt as ints: the software path holds every value as a float, so
        // an int uniform arrives here as one.
        std::vector<GLint> whole(static_cast<std::size_t>(rows) * static_cast<std::size_t>(count));
        for (std::size_t i = 0; i < whole.size(); ++i) { whole[i] = static_cast<GLint>(values[i]); }
        switch (rows) {
        case 1: glUniform1iv(location, count, whole.data()); break;
        case 2: glUniform2iv(location, count, whole.data()); break;
        case 3: glUniform3iv(location, count, whole.data()); break;
        case 4: glUniform4iv(location, count, whole.data()); break;
        default: break;
        }
        return;
    }
    switch (rows) {
    case 1: glUniform1fv(location, count, values); break;
    case 2: glUniform2fv(location, count, values); break;
    case 3: glUniform3fv(location, count, values); break;
    case 4: glUniform4fv(location, count, values); break;
    default: break;
    }
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
void device::viewport(int, int, int, int) {}
void device::set_capability(int, bool) {}
unsigned device::create_shader(int) {
    return 0;
}
void device::shader_source(unsigned, const std::string &) {}
void device::compile_shader(unsigned) {}
bool device::shader_compiled(unsigned) const {
    return false;
}
std::string device::shader_log(unsigned) const {
    return {};
}
unsigned device::create_program() {
    return 0;
}
void device::attach_shader(unsigned, unsigned) {}
void device::link_program(unsigned) {}
bool device::program_linked(unsigned) const {
    return false;
}
std::string device::program_log(unsigned) const {
    return {};
}
void device::use_program(unsigned) {}
int device::attribute_location(unsigned, const std::string &) const {
    return -1;
}
int device::uniform_location(unsigned, const std::string &) const {
    return -1;
}
unsigned device::create_buffer() {
    return 0;
}
void device::bind_buffer(int, unsigned) {}
void device::buffer_data(int, const void *, std::size_t, int) {}
void device::enable_attribute(unsigned, bool) {}
void device::attribute_pointer(unsigned, int, int, bool, int, std::size_t) {}
void device::draw_arrays(int, int, int) {}
void device::draw_elements(int, int, int, std::size_t) {}
void device::depth_func(int) {}
void device::depth_mask(bool) {}
void device::blend_func(int, int) {}
unsigned device::create_texture() {
    return 0;
}
void device::bind_texture(int, unsigned) {}
void device::active_texture(int) {}
void device::texture_image(int, int, int, const void *) {}
void device::texture_parameter(int, int, int) {}
void device::set_uniform(unsigned, const std::string &, const float *, int, int, int, bool) {}
bool device::read_pixels(paint::bitmap &) const {
    return false;
}

#endif // CTBROWSER_WITH_ANGLE

} // namespace ctbrowser::raster::gles
