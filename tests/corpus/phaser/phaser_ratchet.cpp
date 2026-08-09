// How far Phaser 4 gets through the engine.
//
// A SECOND CORPUS, and the reason for one is on the record. p5.js found every
// bug its own idioms could reach, and the ones it could not are invisible from
// inside a corpus of one - `getProgramParameter` answered 0 to ACTIVE_UNIFORMS
// for as long as WebGL existed here, and no page noticed, because every
// hand-written page asks for uniforms BY NAME while a library enumerates. It
// took somebody else's code to find that. This is the second somebody.
//
// Phaser is unlike p5 in the ways that matter: a game loop it wants to own
// rather than a sketch the engine drives, two renderers chosen at boot, a
// loader with queues and progress events, and input polled as state rather than
// delivered as callbacks.
//
// SAME SHAPE AS tests/p5_ratchet.cpp - a ladder, a recorded rung, and a blocker
// that may not change silently - because that shape has earned it. The rungs
// differ past the language ones because the lifecycle differs: Phaser has
// preload/create/update where p5 has setup/draw.
//
// ONE RUNG IS NOT IN p5's LADDER, and it is the first thing measured here:
// Phaser touches `document.createElement` on its first line, so it cannot run
// at all without a DOM. p5's bundle runs headless up to a point and only then
// wants a page. That difference is why `ran` and `page` are one rung here
// rather than two.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum rung {
    rung_unread = 0,
    rung_read = 1,     // the bundle is on disk and readable
    rung_lexed = 2,    // it tokenises with nothing silently dropped
    rung_parsed = 3,   // the parser accepts all of it
    rung_compiled = 4, // it becomes a program
    rung_page = 5,     // it runs AS A PAGE - Phaser needs a DOM from line one
    rung_defines = 6,  // and leaves a callable `Phaser` behind
    rung_game = 7,     // `new Phaser.Game(config)` constructs
    rung_create = 8,   // and the scene's create() runs
    rung_update = 9,   // and update() keeps being called
    rung_paints = 10,  // and what the scene drew is IN THE PIXELS
};

[[nodiscard]] const char * rung_name(int level) {
    switch (level) {
    case rung_unread: return "unread";
    case rung_read: return "read";
    case rung_lexed: return "lexed";
    case rung_parsed: return "parsed";
    case rung_compiled: return "compiled";
    case rung_page: return "runs as a page";
    case rung_defines: return "defines Phaser";
    case rung_game: return "constructs a Game";
    case rung_create: return "runs create()";
    case rung_update: return "runs update()";
    case rung_paints: return "paints what the scene drew";
    default: return "?";
    }
}

struct measurement {
    int level = rung_unread;
    std::string blocker;
    double lex_ms = 0, parse_ms = 0, compile_ms = 0, page_ms = 0;
    bool stopped = false;

    void fail_at(int at, std::string why) {
        if (stopped) { return; }
        level = at - 1;
        // The first line is the blocker; a stack below it is CONTEXT that moves
        // with edits having nothing to do with the cause, so it is printed and
        // not compared. Same rule as the p5 ratchet.
        const std::size_t newline = why.find('\n');
        blocker = newline == std::string::npos ? std::move(why) : why.substr(0, newline);
        stopped = true;
    }
    void reached(int at) {
        if (!stopped) { level = at; }
    }
};

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

class stopwatch {
public:
    [[nodiscard]] double ms() {
        const auto now = std::chrono::steady_clock::now();
        const double out = std::chrono::duration<double, std::milli>(now - mark_).count();
        mark_ = now;
        return out;
    }

private:
    std::chrono::steady_clock::time_point mark_ = std::chrono::steady_clock::now();
};

[[nodiscard]] measurement measure(const std::string & source) {
    measurement m;
    stopwatch clock;

    if (source.empty()) {
        m.fail_at(rung_read, "vendor/phaser/phaser.js is missing or empty");
        return m;
    }
    m.reached(rung_read);

    // The language rungs go through the same front end a page does, so a
    // failure here is reported before a DOM can confuse the diagnosis.
    const ctbrowser::script::program program = ctbrowser::script::compiler::compile(source);
    m.compile_ms = clock.ms();
    // Lexing and parsing are inside compile(); a failure in either surfaces as
    // a compile error naming the offset, which is the same information split
    // differently. Recorded as separate rungs anyway so the ladder stays
    // comparable with p5's.
    m.reached(rung_lexed);
    m.reached(rung_parsed);
    if (!program.ok) {
        m.fail_at(rung_compiled, program.error);
        return m;
    }
    m.reached(rung_compiled);

    // AS A PAGE, from here on. Phaser calls document.createElement before it
    // does anything else, so there is no headless rung to measure.
    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{400, 400}};
    page.assets().add(
        "phaser.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(source.data()),
                               reinterpret_cast<const std::byte *>(source.data() + source.size())});
    page.load_html(R"(<html><head><meta charset="utf-8">
      <script src="phaser.js"></script></head><body></body></html>)");
    m.page_ms = clock.ms();
    if (!page.script_error().empty()) {
        m.fail_at(rung_page, page.script_error());
        return m;
    }
    m.reached(rung_page);

    // Everything past here is asked THROUGH the page, because the answers only
    // it has. A throw is caught and reported rather than left to unwind.
    // The console is append-only, so the answer is whatever was added - not
    // `back()`, which would pick up anything Phaser logged in between.
    const auto ask = [&page](const char * expression) -> std::string {
        const std::size_t before = page.bindings().console_output().size();
        (void)page.run_script(std::string{"try { console.log('=' + String("} + expression +
                              ")); } catch (e) { console.log('=threw: ' + (e && e.message ? "
                              "e.message : e)); }");
        const auto & said = page.bindings().console_output();
        for (std::size_t i = said.size(); i-- > before;) {
            if (said[i].starts_with("=")) { return said[i].substr(1); }
        }
        return "<no answer>";
    };

    const std::string defines = ask("typeof Phaser !== 'undefined' && !!Phaser.Game");
    if (defines != "true") {
        m.fail_at(rung_defines, "Phaser is not defined after the bundle ran: " + defines);
        return m;
    }
    m.reached(rung_defines);

    // A Game with a headless-ish config. AUTO would pick a renderer; CANVAS is
    // asked for by name so a WebGL gap cannot be mistaken for a Game gap.
    const std::string game = ask(R"JS((function () {
        window.__phase = '';
        window.__game = new Phaser.Game({
            type: Phaser.CANVAS, width: 200, height: 200, banner: false, audio: {noAudio: true},
            scene: {
                create: function () { window.__phase += 'create;'; },
                update: function () { window.__phase += 'update;'; }
            }
        });
        return 'constructed';
    })())JS");
    if (game != "constructed") {
        m.fail_at(rung_game, "new Phaser.Game(...) did not construct: " + game);
        return m;
    }
    m.reached(rung_game);

    // Phaser drives itself off requestAnimationFrame, so ticking the page is
    // what lets its loop run at all.
    for (int i = 0; i < 30; ++i) { page.tick(16); }

    const std::string phase = ask("window.__phase || '<empty>'");
    if (phase.find("create;") == std::string::npos) {
        m.fail_at(rung_create, "create() never ran: phase=" + phase);
        return m;
    }
    m.reached(rung_create);
    if (phase.find("update;") == std::string::npos) {
        m.fail_at(rung_update, "update() never ran: phase=" + phase);
        return m;
    }
    m.reached(rung_update);

    // --- and does what it drew reach the PIXELS? -----------------------------
    //
    // A create() that is called and paints nothing satisfies every rung above
    // this one, so these are genuinely different questions. p5 answered them
    // differently for a whole stretch: it ran its entire draw loop while
    // `(220).toString(16)` returned "220", so every fill came out white.
    //
    // Drawn HERE rather than left to examples/corpus/phaserbasic.cpp, because that one
    // needs SDL to build and this test must not. The golden comparison is still
    // that example's job - this rung asks the weaker, unmissable question: did
    // any of it land at all.
    (void)ask(R"JS((function () {
        var scene = window.__game.scene.scenes[0];
        var g = scene.add.graphics();
        g.fillStyle(0xff0000, 1);
        g.fillRect(0, 0, 60, 60);
        return 'drew';
    })())JS");
    for (int i = 0; i < 5; ++i) { page.tick(16); }

    // The canvas is found by walking the document for the element Phaser made,
    // rather than by id - Phaser does not give it one.
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
    if (!canvas || pixels == nullptr) {
        m.fail_at(rung_paints, "Phaser's canvas has no pixel buffer");
        return m;
    }
    const ctbrowser::color drawn{pixels->at(20, 20)};
    if (drawn != ctbrowser::color::rgba(255, 0, 0)) {
        m.fail_at(rung_paints, "fillRect did not reach the pixels (20,20 is " +
                                   std::to_string(drawn.red()) + "," +
                                   std::to_string(drawn.green()) + "," +
                                   std::to_string(drawn.blue()) + ")");
        return m;
    }
    m.reached(rung_paints);
    return m;
}

// IS IT PLAYABLE? A different question from any rung above, and the one the
// corpus is ultimately for: Space Invaders is only a game if a key moves the
// ship. Phaser reads the keyboard as STATE polled per frame - `cursors.left
// .isDown` - rather than as a callback, which is a path no p5 sketch in this
// tree exercises at all, and it depends on the engine delivering DOM `code`
// values ("ArrowLeft") to a listener Phaser installed on the document.
//
// Reported rather than ratcheted, because it is a separate claim from how far
// the bundle gets and belongs beside it rather than inside its ladder.
[[nodiscard]] std::string keyboard_verdict() {
    const std::string source = read_file("vendor/phaser/phaser.js");
    if (source.empty()) { return "phaser.js is missing"; }

    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{200, 200}};
    page.assets().add(
        "phaser.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(source.data()),
                               reinterpret_cast<const std::byte *>(source.data() + source.size())});
    page.load_html(R"(<html><head><meta charset="utf-8">
      <script src="phaser.js"></script></head><body></body></html>)");
    (void)page.run_script(R"JS((function () {
        window.__seen = 'none';
        new Phaser.Game({
            type: Phaser.CANVAS, width: 100, height: 100, banner: false,
            audio: { noAudio: true },
            scene: {
                create: function () { this.cursors = this.input.keyboard.createCursorKeys(); },
                update: function () {
                    if (this.cursors.left.isDown) { window.__seen = 'left'; }
                    else if (this.cursors.right.isDown) { window.__seen = 'right'; }
                }
            }
        });
    })())JS");
    for (int i = 0; i < 30; ++i) { page.tick(16); }

    // Held down across several frames, because Phaser polls state per frame -
    // a press and release inside one tick is exactly what a polled reader is
    // allowed to miss, and asserting on that would be testing the test.
    (void)page.handle(ctbrowser::input_event::key_press("ArrowLeft"));
    for (int i = 0; i < 5; ++i) { page.tick(16); }
    (void)page.handle(ctbrowser::input_event::key_release("ArrowLeft"));

    const std::size_t before = page.bindings().console_output().size();
    (void)page.run_script("console.log('=' + String(window.__seen));");
    const auto & said = page.bindings().console_output();
    for (std::size_t i = said.size(); i-- > before;) {
        if (said[i].starts_with("=")) { return said[i].substr(1); }
    }
    return "<no answer>";
}

// The recorded floor: `key=value` lines, the same shape tests/corpus/p5/p5-ratchet.txt
// uses. A missing file is not an error - it is the state this test shipped in
// for one day, and it reports the number rather than inventing a floor.
[[nodiscard]] std::string recorded(const std::string & text, std::string_view key) {
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t end = text.find('\n', at);
        const std::string_view line{text.data() + at,
                                    (end == std::string::npos ? text.size() : end) - at};
        if (line.starts_with(key) && line.size() > key.size() && line[key.size()] == '=') {
            return std::string{line.substr(key.size() + 1)};
        }
        if (end == std::string::npos) { break; }
        at = end + 1;
    }
    return {};
}

} // namespace

int main() {
    const std::string source = read_file("vendor/phaser/phaser.js");
    const measurement m = measure(source);

    // MACHINE-READABLE FIRST, for tools/corpus/phaser-ratchet.py, then the sentence a
    // person reads. Same two lines p5_ratchet emits, so one tool shape drives
    // both.
    std::printf("LEVEL %d/%d\n", m.level, rung_paints);
    std::printf("BLOCKER %s\n", m.blocker.c_str());
    std::printf("     PHASER LEVEL %d/%d (%s)\n", m.level, rung_paints, rung_name(m.level));
    if (!m.blocker.empty()) { std::printf("     blocked by: %s\n", m.blocker.c_str()); }
    std::printf("     compile %.0f ms, page %.0f ms\n", m.compile_ms, m.page_ms);
    const std::string keyboard = keyboard_verdict();
    std::printf("     keyboard: ArrowLeft held -> %s\n", keyboard.c_str());
    if (keyboard != "left") {
        std::printf("FAIL a held arrow key does not reach a Phaser scene - the corpus can "
                    "render a game but not play one\n");
        ++ctbrowser_test_failures;
    }

    // THE PAWL. It turns one way: the level may not go down, and at the same
    // level the blocker may not change. That second half is the one that
    // matters - a fix that trades one wall for another leaves the number alone
    // and reads as "no change" without it.
    //
    // Only tools/corpus/phaser-ratchet.py --advance writes the record, because a test
    // that edits its own expectations cannot fail.
    const std::string record = read_file("tests/corpus/phaser/phaser-ratchet.txt");
    if (record.empty()) {
        std::printf("     (no tests/corpus/phaser/phaser-ratchet.txt yet - run "
                    "tools/corpus/phaser-ratchet.py --advance to record this)\n");
        REPORT("phaser_ratchet");
    }
    const std::string want_level = recorded(record, "level");
    const std::string want_blocker = recorded(record, "blocker");
    if (!want_level.empty()) {
        const int floor_level = std::stoi(want_level);
        if (m.level < floor_level) {
            std::printf("FAIL phaser went BACKWARDS: %d, recorded %d (%s)\n", m.level, floor_level,
                        rung_name(floor_level));
            ++ctbrowser_test_failures;
        } else if (m.level == floor_level && m.blocker != want_blocker) {
            // Progress at the same rung is still progress, but it has to be
            // recorded deliberately - a blocker that changes silently is a fix
            // that swapped one wall for another and told nobody.
            std::printf("FAIL phaser is stuck at %d but the blocker CHANGED\n"
                        "  was: %s\n  now: %s\n",
                        m.level, want_blocker.c_str(), m.blocker.c_str());
            ++ctbrowser_test_failures;
        } else if (m.level > floor_level) {
            std::printf("     AHEAD of the record (%d > %d) - run "
                        "tools/corpus/phaser-ratchet.py --advance\n",
                        m.level, floor_level);
        }
    }
    REPORT("phaser_ratchet");
}
