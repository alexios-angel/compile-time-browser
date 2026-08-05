// A REAL GLES DEVICE, AND WHICH DRIVERS ANSWER.
//
// The first test of the rewritten stack (docs/webgl-rewrite-plan.md). It asks
// the question the plan queued rather than guessing at it: construct a device
// with EACH driver in turn and print `ok()` and `renderer()` for both, side by
// side, in one run. Thirteen WebGL tests fail on a box with no GPU, and the
// hypothesis is that `driver::fastest` cannot come up there while
// `deterministic` can - which is a measurement, not a belief.
//
// OPTIONAL, like svg_basics without plutosvg: a checkout that has not run
// tools/fetch-angle.sh builds and passes and SAYS it did not look. What it must
// not do is pass in silence.

#include <cstdio>

#include <ctbrowser/paint/command.hpp>
#include <ctbrowser/raster/gl.hpp>

#include "check.hpp"

using namespace ctbrowser;

namespace {

struct outcome {
    bool ok = false;
    std::string renderer;
};

[[nodiscard]] outcome try_driver(raster::gl::driver which, const char * name) {
    raster::gl::device device{64, 48, which};
    outcome out;
    out.ok = device.ok();
    out.renderer = device.renderer();
    // BOTH, ALWAYS, SIDE BY SIDE. A run that prints only the failure leaves the
    // reader guessing whether the other one was even tried.
    std::printf("     %-14s ok=%s  %s\n", name, out.ok ? "yes" : "NO ",
                out.ok ? out.renderer.c_str() : device.error().c_str());
    return out;
}

} // namespace

int main() {
    if (!raster::gl::available()) {
        std::printf("SKIP gl_basics: %s\n", raster::gl::unavailable_because().c_str());
        return 0;
    }

    const outcome fastest = try_driver(raster::gl::driver::fastest, "fastest");
    const outcome deterministic = try_driver(raster::gl::driver::deterministic, "deterministic");

    // SWIFTSHADER IS THE FLOOR. It is software, it needs no GPU, and the
    // byte-compared goldens depend on it - so a build with ANGLE where this
    // fails is broken however well the other one does.
    CHECK(deterministic.ok);
    if (deterministic.ok) {
        CHECK(deterministic.renderer.find("SwiftShader") != std::string::npos);
    }

    // `fastest` MAY legitimately fail: a headless box with no GPU has nothing
    // for it to pick. That is why it is reported rather than asserted - and why
    // the engine must fall back rather than hand a page a null context.
    if (!fastest.ok) {
        std::printf("     note: fastest did not come up, so the fallback to "
                    "deterministic is what a page will get\n");
    }

    // --- and the device draws ------------------------------------------------
    {
        raster::gl::device device{64, 48, raster::gl::driver::deterministic};
        CHECK(device.ok());
        paint::bitmap into;
        device.clear(1.0f, 0.0f, 0.0f, 1.0f);
        CHECK(device.read_pixels(into));
        CHECK(into.width == 64);
        const std::uint32_t pixel = into.at(32, 24);
        // ARGB, not GL's byte order. A red clear coming back blue is the one
        // bug this check exists for.
        CHECK(((pixel >> 16) & 0xFF) == 255);
        CHECK(((pixel >> 8) & 0xFF) == 0);
        CHECK((pixel & 0xFF) == 0);
        if (((pixel >> 16) & 0xFF) != 255) { std::printf("     clear came back %08x\n", pixel); }
    }

    REPORT("gl_basics");
}
