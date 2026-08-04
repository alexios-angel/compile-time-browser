// THE SAME PAGE THROUGH BOTH BACKENDS, and do they agree.
//
// Stage 2 of docs/angle-plan.md, first increment. `webgl_context` can now put a
// page's calls through a real GLES device instead of the software rasteriser,
// and the plan keeps BOTH alive so the question stays a measurement.
//
// WHAT IS AND IS NOT CLAIMED HERE. Two rasterisers do not produce identical
// pixels - fill rules, interpolation precision and filtering differ
// legitimately - so this does NOT byte-compare them. That is stage 3, and the
// answer there is to pin ANGLE-over-SwiftShader, which stage 0 measured as
// giving the identical pixel on Linux and Windows.
//
// What it claims is narrower and still worth having: the same page, through the
// same bindings, draws the same SHAPE in the same COLOUR through both - and the
// list of calls the ANGLE path did not forward is EMPTY for this page. That
// second one is the honest half. A backend that silently ignored half a page's
// calls would paint something plausible and pass a loose pixel check.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <ctbrowser.hpp>

#include "check.hpp"

using namespace ctbrowser;

namespace {

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

struct picture {
    int painted = 0;   // pixels that are not the clear colour
    std::uint32_t ink; // the commonest non-background colour
    bool ok = false;
};

// What landed on the page's canvas, reduced to the two things worth comparing
// across two different rasterisers.
[[nodiscard]] picture canvas_of(shell::browser & page, std::uint32_t background) {
    picture out;
    const auto txn = page.doc().read();
    node_id canvas{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!canvas && page.atoms().text(txn.tag(at).value_or(atom{})) == "canvas") { canvas = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    const auto pixels = page.canvases().pixels_of(canvas);
    if (!canvas || pixels == nullptr) { return out; }

    std::vector<std::pair<std::uint32_t, int>> counts;
    for (int y = 0; y < pixels->height; ++y) {
        for (int x = 0; x < pixels->width; ++x) {
            const color c{pixels->at(x, y)};
            const std::uint32_t rgb =
                (std::uint32_t{c.red()} << 16) | (std::uint32_t{c.green()} << 8) | c.blue();
            if (rgb == background) { continue; }
            ++out.painted;
            bool found = false;
            for (auto & [key, n] : counts) {
                if (key == rgb) {
                    ++n;
                    found = true;
                    break;
                }
            }
            if (!found) { counts.emplace_back(rgb, 1); }
        }
    }
    int best = 0;
    for (const auto & [key, n] : counts) {
        if (n > best) {
            best = n;
            out.ink = key;
        }
    }
    out.ok = true;
    return out;
}

} // namespace

int main() {
    const std::string html = read_file("examples/pages/webgl-triangle.html");
    if (html.empty()) {
        std::printf("SKIP webgl_angle: examples/pages/webgl-triangle.html is missing\n");
        return 0;
    }
    if (!raster::gles::available()) {
        std::printf("SKIP webgl_angle: %s\n", raster::gles::unavailable_because().c_str());
        return 0;
    }

    // THE PAGE'S OWN CLEAR COLOUR, which is 0.10, 0.11, 0.16 - and getting this
    // wrong makes the test vacuous rather than wrong-looking. The first version
    // assumed black, so every one of the 25600 pixels counted as "painted" and
    // the comparison would have passed with the geometry missing entirely.
    const std::uint32_t background = 0x1a1c29;

    // --- the software rasteriser, which is the reference ---------------------
    picture software;
    {
        shell::browser page{shell::browser_options{240, 200}};
        page.load_html(html);
        (void)page.frame();
        for (int i = 0; i < 5; ++i) { page.tick(16); }
        software = canvas_of(page, background);
        CHECK(software.ok);
        std::printf("     software: %d pixels of geometry, ink %06x\n", software.painted,
                    software.ink);
        // THE GEOMETRY IS A FRACTION OF THE CANVAS, not all of it. If this ever
        // reads 25600 on a 160x160 canvas, the background is being counted and
        // the comparison below means nothing.
        CHECK(software.painted > 200);
        CHECK(software.painted < 20000);
    }

    // --- and the same page on ANGLE ------------------------------------------
    picture angle;
    std::vector<std::string> unforwarded;
    {
        shell::browser page{shell::browser_options{240, 200}};
        // THE SWITCH IS THROWN BEFORE THE PAGE RUNS, because a context is
        // created when the page asks for one and its backend cannot change
        // underneath a program that is already compiled into it.
        page.prefer_angle_webgl(true);
        page.load_html(html);
        (void)page.frame();
        for (int i = 0; i < 5; ++i) { page.tick(16); }
        angle = canvas_of(page, background);
        CHECK(angle.ok);
        unforwarded = page.bindings().unforwarded_gl_calls();
        // WHAT THE PAGE ITSELF SAID. webgl-triangle.html checks its own compile
        // and link status and logs the reason - so when the canvas comes back
        // empty, the page has usually already explained why and nobody looked.
        for (const std::string & said : page.bindings().console_output()) {
            std::printf("     js: %s\n", said.c_str());
        }
        // AND WHAT GL ITSELF SAYS. The page draws and does not check, so the
        // context is asked afterwards: an error code, and whether the program
        // it built actually linked.
        (void)page.run_script(
            "(function () {"
            "  var gl = document.getElementById('scene').getContext('webgl');"
            "  console.log('getError=' + gl.getError());"
            "  var p = gl.getParameter(gl.CURRENT_PROGRAM);"
            "  console.log('program=' + !!p + ' linked=' +"
            "              (p ? gl.getProgramParameter(p, gl.LINK_STATUS) : 'n/a'));"
            "  console.log('buffer=' + !!gl.getParameter(gl.ARRAY_BUFFER_BINDING));"
            "})()");
        for (const std::string & said : page.bindings().console_output()) {
            std::printf("     gl: %s\n", said.c_str());
        }
        std::printf("     ANGLE:    %d pixels of geometry, ink %06x\n", angle.painted,
                    angle.ink);
    }

    // --- EVERY CALL THE PAGE MADE WAS FORWARDED ------------------------------
    //
    // The honest half of this test. A backend that quietly dropped calls would
    // still paint something, and something is what a loose pixel comparison
    // accepts.
    if (!unforwarded.empty()) {
        std::printf("FAIL %zu call(s) the page made were not forwarded to ANGLE:\n",
                    unforwarded.size());
        for (const std::string & call : unforwarded) { std::printf("       %s\n", call.c_str()); }
        ++ctbrowser_test_failures;
    }

    // --- and the two agree about what was drawn ------------------------------
    CHECK(angle.painted > 0);
    CHECK(software.painted > 0);
    if (angle.painted > 0 && software.painted > 0) {
        // WITHIN 10%, not identical. Two rasterisers disagree at the edges of a
        // triangle by a few pixels and that is not a fault; disagreeing by half
        // is.
        const double ratio = static_cast<double>(angle.painted) /
                             static_cast<double>(software.painted);
        const bool close = ratio > 0.9 && ratio < 1.1;
        if (!close) {
            std::printf("FAIL the two backends drew very different areas: %d against %d\n",
                        angle.painted, software.painted);
            ++ctbrowser_test_failures;
        }
        // THE COLOUR SHOULD MATCH EXACTLY. A shader that computes a flat colour
        // has no edge cases: if these differ, something about the shader or the
        // attribute data went through differently, which is a real finding
        // rather than a rasterisation difference.
        CHECK(angle.ink == software.ink);
        if (angle.ink != software.ink) {
            std::printf("       ANGLE drew %06x where the software path drew %06x\n", angle.ink,
                        software.ink);
        }
    }

    REPORT("webgl_angle");
}
