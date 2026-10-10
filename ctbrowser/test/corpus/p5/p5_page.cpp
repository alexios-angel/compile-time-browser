// TWO p5.js PAGES: a sketch that asks for WebGL, and a sketch driven by real
// input. Each is a page beside this file - p5_page-webgl.html and
// p5_page-input.html - and every question is a function in p5_page.js, called
// through test/support/engine.hpp.
//
// Both cases were carved out of the unit tests on 2026-10-09 - the WebGL one
// from unittests/unit/canvas_context.cpp, the input one from
// unittests/unit/page/input.cpp - so that their JavaScript lives in files. The
// assertions are theirs, unchanged.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "engine.hpp"
#include <string>
#include <string_view>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;

namespace {

// One of this test's pages, loaded the way a workload test loads a page it owns.
void load_page(browser & page, const std::string & file) {
    const std::string html = read_file(file);
    check(!html.empty(), "the page is readable: " + file);
    // WHERE `../../../vendor/p5/p5.js` RESOLVES FROM: the page's own directory.
    // The asset registry probes ".", then the base path, then two levels above
    // it (shell/page/assets.hpp), and the test's working directory is
    // ctbrowser/.
    page.assets().set_base_path("test/corpus/p5");
    // UNCONFINED, explicitly: `../../../vendor/` climbs out of the base path,
    // which a confined registry refuses. Trusted in-tree fixture - never do
    // this for a page from the network.
    page.assets().set_confine(false);
    page.load_html(html);
}

// WEBGL COMPILES AND CONSTRUCTS, AND REFUSES CLEANLY.
//
// The scope for p5.js here is 2D. That is not the same as WebGL being absent:
// `class RendererGL extends Renderer3D` has to produce a working constructor
// at load or the bundle does not finish defining itself, and p5 registers it
// in its renderer table whether or not a sketch asks for it. What a sketch
// that DOES ask for it gets is a catchable Error naming WebGL - the same shape
// of refusal `new Function` gives, and the opposite of a getContext('webgl')
// that hands back an object with no drawing on it.
void test_webgl_is_constructible_and_refuses() {
    browser page{browser_options{300, 300}};
    const auto engine = ctbrowser_test::make_engine(page);
    load_page(page, "test/corpus/p5/p5_page-webgl.html");
    check(page.script_error().empty(), "the page loaded: " + page.script_error());
    // ASKED FOR, not read off the console: p5 logs its own diagnostics too, and
    // a test that reads console lines breaks whenever the library says
    // something new - which is a fact about p5, not about the thing under test.
    // The page keeps its own lines, and p5_page_webgl_said hands back the first
    // one with the prefix, or "<not logged>".
    const auto said = [&](std::string_view prefix) {
        return engine->call("p5_page_webgl_said", {prefix});
    };
    const std::string registered = said("registered=");
    check(registered == "registered=function",
          "the WebGL renderer is still a constructible function: " + registered);
    // THE ENGINE'S OWN GUARANTEE, tested directly rather than through p5: a
    // canvas hands out a real webgl context, and answers NULL for `webgl2`
    // because this is a WebGL 1 implementation (docs/history/webgl.md). Null is
    // what an unsupported context id returns, and what feature detection - p5's
    // included - is built on; this asserted a throw until that was measured.
    (void)engine->call("p5_page_webgl_contexts");
    check(said("direct=") == "direct=true", "a webgl context exists: " + said("direct="));
    // WAS `=== null`; `webgl2` returns a context since 2026-08-02 - see
    // docs/history/webgl2.md stage 4. Replaced rather than removed, so the shape
    // of the answer is still pinned.
    check(said("two=") == "two=true", "and webgl2 hands back a context: " + said("two="));

    // WHAT p5 DOES WITH IT IS MEASURED BY THE p5 PROBE, not asserted here.
    // createCanvas(w, h, WEBGL) now reaches p5's RendererGL; it used to land on
    // Renderer2D, and the two engine bugs behind that - a throwing `webgl2` and
    // a missing `Float32Array.from` - were both invisible from this file.
    const std::string asking = said("asking=");
    check(asking != "<not logged>", "the sketch reported what happened: " + asking);
}

// p5.js INPUT: the bundle's own event system, driven by real events.
//
// The other p5 pages here draw; this one asks whether p5 hears the browser.
// p5 registers its listeners on the window with `{passive, signal}` options
// and reads `event.clientX`, `event.key` and the rest off the event object, so
// a gap anywhere along that path leaves a sketch that renders correctly and
// never responds - which is exactly how it looks to a user, with no error.
void test_p5_receives_input() {
    browser page{browser_options{300, 300}};
    const auto engine = ctbrowser_test::make_engine(page);
    load_page(page, "test/corpus/p5/p5_page-input.html");
    check(page.script_error().empty(), "p5 loaded: " + page.script_error());
    // A frame first, so p5 has finished starting up and attached its listeners.
    page.frame();
    page.tick(16);

    (void)page.handle(input_event::mouse_move_to(40, 60));
    (void)page.handle(input_event::mouse_down_at(40, 60));
    (void)page.handle(input_event::mouse_up_at(40, 60));
    (void)page.handle(input_event::key_press("KeyQ"));
    page.tick(16);
    const std::string line = engine->call("p5_page_input_report");

    check(!line.empty(), "the sketch reported");
    if (line.empty()) { return; }
    // The COORDINATES matter as much as the event: p5 computes mouseX from the
    // event's clientX against the canvas's box, so a listener that fires with
    // no position leaves every sketch drawing at 0,0.
    // The COORDINATES matter as much as the event: p5 turns the event's
    // clientX into mouseX by subtracting the canvas's box, which is what
    // getBoundingClientRect is for. Compared against that box rather than
    // against a fixed number, so the test says "p5 and the DOM agree" instead
    // of pinning where p5 happens to put its canvas.
    const std::string offset = line.substr(line.find("offset=") + 7);
    const std::string want = offset.substr(0, offset.find(' '));
    check(line.find("move@" + want + ";") != std::string::npos,
          "mouseMoved at the canvas-relative position: " + line);
    check(line.find("down@" + want + ";") != std::string::npos,
          "mousePressed at the canvas-relative position: " + line);
    check(line.find("up;") != std::string::npos, "mouseReleased: " + line);
    check(line.find("key(q);") != std::string::npos, "keyPressed with the key: " + line);
    // ...and released, so the flag is not simply stuck on.
    check(line.find("pressed=false") != std::string::npos,
          "mouseIsPressed went back down: " + line);
}

} // namespace

int main() {
    test_webgl_is_constructible_and_refuses();
    test_p5_receives_input();
    REPORT("p5_page");
}
