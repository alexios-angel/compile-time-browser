// How much of the ES module system this engine has, measured.
//
// SAME SHAPE AS p5_ratchet, phaser_ratchet AND webgl2_ratchet - a ladder, a
// recorded rung, and a blocker that may not change silently - because that
// shape has earned it three times.
//
// WHY IT EXISTS. docs/modules-plan.md: this engine runs ONE flavour of
// JavaScript, the classic script, and every script on a page is concatenated
// into a single program. Modules are not something that can be bent into -
// each has its own scope, its own binding namespace, and an execution order
// derived from a dependency graph. Babylon is the corpus that proves the gap:
// its UMD build asks for a shader body with `import()` and rejects its own
// request, which is what stops webgl2 rung 10.
//
// THE LADDER IS ORDERED BY WHAT DEPENDS ON WHAT. Syntax first, because nothing
// can run until it parses. Then one module in its own scope, which is the
// execution path without the graph. Then the graph, which is where live
// bindings and cycles live. Then the loader, which is where a page and a
// network arrive. Babylon last, because it needs all of it.
//
// RUNGS 1-4 GO THROUGH THE COMPILER DIRECTLY rather than through a page. A
// parse error names an offset; a page reports "the script did not run", and
// the difference is a finding against a hunt. The page rungs start at 5,
// which is where a page is genuinely what is being tested.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum rung {
    rung_none = 0,
    rung_syntax = 1,     // import/export parse at all
    rung_one_module = 2, // a module with no imports runs, in its own scope
    rung_graph = 3,      // two modules, and the importer sees the export
    rung_live = 4,       // an imported binding is LIVE, not a copy
    rung_cycle = 5,      // A imports B imports A resolves rather than hangs
    rung_page = 6,       // <script type="module"> on a real page
    rung_relative = 7,   // "./x.js" resolves against the importing module's URL
    rung_dynamic = 8,    // await import("./x.js") - the one Babylon needs
    rung_babylon = 9,    // and Babylon's ES build boots on it
};

[[nodiscard]] const char * rung_name(int level) {
    switch (level) {
    case rung_none: return "nothing";
    case rung_syntax: return "import/export parse";
    case rung_one_module: return "one module runs in its own scope";
    case rung_graph: return "an importer sees an export";
    case rung_live: return "imported bindings are live";
    case rung_cycle: return "a cycle resolves";
    case rung_page: return "<script type=module> runs on a page";
    case rung_relative: return "relative specifiers resolve";
    case rung_dynamic: return "dynamic import() works";
    case rung_babylon: return "Babylon's ES build boots";
    default: return "?";
    }
}

struct measurement {
    int level = rung_none;
    std::string blocker;
    bool stopped = false;

    void fail_at(int at, std::string why) {
        if (stopped) { return; }
        level = at - 1;
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

// The recorded floor: `key=value` lines, the same shape the other three use.
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

[[nodiscard]] std::string ask(ctbrowser::shell::browser & page, const char * expression) {
    const std::size_t before = page.bindings().console_output().size();
    (void)page.run_script(std::string{"try { console.log('=' + String("} + expression +
                          ")); } catch (e) { console.log('=threw: ' + (e && e.message ? "
                          "e.message : e)); }");
    const auto & said = page.bindings().console_output();
    for (std::size_t i = said.size(); i-- > before;) {
        if (said[i].starts_with("=")) { return said[i].substr(1); }
    }
    return "<no answer>";
}

[[nodiscard]] measurement measure() {
    measurement m;

    // --- 1: is module syntax UNDERSTOOD --------------------------------------
    //
    // NOT "does it parse". That was the first version of this rung and it
    // passed on day one, for the worst possible reason: ctjs accepts
    //
    //     this is not javascript at all ###
    //
    // without complaint. A lenient parser that accepts arbitrary text cannot be
    // asked whether it understood something - the answer is always yes.
    //
    // AND NOT "does it work" either, which was the second version and was too
    // strict in the other direction: it demanded dynamic import RETURN a
    // Promise, which is stage 4 of docs/modules-plan.md, so the whole ladder
    // sat at 0 while rungs 2 and 3 went unmeasured. A rung that cannot be
    // reached measures nothing.
    //
    // So: UNDERSTOOD means the engine knows what it is being asked. Refusing by
    // name counts - that is what stage 1 built - and treating `import` as an
    // ordinary identifier does not. The two are easy to tell apart: the first
    // says "not implemented", the second says "`import` is undefined".
    {
        const ctbrowser::script::program dynamic =
            ctbrowser::script::compiler::compile("const p = import('./m.js');");
        const bool understood =
            dynamic.ok || dynamic.error.find("not implemented") != std::string::npos;
        if (!understood) {
            m.fail_at(rung_syntax, "`import(...)` is not understood: " + dynamic.error);
            return m;
        }
        // A COMPILE-TIME REFUSAL IS ITSELF THE PROOF. Before the syntax
        // existed this source COMPILED - `import` was an identifier and
        // `import(...)` an ordinary call - and failed at run time with
        // "`import` is undefined, not a function". A compiler that stops and
        // names the feature has understood it.
        //
        // (`typeof import` was tried as a second check and is not a question
        // with a meaningful answer: `import` is not an expression on its own in
        // any JavaScript, so what it reports says nothing either way.)
    }
    {
        const ctbrowser::script::program meta =
            ctbrowser::script::compiler::compile("const u = import.meta.url;");
        const bool understood = meta.ok || meta.error.find("not implemented") != std::string::npos;
        if (!understood) {
            m.fail_at(rung_syntax, "`import.meta` is not understood: " + meta.error);
            return m;
        }
    }
    // And every declaration form, to the same bar.
    for (const char * form :
         {"import d from './m.js';", "import { a, b as c } from './m.js';",
          "import * as ns from './m.js';", "import './m.js';", "export const x = 1;",
          "export function f() {}", "export class C {}", "const a = 1; export { a };",
          "export default 42;", "export * from './m.js';", "export { x } from './m.js';"}) {
        const ctbrowser::script::program one = ctbrowser::script::compiler::compile(form);
        if (!one.ok && one.error.find("not implemented") == std::string::npos) {
            m.fail_at(rung_syntax, std::string{"`"} + form + "` is not understood: " + one.error);
            return m;
        }
    }
    m.reached(rung_syntax);

    // --- 2: one module, no imports, in ITS OWN SCOPE ------------------------
    // The scope is the point, not the execution. A classic script's `var`
    // reaches the global object; a module's must not, and getting that wrong is
    // how "modules work" while every page silently shares state.
    ctbrowser::shell::browser one{ctbrowser::shell::browser_options{200, 200}};
    one.assets().add("solo.js", std::vector<std::byte>{});
    one.load_html(R"(<html><body><script type="module">
        var moduleLocal = 1; globalThis.__ran = true;
      </script></body></html>)");
    const std::string ran = ask(one, "String(globalThis.__ran) + ',' + typeof moduleLocal");
    if (ran != "true,undefined") {
        m.fail_at(rung_one_module,
                  "a module did not run in its own scope (wanted true,undefined got " + ran + ")");
        return m;
    }
    m.reached(rung_one_module);

    // --- 3: two modules, and the importer sees the export -------------------
    m.fail_at(rung_graph, "not measured yet - rungs 3 to 9 need a module loader");
    return m;
}

// DOES BABYLON'S ES BUILD EVEN EXIST HERE? Reported beside the ladder rather
// than inside it, the way the WebGL 2 ratchet reports Babylon's version: the
// ladder stops at its first failure, and whether the corpus is present at all
// is a different question from how far the engine gets.
[[nodiscard]] std::string babylon_es_verdict() {
    const std::string umd = read_file("examples/assets/babylon/babylon.js");
    if (umd.empty()) { return "no Babylon vendored at all"; }
    if (umd.find("_BabylonUMDDynamicImportUnsupported") != std::string::npos) {
        return "only the UMD build is vendored, and it REJECTS its own dynamic import() - "
               "see docs/modules-plan.md";
    }
    return "a non-UMD Babylon is vendored";
}

} // namespace

int main() {
    const measurement m = measure();

    std::printf("LEVEL %d/%d\n", m.level, rung_babylon);
    std::printf("BLOCKER %s\n", m.blocker.c_str());
    std::printf("     MODULES LEVEL %d/%d (%s)\n", m.level, rung_babylon, rung_name(m.level));
    if (!m.blocker.empty()) { std::printf("     blocked by: %s\n", m.blocker.c_str()); }
    std::printf("     babylon: %s\n", babylon_es_verdict().c_str());

    // THE PAWL, identical in rule to the other three: the level may not go
    // down, and at the same level the blocker may not change. Only
    // tools/module-ratchet.py --advance writes the record.
    const std::string record = read_file("tests/module-ratchet.txt");
    if (record.empty()) {
        std::printf("     (no tests/module-ratchet.txt yet - run "
                    "tools/module-ratchet.py --advance to record this)\n");
        REPORT("module_ratchet");
    }
    const std::string want_level = recorded(record, "level");
    const std::string want_blocker = recorded(record, "blocker");
    if (!want_level.empty()) {
        const int floor_level = std::stoi(want_level);
        if (m.level < floor_level) {
            std::printf("FAIL modules went BACKWARDS: %d, recorded %d (%s)\n", m.level, floor_level,
                        rung_name(floor_level));
            ++ctbrowser_test_failures;
        } else if (m.level == floor_level && m.blocker != want_blocker) {
            std::printf("FAIL modules is stuck at %d but the blocker CHANGED\n"
                        "  was: %s\n  now: %s\n",
                        m.level, want_blocker.c_str(), m.blocker.c_str());
            ++ctbrowser_test_failures;
        } else if (m.level > floor_level) {
            std::printf("     AHEAD of the record (%d > %d) - run "
                        "tools/module-ratchet.py --advance\n",
                        m.level, floor_level);
        }
    }
    REPORT("module_ratchet");
}
