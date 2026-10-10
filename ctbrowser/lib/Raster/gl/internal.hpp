//===- ctbrowser/lib/Raster/gl/internal.hpp - GL device pimpl ---*- C++ -*-===//
//
// Private to lib/Raster/gl/; not installed and in no file set.
// include/ctbrowser/raster/gl.hpp declares the device, and this header lets
// its implementation span context.cpp, draw.cpp, objects.cpp and stub.cpp: it
// holds the includes they all need and the pimpl (device::impl) they all reach
// through.
//
//===----------------------------------------------------------------------===//
#pragma once

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

// A separate include block on purpose, and the blank line matters. gl2ext.h
// includes nothing (it completes a core header rather than standing alone), so
// it has to come second. `SortIncludes` would put GLES2 before GLES3 and break
// the build; `IncludeBlocks: Preserve` sorts within a block and never across
// one, so the blank line is what keeps the order.
//
// gl31.h rather than gl3.h so the 3.1 tokens are declared. The context is
// still ES 3.0 (see eglCreateContext in context.cpp) and nothing calls a 3.1
// entry point.
#include <GLES2/gl2ext.h>
#endif

namespace ctbrowser::raster::gl {

#if CTBROWSER_WITH_ANGLE

/// The ANGLE-backed device state: the EGL handles it owns, and the last error.
struct device::impl {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    /// Kept so a resize can build a matching surface without choosing again.
    EGLConfig config{};
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

#else

/// Without ANGLE the device does not exist, and says so rather than pretending.
struct device::impl {
    std::string error = "built without ANGLE - run tools/fetch-angle.sh and configure again";
};

#endif

} // namespace ctbrowser::raster::gl
