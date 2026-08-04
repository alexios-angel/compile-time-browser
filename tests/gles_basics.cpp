// A REAL GLES DEVICE: can one be made, drawn into, and read back.
//
// Stage 1 of docs/angle-plan.md, and the whole of what it claims. Nothing in the
// engine calls raster/gles.hpp yet - the plan keeps the software path and this
// one alive together so "is ANGLE better" stays a measurement - so this is the
// only thing that exercises it.
//
// OPTIONAL, like svg_basics without plutosvg and check-spirv.py without
// spirv-val: a checkout that has not run tools/fetch-angle.sh builds and passes,
// and says out loud that it did not look. What it must NOT do is pass silently,
// which is why the skip prints a reason and why `available()` is asked rather
// than a build flag consulted.

#include <cstdio>
#include <string>

#include <ctbrowser/paint/command.hpp>
#include <ctbrowser/raster/gles.hpp>

#include "check.hpp"

using namespace ctbrowser;

int main() {
    if (!raster::gles::available()) {
        // THE REASON, not just the fact. "no device" sends the next reader to
        // the wrong place; "eglInitialize failed - is vk_swiftshader_icd.json
        // beside the libraries?" sends them to the right one, and that exact
        // omission cost an hour when the release was being packaged.
        std::printf("SKIP gles_basics: %s\n", raster::gles::unavailable_because().c_str());
        return 0;
    }

    raster::gles::device device{64, 48};
    if (!device.ok()) {
        std::printf("FAIL the device did not come up: %s\n", device.error().c_str());
        ++ctbrowser_test_failures;
        REPORT("gles_basics");
    }
    std::printf("     %s\n     %s\n", device.version().c_str(), device.renderer().c_str());

    CHECK(device.width() == 64);
    CHECK(device.height() == 48);
    // IT IS REALLY OpenGL ES 3, which is what the WebGL 2 path would need. A
    // context that quietly came up as ES 2 would pass every other check here.
    CHECK(device.version().find("OpenGL ES 3") != std::string::npos);

    // --- a clear reaches the pixels -----------------------------------------
    //
    // The smallest claim that means anything: a colour goes in through GL and
    // comes back out in a bitmap the painter could composite. Everything the
    // plan proposes rests on this round trip.
    {
        paint::bitmap into;
        device.clear(1.0f, 0.0f, 0.0f, 1.0f); // opaque red
        CHECK(device.read_pixels(into));
        CHECK(into.width == 64);
        CHECK(into.height == 48);

        const std::uint32_t corner = into.at(0, 0);
        const std::uint32_t middle = into.at(32, 24);
        CHECK(corner == middle); // a clear is uniform
        // ARGB, which is the bitmap's packing and not GL's byte order. Reading
        // the buffer as it arrives swaps red and blue, and a red clear coming
        // back blue is the one bug this check exists for.
        CHECK(((corner >> 16) & 0xFF) == 255); // red
        CHECK(((corner >> 8) & 0xFF) == 0);    // green
        CHECK((corner & 0xFF) == 0);           // blue
        CHECK(((corner >> 24) & 0xFF) == 255); // alpha
        if (((corner >> 16) & 0xFF) != 255) {
            std::printf("     the clear came back as %08x\n", corner);
        }
    }

    // --- a second clear replaces the first ------------------------------------
    //
    // Cheap, and it catches a readback that is returning a stale buffer - which
    // would make every check above pass once and mean nothing afterwards.
    {
        paint::bitmap into;
        device.clear(0.0f, 0.0f, 1.0f, 1.0f); // opaque blue
        CHECK(device.read_pixels(into));
        const std::uint32_t pixel = into.at(10, 10);
        CHECK((pixel & 0xFF) == 255);       // blue
        CHECK(((pixel >> 16) & 0xFF) == 0); // red
    }

    // --- a second device, and the first still works --------------------------
    //
    // A page can have several canvases and they do not share state. Making a
    // second context makes the first one's non-current, which is GL's rule and
    // not something the wrapper can hide - so the check is that `make_current`
    // puts it back rather than that it never left.
    {
        raster::gles::device second{16, 16};
        CHECK(second.ok());
        second.clear(0.0f, 1.0f, 0.0f, 1.0f);

        CHECK(device.make_current());
        paint::bitmap into;
        device.clear(1.0f, 1.0f, 0.0f, 1.0f); // yellow
        CHECK(device.read_pixels(into));
        CHECK(into.width == 64); // the FIRST device's size, not the second's
        const std::uint32_t pixel = into.at(1, 1);
        CHECK(((pixel >> 16) & 0xFF) == 255); // red
        CHECK(((pixel >> 8) & 0xFF) == 255);  // green
        CHECK((pixel & 0xFF) == 0);           // blue
    }

    REPORT("gles_basics");
}
