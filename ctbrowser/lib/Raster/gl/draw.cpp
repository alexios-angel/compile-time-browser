//===- ctbrowser/lib/Raster/gl/draw.cpp - GL device: draws ------*- C++ -*-===//
//
// gl::device: the draw calls, the fixed-function state around them, and
// reading the pixels back.
//
// Member functions of gl::device (include/ctbrowser/raster/gl.hpp); the shared
// pimpl and includes are in internal.hpp beside this file.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::raster::gl {

#if CTBROWSER_WITH_ANGLE

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

    // Make this device's context current, not whichever one happens to be.
    //
    // make_current() ran once, at construction. A second device on the same
    // thread makes the first one's context stale, and present() walks every
    // canvas on the page, so without this a page with two canvases would read
    // one canvas's pixels out of the other's context.
    eglMakeCurrent(impl_->display, impl_->surface, impl_->surface, impl_->context);

    // Read the drawing buffer, not whatever the page left bound. A page that
    // ends a frame with a render target still bound (Babylon does routinely)
    // would otherwise get that target in its canvas instead of the picture it
    // drew.
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
        // Bottom-left to top-left: GL's origin is not a bitmap's.
        const std::size_t row = static_cast<std::size_t>(h - 1 - y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x) {
            const std::size_t at = (row + static_cast<std::size_t>(x)) * 4;
            // RGBA in, ARGB out.
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
