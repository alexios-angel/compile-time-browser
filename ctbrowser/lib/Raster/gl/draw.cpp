// gl::device - the draw calls, the fixed-function state around them, and
// reading the pixels back.
//
// One of four files carved out of a 1,162-line Raster/gl.cpp on 2026-09-08.
// All are member functions of the one class declared in
// include/ctbrowser/raster/gl.hpp; the pimpl both branches define is in
// internal.hpp beside this, with the includes gl.cpp had. Nothing about the
// public header changed.

#include "internal.hpp"

namespace ctbrowser::raster::gl {

#if CTBROWSER_WITH_ANGLE

// WHAT STATE THE FIRST DRAW WAS MADE IN, once, under CTBROWSER_GL_DEBUG.
//
// "The picture is wrong but GL reported nothing" is almost always pipeline
// state, and every one of those facts is a glGetIntegerv away - but only if
// somebody asks. Nobody did, and the p5 WEBGL page differed from its golden by
// exactly one such fact for a whole session.
void device::note_first_draw() {
    if (impl_->announced_draw) { return; }
    const char * want = std::getenv("CTBROWSER_GL_DEBUG");
    if (want == nullptr || std::string_view{want} == "0") { return; }
    impl_->announced_draw = true;

    GLint bits = 0;
    GLint func = 0;
    GLint write = 0;
    GLint framebuffer = 0;
    GLint program = 0;
    GLint array = 0;
    glGetIntegerv(GL_DEPTH_BITS, &bits);
    glGetIntegerv(GL_DEPTH_FUNC, &func);
    glGetIntegerv(GL_DEPTH_WRITEMASK, &write);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &array);
    std::fprintf(stderr,
                 "[draw] depth: test=%d bits=%d func=0x%04X write=%d | blend=%d cull=%d | "
                 "framebuffer=%d program=%d vao=%d\n",
                 static_cast<int>(glIsEnabled(GL_DEPTH_TEST)), bits, static_cast<unsigned>(func),
                 write, static_cast<int>(glIsEnabled(GL_BLEND)),
                 static_cast<int>(glIsEnabled(GL_CULL_FACE)), framebuffer, program, array);
}

void device::draw_arrays(int mode, int first, int count) {
    note_first_draw();
    if (ok()) { glDrawArrays(static_cast<GLenum>(mode), first, count); }
}

void device::draw_elements(int mode, int count, int type, std::size_t offset) {
    note_first_draw();
    if (!ok()) { return; }
    glDrawElements(static_cast<GLenum>(mode), count, static_cast<GLenum>(type),
                   reinterpret_cast<const void *>(offset));
}

void device::draw_arrays_instanced(int mode, int first, int count, int instances) {
    note_first_draw();
    if (ok()) { glDrawArraysInstanced(static_cast<GLenum>(mode), first, count, instances); }
}

void device::draw_elements_instanced(int mode, int count, int type, std::size_t offset,
                                     int instances) {
    note_first_draw();
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

    // THIS DEVICE'S CONTEXT, not whichever one happens to be current.
    //
    // make_current() ran once, at construction. GL's rule is that a second
    // device on the same thread makes the first one's context stale, and
    // present() walks EVERY canvas on the page - so with two of them the second
    // read the first's pixels out of the wrong context. One canvas is the only
    // case that ever worked, and it worked by coincidence.
    eglMakeCurrent(impl_->display, impl_->surface, impl_->surface, impl_->context);

    // THE DRAWING BUFFER, not whatever the page left bound. A page that ends a
    // frame with a render target still bound - which Babylon does routinely -
    // had that target read into its canvas instead of the picture it drew.
    //
    // Put back afterwards: this is a read, and a read that changes the binding
    // is a state mutation the page never asked for.
    GLint bound = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
    if (bound != 0) { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

    const int w = impl_->width;
    const int h = impl_->height;
    if (into.width != w || into.height != h) { into = paint::bitmap{w, h}; }

    // No glFinish: glReadPixels already orders itself after everything that
    // affects the pixels it reads, and an explicit one would only add a stall
    // ANGLE is already reporting under KHR_debug.
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (bound != 0) { glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound)); }

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

#endif

} // namespace ctbrowser::raster::gl
