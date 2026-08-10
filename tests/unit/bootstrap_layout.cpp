// The Bootstrap fixtures, pinned. ctbrowser against its own recorded answer.
//
// THE OTHER HALF OF THE PARITY HARNESS, and the split is the design.
// tools/check/css-parity.py compares against Chrome, needs Playwright and a
// browser download, and is deliberately not a ctest - docs/build.md: "a
// browser-versus-browser diff should be read, not silently failed." That leaves
// nothing catching a REGRESSION automatically, which is this file.
//
// It generates ctbrowser's own side of exactly the same table, from exactly the
// same script (tools/check/css-dump.js) and the same property list
// (tools/check/css-parity-props.txt), and byte-compares
// tests/baseline/bootstrap-*.txt. No Chrome, no Playwright, no SDL, no window -
// so it runs on the devbox, which is where the gate actually is.
//
// The relationship between the two halves is the point: this baseline needs no
// Chrome, and what the Chrome comparison gives you is the knowledge that the
// baseline is RIGHT. Recording a wrong answer is exactly what this file is for
// when a rung lands - the numbers move, you read the diff, and the diff names the
// element and the property. A .ppm cannot tell you that.
//
//   REGOLDEN=1 ctest --preset default -R bootstrap_layout
//   git diff tests/baseline/            # AND READ IT
//
// A regenerated baseline accepted without being read is the text equivalent of
// the empty-button render, which is the reason CLAUDE.md shouts about opening the
// image.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// The viewport css-parity.py uses, and for the same reason: Bootstrap's
// breakpoints are 576/768/992/1200, and 1024 sits 32px inside `lg`, clear of a
// boundary. A fixture that straddled one would turn a rounding difference into a
// two-hundred-pixel layout difference.
constexpr int viewport_width = 1024;
constexpr int viewport_height = 768;

constexpr std::string_view fixtures[] = {"box",        "type",     "grid",
                                         "components", "position", "kitchen"};

[[nodiscard]] std::string read_file(const std::filesystem::path & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

// The dump script, with its inputs bound. PROPS comes off disk rather than being
// spelled again here: css-parity.py --emit-props writes it, so the Python list is
// the only copy and this cannot drift from what the Chrome comparison asks.
//
// The geometry columns are filtered out. They are in the props file because the
// REPORT has a column for each, but they come from getBoundingClientRect rather
// than from getComputedStyle - asking the cascade for `@x` would just add four
// empty answers.
[[nodiscard]] std::string dump_script(const std::string & props_file, const std::string & dump) {
    std::string props = "[";
    std::istringstream lines{props_file};
    std::string line;
    bool first = true;
    while (std::getline(lines, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) { line.pop_back(); }
        if (line.empty() || line[0] == '#' || line[0] == '@') { continue; }
        if (!first) { props += ","; }
        props += "\"" + line + "\"";
        first = false;
    }
    props += "]";
    // COUNT is every element: there is no socket here, so there is nothing to
    // chunk for. css-parity.py chunks because a reply crosses a wire.
    return "var PROPS = " + props + ";\nvar PROPS_VERSION = 1;\nvar FROM = 0;\n" +
           "var COUNT = 1000000;\nvar COMPACT = 1;\n" + dump;
}

// One fixture's table, as ctbrowser sees it.
[[nodiscard]] std::string table_for(std::string_view name, const std::string & script_tail,
                                    std::string & why) {
    const std::filesystem::path page =
        std::filesystem::path{"examples/pages"} / ("bootstrap-" + std::string{name} + ".html");
    const std::string html = read_file(page);
    if (html.empty()) {
        why = "cannot read " + page.string();
        return {};
    }

    browser page_browser{browser_options{viewport_width, viewport_height}};
    // WHERE `../../vendor/bootstrap/bootstrap.css` RESOLVES FROM. The asset
    // registry probes ".", then the base path, then two levels above it
    // (shell/page/assets.hpp), and the test's working directory is the source
    // root - so without this the fixture's own stylesheet does not load and every
    // number in the baseline is the unstyled one.
    page_browser.assets().set_base_path("examples/pages");
    page_browser.load_html(html);
    if (!page_browser.style_error().empty()) {
        why = page_browser.style_error();
        return {};
    }
    // A frame, so layout and paint have both run before anything is asked: the
    // geometry comes off the fragment tree and getComputedStyle reads the box
    // tree, and neither exists until one has.
    if (!page_browser.frame().has_value()) {
        why = "the page did not render";
        return {};
    }
    if (!page_browser.run_script(script_tail)) {
        why = "the dump script did not run: " + page_browser.script_error();
        return {};
    }
    // THE `#head` LINE IS KEPT, on purpose. css-parity.py consumes and strips it,
    // because for a cross-engine diff it is a precondition rather than data. Here
    // it IS data: it records the viewport, the clientWidth and the element count
    // the rest of the table was measured at, so a baseline cannot be read out of
    // context and a change in any of the three shows up as line 1 rather than as
    // every line. It is also the line that will catch the clientWidth-versus-
    // layout-width inconsistency in docs/plans/bootstrap.md the moment S7 fixes it.
    std::string out;
    for (const std::string & logged : page_browser.bindings().console_output()) {
        out += logged;
        out += '\n';
    }
    if (out.empty()) { why = "the dump script logged nothing"; }
    return out;
}

void run_fixture(std::string_view name, const std::string & props, const std::string & dump,
                 bool regolden) {
    const std::filesystem::path baseline =
        std::filesystem::path{"tests/baseline"} / ("bootstrap-" + std::string{name} + ".txt");
    std::string why;
    const std::string table = table_for(name, dump_script(props, dump), why);
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

    // NAME THE FIRST DIFFERING LINE. The whole reason this is a text baseline and
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
                        "git diff tests/baseline/\n");
            return;
        }
    }
}

} // namespace

int main() {
    const std::string props = read_file("tools/check/css-parity-props.txt");
    if (props.empty()) {
        std::printf("FAIL no tools/check/css-parity-props.txt - "
                    "tools/check/css-parity.py --emit-props writes it\n");
        REPORT("bootstrap_layout");
    }
    const std::string dump = read_file("tools/check/css-dump.js");
    if (dump.empty()) {
        std::printf("FAIL no tools/check/css-dump.js\n");
        REPORT("bootstrap_layout");
    }
    const char * regolden = std::getenv("REGOLDEN");
    const bool write = regolden != nullptr && std::string_view{regolden} != "0";

    for (const std::string_view name : fixtures) { run_fixture(name, props, dump, write); }
    REPORT("bootstrap_layout");
}
