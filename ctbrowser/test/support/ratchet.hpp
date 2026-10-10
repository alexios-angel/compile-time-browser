//===- ctbrowser/test/support/ratchet.hpp - Corpus ratchet helpers --------===//
//
// The corpus ratchet, shared by the *_ratchet.cpp tests. Each corpus test
// walks a ladder of rungs (the stages a third-party bundle must pass, such as
// "parsed", "compiled", "constructs a Game"), measures how far the bundle gets and
// which blocker stopped it, and holds that against a recorded floor.
//
// The measurement, the record reader, the ask-the-page helper and the pawl
// are shared by the phaser, webgl2, modules and babylon tests; each corpus
// keeps its own list of rungs, since those are the test. p5's measurement
// keeps a stack trace and runs two ladders, so it has its own.
//
// tools/corpus/ratchet.py reads the `LEVEL n/`, `BLOCKER` and `blocked by:`
// lines, which the callers print, and readers of the ctest log rely on the
// AHEAD/FAIL wording here; keep both verbatim.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include <ctbrowser.hpp>

#include "check.hpp"

namespace ctbrowser_test {

/// Where the ladder stopped, and why. Rung 0 is "nothing yet".
struct measurement {
    int level = 0;
    std::string blocker;
    bool stopped = false;

    /// Records that rung `at` failed with `why`; the level is the rung below.
    /// Only the first failure counts.
    void fail_at(int at, std::string why) {
        if (stopped) { return; }
        level = at - 1;
        // The first line is the blocker. A stack below it is context that
        // changes with unrelated edits, so it is printed but not compared.
        const std::size_t newline = why.find('\n');
        blocker = newline == std::string::npos ? std::move(why) : why.substr(0, newline);
        stopped = true;
    }
    /// Records that rung `at` passed, unless an earlier rung already failed.
    void reached(int at) {
        if (!stopped) { level = at; }
    }
};

/// Returns the value of `key` in a record of `key=value` lines, or empty.
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

/// Evaluates `expression` inside the page, which alone has the answers, and
/// returns String(result) or "threw: <message>". The console is append-only,
/// so the answer is the marked line this call added, not `back()`, which would
/// pick up anything the bundle logged in between.
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

/// The pawl: it lets the ratchet turn one way only. The level may not go down,
/// and at the same level the blocker may not change; without the second rule
/// a fix that trades one blocker for another would read as "no change". Only
/// `<tool> --advance` writes the record, because a test that edits its own
/// expectations cannot fail; a missing record is reported, not failed.
/// Failures bump ctbrowser_test_failures; the caller still calls REPORT().
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
