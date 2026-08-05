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

namespace {

[[nodiscard]] std::string log_of(unsigned object, bool is_program) {
    GLint length = 0;
    if (is_program) {
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
    } else {
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    }
    if (length <= 1) { return {}; }
    std::string text(static_cast<std::size_t>(length), '\0');
    GLsizei written = 0;
    if (is_program) {
        glGetProgramInfoLog(object, length, &written, text.data());
    } else {
        glGetShaderInfoLog(object, length, &written, text.data());
    }
    text.resize(static_cast<std::size_t>(written));
    return text;
}

// ASKED OF THE PROGRAM, one index at a time, which is the only source that can
// answer correctly. `enumerate` is shared because glGetActiveAttrib and
// glGetActiveUniform differ only in which pair of entry points they call.
template <typename Count, typename Get>
[[nodiscard]] std::vector<device::active> enumerate(unsigned program, Count count, Get get) {
    GLint total = 0;
    count(program, &total);
    GLint longest = 0;
    std::vector<device::active> out;
    for (GLint i = 0; i < total; ++i) {
        std::string name(256, '\0');
        GLsizei written = 0;
        GLint size = 0;
        GLenum type = 0;
        get(program, static_cast<GLuint>(i), static_cast<GLsizei>(name.size()), &written, &size,
            &type, name.data());
        name.resize(static_cast<std::size_t>(written));
        out.push_back({name, static_cast<std::uint32_t>(type), static_cast<int>(size)});
    }
    (void)longest;
    return out;
}

} // namespace

unsigned device::create_shader(int kind) {
    return ok() ? glCreateShader(static_cast<GLenum>(kind)) : 0u;
}

void device::shader_source(unsigned shader, const std::string & source) {
    if (!ok()) { return; }
    const char * text = source.c_str();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
}

void device::compile_shader(unsigned shader) {
    if (ok()) { glCompileShader(shader); }
}

bool device::shader_compiled(unsigned shader) const {
    if (!ok()) { return false; }
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status == GL_TRUE;
}

std::string device::shader_log(unsigned shader) const {
    return ok() ? log_of(shader, false) : std::string{};
}

unsigned device::create_program() {
    return ok() ? glCreateProgram() : 0u;
}

void device::attach_shader(unsigned program, unsigned shader) {
    if (ok()) { glAttachShader(program, shader); }
}

void device::link_program(unsigned program) {
    if (ok()) { glLinkProgram(program); }
}

bool device::program_linked(unsigned program) const {
    if (!ok()) { return false; }
    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    return status == GL_TRUE;
}

std::string device::program_log(unsigned program) const {
    return ok() ? log_of(program, true) : std::string{};
}

void device::use_program(unsigned program) {
    if (ok()) { glUseProgram(program); }
}

unsigned device::program_in_use() const {
    if (!ok()) { return 0u; }
    GLint current = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current);
    return static_cast<unsigned>(current);
}

std::vector<device::active> device::active_attributes(unsigned program) const {
    if (!ok()) { return {}; }
    return enumerate(
        program, [](unsigned p, GLint * n) { glGetProgramiv(p, GL_ACTIVE_ATTRIBUTES, n); },
        glGetActiveAttrib);
}

std::vector<device::active> device::active_uniforms(unsigned program) const {
    if (!ok()) { return {}; }
    return enumerate(
        program, [](unsigned p, GLint * n) { glGetProgramiv(p, GL_ACTIVE_UNIFORMS, n); },
        glGetActiveUniform);
}

int device::attribute_location(unsigned program, const std::string & name) const {
    return ok() ? glGetAttribLocation(program, name.c_str()) : -1;
}

int device::uniform_location(unsigned program, const std::string & name) const {
    return ok() ? glGetUniformLocation(program, name.c_str()) : -1;
}

int device::uniform_block_index(unsigned program, const std::string & name) const {
    if (!ok()) { return -1; }
    const GLuint index = glGetUniformBlockIndex(program, name.c_str());
    return index == GL_INVALID_INDEX ? -1 : static_cast<int>(index);
}

void device::uniform_block_binding(unsigned program, unsigned index, unsigned binding) {
    if (ok()) { glUniformBlockBinding(program, index, binding); }
}

void device::set_uniform(int location, const float * values, int count, int rows, int cols,
                         bool integer) {
    if (!ok() || location < 0 || values == nullptr || count <= 0) { return; }
    if (cols > 1) {
        // A MATRIX. Never transposed: WebGL requires the flag to be false and a
        // page's data is already column-major.
        const GLsizei n = static_cast<GLsizei>(count);
        if (rows == 2) { glUniformMatrix2fv(location, n, GL_FALSE, values); }
        if (rows == 3) { glUniformMatrix3fv(location, n, GL_FALSE, values); }
        if (rows == 4) { glUniformMatrix4fv(location, n, GL_FALSE, values); }
        return;
    }
    const GLsizei n = static_cast<GLsizei>(count);
    if (integer) {
        std::vector<GLint> whole(static_cast<std::size_t>(count) * static_cast<std::size_t>(rows));
        for (std::size_t i = 0; i < whole.size(); ++i) { whole[i] = static_cast<GLint>(values[i]); }
        if (rows == 1) { glUniform1iv(location, n, whole.data()); }
        if (rows == 2) { glUniform2iv(location, n, whole.data()); }
        if (rows == 3) { glUniform3iv(location, n, whole.data()); }
        if (rows == 4) { glUniform4iv(location, n, whole.data()); }
        return;
    }
    if (rows == 1) { glUniform1fv(location, n, values); }
    if (rows == 2) { glUniform2fv(location, n, values); }
    if (rows == 3) { glUniform3fv(location, n, values); }
    if (rows == 4) { glUniform4fv(location, n, values); }
}

unsigned device::create_buffer() {
    if (!ok()) { return 0u; }
    GLuint name = 0;
    glGenBuffers(1, &name);
    return name;
}

void device::bind_buffer(int target, unsigned buffer) {
    if (ok()) { glBindBuffer(static_cast<GLenum>(target), buffer); }
}

void device::buffer_data(int target, const void * bytes, std::size_t size, int usage) {
    if (!ok()) { return; }
    glBufferData(static_cast<GLenum>(target), static_cast<GLsizeiptr>(size), bytes,
                 static_cast<GLenum>(usage));
}

void device::buffer_sub_data(int target, std::size_t offset, const void * bytes, std::size_t size) {
    if (!ok() || bytes == nullptr) { return; }
    glBufferSubData(static_cast<GLenum>(target), static_cast<GLintptr>(offset),
                    static_cast<GLsizeiptr>(size), bytes);
}

void device::bind_buffer_base(int target, unsigned index, unsigned buffer) {
    if (ok()) { glBindBufferBase(static_cast<GLenum>(target), index, buffer); }
}

void device::delete_object(unsigned name) {
    if (!ok() || name == 0) { return; }
    // ASK, DO NOT REMEMBER. Each glIs* answers for its own namespace, and a
    // handle belongs to exactly one of them.
    if (glIsBuffer(name) == GL_TRUE) { glDeleteBuffers(1, &name); }
    if (glIsTexture(name) == GL_TRUE) { glDeleteTextures(1, &name); }
    if (glIsFramebuffer(name) == GL_TRUE) { glDeleteFramebuffers(1, &name); }
    if (glIsRenderbuffer(name) == GL_TRUE) { glDeleteRenderbuffers(1, &name); }
    if (glIsProgram(name) == GL_TRUE) { glDeleteProgram(name); }
    if (glIsShader(name) == GL_TRUE) { glDeleteShader(name); }
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

void device::attribute_divisor(unsigned location, unsigned divisor) {
    if (ok()) { glVertexAttribDivisor(location, divisor); }
}

device::attribute_state device::attribute_at(unsigned location) const {
    attribute_state out;
    if (!ok()) { return out; }
    GLint value = 0;
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &value);
    out.enabled = value != 0;
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_SIZE, &value);
    out.size = value;
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &value);
    out.stride = value;
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_TYPE, &value);
    out.type = static_cast<std::uint32_t>(value);
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &value);
    out.normalised = value != 0;
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &value);
    out.divisor = static_cast<std::uint32_t>(value);
    return out;
}

unsigned device::create_vertex_array() {
    if (!ok()) { return 0u; }
    GLuint name = 0;
    glGenVertexArrays(1, &name);
    return name;
}

void device::bind_vertex_array(unsigned array) {
    if (ok()) { glBindVertexArray(array); }
}

void device::delete_vertex_array(unsigned array) {
    if (!ok() || array == 0) { return; }
    glDeleteVertexArrays(1, &array);
}

bool device::is_vertex_array(unsigned array) const {
    return ok() && glIsVertexArray(array) == GL_TRUE;
}

unsigned device::bound_vertex_array() const {
    if (!ok()) { return 0u; }
    GLint current = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current);
    return static_cast<unsigned>(current);
}

unsigned device::create_texture() {
    if (!ok()) { return 0u; }
    GLuint name = 0;
    glGenTextures(1, &name);
    return name;
}

void device::bind_texture(int target, unsigned texture) {
    if (ok()) { glBindTexture(static_cast<GLenum>(target), texture); }
}

void device::active_texture(int unit) {
    if (ok()) { glActiveTexture(static_cast<GLenum>(unit)); }
}

void device::texture_image(int target, int width, int height, const void * rgba) {
    if (!ok()) { return; }
    glTexImage2D(static_cast<GLenum>(target), 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
}

void device::texture_parameter(int target, int name, int value) {
    if (ok()) { glTexParameteri(static_cast<GLenum>(target), static_cast<GLenum>(name), value); }
}

unsigned device::create_framebuffer() {
    if (!ok()) { return 0u; }
    GLuint name = 0;
    glGenFramebuffers(1, &name);
    return name;
}

void device::bind_framebuffer(unsigned framebuffer) {
    if (ok()) { glBindFramebuffer(GL_FRAMEBUFFER, framebuffer); }
}

void device::framebuffer_texture(int attachment, unsigned texture) {
    if (!ok()) { return; }
    glFramebufferTexture2D(GL_FRAMEBUFFER, static_cast<GLenum>(attachment), GL_TEXTURE_2D, texture,
                           0);
}

std::uint32_t device::framebuffer_status() const {
    return ok() ? static_cast<std::uint32_t>(glCheckFramebufferStatus(GL_FRAMEBUFFER)) : 0u;
}

void device::draw_arrays(int mode, int first, int count) {
    if (ok()) { glDrawArrays(static_cast<GLenum>(mode), first, count); }
}

void device::draw_elements(int mode, int count, int type, std::size_t offset) {
    if (!ok()) { return; }
    glDrawElements(static_cast<GLenum>(mode), count, static_cast<GLenum>(type),
                   reinterpret_cast<const void *>(offset));
}

void device::draw_arrays_instanced(int mode, int first, int count, int instances) {
    if (ok()) { glDrawArraysInstanced(static_cast<GLenum>(mode), first, count, instances); }
}

void device::draw_elements_instanced(int mode, int count, int type, std::size_t offset,
                                     int instances) {
    if (!ok()) { return; }
    glDrawElementsInstanced(static_cast<GLenum>(mode), count, static_cast<GLenum>(type),
                            reinterpret_cast<const void *>(offset), instances);
}

void device::cull_face(int which) {
    if (ok()) { glCullFace(static_cast<GLenum>(which)); }
}

void device::front_face(int which) {
    if (ok()) { glFrontFace(static_cast<GLenum>(which)); }
}

void device::depth_func(int how) {
    if (ok()) { glDepthFunc(static_cast<GLenum>(how)); }
}

void device::depth_mask(bool on) {
    if (ok()) { glDepthMask(on ? GL_TRUE : GL_FALSE); }
}

void device::blend_func(int source, int destination) {
    if (ok()) { glBlendFunc(static_cast<GLenum>(source), static_cast<GLenum>(destination)); }
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

unsigned device::create_shader(int) {
    return 0u;
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
    return 0u;
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
unsigned device::program_in_use() const {
    return 0u;
}
std::vector<device::active> device::active_attributes(unsigned) const {
    return {};
}
std::vector<device::active> device::active_uniforms(unsigned) const {
    return {};
}
int device::attribute_location(unsigned, const std::string &) const {
    return -1;
}
int device::uniform_location(unsigned, const std::string &) const {
    return -1;
}
int device::uniform_block_index(unsigned, const std::string &) const {
    return -1;
}
void device::uniform_block_binding(unsigned, unsigned, unsigned) {}
void device::set_uniform(int, const float *, int, int, int, bool) {}

unsigned device::create_buffer() {
    return 0u;
}
void device::bind_buffer(int, unsigned) {}
void device::buffer_data(int, const void *, std::size_t, int) {}
void device::buffer_sub_data(int, std::size_t, const void *, std::size_t) {}
void device::bind_buffer_base(int, unsigned, unsigned) {}
void device::delete_object(unsigned) {}
unsigned device::create_texture() {
    return 0u;
}
void device::bind_texture(int, unsigned) {}
void device::active_texture(int) {}
void device::texture_image(int, int, int, const void *) {}
void device::texture_parameter(int, int, int) {}
unsigned device::create_framebuffer() {
    return 0u;
}
void device::bind_framebuffer(unsigned) {}
void device::framebuffer_texture(int, unsigned) {}
std::uint32_t device::framebuffer_status() const {
    return 0u;
}
void device::draw_arrays(int, int, int) {}
void device::draw_elements(int, int, int, std::size_t) {}
void device::draw_arrays_instanced(int, int, int, int) {}
void device::draw_elements_instanced(int, int, int, std::size_t, int) {}
void device::cull_face(int) {}
void device::front_face(int) {}
void device::depth_func(int) {}
void device::depth_mask(bool) {}
void device::blend_func(int, int) {}
void device::enable_attribute(unsigned, bool) {}
void device::attribute_pointer(unsigned, int, int, bool, int, std::size_t) {}
void device::attribute_divisor(unsigned, unsigned) {}
device::attribute_state device::attribute_at(unsigned) const {
    return {};
}
unsigned device::create_vertex_array() {
    return 0u;
}
void device::bind_vertex_array(unsigned) {}
void device::delete_vertex_array(unsigned) {}
bool device::is_vertex_array(unsigned) const {
    return false;
}
unsigned device::bound_vertex_array() const {
    return 0u;
}

bool device::read_pixels(paint::bitmap &) const {
    return false;
}

#endif

} // namespace ctbrowser::raster::gl
