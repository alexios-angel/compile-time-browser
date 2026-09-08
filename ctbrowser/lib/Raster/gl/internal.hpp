#pragma once
// Private to lib/Raster/gl/. NOT installed and in no file set:
// include/ctbrowser/raster/gl.hpp declares the device whole, and this exists
// only so its implementation can be more than one file - it was 1,162 lines
// in one until 2026-09-08. The includes are gl.cpp's, so every file here
// sees exactly what that one saw, and the pimpl is here because every file
// reaches through it.

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
// gl31.h rather than gl3.h so the 3.1 tokens are DECLARED; the context this
// file creates is ES 3.0 (see eglCreateContext below) and nothing here calls a
// 3.1 entry point.
#include <GLES2/gl2ext.h>
#endif

namespace ctbrowser::raster::gl {

#if CTBROWSER_WITH_ANGLE

struct device::impl {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    // KEPT so a resize can build a matching surface without choosing again.
    EGLConfig config{};
    int width = 0;
    int height = 0;
    bool announced_draw = false;
    std::string error;

    ~impl() {
        if (display == EGL_NO_DISPLAY) { return; }
        if (context != EGL_NO_CONTEXT) { eglDestroyContext(display, context); }
        if (surface != EGL_NO_SURFACE) { eglDestroySurface(display, surface); }
        eglTerminate(display);
    }
};

#else

// WITHOUT ANGLE the device does not exist, and says so rather than pretending.
struct device::impl {
    std::string error = "built without ANGLE - run tools/fetch-angle.sh and configure again";
};

#endif

} // namespace ctbrowser::raster::gl
