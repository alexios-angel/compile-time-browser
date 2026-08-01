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
    // The page asks for `../assets/phaser.js`, which is what it resolves to from
    // examples/pages/ - registered under that exact name so the load is
    // hermetic and no path outside the registry is touched.
    page.assets().add(
        "../assets/phaser.js",
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

} // namespace

int main() {
    const std::string html = read_file("examples/pages/phaser-invaders.html");
    const std::string bundle = read_file("examples/assets/phaser.js");
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

    REPORT("phaser_invaders");
}
