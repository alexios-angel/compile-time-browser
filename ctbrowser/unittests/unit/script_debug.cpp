// THE DEBUG SIDE TABLES: local names, their registers and live ranges, and one
// source offset per instruction.
//
// Phase 44 of the lexical-backend plan. `function_proto` carried names for
// globals and properties and nothing at all for locals, and `source_begin` /
// `source_end` were per FUNCTION - so every question a backend, a stack trace
// or a debugger wants to ask ("which variable is `r4`?", "which line is this
// instruction?") had no answer, and the AOT backend's C++ said `v18[4] = v17`
// where the source said `this._isEnabled = ...`.
//
// WHAT IS ASSERTED HERE, and why each one is not obvious:
//
//   - the NAMES and the REGISTERS, which is the plan's own gate. It is a real
//     gate because `frame::locals` is a stack the compiler POPS at every scope
//     exit: the plan proposed filling the table in `finish_frame`, and by then
//     there is nothing left in it but whatever the outermost scope still holds.
//     A block-scoped `let` would have been silently absent.
//   - the LIVE RANGE, because a table recording every local as live for the
//     whole function is the same table with the useful part removed.
//   - the OFFSETS being PARALLEL to the code, exactly, and pointing at the
//     right text. An off-by-one here is a debugger that steps to the wrong
//     line, which is worse than no debugger.
//   - the ROUND TRIP through a program image, because a serialization that
//     drops the tables is indistinguishable from a build with the option off.
//
// AND THE MEASUREMENT. `CTBROWSER_DEBUG_TABLE_COST=1 ctbrowser-test-script_debug`
// compiles the three corpora in ctbrowser/vendor/ and reports what the tables
// cost in a program image. It is off by default because reading 13 MB of
// JavaScript does not belong in a unit test; it is HERE rather than in a
// throwaway script because the number is one the next person will want to
// re-take.
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <ctbrowser/script/source_lines.hpp>

#include "check.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::script::compiler;
using ctbrowser::script::debug_names_enabled;
using ctbrowser::script::function_proto;
using ctbrowser::script::image_option;
using ctbrowser::script::line_table;
using ctbrowser::script::load_image;
using ctbrowser::script::local_desc;
using ctbrowser::script::program;
using ctbrowser::script::write_image;

namespace {

// The proto whose `name` is `wanted`, or nullptr. Programs are flat: [0] is the
// top-level script and every function is a sibling of every other.
[[nodiscard]] const function_proto * function_named(const program & p, std::string_view wanted) {
    for (const function_proto & fn : p.functions) {
        if (fn.name == wanted) { return &fn; }
    }
    return nullptr;
}

[[nodiscard]] const local_desc * local_named(const function_proto & fn, std::string_view wanted) {
    for (const local_desc & d : fn.locals) {
        if (d.name == wanted) { return &d; }
    }
    return nullptr;
}

// THE PLAN'S OWN GATE, spelled exactly as it is written there:
// "a unit test compiles `function f(a){var n=1;return n+a;}` and asserts
// `locals` names `a` and `n` against the right registers."
void names_and_registers() {
    const program p = compiler::compile("function f(a){var n=1;return n+a;}");
    CHECK(p.ok);
    const function_proto * f = function_named(p, "f");
    CHECK(f != nullptr);
    if (f == nullptr) { return; }

    const local_desc * a = local_named(*f, "a");
    const local_desc * n = local_named(*f, "n");
    CHECK(a != nullptr);
    CHECK(n != nullptr);
    if (a == nullptr || n == nullptr) { return; }

    // THE PARAMETER TAKES REGISTER ZERO because parameters are allocated first
    // - `compile_parameter_prologue`'s comment says the argument values arrive
    // in the registers just past the declared parameters, which only works
    // because the parameters are the low ones.
    CHECK_EQ(a->reg, 0u);
    CHECK_EQ(n->reg, 1u);
    CHECK(!a->boxed);
    CHECK(!n->boxed);

    // AND THEY ARE LIVE TO THE END. Nothing in this body opens a block, so the
    // only scope is the function's own and both ranges close at `finish_frame`.
    CHECK_EQ(a->first_pc, 0u);
    CHECK_EQ(a->last_pc, static_cast<std::uint32_t>(f->code.size()));
    CHECK(n->first_pc <= n->last_pc);
    CHECK_EQ(n->last_pc, static_cast<std::uint32_t>(f->code.size()));
}

// A `let` INSIDE A BLOCK, which is the case that made `finish_frame` the wrong
// place to build this table: by the time a function is finished, `pop_scope`
// has already removed `inner` from the compiler's own list.
void a_block_scoped_local_survives() {
    const program p = compiler::compile("function g(){ { let inner = 1; inner; } return 2; }");
    CHECK(p.ok);
    const function_proto * g = function_named(p, "g");
    CHECK(g != nullptr);
    if (g == nullptr) { return; }

    const local_desc * inner = local_named(*g, "inner");
    CHECK(inner != nullptr);
    if (inner == nullptr) { return; }

    // AND ITS RANGE IS A REAL RANGE THAT ENDS BEFORE THE FUNCTION DOES.
    //
    // BOTH HALVES, and the first one is not decoration. `last_pc < code.size()`
    // alone passes for a table that never closed the range at all, because an
    // unclosed one keeps the `last_pc == first_pc` it was born with and that is
    // less than the code size too. Falsified: commenting out the `close_local`
    // call in `shrink_locals` left this test GREEN until the second assertion
    // was added, and red afterwards.
    CHECK(inner->last_pc > inner->first_pc);
    CHECK(inner->last_pc < g->code.size());
}

// A CAPTURED LOCAL IS BOXED, and it is discovered AFTER the declaration - so a
// table that copied `boxed` at `add_local` and never looked again would say
// `false` here.
void a_captured_local_is_boxed() {
    const program p =
        compiler::compile("function h(){ let c = 0; return function(){ return c; }; }");
    CHECK(p.ok);
    const function_proto * h = function_named(p, "h");
    CHECK(h != nullptr);
    if (h == nullptr) { return; }
    const local_desc * c = local_named(*h, "c");
    CHECK(c != nullptr);
    if (c != nullptr) { CHECK(c->boxed); }
}

// PER-INSTRUCTION OFFSETS: parallel to the code, and pointing at the right
// text. The source is laid out so the line of the `return` is known by
// counting, not by trusting the compiler.
void per_instruction_offsets() {
    const std::string source = "var q = 1;\n"     // line 1
                               "function k() {\n" // line 2
                               "  var z = 2;\n"   // line 3
                               "  return z;\n"    // line 4
                               "}\n";             // line 5
    const program p = compiler::compile(source);
    CHECK(p.ok);
    const function_proto * k = function_named(p, "k");
    CHECK(k != nullptr);
    if (k == nullptr) { return; }

    // EXACTLY PARALLEL. Not "roughly the same length": a reader indexes
    // `code_offsets[ip]` and an off-by-one attributes every instruction after
    // the first to the wrong place.
    CHECK_EQ(k->code_offsets.size(), k->code.size());
    for (const std::uint32_t offset : k->code_offsets) { CHECK(offset <= source.size()); }

    const line_table lines{source};
    // SIX, NOT FIVE. The text ends with a newline, and the offset just past it
    // is on a line of its own - the empty one an editor shows at the bottom of
    // a file that ends properly. `wc -l` says five because it counts newlines;
    // a line TABLE has to be able to answer for `source.size()`.
    CHECK_EQ(lines.line_count(), 6u);
    CHECK_EQ(lines.line_of(0), 1u);
    CHECK_EQ(lines.column_of(0), 1u);
    CHECK_EQ(lines.line_of(static_cast<std::uint32_t>(source.find("return z"))), 4u);
    CHECK_EQ(lines.column_of(static_cast<std::uint32_t>(source.find("return z"))), 3u);

    // AND THE BODY IS ATTRIBUTED TO THE BODY'S LINES. Every instruction in `k`
    // has to come from lines 2 through 5; an instruction attributed to line 1
    // would mean the cursor was never moved off whatever preceded it.
    bool reached_the_return = false;
    for (const std::uint32_t offset : k->code_offsets) {
        const std::uint32_t line = lines.line_of(offset);
        CHECK(line >= 2u && line <= 5u);
        if (line == 4u) { reached_the_return = true; }
    }
    CHECK(reached_the_return);
}

// THE TABLES SURVIVE A PROGRAM IMAGE. Not a formality: the image is the shape
// the AOT compiler's second entry point reads, and a round trip that quietly
// drops the tables looks exactly like a build with the option off.
void the_image_round_trips_them() {
    const program before = compiler::compile("function f(a){var n=1;return n+a;}");
    CHECK(before.ok);
    const std::vector<std::byte> bytes = write_image(before, image_option::keep_source);
    CHECK(!bytes.empty());
    if (bytes.empty()) { return; }

    const auto loaded = load_image(bytes);
    CHECK(loaded.ok);
    if (!loaded.ok) {
        std::printf("  image: %s\n", loaded.error.c_str());
        return;
    }
    const function_proto * f = function_named(loaded.value, "f");
    CHECK(f != nullptr);
    if (f == nullptr) { return; }
    const local_desc * n = local_named(*f, "n");
    CHECK(n != nullptr);
    if (n != nullptr) { CHECK_EQ(n->reg, 1u); }
    CHECK_EQ(f->code_offsets.size(), f->code.size());

    // AND AN IMAGE WITHOUT SOURCE KEEPS THE NAMES AND DROPS THE OFFSETS. Both
    // halves are asserted: the offsets are the expensive part and are useless
    // without the text, and the names are the cheap part and are the whole
    // reason the table exists.
    const std::vector<std::byte> lean = write_image(before, image_option::drop_source);
    CHECK(!lean.empty() && lean.size() < bytes.size());
    const auto loaded_lean = load_image(lean);
    CHECK(loaded_lean.ok);
    if (!loaded_lean.ok) { return; }
    const function_proto * lean_f = function_named(loaded_lean.value, "f");
    CHECK(lean_f != nullptr);
    if (lean_f == nullptr) { return; }
    CHECK(local_named(*lean_f, "n") != nullptr);
    CHECK(lean_f->code_offsets.empty());
}

// --- the measurement ------------------------------------------------------

[[nodiscard]] std::string read_file(const char * path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

// What the tables cost in an image, for one corpus. Reported rather than
// asserted: a size is a fact about the corpus, and a ratchet on it would fail
// the day somebody upgrades p5.
void report_cost(const char * name, const char * path) {
    const std::string source = read_file(path);
    if (source.empty()) {
        std::printf("  %-10s (missing: %s)\n", name, path);
        return;
    }
    program p = compiler::compile(source);
    if (!p.ok) {
        std::printf("  %-10s DID NOT COMPILE: %s\n", name, p.error.c_str());
        return;
    }

    std::size_t locals = 0;
    std::size_t offsets = 0;
    for (const function_proto & fn : p.functions) {
        locals += fn.locals.size();
        offsets += fn.code_offsets.size();
    }

    const std::size_t with = write_image(p, image_option::keep_source).size();
    const std::size_t with_no_source = write_image(p, image_option::drop_source).size();
    // THE SAME PROGRAM WITH THE TABLES REMOVED, which is what a build with
    // CTBROWSER_SCRIPT_DEBUG_NAMES=OFF produces - measured by emptying them
    // rather than by rebuilding, so both numbers come from one run.
    for (function_proto & fn : p.functions) {
        fn.locals.clear();
        fn.code_offsets.clear();
    }
    const std::size_t without = write_image(p, image_option::keep_source).size();
    const std::size_t without_no_source = write_image(p, image_option::drop_source).size();

    const auto kb = [](std::size_t bytes) { return static_cast<double>(bytes) / 1024.0; };
    std::printf("  %-10s protos %6zu  locals %7zu  offsets %8zu\n", name, p.functions.size(),
                locals, offsets);
    std::printf("             image  %8.0f KB -> %8.0f KB  (+%.1f%%)   [source kept]\n",
                kb(without), kb(with),
                100.0 * (static_cast<double>(with) - static_cast<double>(without)) /
                    static_cast<double>(without));
    std::printf("             image  %8.0f KB -> %8.0f KB  (+%.1f%%)   [source dropped]\n",
                kb(without_no_source), kb(with_no_source),
                100.0 *
                    (static_cast<double>(with_no_source) - static_cast<double>(without_no_source)) /
                    static_cast<double>(without_no_source));
}

} // namespace

int main() {
    if (!debug_names_enabled()) {
        // NOT A PASS AND NOT A FAILURE. The option is real, and a test that
        // silently reported ok in a build that cannot possibly satisfy it is
        // the kind of green that costs an afternoon.
        std::printf("ok script_debug (SKIPPED - built with CTBROWSER_SCRIPT_DEBUG_NAMES=OFF)\n");
        return 0;
    }

    names_and_registers();
    a_block_scoped_local_survives();
    a_captured_local_is_boxed();
    per_instruction_offsets();
    the_image_round_trips_them();

    if (const char * asked = std::getenv("CTBROWSER_DEBUG_TABLE_COST");
        asked != nullptr && asked[0] == '1') {
        std::printf("\nwhat the debug side tables cost in a program image:\n");
        report_cost("bootstrap", "vendor/bootstrap/bootstrap.bundle.js");
        report_cost("p5", "vendor/p5/p5.js");
        report_cost("phaser", "vendor/phaser/phaser.js");
        std::printf("\n");
    }

    REPORT("script_debug (local names, live ranges and per-instruction source offsets)");
}
