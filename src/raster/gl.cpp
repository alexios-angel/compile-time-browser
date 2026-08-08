#include <ctbrowser/raster/gl.hpp>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if CTBROWSER_WITH_ANGLE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>

// SEPARATE BLOCK ON PURPOSE, and the blank line is load-bearing. gl2ext.h
// includes NOTHING - it completes a core header rather than standing alone - so
// it has to come second. `SortIncludes` would put GLES2 before GLES3 and break
// the build; `IncludeBlocks: Preserve` sorts within a block and never across
// one, so the blank line is what keeps the order.
//
// gl31.h rather than gl3.h because the context this file creates is ES 3.1.
#include <GLES2/gl2ext.h>
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

// WHOLE WORDS, SPACE SEPARATED. Every extension name asked about here is a
// prefix of some other one - `EGL_ANGLE_robust_resource_initialization` sits
// beside `..._initialization_2` in some builds - so a plain substring search
// reports an extension the driver does not have, and the attribute is then
// rejected by eglCreateContext.
//
// One function, because EGL and GL both answer with a space-separated list and
// two copies of this loop is how they drift.
[[nodiscard]] bool contains_word(std::string_view all, std::string_view name) {
    for (std::size_t at = all.find(name); at != std::string_view::npos;
         at = all.find(name, at + 1)) {
        const std::size_t end = at + name.size();
        const bool left = at == 0 || all[at - 1] == ' ';
        const bool right = end == all.size() || all[end] == ' ';
        if (left && right) { return true; }
    }
    return false;
}

// Does this display advertise an extension? A DISPLAY extension is only
// queryable after eglInitialize, which is why this is asked at construction
// rather than folded into a constant attribute list.
[[nodiscard]] bool has_extension(EGLDisplay display, std::string_view name) {
    const char * all = eglQueryString(display, EGL_EXTENSIONS);
    return all != nullptr && contains_word(all, name);
}

// ANGLE'S OWN WORDS, NOT A NUMBER.
//
// `glGetError` answers 0x0502 for a draw GL dropped, which names the category
// and not the defect. KHR_debug answers "An enabled vertex array has no
// buffer." - and the difference between those two strings is the difference
// between a pass of guessing and a fix. Every silent-draw defect this back end
// has had would have announced itself here.
void GL_APIENTRY report_gl_message(GLenum /*source*/, GLenum type, GLuint id, GLenum severity,
                                   GLsizei length, const GLchar * message, const void * /*user*/) {
    const std::string text = length < 0 || message == nullptr
                                 ? std::string{message == nullptr ? "" : message}
                                 : std::string{message, static_cast<std::size_t>(length)};
    std::fprintf(stderr, "[gl] type=0x%04X severity=0x%04X id=%u  %s\n",
                 static_cast<unsigned>(type), static_cast<unsigned>(severity),
                 static_cast<unsigned>(id), text.c_str());
}

// OFF unless `CTBROWSER_GL_DEBUG` is set, and free when it is not: with no
// callback installed ANGLE never formats a string.
//
// SYNCHRONOUS on purpose. An asynchronous callback arrives detached from the
// call that caused it, and a log you cannot attribute to a line is the kind of
// evidence that produced four wrong conclusions on this branch already.
//
// Resolved through eglGetProcAddress rather than linked: KHR_debug is an
// extension, the entry point is only present when the context has it, and
// asking is how you find out rather than assuming and failing to link.
void install_debug_callback() {
    const char * want = std::getenv("CTBROWSER_GL_DEBUG");
    if (want == nullptr || std::string_view{want} == "0") { return; }

    const GLubyte * extensions = glGetString(GL_EXTENSIONS);
    if (extensions == nullptr ||
        !contains_word(reinterpret_cast<const char *>(extensions), "GL_KHR_debug")) {
        std::fprintf(stderr, "[gl] CTBROWSER_GL_DEBUG is set but this context has no KHR_debug\n");
        return;
    }
    const auto set_callback = reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKKHRPROC>(
        eglGetProcAddress("glDebugMessageCallbackKHR"));
    if (set_callback == nullptr) {
        std::fprintf(stderr, "[gl] KHR_debug is advertised but glDebugMessageCallbackKHR is not\n");
        return;
    }
    glEnable(GL_DEBUG_OUTPUT_KHR);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
    set_callback(report_gl_message, nullptr);
    std::fprintf(stderr, "[gl] KHR_debug is on - ANGLE will name what it rejects\n");
}

} // namespace

bool available() {
    return unavailable_because().empty();
}

std::string unavailable_because() {
    // ASKS THE SAME QUESTION THE CONSTRUCTOR DOES, both drivers, or it answers
    // about a device nobody would have been given. The first version asked only
    // about `fastest` and reported the whole subsystem missing on a box where
    // SwiftShader works perfectly - which skipped the one test that would have
    // said so.
    for (const driver attempt : {driver::fastest, driver::deterministic}) {
        const EGLDisplay display = open_display(attempt);
        if (display == EGL_NO_DISPLAY) { continue; }
        if (eglInitialize(display, nullptr, nullptr) == EGL_TRUE) {
            eglTerminate(display);
            return {};
        }
    }
    const EGLDisplay display = open_display(driver::deterministic);
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

    // SAFE MODE IS A FLOOR, NOT A CLIFF. `fastest` asks for ANGLE's own device
    // selection, which on a headless box with no GPU cannot come up at all -
    // measured: VK_EXT_headless_surface and VK_KHR_surface are simply absent.
    //
    // Falling back to SwiftShader is what a browser does and what keeps a page
    // from getting a null context on a machine that can render perfectly well in
    // software. Reported through `renderer()` rather than silently, because
    // "ANGLE (Vulkan ... SwiftShader Device)" and a real GPU are the same code
    // path and very different numbers.
    for (const driver attempt : {which, driver::deterministic}) {
        impl_->display = open_display(attempt);
        if (impl_->display == EGL_NO_DISPLAY) { continue; }
        if (eglInitialize(impl_->display, nullptr, nullptr) == EGL_TRUE) {
            impl_->error.clear();
            break;
        }
        eglTerminate(impl_->display);
        impl_->display = EGL_NO_DISPLAY;
        impl_->error = "eglInitialize failed for the requested driver";
    }
    if (impl_->display == EGL_NO_DISPLAY) {
        if (impl_->error.empty()) { impl_->error = "no ANGLE platform display"; }
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

    // THE CONTEXT A BROWSER ASKS FOR, WHICH IS NOT THE ONE A NATIVE APP ASKS
    // FOR. ANGLE trusts a GLES client: a uniform buffer bound to a binding point
    // with no storage behind it is undefined behaviour, and the Vulkan back end
    // dereferences the null BufferHelper rather than complaining. The same
    // mistake in a WEB PAGE has to be an INVALID_OPERATION, and the switch that
    // makes it one is the context attribute Chrome sets and this did not.
    //
    // Robust resource initialisation is the second half of the same argument and
    // is load-bearing for a different reason: WebGL requires an untouched buffer
    // or texture to read as ZERO, and a byte-compared golden cannot survive
    // reading whatever the last allocation left in device memory.
    //
    // TEMPORARY, AND SAY SO: `CTBROWSER_GL_COMPAT=0` turns both off, so one
    // build can measure with and against - the discipline this work kept failing
    // on. It comes out when the question is answered.
    //
    // ES 3.0, NOT 3.1, AND THE VERSION IS LOAD-BEARING. Measured on the devbox
    // against SwiftShader, one attribute changed per run:
    //
    //     ES3.1 bare             OK
    //     ES3.1 +webgl           FAIL (EGL_BAD_MATCH 0x3009)
    //     ES3.1 +robust          OK
    //     ES3.0 +webgl           OK
    //     ES3.0 +webgl +robust   OK
    //
    // ANGLE REFUSES WebGL compatibility on an ES 3.1 context. Asking for 3.1
    // and then adding the attribute produced NO CONTEXT AT ALL - every WebGL
    // test failing with "eglCreateContext failed", which reads like ANGLE is
    // missing rather than like one attribute is wrong.
    //
    // 3.1 was over-asking in the first place: WebGL 2 is defined against GLES
    // 3.0, nothing in this file calls a 3.1 entry point, and 3.0 is what Chrome
    // requests. It also buys correct SHADER validation - ANGLE's
    // Compiler::SelectShaderSpec answers SH_WEBGL2_SPEC for minor 0 and
    // SH_GLES3_1_SPEC for minor 1, so 3.1 would have validated page shaders
    // against the wrong dialect even had the context come up.
    std::vector<EGLint> context_attrs{EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0};
    const char * compat = std::getenv("CTBROWSER_GL_COMPAT");
    if (compat == nullptr || std::string_view{compat} != "0") {
        if (has_extension(impl_->display, "EGL_ANGLE_create_context_webgl_compatibility")) {
            context_attrs.push_back(EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE);
            context_attrs.push_back(EGL_TRUE);
        }
        if (has_extension(impl_->display, "EGL_ANGLE_robust_resource_initialization")) {
            context_attrs.push_back(EGL_ROBUST_RESOURCE_INITIALIZATION_ANGLE);
            context_attrs.push_back(EGL_TRUE);
        }
    }
    context_attrs.push_back(EGL_NONE);
    impl_->context = eglCreateContext(impl_->display, config, EGL_NO_CONTEXT, context_attrs.data());
    if (impl_->context == EGL_NO_CONTEXT) {
        // THE EGL ERROR CODE, because the attribute list is the thing most
        // likely to be wrong and the code is the only part that says which way.
        // Without it this read "eglCreateContext failed" for a whole session
        // while the cause was one attribute the driver answers EGL_BAD_MATCH
        // (0x3009) to - indistinguishable, from the message alone, from ANGLE
        // not being installed.
        char code[32];
        std::snprintf(code, sizeof code, " (EGL error 0x%04X)",
                      static_cast<unsigned>(eglGetError()));
        impl_->error = "eglCreateContext failed for ES 3.0";
        impl_->error += code;
        return;
    }
    if (!make_current()) {
        impl_->error = "eglMakeCurrent failed";
        return;
    }
    // AFTER make_current, because a debug callback is context state and there
    // was no current context to attach it to until this line.
    install_debug_callback();
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

bool device::webgl_compatible() const {
    // ASKED OF GL, not remembered from the attribute list. eglCreateContext is
    // entitled to ignore an attribute it does not implement, so "we requested
    // it" and "we got it" are two different facts and only the second one is
    // worth reporting.
    if (!ok()) { return false; }
    return contains_word(ask(GL_EXTENSIONS), "GL_ANGLE_webgl_compatibility");
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

unsigned device::bound_buffer(int target) const {
    if (!ok()) { return 0u; }
    // A TARGET AND ITS BINDING QUERY ARE DIFFERENT NUMBERS, and there is no
    // arithmetic between them - GL_ARRAY_BUFFER is 0x8892 and its query 0x8894,
    // but GL_UNIFORM_BUFFER is 0x8A11 and its query 0x8A28. A table, not a
    // formula.
    GLenum query = 0;
    switch (static_cast<GLenum>(target)) {
    case GL_ARRAY_BUFFER: query = GL_ARRAY_BUFFER_BINDING; break;
    case GL_ELEMENT_ARRAY_BUFFER: query = GL_ELEMENT_ARRAY_BUFFER_BINDING; break;
    case GL_UNIFORM_BUFFER: query = GL_UNIFORM_BUFFER_BINDING; break;
    case GL_COPY_READ_BUFFER: query = GL_COPY_READ_BUFFER_BINDING; break;
    case GL_COPY_WRITE_BUFFER: query = GL_COPY_WRITE_BUFFER_BINDING; break;
    case GL_PIXEL_PACK_BUFFER: query = GL_PIXEL_PACK_BUFFER_BINDING; break;
    case GL_PIXEL_UNPACK_BUFFER: query = GL_PIXEL_UNPACK_BUFFER_BINDING; break;
    default: return 0u;
    }
    GLint bound = 0;
    glGetIntegerv(query, &bound);
    return static_cast<unsigned>(bound);
}

void device::delete_object(object_kind kind, unsigned name) {
    if (!ok() || name == 0) { return; }

    // WHAT ELSE WEARS THIS NUMBER. Off unless asked for, and it exists because
    // the bug it documents was invisible for five commits: the collision is
    // silent, GL raises nothing, and the damage only surfaces later as a
    // wrongly-sized buffer. One line per delete says whether the namespaces
    // really do overlap on this page rather than leaving it an argument.
    const char * trace = std::getenv("CTBROWSER_GL_DELETE");
    if (trace != nullptr && std::string_view{trace} != "0") {
        std::fprintf(stderr, "[del] kind=%d name=%u  also:%s%s%s%s%s%s\n", static_cast<int>(kind),
                     name, glIsBuffer(name) == GL_TRUE ? " buffer" : "",
                     glIsTexture(name) == GL_TRUE ? " texture" : "",
                     glIsFramebuffer(name) == GL_TRUE ? " framebuffer" : "",
                     glIsRenderbuffer(name) == GL_TRUE ? " renderbuffer" : "",
                     glIsProgram(name) == GL_TRUE ? " program" : "",
                     glIsShader(name) == GL_TRUE ? " shader" : "");
    }

    // SWITCH, DO NOT PROBE. The caller named the class; asking GL which classes
    // the number happens to belong to answers a different question and deletes
    // whatever else it finds.
    switch (kind) {
    case object_kind::buffer: glDeleteBuffers(1, &name); break;
    case object_kind::texture: glDeleteTextures(1, &name); break;
    case object_kind::framebuffer: glDeleteFramebuffers(1, &name); break;
    case object_kind::renderbuffer: glDeleteRenderbuffers(1, &name); break;
    case object_kind::program: glDeleteProgram(name); break;
    case object_kind::shader: glDeleteShader(name); break;
    }
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

    // A NAME IS NOT AN OBJECT UNTIL IT IS BOUND. glGenVertexArrays only
    // RESERVES the number; until the first glBindVertexArray, glIsVertexArray
    // answers GL_FALSE for it. That is GL's rule and it is not WebGL's -
    // `createVertexArray` creates the object, and a page that asks
    // `isVertexArray` about one it just made is told yes by every browser.
    //
    // So bind it once and put the previous binding back. Restoring matters:
    // creating a vertex array must not disturb the one the page is building,
    // and leaving the new one current would silently retarget every
    // vertexAttribPointer that followed.
    GLint previous = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous);
    glBindVertexArray(name);
    glBindVertexArray(static_cast<GLuint>(previous));
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
bool device::webgl_compatible() const {
    return false;
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
unsigned device::bound_buffer(int) const {
    return 0u;
}
void device::delete_object(object_kind, unsigned) {}
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
