#pragma once
// THE CORPUS RATCHET, shared by the *_ratchet.cpp tests that climb a ladder.
//
// Each corpus test measures how FAR a bundle gets - a rung number and the
// blocker that stopped it - and holds that against a recorded floor. The
// measurement, the record reader, the ask-the-page helper and the pawl were
// re-typed in phaser, webgl2, modules and babylon; the rungs themselves stay
// per corpus, since they ARE the test. p5's measurement keeps a stack trace
// and runs two ladders, so it keeps its own.
//
// tools/corpus/ratchet.py reads the `LEVEL n/`, `BLOCKER` and `blocked by:`
// lines, which the callers print, and the AHEAD/FAIL wording here is what a
// reader of the ctest log has learned; keep both verbatim.

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include <ctbrowser.hpp>

#include "check.hpp"

namespace ctbrowser_test {

// Where the ladder stopped, and why. Rung 0 is "nothing yet".
struct measurement {
    int level = 0;
    std::string blocker;
    bool stopped = false;

    void fail_at(int at, std::string why) {
        if (stopped) { return; }
        level = at - 1;
        // The first line is the blocker; a stack below it is CONTEXT that moves
        // with edits having nothing to do with the cause, so it is printed and
        // not compared.
        const std::size_t newline = why.find('\n');
        blocker = newline == std::string::npos ? std::move(why) : why.substr(0, newline);
        stopped = true;
    }
    void reached(int at) {
        if (!stopped) { level = at; }
    }
};

// The recorded floor: `key=value` lines.
[[nodiscard]] inline std::string recorded(const std::string & text, std::string_view key) {
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

// One expression asked THROUGH the page, because the answers only it has. A
// throw is caught and reported rather than left to unwind. The console is
// append-only, so the answer is whatever was added - not `back()`, which
// would pick up anything the bundle logged in between.
[[nodiscard]] inline std::string ask(ctbrowser::browser & page, std::string_view expression) {
    const std::size_t before = page.bindings().console_output().size();
    std::string script{"try { console.log('=' + String("};
    script.append(expression);
    script += ")); } catch (e) { console.log('=threw: ' + (e && e.message ? e.message : e)); }";
    (void)page.run_script(script);
    const auto & said = page.bindings().console_output();
    for (std::size_t i = said.size(); i-- > before;) {
        if (said[i].starts_with("=")) { return said[i].substr(1); }
    }
    return "<no answer>";
}

// THE PAWL. It turns one way: the level may not go down, and at the same
// level the blocker may not change. That second half is the one that
// matters - a fix that trades one wall for another leaves the number alone
// and reads as "no change" without it. Only `<tool> --advance` writes the
// record, because a test that edits its own expectations cannot fail; a
// missing record is reported, not failed. Failures bump
// ctbrowser_test_failures; the caller REPORT()s.
template <typename RungName>
void ratchet_pawl(const char * tag, const char * record_path, const char * tool,
                  const measurement & m, RungName rung_name) {
    const std::string record = read_file(record_path);
    if (record.empty()) {
        std::printf("     (no %s yet - run %s --advance to record this)\n", record_path, tool);
        return;
    }
    const std::string want_level = recorded(record, "level");
    const std::string want_blocker = recorded(record, "blocker");
    if (want_level.empty()) { return; }
    const int floor_level = std::stoi(want_level);
    if (m.level < floor_level) {
        std::printf("FAIL %s went BACKWARDS: %d, recorded %d (%s)\n", tag, m.level, floor_level,
                    rung_name(floor_level));
        ++ctbrowser_test_failures;
    } else if (m.level == floor_level && m.blocker != want_blocker) {
        std::printf("FAIL %s is stuck at %d but the blocker CHANGED\n  was: %s\n  now: %s\n", tag,
                    m.level, want_blocker.c_str(), m.blocker.c_str());
        ++ctbrowser_test_failures;
    } else if (m.level > floor_level) {
        std::printf("     AHEAD of the record (%d > %d) - run %s --advance\n", m.level, floor_level,
                    tool);
    }
}

} // namespace ctbrowser_test
