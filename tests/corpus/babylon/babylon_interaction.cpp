// CAN YOU DRAG THE SCENE AROUND WITH THE MOUSE.
//
// examples/pages/babylon-orbit.html is the demo; this is the claim behind it.
// A golden cannot make it: a golden proves one frame was drawn, and every
// picture in this file is a SECOND frame that had to differ from the first
// because of an event.
//
// NOTHING IN THE PAGE HANDLES A MOUSE. The camera is an ArcRotateCamera with
// `attachControl`, so the whole path is Babylon's own input manager reading DOM
// events off the canvas - which means what is being tested is that the events
// ARRIVE, carry the right coordinates, and land on the canvas rather than
// somewhere above it.
//
// TWO SEPARATE ASSERTIONS EVERY TIME: the camera moved, AND the picture
// changed. They are not the same claim. A camera whose angle updates while the
// render does not follow is exactly the half-working this corpus keeps
// producing - the world matrix reaching the shader and the animation system
// advancing a property were two different bugs in the WebGL 2 work, a week
// apart.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <ctbrowser.hpp>

#include "check.hpp"

using ctbrowser::shell::input_event;

namespace {

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string ask(ctbrowser::shell::browser & page, const std::string & expression) {
    const std::size_t before = page.bindings().console_output().size();
    (void)page.run_script("try { console.log('=' + String(" + expression +
                          ")); } catch (e) { console.log('=threw: ' + (e && e.message ? "
                          "e.message : e)); }");
    const auto & said = page.bindings().console_output();
    for (std::size_t i = said.size(); i-- > before;) {
        if (said[i].starts_with("=")) { return said[i].substr(1); }
    }
    return "<no answer>";
}

// Every pixel of the canvas, in scan order, as one number. What "the picture
// changed" means, and the only thing that can tell an orbit from a still frame:
// a rotation preserves area, so counting painted pixels reads a turning camera
// as a stationary one - which is a mistake this tree has already made once, in
// tests/corpus/babylon/babylon_ratchet.cpp rung 4.
[[nodiscard]] std::uint64_t canvas_digest(ctbrowser::shell::browser & page) {
    const auto txn = page.doc().read();
    ctbrowser::node_id canvas{};
    const auto walk = [&](auto && self, ctbrowser::node_id at) -> void {
        if (!canvas && page.atoms().text(txn.tag(at).value_or(ctbrowser::atom{})) == "canvas") {
            canvas = at;
        }
        for (const ctbrowser::node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    const auto pixels = page.canvases().pixels_of(canvas);
    if (!canvas || pixels == nullptr) { return 0; }
    std::uint64_t digest = 1469598103934665603ULL;
    for (int y = 0; y < static_cast<int>(pixels->height); ++y) {
        for (int x = 0; x < static_cast<int>(pixels->width); ++x) {
            digest = (digest ^ pixels->at(x, y)) * 1099511628211ULL;
        }
    }
    return digest;
}

[[nodiscard]] std::unique_ptr<ctbrowser::shell::browser> orbit_page(const std::string & bundle,
                                                                    const std::string & page_html) {
    auto page =
        std::make_unique<ctbrowser::shell::browser>(ctbrowser::shell::browser_options{420, 360});
    page->assets().add(
        "../../vendor/babylon/babylon.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(bundle.data()),
                               reinterpret_cast<const std::byte *>(bundle.data() + bundle.size())});
    page->load_html(page_html);
    // A FRAME FIRST, AND IT IS LOAD-BEARING. Hit testing reads the LAYOUT, and
    // without one every event lands on the body: the canvas is not there to be
    // hit yet, so Babylon's listeners never fire and the camera never moves.
    // This cost an hour, so it is stated rather than left as a call that looks
    // like a formality.
    (void)page->frame();
    for (int i = 0; i < 90; ++i) { page->tick(16); }
    return page;
}

// Press, move in steps, release - which is what a drag is, and what Babylon's
// pointer input reads. One event per tick, because the camera integrates
// movement per frame.
void drag(ctbrowser::shell::browser & page, float from_x, float from_y, float to_x, float to_y,
          int steps = 12) {
    (void)page.handle(input_event::mouse_move_to(from_x, from_y));
    (void)page.handle(input_event::mouse_down_at(from_x, from_y));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        (void)page.handle(
            input_event::mouse_move_to(from_x + (to_x - from_x) * t, from_y + (to_y - from_y) * t));
        page.tick(16);
    }
    (void)page.handle(input_event::mouse_up_at(to_x, to_y));
    for (int i = 0; i < 6; ++i) { page.tick(16); }
}

// Reports the claim either way, because a passing interaction test that prints
// nothing tells a reader less than one that says what moved.
void expect(bool ok, const std::string & what) {
    if (ok) {
        std::printf("     %s\n", what.c_str());
        return;
    }
    std::printf("FAIL %s\n", what.c_str());
    ++ctbrowser_test_failures;
}

} // namespace

int main() {
    // SKIPPED FOR NOW, AND THE REASON IS A NUMBER: this test takes 363 SECONDS
    // and is the entire serial tail of a suite that otherwise finishes in
    // seconds. Every one of its five scenarios renders about ninety frames of a
    // real Babylon scene through the software rasteriser, which manages 1.03 M
    // fragments per second.
    //
    // SPLITTING IT INTO FIVE BINARIES WAS THE WRONG FIX and was reverted. It
    // would have bought parallelism over a cost that is about to disappear:
    // docs/plans/angle.md has ANGLE at 192 M fragments per second on the same
    // machine, and when the WebGL path defaults to it this test is seconds
    // rather than minutes. Restructuring the test to work around a slow
    // renderer, and then keeping that structure afterwards, is how a suite ends
    // up shaped by problems nobody has any more.
    //
    // TO RE-ENABLE: delete this block. It goes when ANGLE becomes the default.
    if (std::getenv("CTBROWSER_SLOW_TESTS") == nullptr) {
        std::printf("SKIP babylon_interaction: 363s on the software rasteriser - re-enabled when "
                    "the WebGL path defaults to ANGLE. CTBROWSER_SLOW_TESTS=1 runs it anyway.\n");
        return 0;
    }

    const std::string bundle = read_file("vendor/babylon/babylon.js");
    const std::string page_html = read_file("examples/pages/babylon-orbit.html");
    if (bundle.empty() || page_html.empty()) {
        std::printf("SKIP babylon_interaction: the babylon corpus or the page is missing\n");
        return 0;
    }

    // --- the page comes up at all -------------------------------------------
    {
        auto page = orbit_page(bundle, page_html);
        expect(page->script_error().empty(), "the orbit page ran: " + page->script_error());
        const std::string ready =
            ask(*page, "String(typeof BABYLON !== 'undefined' && "
                       "document.getElementById('scene') !== null && "
                       "BABYLON.Engine.LastCreatedScene.activeCamera !== null)");
        expect(ready == "true", "the bundle, the canvas and a camera are all there: " + ready);
    }

    // --- a horizontal drag orbits -------------------------------------------
    {
        auto page = orbit_page(bundle, page_html);
        // THE CAMERA IS REACHED THROUGH THE SCENE, not through a global the page
        // exports for the test's benefit. A page written to be testable is a
        // page that proves less: this one is exactly what a human runs.
        (void)page->run_script("window.__cam = BABYLON.Engine.LastCreatedScene.activeCamera;");
        const std::string before = ask(*page, "window.__cam.alpha.toFixed(4)");
        const std::uint64_t picture_before = canvas_digest(*page);

        drag(*page, 210, 150, 310, 150);

        const std::string after = ask(*page, "window.__cam.alpha.toFixed(4)");
        const std::uint64_t picture_after = canvas_digest(*page);

        expect(before != after,
               "a horizontal drag turned the camera: alpha " + before + " -> " + after);
        expect(picture_before != picture_after,
               "and the picture followed it (digest " +
                   std::string{picture_before == picture_after ? "unchanged" : "changed"} + ")");
        std::printf("     alpha %s -> %s\n", before.c_str(), after.c_str());
    }

    // --- a vertical drag changes the elevation ------------------------------
    //
    // A SEPARATE AXIS, because one accumulator wired to both would pass the test
    // above and produce a camera that cannot look up or down.
    {
        auto page = orbit_page(bundle, page_html);
        (void)page->run_script("window.__cam = BABYLON.Engine.LastCreatedScene.activeCamera;");
        const std::string before = ask(*page, "window.__cam.beta.toFixed(4)");
        const std::string alpha_before = ask(*page, "window.__cam.alpha.toFixed(4)");
        const std::uint64_t picture_before = canvas_digest(*page);

        drag(*page, 210, 120, 210, 200);

        const std::string after = ask(*page, "window.__cam.beta.toFixed(4)");
        const std::string alpha_after = ask(*page, "window.__cam.alpha.toFixed(4)");
        expect(before != after,
               "a vertical drag changed the elevation: beta " + before + " -> " + after);
        expect(alpha_before == alpha_after,
               "and left the bearing alone: alpha " + alpha_before + " -> " + alpha_after);
        expect(picture_before != canvas_digest(*page), "the picture followed the elevation");
        std::printf("     beta %s -> %s\n", before.c_str(), after.c_str());
    }

    // --- the wheel zooms -----------------------------------------------------
    {
        auto page = orbit_page(bundle, page_html);
        (void)page->run_script("window.__cam = BABYLON.Engine.LastCreatedScene.activeCamera;");
        const std::string before = ask(*page, "window.__cam.radius.toFixed(3)");
        const std::uint64_t picture_before = canvas_digest(*page);

        // NOTCHES AWAY FROM THE USER, which is the direction that zooms IN -
        // and the DIRECTION is the assertion rather than merely that the number
        // moved. The DOM's `deltaY` has the opposite sign to this engine's
        // `wheel_y`, so a mapping that dropped the negation would pass "it
        // changed" while zooming the wrong way, which reads as a preference
        // rather than as a fault.
        for (int i = 0; i < 3; ++i) {
            (void)page->handle(input_event::wheel_at(210, 150, 1));
            page->tick(16);
        }
        for (int i = 0; i < 6; ++i) { page->tick(16); }

        const std::string after = ask(*page, "window.__cam.radius.toFixed(3)");
        expect(std::stod(after) < std::stod(before),
               "the wheel zoomed IN: radius " + before + " -> " + after);
        expect(picture_before != canvas_digest(*page), "and the picture followed the zoom");
    }

    // --- a click with no movement leaves the camera alone --------------------
    //
    // THE OTHER DIRECTION, and it is the one that catches an input path wired
    // to the wrong event: a camera that jumps on every press would pass all
    // three tests above and be unusable.
    {
        auto page = orbit_page(bundle, page_html);
        (void)page->run_script("window.__cam = BABYLON.Engine.LastCreatedScene.activeCamera;");
        const std::string before = ask(*page, "window.__cam.alpha.toFixed(4) + ',' + "
                                              "window.__cam.beta.toFixed(4)");
        (void)page->handle(input_event::mouse_move_to(210, 150));
        (void)page->handle(input_event::mouse_down_at(210, 150));
        (void)page->handle(input_event::mouse_up_at(210, 150));
        for (int i = 0; i < 6; ++i) { page->tick(16); }
        const std::string after = ask(*page, "window.__cam.alpha.toFixed(4) + ',' + "
                                             "window.__cam.beta.toFixed(4)");
        expect(before == after, "a click that did not move left the camera where it was: " +
                                    before + " -> " + after);
    }

    REPORT("babylon_interaction");
}
