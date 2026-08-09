// Is the game PLAYABLE?
//
// A question no rung of tests/phaser_ratchet.cpp and no probe in
// tests/phaser_api.cpp asks. The ratchet says the bundle reaches update() and
// paints; the probes say each call works on its own; the golden says the frame
// is right. All three were true while every arrow key in a Phaser game did
// nothing at all, because Phaser matches keys on the legacy `keyCode` and the
// engine only set `code` and `key`. The listener fired, the event arrived, and
// no key ever matched.
//
// So this drives examples/pages/phaser-invaders.html - THE REAL PAGE, not a
// reduction of it - and asks whether holding a key changes where the ship goes.
// That is the difference between a picture of Space Invaders and a game.
//
// HELD, NOT TAPPED. Phaser polls key state once per frame, so a press and
// release inside a single tick is exactly what a polled reader is allowed to
// miss; asserting on that would be testing the test.
//
// AGAINST THE SCRIPTED RUN, not against a fixed number. The page sweeps the
// ship on a frame-count script when nothing is held - that is what makes its
// golden reproducible - so the claim worth making is that input CHANGES the
// outcome, which is exactly what a comparison of the two runs says and what a
// hardcoded x would not.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser.hpp>

#include "check.hpp"

namespace {

using ctbrowser::input_event;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

// The same one-line helper the other page-driving tests keep locally: check.hpp
// gives CHECK(expr), and these assertions want a sentence rather than an
// expression printed back.
void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string ask(browser & page, const char * expression) {
    const std::size_t before = page.bindings().console_output().size();
    (void)page.run_script(std::string{"try { console.log('=' + String("} + expression +
                          ")); } catch (e) { console.log('=threw: "
                          "' + (e && e.message ? "
                          "e.message : e)); }");
    const auto & said = page.bindings().console_output();
    for (std::size_t i = said.size(); i-- > before;) {
        if (said[i].starts_with("=")) { return said[i].substr(1); }
    }
    return "<no answer>";
}

// One run of the page: boot it, then tick `frames` times with `hold` held down
// the whole way (or nothing held when it is empty). Answers the ship's x.
[[nodiscard]] std::string run(const std::string & html, const std::string & bundle,
                              const char * hold, int frames) {
    browser page{browser_options{320, 240}};
    // The page asks for `../../vendor/phaser/phaser.js`, which is what it resolves to from
    // examples/pages/ - registered under that exact name so the load is
    // hermetic and no path outside the registry is touched.
    page.assets().add(
        "../../vendor/phaser/phaser.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(bundle.data()),
                               reinterpret_cast<const std::byte *>(bundle.data() + bundle.size())});
    page.load_html(html);
    // Boot: the three base64 textures have to settle before create() runs.
    for (int i = 0; i < 20; ++i) { page.tick(16); }

    if (hold != nullptr) { (void)page.handle(input_event::key_press(hold)); }
    for (int i = 0; i < frames; ++i) { page.tick(16); }
    if (hold != nullptr) { (void)page.handle(input_event::key_release(hold)); }

    return ask(page, "window.__game.scene.scenes[0].player.x");
}

// AND DOES IT PLAY? Moving the ship is one claim; a game running its whole loop
// unattended is another, and it is the one the corpus exists for. Left alone
// the page shoots on a timer, the formation marches and drops bombs, and both
// overlap callbacks fire - so after enough frames the score has gone up AND a
// life has come off, with no input at all.
//
// This is the assertion that would notice the loop dying halfway: a scene whose
// update() throws after frame 200 still passes every other test in this tree,
// because they all stop before it.
[[nodiscard]] std::string play(const std::string & html, const std::string & bundle, int frames) {
    browser page{browser_options{320, 240}};
    page.assets().add(
        "../../vendor/phaser/phaser.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(bundle.data()),
                               reinterpret_cast<const std::byte *>(bundle.data() + bundle.size())});
    page.load_html(html);
    for (int i = 0; i < frames; ++i) { page.tick(16); }
    // The script error too, because a callback that throws is cleared and the
    // page carries on looking healthy - which is exactly how this page hid an
    // undefined method for an afternoon.
    const std::string state = ask(page, "(function () { var s = window.__game.scene.scenes[0];"
                                        "return s.score + ',' + s.lives + ',' + s.over; })()");
    return state + "|" + page.script_error();
}

// AND DOES IT COME BACK? `scene.restart()` tears the whole scene down and runs
// create() again - every game object destroyed, every group emptied, the
// display list cleared, the timers dropped. That is a TEARDOWN path, and
// nothing else in this tree goes near one: every other page here is built once
// and runs until the process ends.
//
// A teardown that half-works looks fine for a frame and then leaks - stale
// bodies still in the physics world, a display list holding destroyed objects -
// so the assertions are on the state being FRESH, not merely present.
[[nodiscard]] std::string restart(const std::string & html, const std::string & bundle) {
    browser page{browser_options{320, 240}};
    page.assets().add(
        "../../vendor/phaser/phaser.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(bundle.data()),
                               reinterpret_cast<const std::byte *>(bundle.data() + bundle.size())});
    page.load_html(html);
    // Long enough that the scene is thoroughly dirty: the score has moved, the
    // formation has thinned, and bullets and bombs are in flight.
    for (int i = 0; i < 300; ++i) { page.tick(16); }
    const std::string before =
        ask(page, "(function () { var s = window.__game.scene.scenes[0];"
                  "return s.score + '/' + s.invaders.getChildren().length; })()");

    (void)page.run_script("window.__game.scene.scenes[0].scene.restart();");
    // Three frames, not thirty: the formation is whole until a bullet reaches
    // it, and this is asking what create() built rather than what play did to
    // it afterwards.
    for (int i = 0; i < 3; ++i) { page.tick(16); }
    const std::string after =
        ask(page, "(function () { var s = window.__game.scene.scenes[0];"
                  "return s.score + '/' + s.invaders.getChildren().length + '/' + s.lives"
                  " + '/' + s.over + '/' + s.bullets.getChildren().length; })()");
    return before + " -> " + after + " |" + page.script_error();
}

} // namespace

int main() {
    const std::string html = read_file("examples/pages/phaser-invaders.html");
    const std::string bundle = read_file("vendor/phaser/phaser.js");
    if (html.empty() || bundle.empty()) {
        std::printf("FAIL examples/pages/phaser-invaders.html or the bundle is missing\n");
        ++ctbrowser_test_failures;
        REPORT("phaser_invaders");
    }
    // The page keeps its game on a global so this can reach in; without it
    // there is no handle on the scene from outside.
    if (html.find("window.__game") == std::string::npos) {
        std::printf("FAIL the page no longer exposes window.__game - this test cannot see it\n");
        ++ctbrowser_test_failures;
        REPORT("phaser_invaders");
    }

    const std::string drifted = run(html, bundle, nullptr, 40);
    const std::string pushed_left = run(html, bundle, "ArrowLeft", 40);
    const std::string pushed_right = run(html, bundle, "ArrowRight", 40);
    std::printf("     ship x after 40 frames: scripted %s, ArrowLeft %s, ArrowRight %s\n",
                drifted.c_str(), pushed_left.c_str(), pushed_right.c_str());

    const auto number = [](const std::string & text) {
        try {
            return std::stod(text);
        } catch (...) { return std::nan(""); }
    };
    const double idle = number(drifted);
    const double left = number(pushed_left);
    const double right = number(pushed_right);
    if (std::isnan(idle) || std::isnan(left) || std::isnan(right)) {
        std::printf("FAIL the ship's position could not be read: [%s] [%s] [%s]\n", drifted.c_str(),
                    pushed_left.c_str(), pushed_right.c_str());
        ++ctbrowser_test_failures;
        REPORT("phaser_invaders");
    }

    // HELD LEFT MUST BEAT THE SCRIPT, which sweeps right at 60 px/s while the
    // key drives -120. Comparing against the scripted run rather than a number
    // is what keeps this test about INPUT and not about the page's timing.
    check(left < idle, "holding ArrowLeft moves the ship left of where it drifts to");
    check(right > left, "and ArrowRight puts it right of where ArrowLeft does");
    // The two directions must actually differ - if input were ignored entirely
    // all three runs would be identical, and the two checks above could still
    // pass on floating-point noise.
    check(std::abs(right - left) > 20.0,
          "the two directions land far apart: " + std::to_string(right - left) + " px");

    // --- and it plays on its own -------------------------------------------
    const std::string played = play(html, bundle, 700);
    std::printf("     after 700 frames: score,lives,over = %s\n", played.c_str());
    const std::size_t bar = played.find('|');
    const std::string state = played.substr(0, bar);
    const std::string failure = played.substr(bar + 1);

    check(failure.empty(), "no callback threw across 700 frames: " + failure);
    // score,lives,over - parsed rather than matched exactly, because the exact
    // score depends on timing this test has no business pinning. What it does
    // pin is that BOTH overlap callbacks fired at least once.
    const std::size_t first = state.find(',');
    const std::size_t second = state.find(',', first + 1);
    if (first == std::string::npos || second == std::string::npos) {
        std::printf("FAIL could not read the scene's state: [%s]\n", state.c_str());
        ++ctbrowser_test_failures;
        REPORT("phaser_invaders");
    }
    const double score = std::stod(state.substr(0, first));
    const double lives = std::stod(state.substr(first + 1, second - first - 1));
    check(score > 0, "the ship shot something: score " + std::to_string(score));
    check(lives < 3, "and a bomb reached the ship: lives " + std::to_string(lives));
    check(lives >= 0, "lives never went negative: " + std::to_string(lives));

    // --- and it can be restarted -------------------------------------------
    const std::string cycled = restart(html, bundle);
    std::printf("     restart: %s\n", cycled.c_str());
    const std::size_t split = cycled.find(" |");
    const std::string states = cycled.substr(0, split);
    check(cycled.substr(split + 2).empty(),
          "restarting threw nothing: " + cycled.substr(split + 2));
    // A dirty scene first, or the comparison proves nothing: a restart that
    // resets everything is indistinguishable from a game that never started.
    check(states.find("0/15 ->") == std::string::npos,
          "the scene was dirty before the restart: " + states);
    const std::size_t arrow = states.find(" -> ");
    check(arrow != std::string::npos, "both states were read: " + states);
    if (arrow != std::string::npos) {
        // score/invaders/lives/over/bullets, all as create() leaves them.
        check(states.substr(arrow + 4) == "0/15/3/false/0",
              "create() ran again from scratch: " + states.substr(arrow + 4));
    }

    REPORT("phaser_invaders");
}
