//===- unittests/unit/bootstrap_layout.cpp - Bootstrap layout baselines ---===//
//
// The Bootstrap fixtures, pinned: ctbrowser checked against its own recorded
// answer.
//
// This is one half of the CSS parity harness. tools/check/css-parity.py
// compares against Chrome, needs Playwright and a browser download, and is
// deliberately not a ctest (docs/build.md: "a browser-versus-browser diff
// should be read, not silently failed"). This file is what catches a
// regression automatically.
//
// It generates ctbrowser's side of the same table, from the same script
// (tools/check/css-dump.js) and the same property list
// (tools/check/css-parity-props.txt), and byte-compares
// test/baseline/bootstrap-*.txt. No Chrome, no Playwright, no SDL, no window -
// so it runs as an ordinary ctest wherever the suite runs, including the remote
// build machine.
//
// The two halves work together: the Chrome comparison shows that a baseline is
// right, and this test shows that it has not changed. When a layout change
// moves the numbers, regenerate, read the diff (it names the element and the
// property), and check the new values against Chrome. An image diff could not
// say which element moved.
//
//   REGOLDEN=1 ctest --preset default -R bootstrap_layout
//   git diff test/baseline/            # and read it
//
// Never accept a regenerated baseline without reading the diff: a wrong answer
// recorded as golden passes forever.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"
#include "engine.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

/// The viewport css-parity.py uses, and for the same reason: Bootstrap's
/// breakpoints are 576/768/992/1200, and 1024 sits 32px inside `lg`, clear of a
/// boundary. A fixture that straddled one would turn a rounding difference into
/// a two-hundred-pixel layout difference.
constexpr int viewport_width = 1024;
constexpr int viewport_height = 768;

/// Fixture names: examples/pages/bootstrap-<name>.html and
/// test/baseline/bootstrap-<name>.txt.
constexpr std::string_view fixtures[] = {"box",        "type",     "grid",
                                         "components", "position", "kitchen"};

/// One fixture's table, as ctbrowser sees it. `props` is the text of
/// css-parity-props.txt, handed to the dump as data. Returns an empty string
/// and sets `why` on failure.
[[nodiscard]] std::string table_for(std::string_view name, const std::string & props,
                                    std::string & why) {
    const std::filesystem::path page =
        std::filesystem::path{"examples/pages"} / ("bootstrap-" + std::string{name} + ".html");
    const std::string html = read_file(page);
    if (html.empty()) {
        why = "cannot read " + page.string();
        return {};
    }

    browser page_browser{browser_options{viewport_width, viewport_height}};
    // Where `../../vendor/bootstrap/bootstrap.css` resolves from. The asset
    // registry probes ".", then the base path, then two levels above it
    // (shell/page/assets.hpp), and the test's working directory is the source
    // root - so without this the fixture's own stylesheet does not load and every
    // number in the baseline is the unstyled one.
    page_browser.assets().set_base_path("examples/pages");
    // Unconfined, explicitly: `../../vendor/` climbs out of the base path, which
    // a confined registry refuses. Trusted in-tree fixture - never do this for
    // a page from the network.
    page_browser.assets().set_confine(false);
    page_browser.load_html(html);
    if (!page_browser.style_error().empty()) {
        why = page_browser.style_error();
        return {};
    }
    // A frame, so layout and paint have both run before anything is asked: the
    // geometry comes off the fragment tree and getComputedStyle reads the box
    // tree, and neither exists until one has.
    page_browser.frame();
    // The dump is loaded into the fixture rather than listed by it: the fixture is
    // shared with css-parity.py, which runs the same file its own way.
    // `../tools/`: see main().
    const auto engine = ctbrowser_test::make_engine(page_browser);
    if (const std::string error = engine->load("../tools/check/css-dump.js"); !error.empty()) {
        why = "the dump script did not run: " + error;
        return {};
    }
    // Its inputs, as strings: PROPS, PROPS_VERSION, FROM, COUNT and COMPACT.
    // PROPS comes off disk rather than being spelled again here: css-parity.py
    // --emit-props writes it, so the Python list is the only copy and this cannot
    // drift from what the Chrome comparison asks. COUNT is every element: there is
    // no socket here, so there is nothing to chunk for. css-parity.py chunks
    // because a reply crosses a wire. COMPACT asks for the one-line-per-element
    // form the baselines are in.
    std::string table = engine->call("css_dump_table", {props, "1", "0", "1000000", "1"});
    // The `#head` line is kept on purpose. css-parity.py consumes and strips it,
    // because for a cross-engine diff it is a precondition rather than data. Here
    // it is data: it records the viewport, the clientWidth and the element count
    // the rest of the table was measured at, so a baseline cannot be read out of
    // context and a change in any of the three shows up as line 1 rather than as
    // every line. It also records clientWidth next to the layout width, so a fix
    // for the known mismatch between the two (see docs/plans/bootstrap.md)
    // shows up here as a one-line change.
    //
    // So every valid table opens with it. Any other answer is the engine's
    // "threw: ..." or "<no answer>", and must never reach a baseline under
    // REGOLDEN=1.
    if (!table.starts_with("#head ")) {
        why = "the dump script did not run: " + table;
        return {};
    }
    return table;
}

/// Compares one fixture's table with its baseline, or rewrites the baseline
/// when `regolden` is set. Every failure increments ctbrowser_test_failures.
void run_fixture(std::string_view name, const std::string & props, bool regolden) {
    const std::filesystem::path baseline =
        std::filesystem::path{"test/baseline"} / ("bootstrap-" + std::string{name} + ".txt");
    std::string why;
    const std::string table = table_for(name, props, why);
    if (table.empty()) {
        std::printf("FAIL bootstrap-%s: %s\n", std::string{name}.c_str(), why.c_str());
        ++ctbrowser_test_failures;
        return;
    }

    if (regolden) {
        std::filesystem::create_directories(baseline.parent_path());
        std::ofstream out{baseline, std::ios::binary};
        out << table;
        std::printf("REGOLDEN bootstrap-%s: wrote %s (%zu lines)\n", std::string{name}.c_str(),
                    baseline.string().c_str(),
                    static_cast<std::size_t>(std::count(table.begin(), table.end(), '\n')));
        return;
    }

    const std::string want = read_file(baseline);
    if (want.empty()) {
        std::printf("FAIL bootstrap-%s: no baseline at %s - REGOLDEN=1 to write one\n",
                    std::string{name}.c_str(), baseline.string().c_str());
        ++ctbrowser_test_failures;
        return;
    }
    if (want == table) { return; }

    // Name the first differing line. The whole reason this is a text baseline and
    // not an image is that it can say which element and which property moved;
    // printing "the tables differ" would throw that away.
    ++ctbrowser_test_failures;
    std::istringstream mine{table};
    std::istringstream theirs{want};
    std::string a, b;
    std::size_t at = 0;
    while (true) {
        const bool got_a = static_cast<bool>(std::getline(mine, a));
        const bool got_b = static_cast<bool>(std::getline(theirs, b));
        ++at;
        if (!got_a && !got_b) { break; }
        if (a != b) {
            std::printf("FAIL bootstrap-%s: line %zu differs\n  now: %s\n  was: %s\n",
                        std::string{name}.c_str(), at, got_a ? a.c_str() : "(end of table)",
                        got_b ? b.c_str() : "(end of baseline)");
            std::printf("  REGOLDEN=1 ctest --preset default -R bootstrap_layout, then READ "
                        "git diff test/baseline/\n");
            return;
        }
    }
}

} // namespace

int main() {
    // `../tools/`, not `tools/`. ctest runs this with WORKING_DIRECTORY =
    // PROJECT_SOURCE_DIR, which is the ctbrowser project directory, and tools/
    // is at the repository root one level up because it drives both projects.
    // css-dump.js is reached the same way, by the engine's load() in
    // table_for().
    //
    // A missing input must count as a failure: REPORT exits nonzero only when
    // ctbrowser_test_failures is nonzero, so a bare printf("FAIL ...") would
    // pass. A missing css-dump.js fails every fixture through run_fixture.
    const std::string props = read_file("../tools/check/css-parity-props.txt");
    if (props.empty()) {
        std::printf("FAIL no tools/check/css-parity-props.txt - "
                    "tools/check/css-parity.py --emit-props writes it\n");
        ++ctbrowser_test_failures;
        REPORT("bootstrap_layout");
    }
    const char * regolden = std::getenv("REGOLDEN");
    const bool write = regolden != nullptr && std::string_view{regolden} != "0";

    for (const std::string_view name : fixtures) { run_fixture(name, props, write); }
    REPORT("bootstrap_layout");
}
