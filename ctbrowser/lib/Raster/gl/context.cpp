// gl::device - the EGL side: the driver, the display, the context and the
// surface, and the state that is global to a context: viewport, scissor, the
// clears, capabilities, errors and limits.
//
// One of four files carved out of a 1,162-line Raster/gl.cpp on 2026-09-08.
// All are member functions of the one class declared in
// include/ctbrowser/raster/gl.hpp; the pimpl both branches define is in
// internal.hpp beside this, with the includes gl.cpp had. Nothing about the
// public header changed.

#include "internal.hpp"

namespace ctbrowser::raster::gl {

#if CTBROWSER_WITH_ANGLE

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

    // THE DRIVER, OVERRIDABLE AT RUN TIME.
    //
    // `CTBROWSER_GL_DRIVER=deterministic` forces SwiftShader and `=fastest` asks
    // for whatever the machine has. It matters most where the two actually
    // differ, which is nowhere the tests run: the devbox has no GPU, so both
    // land on SwiftShader, and the Windows .exe on real hardware is the only
    // place a page gets a GPU at all (docs/platform.md). Comparing the two is
    // otherwise a rebuild rather than a run.
    //
    // Named in the old notes as though it existed; it did not, and its absence
    // is why "is this a driver difference?" was never a question anyone could
    // answer in one command.
    if (const char * want = std::getenv("CTBROWSER_GL_DRIVER"); want != nullptr) {
        const std::string_view asked{want};
        if (asked == "deterministic" || asked == "swiftshader") {
            which = driver::deterministic;
        } else if (asked == "fastest") {
            which = driver::fastest;
        }
    }

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
    EGLint configs = 0;
    if (eglChooseConfig(impl_->display, config_attrs, &impl_->config, 1, &configs) != EGL_TRUE ||
        configs == 0) {
        impl_->error = "no EGL config with depth and stencil";
        return;
    }
    const EGLConfig config = impl_->config;

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

bool device::resize(int width, int height) {
    if (!ok()) { return false; }
    if (width == impl_->width && height == impl_->height) { return true; }

    // THE NEW SURFACE FIRST. If eglCreatePbufferSurface fails - a size beyond
    // EGL_MAX_PBUFFER_WIDTH, say - the old one is still current and the page
    // keeps the drawing buffer it had, which is a stale size rather than no
    // context at all.
    const EGLint attrs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    const EGLSurface fresh = eglCreatePbufferSurface(impl_->display, impl_->config, attrs);
    if (fresh == EGL_NO_SURFACE) { return false; }

    if (eglMakeCurrent(impl_->display, fresh, fresh, impl_->context) != EGL_TRUE) {
        eglDestroySurface(impl_->display, fresh);
        return false;
    }
    // ONLY NOW is the old one unreferenced. Destroying a surface while it is
    // still the read or draw surface is what EGL calls undefined.
    if (impl_->surface != EGL_NO_SURFACE) { eglDestroySurface(impl_->display, impl_->surface); }
    impl_->surface = fresh;
    impl_->width = width;
    impl_->height = height;
    return true;
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

int device::limit(int name) const {
    if (!ok()) { return 0; }
    GLint value = 0;
    glGetIntegerv(static_cast<GLenum>(name), &value);
    return value;
}

void device::clear(float red, float green, float blue, float alpha) {
    if (!ok()) { return; }
    glClearColor(red, green, blue, alpha);
    glClear(static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT) | GL_DEPTH_BUFFER_BIT |
            GL_STENCIL_BUFFER_BIT);
}

#endif

} // namespace ctbrowser::raster::gl
