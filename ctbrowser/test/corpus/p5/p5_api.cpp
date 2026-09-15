// How much of p5.js works.
//
// test/corpus/p5/p5_ratchet.cpp measures how FAR the bundle gets: it climbs a ladder and
// records one number. This measures how WIDE the working surface is - it calls
// as much of p5's 417-function API as can be run headlessly and records which
// calls pass.
//
// The two ask different questions and the difference is the point. The ratchet
// read 12/12 - loads, constructs, runs setup, runs draw, paints - for days
// while `colorMode(HSB)` was broken, because nothing on the ladder called it.
// A wide, shallow probe found five real bugs in one run.
//
// THE PAWL, same as the ratchet's and for the same reason:
//
//   a recorded PASS that now fails   FAIL. Something regressed.
//   a new pass                       print ADVANCE; recording is deliberate.
//   a failure that was already known listed, not fatal - it is the work queue.
//
// A test that edits its own expectations cannot fail, so `tools/corpus/ratchet.py p5 api
// --advance` is the only thing that writes test/corpus/p5/p5-api.txt.
//
// The probes live in test/corpus/p5/p5-api-probe.js rather than in a string here, so
// adding one needs no rebuild and `tools/corpus/ratchet.py p5 api --coverage` can list which
// of the bundle's `fn.*` no probe mentions - that list is what to write next.

#include <ctbrowser.hpp>

#include "api_pawl.hpp"
#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    const std::string bundle = read_file("vendor/p5/p5.js");
    const std::string probes = read_file("test/corpus/p5/p5-api-probe.js");
    if (bundle.empty() || probes.empty()) {
        std::printf("FAIL vendor/p5/p5.js or test/corpus/p5/p5-api-probe.js is missing\n");
        ++ctbrowser_test_failures;
        REPORT("p5_api");
    }

    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{400, 400}};
    // The save probes really do write files - that is the point of them - so they
    // write into the build tree rather than into the checkout. Without this, `p5
    // API` leaves probe-out.png beside the source and the next `git status` is a
    // surprise.
    page.set_download_directory("../build/downloads");
    const auto add = [&](const char * name, const std::string & text) {
        page.assets().add(
            name,
            std::vector<std::byte>{reinterpret_cast<const std::byte *>(text.data()),
                                   reinterpret_cast<const std::byte *>(text.data() + text.size())});
    };
    add("p5.js", bundle);
    add("probe.js", probes);
    // Baked so the loader probes are hermetic: they exercise the real
    // fetch-and-parse path without reaching the network, which is what
    // CTBROWSER_NETWORK=0 asks of everything else in this tree.
    add("probe-data.json", R"({"name":"probe","n":4})");
    add("probe-lines.txt", "one\ntwo\nthree");
    add("probe-table.csv", "a,b\n1,2\n3,4");
    add("probe.xml", "<list><item>first</item><item>second</item></list>");
    // A 4x4 image for the loadImage probe, ASSEMBLED here rather than committed:
    // a test that depends on a binary beside it fails for reasons that have
    // nothing to do with the code. 24bpp bottom-up BMP, solid green.
    {
        std::vector<unsigned char> bmp(54 + 4 * 4 * 3, 0);
        const auto put32 = [&bmp](std::size_t at, std::uint32_t v) {
            for (int byte = 0; byte < 4; ++byte) {
                bmp[at + static_cast<std::size_t>(byte)] =
                    static_cast<unsigned char>((v >> (8 * byte)) & 0xFF);
            }
        };
        bmp[0] = 'B';
        bmp[1] = 'M';
        put32(2, static_cast<std::uint32_t>(bmp.size()));
        put32(10, 54); // where the pixels start
        put32(14, 40); // BITMAPINFOHEADER
        put32(18, 4);
        put32(22, 4);
        bmp[26] = 1;  // planes
        bmp[28] = 24; // bits per pixel
        for (std::size_t at = 54; at < bmp.size(); at += 3) {
            bmp[at + 1] = 0xFF; // BGR, so this is green
        }
        std::vector<std::byte> bytes(bmp.size());
        for (std::size_t i = 0; i < bmp.size(); ++i) { bytes[i] = static_cast<std::byte>(bmp[i]); }
        page.assets().add("probe-image.bmp", std::move(bytes));
    }

    // IS_MINIFIED, like the ratchet: the probe measures the drawing surface,
    // and the translator fetch is a different question answered elsewhere.
    page.load_html(R"(<html><head><meta charset="utf-8">
        <script>var IS_MINIFIED = true;</script>
        <script src="p5.js"></script>
        <script src="probe.js"></script></head><body></body></html>)");
    if (!page.script_error().empty()) {
        std::printf("FAIL loading p5 or the probes: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("p5_api");
    }

    // The sketch the probes run against. Small, because several read every
    // pixel back and the probe is run on every build.
    (void)page.run_script(R"(
        var __out = '';
        new p5(function (s) {
          s.setup = function () {
            s.createCanvas(24, 24);
            s.noLoop();
            s.pixelDensity(1);
            // The runner is async now, because a loader probe returns a
            // promise. Its result lands in a global when it finishes, so the
            // test drives the loop until it does.
            globalThis.__runProbes(s).then(function (json) { __out = json; });
          };
          s.draw = function () {};
        });
    )");
    // Enough turns for every await in the probe list - each loader costs
    // several, and the runner is one long chain of them.
    for (int frame = 0; frame < 60; ++frame) { page.tick(16); }

    std::string reported;
    page.set_alert_hook([&reported](const std::string & said) { reported = said; });
    (void)page.run_script("alert(__out);");
    if (reported.empty()) {
        std::printf("FAIL the probes did not run: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("p5_api");
    }

    ctbrowser_test::api_pawl("p5 API", reported, "test/corpus/p5/p5-api.txt",
                             "tools/corpus/ratchet.py p5 api");
    REPORT("p5_api");
}
