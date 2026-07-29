// How far does p5.js get?
//
// examples/assets/p5.js is p5.js v2.3.1 - 4.5 MB and 138,938 lines of modern
// JavaScript that nobody wrote for this engine. It is roughly 900x the largest
// script this tree runs today (examples/pages/pong.html, 5,018 bytes), and
// unlike an ordinary page nearly all of it must EXECUTE at load: the bundle is
// one IIFE that defines 1,452 things and then hands back `p5`.
//
// Getting there is a long program of work, so this test exists to make that
// program legible. It pushes the bundle up a LADDER - lex, parse, compile, fit
// the bytecode, run, produce p5 - and records the highest rung reached plus the
// blocker that stopped it, in tests/p5-ratchet.txt. The rules are a pawl:
//
//   measured < recorded   FAIL. Something regressed.
//   measured = recorded   pass, unless the blocker STRING changed - then FAIL,
//                         because the same rung with a different blocker means
//                         something moved sideways and that is worth hearing.
//   measured > recorded   pass, and say so. Advancing the file is a deliberate
//                         act (tools/p5-ratchet.py --advance): a test that
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
// the ast itself, not compile()'s error string. tests/CMakeLists.txt puts the
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
    // 12 the render byte-matches a golden
    level_ceiling = level_draws,
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

[[nodiscard]] measurement measure(const std::string & source) {
    measurement m;
    stopwatch clock;

    if (source.empty()) {
        m.fail_at(level_read, "examples/assets/p5.js is missing or empty");
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
    // So this is the `p5-min` rung. Running the full build - fetch, FES and all
    // - is a later one, and it is a different question from whether the engine
    // can run p5 at all. A test that reaches the network fails for reasons that
    // have nothing to do with this code, which is why CTBROWSER_NETWORK=0
    // exists everywhere else in this tree.
    page.load_html(R"(<html><head><script>var IS_MINIFIED = true;</script>)"
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
            s.draw = function () { __ran += 'draw;'; };
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
    return m;
}

// --- the recorded high-water mark ------------------------------------------

struct recorded {
    int level = level_unread;
    std::string blocker;
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
        if (key == "budget-ms") { r.budget_ms = std::atof(val.c_str()); }
    }
    return r;
}

} // namespace

int main(int argc, char ** argv) {
    // With a path, measure THAT and report - no pawl, no recorded file. This is
    // what tools/p5-ratchet.py --bisect drives: one rollup module carved out of
    // the bundle is a 3,000-line reproducer instead of a needle in 4.5 MB.
    // `--sketch FILE`: load p5 as a page, then run FILE against it and print
    // whatever it alerts. The ladder answers one question; this answers any of
    // them, which is what an inner loop needs.
    if (argc > 2 && std::string{argv[1]} == "--sketch") {
        const std::string bundle = read_file("examples/assets/p5.js");
        ctbrowser::shell::browser page{ctbrowser::shell::browser_options{400, 400}};
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
        for (int frame = 0; frame < 5; ++frame) { page.tick(16); }
        std::printf("ran=%d error=%s\n", int(ok), page.script_error().c_str());
        return 0;
    }

    const bool bisecting = argc > 1;
    const std::string path = bisecting ? argv[1] : "examples/assets/p5.js";

    const std::string source = read_file(path);
    std::printf("     %s: %zu bytes\n", path.c_str(), source.size());

    const measurement m = measure(source);

    if (bisecting) {
        std::printf("\n     LEVEL %d/%d (%s)\n", m.level, level_ceiling, level_name(m.level));
        if (!m.blocker.empty()) { std::printf("     BLOCKER %s\n", m.blocker.c_str()); }
        if (!m.trace.empty()) { std::printf("%s\n", m.trace.c_str()); }
        return 0; // a reproducer reports; it does not judge
    }

    const recorded r = read_ratchet("tests/p5-ratchet.txt");

    std::printf("\n     LEVEL %d/%d (%s)\n", m.level, level_ceiling, level_name(m.level));
    if (!m.blocker.empty()) { std::printf("     BLOCKER %s\n", m.blocker.c_str()); }
    if (!m.trace.empty()) { std::printf("%s\n", m.trace.c_str()); }
    std::printf("\n");

    if (!r.found) {
        std::printf("FAIL tests/p5-ratchet.txt is missing - it is the recorded high-water mark\n");
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
        std::printf("     ADVANCE: level %d -> %d (%s). Run tools/p5-ratchet.py --advance\n",
                    r.level, m.level, level_name(m.level));
    } else if (m.blocker != r.blocker) {
        std::printf("FAIL same level %d, different blocker\n", m.level);
        std::printf("     now:      %s\n", m.blocker.c_str());
        std::printf("     recorded: %s\n", r.blocker.c_str());
        std::printf("     If this is progress, run tools/p5-ratchet.py --advance\n");
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
