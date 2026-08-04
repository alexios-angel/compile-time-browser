#pragma once
#include <memory>
#include <string>

#include <ctbrowser/paint/command.hpp>

// A REAL OpenGL ES DEVICE, offscreen, for the WebGL path.
//
// Stage 1 of docs/angle-plan.md. The software rasteriser executes a page's
// shaders one fragment at a time and manages 1.03 M fragments per second;
// measured against the same shader, ANGLE does 192 M on Linux and 332 M on
// Windows. This is the seam that would let a page's draws go to the second one.
//
// NOTHING CALLS IT YET, deliberately. The plan keeps both paths alive behind a
// switch so "is ANGLE better" stays a measurement rather than a belief, and the
// first step is a device that can be created, drawn into and read back without
// anything depending on it.
//
// NO ANGLE TYPE APPEARS HERE, and that is a rule this repository enforces rather
// than prefers: `tests/api_surface` lints that no third-party header reaches a
// public one, because everyone who touches the engine pays for what a header
// includes. `core/cpu_time.hpp` is the pattern - one declaration, and its .cpp
// owns the platform. EGL and GLES live entirely inside gles.cpp.
//
// THERE IS NO WINDOW AND THERE WILL NOT BE ONE. A page's canvas is a
// paint::bitmap that the software painter composites, so this renders to an
// offscreen surface and hands the pixels back. That readback is the cost the
// plan worried would eat the win; it measured at 8%.

namespace ctbrowser::raster::gles {

// Whether a device can be made AT ALL: built with ANGLE, and the libraries
// actually load and initialise. Asked rather than assumed, for the same reason
// `ctbrowser::allocator_name()` is - a build flag reports an intention and this
// reports what happened.
[[nodiscard]] bool available();

// Why `available()` said no, for a report a person can act on. Empty when it
// said yes.
[[nodiscard]] std::string unavailable_because();

// An offscreen GLES device of a fixed size.
//
// ONE PER SURFACE, not one per process: a page can have several canvases and
// they do not share state. Non-copyable for the obvious reason - it owns a
// display, a surface and a context.
class device {
public:
    device(int width, int height);
    ~device();
    device(const device &) = delete;
    device & operator=(const device &) = delete;
    device(device &&) noexcept;
    device & operator=(device &&) noexcept;

    [[nodiscard]] bool ok() const noexcept;
    // Empty when ok(). The GL error string is not enough on its own - most of
    // what goes wrong here happens in EGL, before there is a context to ask.
    [[nodiscard]] const std::string & error() const noexcept;

    // What actually answered. Worth having in a report: "ANGLE (Vulkan 1.3.0
    // (SwiftShader Device))" and "ANGLE (Vulkan ... Intel Arc)" are the same
    // code path and very different numbers.
    [[nodiscard]] std::string renderer() const;
    [[nodiscard]] std::string version() const;

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    // Make this device's context current on the calling thread. Every call
    // below assumes it - and a second device on the same thread makes the first
    // one's context stale, which is a rule GL has and this cannot hide.
    bool make_current();

    void clear(float red, float green, float blue, float alpha);

    // The pixels, into a bitmap the painter can composite. Resized if it does
    // not already match.
    //
    // FLIPPED, because GL's origin is the bottom left and a bitmap's is the top
    // left. Handing back the unflipped buffer produces a picture that is upside
    // down and otherwise perfect, which is the kind of wrong that survives a
    // long time in a test that only checks a pixel count.
    [[nodiscard]] bool read_pixels(paint::bitmap & into) const;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace ctbrowser::raster::gles
