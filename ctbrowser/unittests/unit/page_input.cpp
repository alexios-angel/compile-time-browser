// INPUT REACHING SCRIPT, AND REAL PAGES RESPONDING TO IT: keyboard and mouse
// events delivered to listeners with the legacy `keyCode` and `which`,
// `preventDefault` stopping the browser's own handling, p5.js's event system
// driven by real events, and MDN's breakout, pong and the invaders page played
// by injecting input and asserting that the frames change. Every page here is
// one nobody wrote for this engine.
//
// One of eight files carved out of unittests/unit/bindings_basics.cpp on
// 2026-09-07, when it had reached 3,365 lines. Every case is verbatim and in
// the order it had; the helpers more than one of the eight needs are in
// page_probe.hpp beside this, and `find_id` is test/support/dom_probe.hpp's.

#include <ctbrowser/app/app.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "dom_probe.hpp"
#include "page_probe.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using ctbrowser_test::check;
using ctbrowser_test::find_id;
using ctbrowser_test::log_of;
using ctbrowser_test::read_bytes;

namespace {

// p5.js INPUT: the bundle's own event system, driven by real events.
//
// The other p5 pages here draw; this one asks whether p5 hears the browser.
// p5 registers its listeners on the window with `{passive, signal}` options
// and reads `event.clientX`, `event.key` and the rest off the event object, so
// a gap anywhere along that path leaves a sketch that renders correctly and
// never responds - which is exactly how it looks to a user, with no error.
//
// Instance mode, so the assertions name what they mean rather than relying on
// p5 having installed 200 globals.
void test_p5_receives_input() {
    browser page{browser_options{300, 300}};
    page.assets().add("p5.js", read_bytes("vendor/p5/p5.js"));
    page.load_html(R"(<html><head><script>var IS_MINIFIED = true;</script>
        <script src="p5.js"></script></head><body style="margin:0"><script>
        var log = '';
        var sketch = null;
        new p5(function (s) {
          sketch = s;
          s.setup = function () { s.createCanvas(200, 200); s.noLoop(); };
          s.draw = function () {};
          s.mousePressed = function () { log += 'down@' + s.mouseX + ',' + s.mouseY + ';'; };
          s.mouseReleased = function () { log += 'up;'; };
          s.mouseMoved = function () { log += 'move@' + s.mouseX + ',' + s.mouseY + ';'; };
          s.keyPressed = function () { log += 'key(' + s.key + ');'; };
        });
        // p5 computes mouseX by subtracting the canvas's own box from the
        // event's viewport coordinates, so the check is that the two AGREE -
        // not that the canvas sits at any particular place on the page.
        function report() {
          const box = sketch._renderer.canvas.getBoundingClientRect();
          console.log(log + ' | offset=' + (40 - box.left) + ',' + (60 - box.top) +
                      ' | pressed=' + sketch.mouseIsPressed);
        }
    </script></body></html>)");
    check(page.script_error().empty(), "p5 loaded: " + page.script_error());
    // A frame first, so p5 has finished starting up and attached its listeners.
    (void)page.frame();
    page.tick(16);

    (void)page.handle(input_event::mouse_move_to(40, 60));
    (void)page.handle(input_event::mouse_down_at(40, 60));
    (void)page.handle(input_event::mouse_up_at(40, 60));
    (void)page.handle(input_event::key_press("KeyQ"));
    page.tick(16);
    (void)page.run_script("report();");

    const auto & log = log_of(page);
    check(!log.empty(), "the sketch reported");
    if (log.empty()) { return; }
    const std::string & line = log.back();
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

// `keyCode` AND `which` - deprecated, and universally used.
//
// The engine had `code` and `key`, the modern pair, and stopped there. The
// event looked complete and was unusable to a large amount of real code:
// PHASER'S ENTIRE KEYBOARD SYSTEM MATCHES ON keyCode - `KeyCodes.LEFT` is 37 -
// so every arrow key in a Phaser game did nothing. The listener fired, the
// event arrived, `code` was correct, and no key ever matched. A game that
// renders and cannot be played.
void test_key_events_carry_the_legacy_codes() {
    browser page{browser_options{200, 150}};
    page.load_html(R"(<html><body><script>
        window.__log = [];
        window.addEventListener('keydown', function (e) {
          window.__log.push(e.code + ' ' + e.keyCode + ' ' + e.which + ' ' + e.key);
        });
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");

    // The arrows first, because they are the ones that were broken. The numbers
    // are the well-known ones every browser reports rather than anything
    // derived, which is the whole reason they are worth having.
    for (const char * code : {"ArrowLeft", "ArrowUp", "ArrowRight", "ArrowDown", "Space", "Enter",
                              "Escape", "KeyA", "KeyZ", "Digit0", "Digit9", "F1", "F12"}) {
        (void)page.handle(input_event::key_press(code));
    }

    const std::size_t before = page.bindings().console_output().size();
    (void)page.run_script("console.log('=' + window.__log.join('|'));");
    std::string answer;
    const auto & said = page.bindings().console_output();
    for (std::size_t i = said.size(); i-- > before;) {
        if (said[i].starts_with("=")) {
            answer = said[i].substr(1);
            break;
        }
    }
    const std::string want =
        "ArrowLeft 37 37 ArrowLeft|ArrowUp 38 38 ArrowUp|ArrowRight 39 39 ArrowRight|"
        "ArrowDown 40 40 ArrowDown|Space 32 32  |Enter 13 13 Enter|Escape 27 27 Escape|"
        "KeyA 65 65 a|KeyZ 90 90 z|Digit0 48 48 0|Digit9 57 57 9|F1 112 112 F1|"
        "F12 123 123 F12";
    check(answer == want,
          "keyCode and which are the legacy numbers:\n  got  " + answer + "\n  want " + want);
}

// --- input reaches script -------------------------------------------------
//
// This is the gap that made every game unplayable: the browser handled keys
// itself - scrolling, caret movement - and never told the page. A page could
// register a keydown listener and receive nothing, forever, with no error.

void test_keyboard_reaches_script() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<body><script>
      document.addEventListener('keydown', function (e) {
        console.log('down ' + e.code + ' key=' + e.key + ' shift=' + e.shiftKey);
      });
      document.addEventListener('keyup', function (e) { console.log('up ' + e.code); });
    </script></body>)");
    check(page.script_error().empty(), "the script ran");

    (void)page.handle(input_event::key_press("ArrowRight"));
    (void)page.handle(input_event::key_release("ArrowRight"));
    (void)page.handle(input_event::key_press("KeyA"));
    (void)page.handle(input_event::key_press("KeyA", true));
    (void)page.handle(input_event::key_press("Space"));

    const auto log = page.bindings().console_output();
    check(log.size() == 5, "five key events reached the page");
    if (log.size() == 5) {
        check(log[0] == "down ArrowRight key=ArrowRight shift=false", "an arrow key");
        // A RELEASE, which did not exist at all before: without it a game that
        // tracks held keys never stops moving.
        check(log[1] == "up ArrowRight", "the release");
        // `code` is the physical key, `key` is what it means - and shift is
        // what makes them differ.
        check(log[2] == "down KeyA key=a shift=false", "a letter");
        check(log[3] == "down KeyA key=A shift=true", "the same letter shifted");
        check(log[4] == "down Space key=  shift=false", "space's key is a space");
    }
}

void test_preventDefault_stops_the_browser_acting() {
    const char * tall = "<body><div style='height:2000px'>tall</div>";

    browser page{browser_options{200, 100}};
    page.load_html(std::string{tall} + R"(<script>
      document.addEventListener('keydown', function (e) { e.preventDefault(); });
    </script></body>)");
    check(page.frame().has_value(), "the page renders");
    check(page.max_scroll() > 0, "the page is taller than the viewport");
    (void)page.handle(input_event::key_press("Space"));
    check(page.scroll_y() == 0, "a cancelled keydown does not scroll the page");

    // ...and without the listener it does, which is what makes the test above
    // about preventDefault rather than about Space doing nothing.
    browser plain{browser_options{200, 100}};
    plain.load_html(std::string{tall} + "</body>");
    check(plain.frame().has_value(), "the plain page renders");
    (void)plain.handle(input_event::key_press("Space"));
    check(plain.scroll_y() > 0, "an uncancelled Space still scrolls");
}

void test_mouse_reaches_script() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<body><script>
      document.addEventListener('mousemove', function (e) {
        console.log('move ' + e.clientX + ',' + e.clientY);
      });
      document.addEventListener('mousedown', function (e) { console.log('down ' + e.button); });
      document.addEventListener('mouseup', function () { console.log('up'); });
    </script></body>)");
    check(page.script_error().empty(), "the script ran");

    (void)page.handle(input_event::mouse_move_to(40, 12));
    (void)page.handle(input_event::mouse_down_at(40, 12));
    (void)page.handle(input_event::mouse_up_at(40, 12));

    const auto log = page.bindings().console_output();
    check(log.size() == 3, "three mouse events reached the page");
    if (log.size() == 3) {
        // MDN's breakout moves its paddle from clientX alone, so the
        // coordinates are the whole content of the event.
        check(log[0] == "move 40,12", "the pointer position");
        check(log[1] == "down 0", "the left button is 0 in the DOM, not 1");
        check(log[2] == "up", "and the release");
    }
}

// The end-to-end version of the three tests above, against a page nobody wrote
// for this engine: MDN's breakout reads e.code and e.clientX, and if input does
// not reach it the paddle simply never moves. Comparing frames with and without
// input is the assertion, because "the paddle moved" is JS state this test
// cannot see - but it can see the pixels.
// The reason any of this exists: MDN's breakout ENDS by calling alert("GAME
// OVER") and then document.location.reload(). Both were undefined identifiers,
// so the one page in the suite that proves web compatibility died on its own
// game-over - after the point every other test stops looking.
void test_the_breakout_page_survives_its_own_game_over() {
    browser page{browser_options{480, 320}};
    std::ifstream in{"examples/pages/pong.html", std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    page.load_html(buffer.str());
    check(page.script_error().empty(), "the page loaded");

    // Left alone the paddle never moves, so the ball is missed and the game
    // ends. Bounded so a page that never ends fails the check below instead of
    // hanging.
    for (int frame = 0; frame < 2000 && page.alerts().empty(); ++frame) {
        (void)page.tick(1000.0 / 60.0);
        (void)page.frame();
    }
    check(!page.alerts().empty(), "the game ended and alerted");
    if (!page.alerts().empty()) { check(page.alerts()[0] == "GAME OVER", "with GAME OVER"); }
    check(page.script_error().empty(), "and reloading itself did not break the script");

    // The reload really re-ran the page: it is playing again, so it can end
    // AGAIN rather than sitting on a dead context.
    const std::size_t after_first = page.alerts().size();
    for (int frame = 0; frame < 2000 && page.alerts().size() == after_first; ++frame) {
        (void)page.tick(1000.0 / 60.0);
        (void)page.frame();
    }
    check(page.alerts().size() > after_first, "and the reloaded game runs and ends too");
}

// The paddle stays ON the canvas however the mouse is moved.
//
// MDN's mouseMoveHandler gates on the CURSOR being inside the canvas and then
// centres the paddle on it WITHOUT clamping the resulting rect, so the paddle
// hangs up to half its width off either edge - in Chrome and Firefox too. The
// page carries one deviation from the tutorial to fix that (see the comment in
// examples/demos/pong.cpp); this is what says the deviation is still there.
void test_the_breakout_paddle_stays_on_the_canvas() {
    browser page{browser_options{480, 320}};
    std::ifstream in{"examples/pages/pong.html", std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    page.load_html(buffer.str());
    check(page.script_error().empty(), "the page loaded");
    // The page computes relativeX as clientX - canvas.offsetLeft, and offsetLeft
    // is 0 only because the canvas is exactly the viewport width and nothing
    // scrolls. Assert it rather than assume it: if either stops holding, the
    // arithmetic below is measuring something else and the test goes vacuous.
    check(page.max_scroll() <= 0, "the page does not scroll, so the canvas is at x=0");

    // Ask the page a question in its own context. `alert` is recorded on the
    // browser, which is what makes a JS-side value readable from a test at all.
    const auto ask = [&page](const char * expression) {
        const std::size_t before = page.alerts().size();
        (void)page.run_script(std::string{"alert("} + expression + ");");
        return page.alerts().size() > before ? page.alerts().back() : std::string{"<no answer>"};
    };

    // Hard against the left wall. relativeX = 2 puts MDN's unclamped paddle at
    // -35.5 - most of a 75px paddle off the canvas.
    (void)page.handle(input_event::mouse_move_to(2, 300));
    check(ask("paddleX >= 0 ? 'in' : 'out'") == "in", "the paddle does not cross the left wall");

    // And the right, where the bound is canvas.width - paddleWidth = 405;
    // relativeX = 478 puts the unclamped paddle at 440.5.
    (void)page.handle(input_event::mouse_move_to(478, 300));
    check(ask("paddleX <= 480 - 75 ? 'in' : 'out'") == "in",
          "the paddle does not cross the right wall");

    // The control: the clamp must not have pinned the paddle to a wall for
    // every input. A cursor mid-canvas still centres the paddle on it.
    (void)page.handle(input_event::mouse_move_to(240, 300));
    check(ask("paddleX > 0 && paddleX < 405 ? 'free' : 'stuck'") == "free",
          "and it still tracks the mouse in between");
}

void test_a_real_page_responds_to_input() {
    const auto render = [](std::string_view held) {
        browser page{browser_options{480, 320}};
        std::ifstream in{"examples/pages/pong.html", std::ios::binary};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        page.load_html(buffer.str());
        if (!held.empty()) { (void)page.handle(input_event::key_press(std::string{held})); }
        for (int frame = 0; frame < 20; ++frame) {
            (void)page.tick(1000.0 / 60.0);
            (void)page.frame();
        }
        const auto image = page.read_pixels();
        std::vector<std::uint32_t> pixels;
        if (image) {
            for (int y = 0; y < image->height(); ++y) {
                const auto row = image->row(y);
                pixels.insert(pixels.end(), row.begin(), row.end());
            }
        }
        return pixels;
    };

    const std::vector<std::uint32_t> idle = render("");
    const std::vector<std::uint32_t> pressed = render("ArrowRight");
    check(!idle.empty(), "the page rendered");
    check(idle.size() == pressed.size(), "both runs are the same size");
    check(idle != pressed, "holding a key changes what MDN's breakout draws");

    // The control: a key the page does not read must change NOTHING. Without
    // it, "the frames differ" could just mean the run is not reproducible, and
    // the test above would pass whether or not input worked.
    check(render("KeyQ") == idle, "a key the page ignores changes nothing");
}

// The same question asked of the ported example page, because it is the one
// whose key names I had to change: e.key was "Left", which nothing produces.
void test_the_invaders_page_responds_to_input() {
    const auto ship_row = [](std::string_view held) {
        browser page{browser_options{320, 240}};
        std::ifstream in{"examples/pages/invaders.html", std::ios::binary};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        page.load_html(buffer.str());
        if (!held.empty()) { (void)page.handle(input_event::key_press(std::string{held})); }
        for (int frame = 0; frame < 30; ++frame) {
            (void)page.tick(1000.0 / 60.0);
            (void)page.frame();
        }
        // The ship's row of the canvas: moving left or right changes it, and
        // the drifting aliens above do not touch it.
        std::vector<std::uint32_t> row;
        if (const auto pixels = page.canvases().pixels_of(find_id(page, "game"))) {
            for (int x = 0; x < pixels->width; ++x) { row.push_back(pixels->at(x, 224)); }
        }
        return row;
    };

    const std::vector<std::uint32_t> still = ship_row("");
    check(!still.empty(), "the game drew a ship");
    check(ship_row("ArrowLeft") != still, "holding left moves the ship");
    check(ship_row("KeyQ") == still, "a key the page ignores does not");
}

// Space fires. It did not, and the reason was not input at all: the page tests
// `e.code === "Space"`, and `===` compared string ALLOCATIONS, so it was false
// for every event. Counting the bullet's own colour is what distinguishes
// "the key arrived" from "the page acted on it".
void test_the_invaders_page_shoots() {
    browser page{browser_options{320, 240}};
    // run_app installs playSound; a bare browser does not, and the page must
    // not depend on that to fire.
    page.define_native("playSound", [](script::context &, std::span<script::value>) {
        return script::value::boolean(true);
    });
    std::ifstream in{"examples/pages/invaders.html", std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    page.load_html(buffer.str());

    const auto bullet_pixels = [&] {
        std::size_t found = 0;
        if (const auto pixels = page.canvases().pixels_of(find_id(page, "game"))) {
            for (int y = 0; y < pixels->height; ++y) {
                for (int x = 0; x < pixels->width; ++x) {
                    if (pixels->at(x, y) == 0xFFFFFF00U) { ++found; } // the bullet's yellow
                }
            }
        }
        return found;
    };
    const auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            (void)page.tick(1000.0 / 60.0);
            (void)page.frame();
        }
    };

    run(5);
    check(bullet_pixels() == 0, "nothing is firing yet");
    (void)page.handle(input_event::key_press("Space"));
    run(5);
    check(bullet_pixels() > 0, "Space fires a bullet");
}

// A letterboxed page is authored at its LOGICAL size and the window only
// decides how big that gets drawn. SDL announces the window's pixel size on the
// first frame, and taking that as a page resize left the canvas - 320x240 by
// its own attributes - occupying a ninth of the viewport.
void test_a_letterboxed_page_keeps_its_size() {
    browser page{browser_options{320, 240}};
    std::ifstream in{"examples/pages/invaders.html", std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    page.load_html(buffer.str());
    check(page.frame().has_value(), "the page renders");

    const auto canvas_box = [&] {
        const node_id want = find_id(page, "game");
        const auto walk = [&](auto && self, const layout::fragment & f, float dx,
                              float dy) -> rect {
            const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
            if (f.source == want) { return box; }
            for (const auto & child : f.children) {
                if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
            }
            return rect{};
        };
        return walk(walk, page.fragments(), 0, 0);
    };
    // The canvas fills the logical viewport exactly, which is the whole point
    // of authoring at 320x240 and letting SDL scale it.
    check(canvas_box().width == 320.0f, "the canvas is as wide as the page");
    check(canvas_box().height == 240.0f, "and as tall");
}

} // namespace

int main() {
    test_key_events_carry_the_legacy_codes();
    test_keyboard_reaches_script();
    test_preventDefault_stops_the_browser_acting();
    test_mouse_reaches_script();
    test_a_real_page_responds_to_input();
    test_the_breakout_page_survives_its_own_game_over();
    test_the_breakout_paddle_stays_on_the_canvas();
    test_the_invaders_page_responds_to_input();
    test_the_invaders_page_shoots();
    test_a_letterboxed_page_keeps_its_size();
    test_p5_receives_input();
    REPORT("page_input");
}
