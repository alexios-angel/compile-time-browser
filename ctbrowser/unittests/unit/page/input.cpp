//===- ctbrowser/unittests/unit/page/input.cpp - Page input tests ---------===//
//
// Input reaching script, and real pages responding to it: keyboard and mouse
// events delivered to listeners with the legacy `keyCode` and `which`,
// `preventDefault` stopping the browser's own handling, and MDN's breakout
// (examples/pages/pong.html) and the invaders page played by injecting input
// and asserting that the frames change. Tests that drive p5.js's event system
// are in test/corpus/p5/p5_page.cpp. Helpers shared with the other unit tests
// are in ../chrome_probe.hpp; `find_id` is in test/support/dom_probe.hpp.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/app/app.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "../chrome_probe.hpp"
#include "check.hpp"
#include "dom_probe.hpp"
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
using ctbrowser_test::find_id;
using ctbrowser_test::log_of;

namespace {

/// `keyCode` and `which`: deprecated, and still universally used.
///
/// Real code still depends on them: Phaser's keyboard system matches on
/// keyCode (`KeyCodes.LEFT` is 37), so without it no arrow key matches.
void test_key_events_carry_the_legacy_codes() {
    browser page{browser_options{200, 150}};
    page.load_html(R"(<html><body><script>
        window.__log = [];
        window.addEventListener('keydown', function (e) {
          window.__log.push(e.code + ' ' + e.keyCode + ' ' + e.which + ' ' + e.key);
        });
    </script></body></html>)");
    page.frame();

    // The numbers are the well-known ones every browser reports rather than
    // anything derived, which is the whole reason they are worth having.
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
// Keys must reach the page's listeners before the browser's own handling
// (scrolling, caret movement); otherwise a keydown listener silently receives
// nothing.

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
        // The release: without it a game that tracks held keys never stops
        // moving.
        check(log[1] == "up ArrowRight", "the release");
        // `code` is the physical key, `key` is what it means, and shift is
        // what makes them differ.
        check(log[2] == "down KeyA key=a shift=false", "a letter");
        check(log[3] == "down KeyA key=A shift=true", "the same letter shifted");
        check(log[4] == "down Space key=  shift=false", "space's key is a space");
    }
}

/// A keydown listener that calls preventDefault stops the browser's own
/// handling of the key (here, Space scrolling the page).
void test_preventDefault_stops_the_browser_acting() {
    const char * tall = "<body><div style='height:2000px'>tall</div>";

    browser page{browser_options{200, 100}};
    page.load_html(std::string{tall} + R"(<script>
      document.addEventListener('keydown', function (e) { e.preventDefault(); });
    </script></body>)");
    page.frame();
    check(page.max_scroll() > 0, "the page is taller than the viewport");
    (void)page.handle(input_event::key_press("Space"));
    check(page.scroll_y() == 0, "a cancelled keydown does not scroll the page");

    // Without the listener it does, which is what makes the test above
    // about preventDefault rather than about Space doing nothing.
    browser plain{browser_options{200, 100}};
    plain.load_html(std::string{tall} + "</body>");
    plain.frame();
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

/// MDN's breakout ends by calling alert("GAME OVER") and then
/// document.location.reload(). The page must survive both and play again,
/// which is past the point every other test stops looking.
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
        page.frame();
    }
    check(!page.alerts().empty(), "the game ended and alerted");
    if (!page.alerts().empty()) { check(page.alerts()[0] == "GAME OVER", "with GAME OVER"); }
    check(page.script_error().empty(), "and reloading itself did not break the script");

    // The reload really re-ran the page: it is playing again, so it can end
    // again rather than sitting on a dead context.
    const std::size_t after_first = page.alerts().size();
    for (int frame = 0; frame < 2000 && page.alerts().size() == after_first; ++frame) {
        (void)page.tick(1000.0 / 60.0);
        page.frame();
    }
    check(page.alerts().size() > after_first, "and the reloaded game runs and ends too");
}

/// The paddle stays on the canvas however the mouse is moved.
///
/// MDN's mouseMoveHandler gates on the cursor being inside the canvas and then
/// centres the paddle on it without clamping the resulting rect, so the paddle
/// hangs up to half its width off either edge, in Chrome and Firefox too. The
/// page carries one deviation from the tutorial to fix that (see the comment in
/// examples/demos/pong.cpp); this checks the deviation is still there.
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
    // -35.5, most of a 75px paddle off the canvas.
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

/// The end-to-end version of the keyboard and mouse tests above, against a
/// page nobody wrote for this engine: MDN's breakout reads e.code and
/// e.clientX, and if input does not reach it the paddle never moves. Comparing
/// frames with and without input is the assertion, because "the paddle moved"
/// is JS state this test cannot see, but it can see the pixels.
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
            page.frame();
        }
        const raster::surface & image = page.read_pixels();
        std::vector<std::uint32_t> pixels;
        for (int y = 0; y < image.height(); ++y) {
            const auto row = image.row(y);
            pixels.insert(pixels.end(), row.begin(), row.end());
        }
        return pixels;
    };

    const std::vector<std::uint32_t> idle = render("");
    const std::vector<std::uint32_t> pressed = render("ArrowRight");
    check(!idle.empty(), "the page rendered");
    check(idle.size() == pressed.size(), "both runs are the same size");
    check(idle != pressed, "holding a key changes what MDN's breakout draws");

    // The control: a key the page does not read must change nothing. Without
    // it, "the frames differ" could just mean the run is not reproducible, and
    // the test above would pass whether or not input worked.
    check(render("KeyQ") == idle, "a key the page ignores changes nothing");
}

/// The same question for the invaders example page, whose key names were
/// changed from the non-standard "Left"/"Right" (which no browser produces) to
/// "ArrowLeft"/"ArrowRight".
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
            page.frame();
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

/// Space fires a bullet. The page tests `e.code === "Space"`, so this also
/// checks that `===` compares string contents, not allocations. Counting the
/// bullet's own colour is what distinguishes "the key arrived" from "the page
/// acted on it".
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
            page.frame();
        }
    };

    run(5);
    check(bullet_pixels() == 0, "nothing is firing yet");
    (void)page.handle(input_event::key_press("Space"));
    run(5);
    check(bullet_pixels() > 0, "Space fires a bullet");
}

/// A letterboxed page is authored at its logical size and the window only
/// decides how big that gets drawn. SDL announces the window's pixel size on
/// the first frame, and that must not be taken as a page resize, or the canvas
/// (320x240 by its own attributes) would occupy a fraction of the viewport.
void test_a_letterboxed_page_keeps_its_size() {
    browser page{browser_options{320, 240}};
    std::ifstream in{"examples/pages/invaders.html", std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    page.load_html(buffer.str());
    page.frame();

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
    REPORT("page_input");
}
