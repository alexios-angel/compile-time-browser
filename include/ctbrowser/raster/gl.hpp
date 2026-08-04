#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <ctbrowser/paint/command.hpp>

// THE GL DEVICE. One implementation, no software twin.
//
// The stack this replaces kept a software mirror of GL state and, separately and
// by hand, forwarded some calls to ANGLE. Nineteen methods never forwarded at
// all, and because a call that never learned to forward also never learned to
// record that it hadn't, the ledger could not see them. See
// docs/webgl-rewrite-plan.md.
//
// So there is nothing here but a handle onto ANGLE. No state is remembered that
// GL can be asked for, because remembered state is the thing that drifts.
//
// NO EGL OR GLES TYPE APPEARS IN THIS HEADER, and `tests/api_surface` enforces
// it: every consumer pays for what a header includes. Parameters are plain
// integers because the numbers a page passes came from JavaScript and are
// already plain integers by the time they arrive.

namespace ctbrowser::raster::gl {

// WHICH DRIVER. The old code hard-coded SwiftShader and left no way to ask for
// anything else, which made "safe mode" a rewrite rather than a switch.
//
// `fastest` is the default because it is what a user should get; `deterministic`
// is SwiftShader, which is what the byte-compared goldens need and what a player
// or a debugging session can select at run time.
enum class driver {
    fastest,
    deterministic
};

// Whether a device can be made AT ALL: built with ANGLE, and the libraries load
// and initialise. Asked rather than assumed - a build flag reports an intention
// and this reports what happened.
[[nodiscard]] bool available();

// Why `available()` said no, in words a person can act on. Empty when it said
// yes. Most of what goes wrong happens in EGL, before there is a context to ask
// for an error string.
[[nodiscard]] std::string unavailable_because();

class device {
public:
    device(int width, int height, driver which = driver::fastest);
    ~device();
    device(const device &) = delete;
    device & operator=(const device &) = delete;
    device(device &&) noexcept;
    device & operator=(device &&) noexcept;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::string & error() const noexcept;

    // What actually answered - "ANGLE (Vulkan 1.3.0 (SwiftShader Device))" and
    // "ANGLE (Vulkan ... Intel Arc)" are the same code path and very different
    // numbers, so a report that omits this is not reproducible.
    [[nodiscard]] std::string renderer() const;
    [[nodiscard]] std::string version() const;

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    // Every call below assumes this device's context is current. A second device
    // on the same thread makes the first one's stale, which is GL's rule and not
    // something a wrapper can hide.
    bool make_current();

    void viewport(int x, int y, int width, int height);
    void scissor(int x, int y, int width, int height);
    void clear(float red, float green, float blue, float alpha);
    // THE MASK A PAGE PASSED, not a fixed set. `gl.clear(COLOR_BUFFER_BIT)`
    // must not silently clear depth as well - a page that clears colour
    // per-object and depth per-frame renders inside out if it does.
    void clear_buffers(std::uint32_t mask);
    void clear_color(float red, float green, float blue, float alpha);
    void clear_depth(float depth);
    void set_capability(int capability, bool on);
    // GL's own error, reported and cleared. Asked of the driver rather than
    // tracked here, because tracked state is what drifts.
    [[nodiscard]] std::uint32_t take_error();

    // The pixels, into a bitmap the painter composites.
    //
    // FLIPPED, because GL's origin is bottom-left and a bitmap's is top-left,
    // and ARGB, because that is the bitmap's packing and not GL's byte order.
    // Handing back the raw buffer gives a picture that is upside down with red
    // and blue swapped and otherwise perfect - the kind of wrong that survives a
    // test which only counts painted pixels.
    [[nodiscard]] bool read_pixels(paint::bitmap & into) const;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace ctbrowser::raster::gl
