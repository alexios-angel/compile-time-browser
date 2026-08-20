// How far does p5.js get?
//
// vendor/p5/p5.js is p5.js v2.3.1 - 4.5 MB and 138,938 lines of modern
// JavaScript that nobody wrote for this engine. It is roughly 900x the largest
// script this tree runs today (examples/pages/pong.html, 5,018 bytes), and
// unlike an ordinary page nearly all of it must EXECUTE at load: the bundle is
// one IIFE that defines 1,452 things and then hands back `p5`.
//
// Getting there is a long program of work, so this test exists to make that
// program legible. It pushes the bundle up a LADDER - lex, parse, compile, fit
// the bytecode, run, produce p5 - and records the highest rung reached plus the
// blocker that stopped it, in test/corpus/p5/p5-ratchet.txt. The rules are a pawl:
//
//   measured < recorded   FAIL. Something regressed.
//   measured = recorded   pass, unless the blocker STRING changed - then FAIL,
//                         because the same rung with a different blocker means
//                         something moved sideways and that is worth hearing.
//   measured > recorded   pass, and say so. Advancing the file is a deliberate
//                         act (tools/corpus/p5-ratchet.py --advance): a test that
//                         edits its own expectations cannot fail.
//
// That is the same reasoning as page_scripts.cpp's must_stop_at_source, which
// asserts the blocker rather than merely printing it - a deferred feature and a
// mis-compile look identical until you write down which one you have.
//
// The ladder trace is printed IN FULL on every run, not just up to the failure,
// wherever a rung can be evaluated independently. The recorded number tracks
// one thing; a developer wants to see everything that is wrong at once.

#include <ctbrowser.hpp>

#include "check.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The parser is ctjs's, and the ratchet reports WHERE it stopped - which needs
// the ast itself, not compile()'s error string. test/CMakeLists.txt puts the
// brick's headers on every test's include path for the same kind of reason.
#include <ctjs/vparse.hpp>

namespace {

// --- the ladder ------------------------------------------------------------
//
// Rungs 8 and up need the DOM, a page and a frame loop; they arrive with the
// work that makes them reachable. Naming them here keeps the target visible.
enum : int {
    level_unread = 0,
    level_read = 1,       // the bundle is on disk and readable
    level_lexed = 2,      // it tokenises with nothing silently dropped
    level_parsed = 3,     // ctjs's parser accepts all of it
    level_compiled = 4,   // it becomes a program
    level_fits = 5,       // every operand that program emitted fits its field
    level_ran = 6,        // the top-level IIFE executes without a fault
    level_defines_p5 = 7, // and leaves a callable `p5` behind
    level_page = 8,       // it loads AS A PAGE, with a DOM under it
    level_constructs = 9, // `new p5(sketch)` builds a sketch
    level_setup = 10,     // and calls its setup()
    level_draws = 11,     // and keeps calling draw()
    level_paints = 12,    // and what draw() asked for is IN THE PIXELS
    level_ceiling = level_paints,
};

const char * level_name(int level) {
    switch (level) {
    case level_unread: return "unread";
    case level_read: return "read";
    case level_lexed: return "lexed";
    case level_parsed: return "parsed";
    case level_compiled: return "compiled";
    case level_fits: return "fits the bytecode";
    case level_ran: return "top level ran";
    case level_defines_p5: return "defines p5";
    case level_page: return "loads as a page";
    case level_constructs: return "constructs a sketch";
    case level_setup: return "runs setup";
    case level_draws: return "runs draw";
    case level_paints: return "paints what the sketch drew";
    default: return "?";
    }
}

struct measurement {
    int level = level_unread;
    std::string blocker;
    double lex_ms = 0, parse_ms = 0, compile_ms = 0, run_ms = 0;

    // The gate is the FIRST rung that failed; the trace keeps going as far as
    // it physically can. Those are different questions. Dropped `#` bytes do
    // not stop the parser, so reporting only them would hide the destructuring
    // that does - and a developer wants both in one run.
    bool stopped = false;
    // The stack a failure happened on is CONTEXT, not identity: it moves with
    // edits that have nothing to do with the blocker. So the recorded string is
    // the first line - the message - and the rest is printed but not compared.
    std::string trace;
    std::string said; // what the page alerted back, for the questions only it can answer
    void fail_at(int rung, std::string why) {
        if (stopped) { return; }
        level = rung - 1;
        const std::size_t newline = why.find('\n');
        if (newline == std::string::npos) {
            blocker = std::move(why);
        } else {
            blocker = why.substr(0, newline);
            trace = why.substr(newline + 1);
        }
        stopped = true;
    }
    void reached(int rung) {
        if (!stopped) { level = rung; }
    }
};

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// What a reported position is named after. --bisect pads its fragment with
// blank lines so the numbers stay p5.js's own, which is the only way a
// reproducer's diagnostic is still useful against the bundle it came from.
std::string position_label = "p5.js";

// A byte offset into the bundle, as the line:column a person can jump to.
[[nodiscard]] std::string where(const std::string & source, std::size_t offset) {
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < offset && i < source.size(); ++i) {
        if (source[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return position_label + ":" + std::to_string(line) + ":" + std::to_string(column);
}

class stopwatch {
public:
    [[nodiscard]] double lap() {
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - mark_).count();
        mark_ = now;
        return ms;
    }

private:
    std::chrono::steady_clock::time_point mark_ = std::chrono::steady_clock::now();
};

// Does everything the program emitted actually fit the operand fields it was
// emitted into?
//
// This is the rung with no precedent in the tree, and it earns its place: the
// caps are SILENT. add_name returns a uint16 that every emit site truncates to
// a uint8, so the 257th distinct property name in a function reads a different
// property, with no diagnostic anywhere. A bundle this size passes that mark in
// its first few hundred lines.
//
// Only the pools can be checked after the fact, because only they keep their
// real size - the vector genuinely holds 300 names, and it is the OPERAND that
// truncates. A frame that wanted 1,452 registers cannot be detected here at
// all: next_reg wrapped during compilation and frame_size stores the wrapped
// value, so the evidence is gone. That is why the compiler is made to say so
// itself, and why an over-cap function fails at `compiled` rather than here.
//
// The limits are read off the instruction type rather than written down, so
// this rung keeps meaning the same thing after the operands are widened.
[[nodiscard]] std::string first_overflow(const ctbrowser::script::program & prog) {
    using ctbrowser::script::instruction;
    constexpr auto narrow_max =
        static_cast<std::size_t>(std::numeric_limits<decltype(instruction::a)>::max());
    constexpr auto wide_max =
        static_cast<std::size_t>(std::numeric_limits<decltype(instruction{}.bx())>::max());
    // A jump displacement is signed, so a body must fit the positive half for
    // any jump inside it to be representable. Conservative and sound: no need
    // to know which opcodes branch.
    const std::size_t jump_max = wide_max / 2;

    for (std::size_t i = 0; i < prog.functions.size(); ++i) {
        const auto & fn = prog.functions[i];
        const std::string who = "function #" + std::to_string(i) + " (" +
                                (fn.name.empty() ? "<script>" : fn.name) + ")";
        if (fn.names.size() > narrow_max) {
            return who + " mentions " + std::to_string(fn.names.size()) +
                   " property names; the operand holds " + std::to_string(narrow_max);
        }
        if (fn.upvalues.size() > narrow_max) {
            return who + " captures " + std::to_string(fn.upvalues.size()) +
                   " variables; the operand holds " + std::to_string(narrow_max);
        }
        if (fn.strings.size() > wide_max) {
            return who + " holds " + std::to_string(fn.strings.size()) + " strings; the limit is " +
                   std::to_string(wide_max);
        }
        if (fn.constants.size() > wide_max) {
            return who + " holds " + std::to_string(fn.constants.size()) +
                   " constants; the limit is " + std::to_string(wide_max);
        }
        if (fn.nested.size() > wide_max) {
            return who + " nests " + std::to_string(fn.nested.size()) +
                   " functions; the limit is " + std::to_string(wide_max);
        }
        if (fn.code.size() > jump_max) {
            return who + " compiles to " + std::to_string(fn.code.size()) +
                   " instructions; a jump reaches " + std::to_string(jump_max);
        }
    }
    return {};
}

[[nodiscard]] measurement measure(const std::string & source, bool minified = true) {
    measurement m;
    stopwatch clock;

    if (source.empty()) {
        m.fail_at(level_read, "vendor/p5/p5.js is missing or empty");
        return m;
    }
    m.reached(level_read);

    // --- lex ---------------------------------------------------------------
    //
    // A byte matching no token is skipped rather than reported, which keeps the
    // lexer total but means `this.#count` silently becomes `this.count`. p5
    // declares 174 private fields, so this is not a corner case: it is 174
    // fields aliasing their public namesakes, with no error at any later stage.
    ctjs::vp::lex_report report;
    const std::vector<ctjs::vp::token> tokens = ctjs::vp::lex(source, &report);
    m.lex_ms = clock.lap();
    std::printf("     lex     %7.1f ms  %zu tokens\n", m.lex_ms, tokens.size());
    if (report.skipped != 0) {
        // Not fatal - the tokens still come out, just not the ones that were
        // written. So the trace goes on; only the gate stops here.
        const std::string why = std::to_string(report.skipped) +
                                " bytes match no token and are dropped; the first is at " +
                                where(source, report.first_skip);
        std::printf("     !! %s\n", why.c_str());
        m.fail_at(level_lexed, why);
    } else {
        m.reached(level_lexed);
    }

    // --- parse -------------------------------------------------------------
    const ctjs::vp::ast tree = ctjs::vp::parse(source);
    m.parse_ms = clock.lap();
    std::printf("     parse   %7.1f ms  %zu nodes\n", m.parse_ms, tree.nodes.size());
    if (!tree.ok) {
        const std::string why = "parse stops at " + where(source, tree.error_offset) +
                                ", expecting `" + std::string{tree.error} + "`";
        std::printf("     !! %s\n", why.c_str());
        m.fail_at(level_parsed, why);
        return m; // genuinely fatal: there is no tree to compile
    }
    m.reached(level_parsed);

    // --- compile -----------------------------------------------------------
    //
    // Declared before the context on purpose: a closure holds a
    // `const function_proto *` into the program, so the program must outlive
    // the context that ran it. Reverse destruction order is what guarantees it.
    const ctbrowser::script::program prog = ctbrowser::script::compiler::compile(source);
    m.compile_ms = clock.lap();
    std::printf("     compile %7.1f ms  %zu functions\n", m.compile_ms, prog.functions.size());
    if (!prog.ok) {
        std::printf("     !! %s\n", prog.error.c_str());
        m.fail_at(level_compiled, prog.error);
        return m; // fatal: there is no program to inspect or run
    }
    m.reached(level_compiled);

    // --- does it fit? ------------------------------------------------------
    const std::string overflow = first_overflow(prog);
    if (!overflow.empty()) {
        // Not fatal: the program runs, it just runs the wrong thing. Keep
        // going, because what it does next is worth seeing.
        std::printf("     !! %s\n", overflow.c_str());
        m.fail_at(level_fits, overflow);
    } else {
        m.reached(level_fits);
    }

    // --- run, WITH A DOM UNDER IT ------------------------------------------
    //
    // p5's top level is not self-contained: it reads `performance.now`, hangs
    // listeners off `window`, and looks for a `document` to put a canvas in.
    // A bare script::context has none of those, so running the bundle there
    // stops at the first host object and says nothing about the engine.
    //
    // So the run happens through the browser, as a page, with the bundle
    // delivered by `<script src>` the way a real page would get it. That also
    // means rung 8 is proved by rungs 6 and 7 rather than being a separate
    // exercise.
    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{400, 400}};
    page.assets().add(
        "p5.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(source.data()),
                               reinterpret_cast<const std::byte *>(source.data() + source.size())});
    // `IS_MINIFIED` IS A MODE, not a trick.
    //
    // p5.js branches on it in seven places, and it is what p5's OWN minified
    // build defines. Undefined, the bundle calls i18next's `initialize()` -
    // which fetches a translation catalogue from cdn.jsdelivr.net - and
    // installs the Friendly Error System, whose stack parsing and error
    // listeners are a large surface of their own. Defined, both collapse: the
    // translator promise becomes `Promise.resolve()` and the FES never
    // installs.
    //
    // So `minified` picks the RUNG. p5-min is the one the ladder is recorded
    // against; p5-full runs the same ladder with the flag left undefined, which
    // takes the bundle through the Friendly Error System and i18next's setup.
    // Neither reaches the network: the fetch is guarded, and a test that
    // reached out would fail for reasons that have nothing to do with this
    // code, which is why CTBROWSER_NETWORK=0 exists everywhere else here.
    const std::string prelude =
        minified ? R"(<script>var IS_MINIFIED = true;</script>)" : std::string{};
    page.load_html("<html><head>" + prelude +
                   R"(<script src="p5.js"></script></head><body></body></html>)");
    m.run_ms = clock.lap();
    std::printf("     page    %7.1f ms\n", m.run_ms);
    if (!page.script_error().empty()) {
        std::printf("     !! %s\n", page.script_error().c_str());
        m.fail_at(level_ran, page.script_error());
        return m;
    }
    m.reached(level_ran);

    // --- and is there a p5? ------------------------------------------------
    //
    // Asked from inside the page, because that is the only place its globals
    // live. run_script reports whether it RAN, so the answer comes back as an
    // alert - the one channel a page has that the host can read directly.
    page.set_alert_hook([&m](const std::string & said) { m.said = said; });
    (void)page.run_script("alert(typeof p5);");
    if (m.said != "function") {
        const std::string why = "`p5` is " +
                                (m.said.empty() ? std::string{"unreachable"} : m.said) +
                                ", not a function";
        std::printf("     !! %s\n", why.c_str());
        m.fail_at(level_defines_p5, why);
        return m;
    }
    m.reached(level_defines_p5);
    m.reached(level_page);

    // --- and does a SKETCH run? --------------------------------------------
    //
    // Instance mode rather than global: a sketch function gets its own p5, so
    // the test says exactly what it is asking for instead of relying on p5
    // having installed 200 names as globals.
    //
    // Progress is recorded in a global string rather than returned, because
    // setup and draw are called BY p5, from inside its own machinery - there is
    // nothing for the test to take a return value from.
    (void)page.run_script(R"(
        var __ran = '';
        var __err = '';
        try {
          new p5(function (s) {
            s.setup = function () { s.createCanvas(100, 100); __ran += 'setup;'; };
            s.draw = function () {
              // Two colours nothing else in the pipeline produces, so finding
              // them in the buffer cannot be a coincidence.
              s.background(10, 20, 30);
              s.noStroke();
              s.fill(200, 100, 50);
              s.rect(20, 20, 40, 40);
              __ran += 'draw;';
            };
          });
        } catch (e) { __err = '' + (e && e.message ? e.message : e); }
    )");
    if (!page.script_error().empty()) {
        std::printf("     !! %s\n", page.script_error().c_str());
        m.fail_at(level_constructs, page.script_error());
        return m;
    }
    page.set_alert_hook([&m](const std::string & said) { m.said = said; });
    (void)page.run_script("alert(__err || 'ok');");
    if (m.said != "ok") {
        const std::string why = "new p5(sketch) threw: " + m.said;
        std::printf("     !! %s\n", why.c_str());
        m.fail_at(level_constructs, why);
        return m;
    }
    m.reached(level_constructs);

    // A frame has to actually RUN for draw to be called: setup and draw are
    // driven by requestAnimationFrame, which is the page's own loop.
    for (int frame = 0; frame < 5; ++frame) { page.tick(16); }
    (void)page.run_script("alert(__ran);");
    if (m.said.find("setup;") == std::string::npos) {
        const std::string why = "setup() did not run (saw \"" + m.said + "\")";
        std::printf("     !! %s\n", why.c_str());
        m.fail_at(level_setup, why);
        return m;
    }
    m.reached(level_setup);
    if (m.said.find("draw;") == std::string::npos) {
        const std::string why = "draw() did not run (saw \"" + m.said + "\")";
        std::printf("     !! %s\n", why.c_str());
        m.fail_at(level_draws, why);
        return m;
    }
    m.reached(level_draws);

    // --- and does what it drew reach the PIXELS? -----------------------------
    //
    // A draw() that is called and paints nothing satisfies every rung above
    // this one. The two questions are genuinely different, and for a long
    // stretch the answer to them differed: p5 ran its whole draw loop while
    // `(220).toString(16)` returned "220", so every colour it computed was a
    // string the canvas could not read and every fill came out white.
    //
    // The canvas is found by walking the document for the element p5 made,
    // rather than by id: `defaultCanvas0` is p5's name for it and not a promise.
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
        const std::string why = "the sketch's canvas has no pixel buffer";
        std::printf("     !! %s\n", why.c_str());
        m.fail_at(level_paints, why);
        return m;
    }
    const ctbrowser::color background{pixels->at(5, 5)};
    const ctbrowser::color inside{pixels->at(40, 40)};
    if (background != ctbrowser::color::rgba(10, 20, 30)) {
        const std::string why = "background(10, 20, 30) did not reach the pixels (corner is " +
                                std::to_string(background.red()) + "," +
                                std::to_string(background.green()) + "," +
                                std::to_string(background.blue()) + ")";
        std::printf("     !! %s\n", why.c_str());
        m.fail_at(level_paints, why);
        return m;
    }
    if (inside != ctbrowser::color::rgba(200, 100, 50)) {
        const std::string why =
            "rect() did not reach the pixels (centre is " + std::to_string(inside.red()) + "," +
            std::to_string(inside.green()) + "," + std::to_string(inside.blue()) + ")";
        std::printf("     !! %s\n", why.c_str());
        m.fail_at(level_paints, why);
        return m;
    }
    m.reached(level_paints);
    return m;
}

// --- the recorded high-water mark ------------------------------------------

struct recorded {
    int level = level_unread;
    std::string blocker;
    // The SAME ladder with IS_MINIFIED left undefined, which takes the bundle
    // through the Friendly Error System and i18next's setup. A second number
    // rather than a second file: it is the same measurement of the same bundle,
    // and one of them being behind is exactly what wants to be visible.
    int full_level = level_unread;
    std::string full_blocker;
    double budget_ms = 0;
    bool found = false;
};

[[nodiscard]] recorded read_ratchet(const std::string & path) {
    recorded r;
    std::ifstream in{path};
    if (!in) { return r; }
    r.found = true;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') { continue; }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) { continue; }
        const std::string key = line.substr(0, equals);
        const std::string val = line.substr(equals + 1);
        if (key == "level") { r.level = std::atoi(val.c_str()); }
        if (key == "blocker") { r.blocker = val; }
        if (key == "full-level") { r.full_level = std::atoi(val.c_str()); }
        if (key == "full-blocker") { r.full_blocker = val; }
        if (key == "budget-ms") { r.budget_ms = std::atof(val.c_str()); }
    }
    return r;
}

} // namespace

int main(int argc, char ** argv) {
    // With a path, measure THAT and report - no pawl, no recorded file. This is
    // what tools/corpus/p5-ratchet.py --bisect drives: one rollup module carved out of
    // the bundle is a 3,000-line reproducer instead of a needle in 4.5 MB.
    // `--sketch FILE`: load p5 as a page, then run FILE against it and print
    // whatever it alerts. The ladder answers one question; this answers any of
    // them, which is what an inner loop needs.
    // `--source N...`: print the source of compiled function N.
    //
    // A stack trace names functions as `fn#3778`, because most of a bundle's
    // functions are anonymous and an index is the only thing that identifies
    // one. The index is useless without this: it is a position in a table
    // nothing outside the compiler can see. With it, a trace out of 4.5 MB of
    // minified-ish JavaScript points at the exact lines to read.
    if (argc > 2 && std::string{argv[1]} == "--source") {
        const std::string source = read_file("vendor/p5/p5.js");
        const ctbrowser::script::program prog = ctbrowser::script::compiler::compile(source);
        if (!prog.ok) {
            std::printf("compile failed: %s\n", prog.error.c_str());
            return 1;
        }
        for (int i = 2; i < argc; ++i) {
            const auto which = static_cast<std::size_t>(std::atoi(argv[i]));
            if (which >= prog.functions.size()) {
                std::printf("fn#%zu: out of range, %zu functions\n", which, prog.functions.size());
                continue;
            }
            const auto & fp = prog.functions[which];
            // The line as well as the offset: the offset locates it in the
            // program, the line locates it in the file you are about to open.
            const std::size_t line =
                1 + static_cast<std::size_t>(std::count(
                        source.begin(),
                        source.begin() + static_cast<std::ptrdiff_t>(fp.source_begin), '\n'));
            std::printf("\n=== fn#%zu `%s`  p5.js:%zu ===\n", which, fp.name.c_str(), line);
            std::printf("%s\n",
                        source.substr(fp.source_begin, fp.source_end - fp.source_begin).c_str());
        }
        return 0;
    }

    if (argc > 2 && std::string{argv[1]} == "--sketch") {
        const std::string bundle = read_file("vendor/p5/p5.js");
        ctbrowser::shell::browser page{ctbrowser::shell::browser_options{400, 400}};
        // A few assets a --sketch probe can load, so the loaders can be tried
        // without reaching the network.
        const auto bake = [&](const char * name, const char * text) {
            const std::string_view body{text};
            page.assets().add(name,
                              std::vector<std::byte>{
                                  reinterpret_cast<const std::byte *>(body.data()),
                                  reinterpret_cast<const std::byte *>(body.data() + body.size())});
        };
        bake("data.json", R"({"name":"probe","n":4})");
        bake("lines.txt", "one\ntwo\nthree");
        bake("table.csv", "a,b\n1,2\n3,4");
        page.assets().add("p5.js",
                          std::vector<std::byte>{
                              reinterpret_cast<const std::byte *>(bundle.data()),
                              reinterpret_cast<const std::byte *>(bundle.data() + bundle.size())});
        page.load_html(R"(<html><head><script>var IS_MINIFIED = true;</script>)"
                       R"(<script src="p5.js"></script></head><body></body></html>)");
        if (!page.script_error().empty()) {
            std::printf("load error: %s\n", page.script_error().c_str());
            return 1;
        }
        page.set_alert_hook(
            [](const std::string & said) { std::printf("alert: %s\n", said.c_str()); });
        const bool ok = page.run_script(read_file(argv[2]));
        // More frames than the ladder runs: a probe usually wants to see what
        // the loop settles into, not what it does on its first tick.
        for (int frame = 0; frame < 30; ++frame) { page.tick(16); }
        std::printf("ran=%d error=%s\n", int(ok), page.script_error().c_str());
        return 0;
    }

    const bool bisecting = argc > 1;
    const std::string path = bisecting ? argv[1] : "vendor/p5/p5.js";

    const std::string source = read_file(path);
    std::printf("     %s: %zu bytes\n", path.c_str(), source.size());

    const measurement m = measure(source);

    if (bisecting) {
        std::printf("\n     LEVEL %d/%d (%s)\n", m.level, level_ceiling, level_name(m.level));
        if (!m.blocker.empty()) { std::printf("     BLOCKER %s\n", m.blocker.c_str()); }
        if (!m.trace.empty()) { std::printf("%s\n", m.trace.c_str()); }
        return 0; // a reproducer reports; it does not judge
    }

    // AND AGAIN WITHOUT `IS_MINIFIED`. Same bundle, same ladder, the full
    // build - the error system and the translator setup included. Measured
    // second because it is the slower of the two and the one more likely to
    // stop early; measured at all because "p5 runs" is a different claim when
    // half of p5 is switched off.
    std::printf("\n     --- p5-full (IS_MINIFIED undefined) ---\n");
    const measurement full = measure(source, false);

    const recorded r = read_ratchet("test/corpus/p5/p5-ratchet.txt");

    std::printf("\n     LEVEL %d/%d (%s)\n", m.level, level_ceiling, level_name(m.level));
    if (!m.blocker.empty()) { std::printf("     BLOCKER %s\n", m.blocker.c_str()); }
    if (!m.trace.empty()) { std::printf("%s\n", m.trace.c_str()); }
    std::printf("     FULL LEVEL %d/%d (%s)\n", full.level, level_ceiling, level_name(full.level));
    if (!full.blocker.empty()) { std::printf("     FULL BLOCKER %s\n", full.blocker.c_str()); }
    std::printf("\n");

    if (!r.found) {
        std::printf("FAIL test/corpus/p5/p5-ratchet.txt is missing - it is the recorded "
                    "high-water mark\n");
        ++ctbrowser_test_failures;
        REPORT("p5_ratchet");
    }

    if (m.level < r.level) {
        std::printf("FAIL REGRESSED: level %d (%s), was %d (%s)\n", m.level, level_name(m.level),
                    r.level, level_name(r.level));
        std::printf("     now stops at: %s\n", m.blocker.c_str());
        std::printf("     was stopping at: %s\n", r.blocker.c_str());
        ++ctbrowser_test_failures;
    } else if (m.level > r.level) {
        // Not a failure - but not silent either, and not self-applied.
        std::printf("     ADVANCE: level %d -> %d (%s). Run tools/corpus/p5-ratchet.py --advance\n",
                    r.level, m.level, level_name(m.level));
    } else if (m.blocker != r.blocker) {
        std::printf("FAIL same level %d, different blocker\n", m.level);
        std::printf("     now:      %s\n", m.blocker.c_str());
        std::printf("     recorded: %s\n", r.blocker.c_str());
        std::printf("     If this is progress, run tools/corpus/p5-ratchet.py --advance\n");
        ++ctbrowser_test_failures;
    }

    // The full build gets its own pawl, on the same terms.
    if (full.level < r.full_level) {
        std::printf("FAIL p5-full REGRESSED: level %d (%s), was %d (%s)\n", full.level,
                    level_name(full.level), r.full_level, level_name(r.full_level));
        std::printf("     now stops at: %s\n", full.blocker.c_str());
        ++ctbrowser_test_failures;
    } else if (full.level > r.full_level) {
        std::printf("     ADVANCE: p5-full level %d -> %d (%s). Run tools/corpus/p5-ratchet.py "
                    "--advance\n",
                    r.full_level, full.level, level_name(full.level));
    } else if (full.blocker != r.full_blocker) {
        std::printf("FAIL p5-full at the same level %d with a different blocker\n", full.level);
        std::printf("     now:      %s\n", full.blocker.c_str());
        std::printf("     recorded: %s\n", r.full_blocker.c_str());
        ++ctbrowser_test_failures;
    }

    // The compiler has several linear scans that run per declaration and per
    // reference, so its cost is quadratic in exactly the dimension a 4.5 MB
    // bundle is large. Without a budget in the ratchet, the first run that
    // reaches `compiled` is a forty-minute test that then quietly rots.
    const double spent = m.lex_ms + m.parse_ms + m.compile_ms + m.run_ms;
    if (r.budget_ms > 0 && spent > r.budget_ms) {
        std::printf("FAIL %.0f ms to get to level %d; the budget is %.0f ms\n", spent, m.level,
                    r.budget_ms);
        ++ctbrowser_test_failures;
    }

    REPORT("p5_ratchet");
}
