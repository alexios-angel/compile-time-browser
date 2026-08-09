// Stage 0 of docs/plans/angle.md: does ANGLE render here, and how fast.
//
// Surfaceless EGL, one full-screen triangle, read back into a bitmap - which is
// exactly the shape a canvas would need, since a page's canvas is a
// paint::bitmap that the software painter composites. The readback is measured
// SEPARATELY because the plan says it could eat the win.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
const char * vertex_src = R"(#version 300 es
in vec2 at;
out vec2 uv;
void main() { uv = at * 0.5 + 0.5; gl_Position = vec4(at, 0.0, 1.0); }
)";
// DELIBERATELY NOT TRIVIAL. A shader that writes a constant measures the
// rasteriser and not the shader, and the number this is compared against - 1.03
// M frag/s - came from p5's lightTextureFrag, which does real work per fragment.
const char * fragment_src = R"(#version 300 es
precision highp float;
in vec2 uv;
out vec4 colour;
void main() {
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 8; i++) {
    float f = float(i) + 1.0;
    acc += vec3(sin(uv.x * f), cos(uv.y * f), sin((uv.x + uv.y) * f)) / f;
  }
  colour = vec4(abs(acc) * 0.3 + vec3(uv, 0.5) * 0.4, 1.0);
}
)";

GLuint make_shader(GLenum kind, const char * src) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::printf("shader failed: %s\n", log);
        std::exit(1);
    }
    return s;
}
} // namespace

int main() {
    // THE HEADLESS PATH, AND THE DEVICE ASKED FOR BY NAME.
    //
    // There is no window and there will not be one: a canvas is an offscreen
    // surface the page's painter composites. `eglGetDisplay(EGL_DEFAULT_DISPLAY)`
    // asks ANGLE to pick, and it picked the host's Vulkan - which here has no
    // VK_EXT_headless_surface, so initialisation failed. SwiftShader is
    // ANGLE'S OWN software Vulkan, built beside it, and asking for it by name
    // is both what works and what a deterministic golden would want.
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (get_platform_display == nullptr) {
        std::printf("no eglGetPlatformDisplayEXT\n");
        return 1;
    }
    // EGLint, NOT EGLAttrib. eglGetPlatformDisplayEXT takes 32-bit attributes
    // and eglGetPlatformDisplay takes pointer-sized ones; building the 64-bit
    // array and casting it made every second word read as zero, and the only
    // symptom was EGL_NO_DISPLAY with nothing logged.
    const EGLint display_attrs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE,
                                    EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
                                    EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
                                    EGL_PLATFORM_ANGLE_DEVICE_TYPE_SWIFTSHADER_ANGLE, EGL_NONE};
    EGLDisplay display =
        get_platform_display(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, display_attrs);
    if (display == EGL_NO_DISPLAY) {
        std::printf("no display\n");
        return 1;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        std::printf("eglInitialize failed: 0x%x\n", eglGetError());
        return 1;
    }
    std::printf("EGL %d.%d  vendor=%s\n", major, minor, eglQueryString(display, EGL_VENDOR));

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
                                   EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    if (!eglChooseConfig(display, config_attrs, &config, 1, &count) || count == 0) {
        std::printf("no config\n");
        return 1;
    }
    const int side = 512;
    const EGLint surface_attrs[] = {EGL_WIDTH, side, EGL_HEIGHT, side, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attrs);
    if (surface == EGL_NO_SURFACE) {
        std::printf("no pbuffer: 0x%x\n", eglGetError());
        return 1;
    }

    const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    if (context == EGL_NO_CONTEXT) {
        std::printf("no context\n");
        return 1;
    }
    eglMakeCurrent(display, surface, surface, context);

    std::printf("GL_RENDERER = %s\n", glGetString(GL_RENDERER));
    std::printf("GL_VERSION  = %s\n", glGetString(GL_VERSION));

    GLuint program = glCreateProgram();
    glAttachShader(program, make_shader(GL_VERTEX_SHADER, vertex_src));
    glAttachShader(program, make_shader(GL_FRAGMENT_SHADER, fragment_src));
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        std::printf("link failed\n");
        return 1;
    }
    glUseProgram(program);

    // A full-screen triangle: three vertices, every pixel covered once.
    const float verts[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    const GLint at = glGetAttribLocation(program, "at");
    glEnableVertexAttribArray(static_cast<GLuint>(at));
    glVertexAttribPointer(static_cast<GLuint>(at), 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glViewport(0, 0, side, side);

    // --- does it draw at all -------------------------------------------------
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(side) * side * 4);
    glReadPixels(0, 0, side, side, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::printf("centre pixel = %d,%d,%d  (blue clear would be 0,0,255)\n",
                pixels[(static_cast<std::size_t>(side) * side / 2 + side / 2) * 4],
                pixels[(static_cast<std::size_t>(side) * side / 2 + side / 2) * 4 + 1],
                pixels[(static_cast<std::size_t>(side) * side / 2 + side / 2) * 4 + 2]);

    // --- how fast, without readback -----------------------------------------
    const int rounds = 200;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i) { glDrawArrays(GL_TRIANGLES, 0, 3); }
    glFinish();
    auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    const double fragments = static_cast<double>(rounds) * side * side;
    std::printf("\nDRAW ONLY   %.2f M frag/s  (%.3f ms per %dx%d pass)\n",
                fragments / seconds / 1e6, seconds / rounds * 1000.0, side, side);

    // --- and with the readback the canvas would need -------------------------
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i) {
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(0, 0, side, side, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    glFinish();
    end = std::chrono::steady_clock::now();
    const double with_read = std::chrono::duration<double>(end - start).count();
    std::printf("WITH READBACK %.2f M frag/s  (%.3f ms per pass)\n", fragments / with_read / 1e6,
                with_read / rounds * 1000.0);
    std::printf("\nthe software interpreter, for comparison:   1.03 M frag/s\n");
    return 0;
}
