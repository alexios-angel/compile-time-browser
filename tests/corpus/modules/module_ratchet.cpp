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
#include <memory>
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

[[nodiscard]] std::string babylon_es_verdict();

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
        // AS A MODULE. `import` in a classic script is an error in every
        // engine, and this one says so now - so compiling these as classic
        // scripts would be testing the wrong thing.
        const ctbrowser::script::program one =
            ctbrowser::script::compiler::compile(form, ctbrowser::script::script_kind::module_);
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
    // THE WHOLE DOCUMENT, so a rung can put more than one script on a page and
    // ask about ORDER - which is the half of "a module script" that has nothing
    // to do with the module system.
    const auto page_of = [](const std::string & html,
                            std::initializer_list<std::pair<const char *, const char *>> files) {
        auto page = std::make_unique<ctbrowser::shell::browser>(
            ctbrowser::shell::browser_options{200, 200});
        for (const auto & [name, text] : files) {
            const std::string_view bytes{text};
            page->assets().add(
                name, std::vector<std::byte>{
                          reinterpret_cast<const std::byte *>(bytes.data()),
                          reinterpret_cast<const std::byte *>(bytes.data() + bytes.size())});
        }
        page->load_html(html);
        return page;
    };
    const auto page_with =
        [&page_of](const char * module_source,
                   std::initializer_list<std::pair<const char *, const char *>> files) {
            return page_of(std::string{R"(<html><body><script type="module">)"} + module_source +
                               "</script></body></html>",
                           files);
        };

    {
        auto page = page_with("import { greeting } from './m.js';"
                              "globalThis.__saw = greeting;",
                              {{"./m.js", "export const greeting = 'hello';"}});
        const std::string saw = ask(*page, "String(globalThis.__saw)");
        if (saw != "hello") {
            m.fail_at(rung_graph,
                      "an importer did not see the export (got " + saw + ")" +
                          (page->script_error().empty() ? "" : " | " + page->script_error()));
            return m;
        }
    }
    m.reached(rung_graph);

    // --- 4: the binding is LIVE, not a copy ---------------------------------
    // THE RUNG THAT CATCHES THE SHORTCUT. Handing the importer the VALUE at
    // link time passes rung 3 and fails here: the exporter's later write must
    // be visible, because an ES import is a binding and not an assignment.
    // Copying is the CommonJS behaviour and is what docs/modules-plan.md names
    // in advance as the thing that would look like progress.
    {
        auto page = page_with("import { count, bump } from './c.js';"
                              "globalThis.__before = count;"
                              "bump();"
                              "globalThis.__after = count;",
                              {{"./c.js", "export let count = 1;"
                                          "export function bump() { count = 42; }"}});
        const std::string moved = ask(*page, "String(globalThis.__before) + ',' + "
                                             "String(globalThis.__after)");
        if (moved != "1,42") {
            // SAYS WHAT IT SAW, not what it thinks caused it. "1,42" is live;
            // "1,1" is the copy this rung exists to catch; anything else is a
            // third thing, and calling that a copy would be a diagnosis the
            // measurement does not support.
            m.fail_at(rung_live,
                      "an imported binding did not update (wanted 1,42 got " + moved + ")" +
                          (page->script_error().empty() ? "" : " | " + page->script_error()));
            return m;
        }
    }
    // AND A CHAIN, still rung 4, because the ladder had no rung for it and a
    // real bug walked through the gap. The entry module imports `a.js`, which
    // imports `b.js` and calls it FROM INSIDE A FUNCTION. Nothing above asks
    // that: rung 3 calls an import at the module's top level, and this calls
    // one from a closure - which is where a module's imports are actually used.
    //
    // It read `undefined` for a while, and the rung that caught it was the
    // CYCLE rung, which is the wrong place: the same source with the cycle
    // removed failed identically. A rung reporting a failure two levels above
    // where it lives sends the next reader after the wrong thing.
    {
        auto page = page_with("import { fromA } from './a.js';"
                              "globalThis.__chain = fromA();",
                              {{"./a.js", "import { fromB } from './b.js';"
                                          "export function fromA() { return 'a' + fromB(); }"},
                               {"./b.js", "export function fromB() { return 'b'; }"}});
        const std::string chained = ask(*page, "String(globalThis.__chain)");
        if (chained != "ab") {
            m.fail_at(rung_live,
                      "an import used inside a function did not resolve (wanted ab got " + chained +
                          ")" + (page->script_error().empty() ? "" : " | " + page->script_error()));
            return m;
        }
    }
    // AND THROUGH AN INDEX FILE, still rung 4, because that is what every real
    // ES library is: `index.js` says `export * from './thing.js'` and nothing
    // else, and a page imports the index. Three properties at once - a
    // re-export by name, a star re-export, and both still LIVE, which a
    // re-export implemented as a copy fails while passing everything above.
    {
        auto page = page_with("import { named, starred, bump } from './index.js';"
                              "globalThis.__idx = named + ',' + starred;"
                              "bump();"
                              "globalThis.__idx2 = starred;",
                              {{"./index.js", "export { thing as named } from './a.js';"
                                              "export * from './b.js';"},
                               {"./a.js", "export const thing = 'A';"},
                               {"./b.js", "export let starred = 'B';"
                                          "export function bump() { starred = 'B2'; }"}});
        const std::string through = ask(*page, "String(globalThis.__idx) + ',' + "
                                               "String(globalThis.__idx2)");
        if (through != "A,B,B2") {
            m.fail_at(rung_live,
                      "a re-export did not carry the binding (wanted A,B,B2 got " + through + ")" +
                          (page->script_error().empty() ? "" : " | " + page->script_error()));
            return m;
        }
    }
    m.reached(rung_live);

    // --- 5: a cycle resolves rather than hanging ----------------------------
    {
        auto page = page_with("import { fromA } from './a.js';"
                              "globalThis.__cycle = fromA();",
                              {{"./a.js", "import { fromB } from './b.js';"
                                          "export function fromA() { return 'a' + fromB(); }"},
                               {"./b.js", "import { fromA } from './a.js';"
                                          "export function fromB() { return 'b'; }"}});
        const std::string cycled = ask(*page, "String(globalThis.__cycle)");
        if (cycled != "ab") {
            m.fail_at(rung_cycle,
                      "a cycle did not resolve (wanted ab got " + cycled + ")" +
                          (page->script_error().empty() ? "" : " | " + page->script_error()));
            return m;
        }
    }
    m.reached(rung_cycle);

    // --- 6: <script type=module> as a PAGE script ---------------------------
    //
    // Rungs 2 to 5 all used one, so what is left to measure is the half that is
    // not about the module system at all: a module script is DEFERRED - it
    // waits for the document rather than running where it sits - and a page may
    // carry more than one of them, and one may come from `src`.
    {
        // A module written FIRST still runs after a classic script written
        // second. That is the deferral, and it is observable.
        auto page = page_of(R"(<html><body>
            <script type="module">globalThis.__o = (globalThis.__o || '') + 'M';</script>
            <script>globalThis.__o = (globalThis.__o || '') + 'C';</script>
            </body></html>)",
                            {});
        const std::string order = ask(*page, "String(globalThis.__o)");
        if (order != "CM") {
            m.fail_at(rung_page,
                      "a module script did not defer past a classic one (wanted CM got " + order +
                          ")" + (page->script_error().empty() ? "" : " | " + page->script_error()));
            return m;
        }
    }
    {
        // TWO module scripts, and both run, in order. Worth its own case
        // because the failure is silent: whatever keys the module registry has
        // to distinguish them, and a shared key means the second one is taken
        // for a module already loaded and never runs at all.
        auto page = page_of(R"(<html><body>
            <script type="module">globalThis.__two = 'a';</script>
            <script type="module">globalThis.__two += 'b';</script>
            </body></html>)",
                            {});
        const std::string both = ask(*page, "String(globalThis.__two)");
        if (both != "ab") {
            m.fail_at(rung_page,
                      "two module scripts did not both run (wanted ab got " + both + ")" +
                          (page->script_error().empty() ? "" : " | " + page->script_error()));
            return m;
        }
    }
    {
        // AND ONE FROM `src`, which is how every real page carries a module.
        auto page =
            page_of(R"(<html><body><script type="module" src="./entry.js"></script></body></html>)",
                    {{"./entry.js", "import { v } from './dep.js'; globalThis.__src = v;"},
                     {"./dep.js", "export const v = 'from-src';"}});
        const std::string sourced = ask(*page, "String(globalThis.__src)");
        if (sourced != "from-src") {
            m.fail_at(rung_page,
                      "<script type=module src> did not run (wanted from-src got " + sourced + ")" +
                          (page->script_error().empty() ? "" : " | " + page->script_error()));
            return m;
        }
    }
    m.reached(rung_page);

    // --- 7: relative specifiers resolve against the IMPORTER ----------------
    //
    // `./dep.js` inside `sub/a.js` means `sub/dep.js`, not `dep.js`. Using the
    // specifier as written happens to work when everything sits in one
    // directory, which is exactly why this needs measuring: it passes rungs 3
    // to 6 and fails on the first library laid out in folders.
    {
        auto page =
            page_of(R"(<html><body><script type="module" src="./sub/a.js"></script></body></html>)",
                    {{"./sub/a.js", "import { deep } from './b.js';"
                                    "import { up } from '../top.js';"
                                    "globalThis.__rel = deep + up;"},
                     {"./sub/b.js", "export const deep = 'deep';"},
                     {"./top.js", "export const up = '-up';"},
                     // THE DECOYS: if `./b.js` resolves against the PAGE rather than
                     // against `sub/a.js`, it finds these instead and the rung passes
                     // for the wrong reason.
                     {"./b.js", "export const deep = 'WRONG';"},
                     {"./sub/top.js", "export const up = '-WRONG';"}});
        const std::string resolved = ask(*page, "String(globalThis.__rel)");
        if (resolved != "deep-up") {
            m.fail_at(rung_relative,
                      "a relative specifier did not resolve against the importer (wanted deep-up "
                      "got " +
                          resolved + ")" +
                          (page->script_error().empty() ? "" : " | " + page->script_error()));
            return m;
        }
    }
    m.reached(rung_relative);

    // --- 8: dynamic import() ------------------------------------------------
    //
    // THE ONE BABYLON NEEDS. It returns a PROMISE for the module's namespace
    // object, so this measures three things at once: that the call works, that
    // what it resolves to is a namespace, and that the promise settles.
    {
        auto page =
            page_of(R"(<html><body><script type="module" src="./go.js"></script></body></html>)",
                    {{"./go.js", "import('./lazy.js').then(function (ns) {"
                                 "  globalThis.__dyn = ns.value + ',' + typeof ns;"
                                 "});"},
                     {"./lazy.js", "export const value = 'lazy';"}});
        page->run_script("0"); // drain whatever the load queued
        const std::string dynamic = ask(*page, "String(globalThis.__dyn)");
        if (dynamic != "lazy,object") {
            m.fail_at(rung_dynamic,
                      "dynamic import() did not resolve to a namespace (wanted lazy,object got " +
                          dynamic + ")" +
                          (page->script_error().empty() ? "" : " | " + page->script_error()));
            return m;
        }
    }
    m.reached(rung_dynamic);

    m.fail_at(rung_babylon, "no ES Babylon is vendored to boot - " + babylon_es_verdict());
    return m;
}

// DOES BABYLON'S ES BUILD EVEN EXIST HERE? Reported beside the ladder rather
// than inside it, the way the WebGL 2 ratchet reports Babylon's version: the
// ladder stops at its first failure, and whether the corpus is present at all
// is a different question from how far the engine gets.
[[nodiscard]] std::string babylon_es_verdict() {
    const std::string umd = read_file("vendor/babylon/babylon.js");
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
    const std::string record = read_file("tests/corpus/modules/module-ratchet.txt");
    if (record.empty()) {
        std::printf("     (no tests/corpus/modules/module-ratchet.txt yet - run "
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
