// ctbrowser.aot: the runtime calls a hand-authored compiled function.
//
// This is Phase 2's gate, and until now it had never been met. The AOT ABI has
// been specified since Phase 2 - 1,881 lines, 68 helpers, each row naming the
// runtime code it delegates to - and not one line of it had ever run. A
// contract nobody executes is prose, and everything downstream is written
// against it: 68 helper bodies and two code generators.
//
// So this test does the cheapest possible falsification. A function is compiled
// by the engine's own compiler, its proto is stamped with a hand-written native
// body, and it is called from ordinary interpreted JavaScript. The native body
// uses the real helpers - ct_aot_enter, ct_aot_return_value, ct_aot_leave and
// ct_aot_check - through the real signatures the table declares.
//
// THE ANSWER IS NOT THE EVIDENCE. Both arms return 42, so a run that quietly
// took the interpreter would look exactly like a run that took the native body.
// The native body counts its own calls and the test asserts that counter, which
// is the only thing that distinguishes them.
#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/vm.hpp>

#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <new>
#include <string>
#include <string_view>

using ctbrowser::script::context;
using ctbrowser::script::function_proto;
using ctbrowser::script::program;
using ctbrowser::script::value;

namespace {

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %.*s\n", static_cast<int>(what.size()), what.data());
        ++ctbrowser_test_failures;
    }
}

// HOW MANY TIMES THE COMPILED BODY ACTUALLY RAN. Without this the test proves
// nothing: the interpreter produces the same 42.
std::size_t native_calls = 0;

// A HAND-AUTHORED COMPILED BODY for `function addup(a, b) { return a + b; }`.
//
// This is what a backend would emit, written by hand: enter a frame, do the
// work, hand the result through ct_aot_return_value, leave. It is NOT a general
// `+` - it adds two numbers and nothing else, which is all the sample needs and
// all the four implemented rows can support. A real backend would call
// ct_aot_add for the general case, and that row has no body yet.
extern "C" std::int32_t sample_addup(ctbrowser::aot::ct_aot_ctx * ctx,
                                     const ctbrowser::aot::ct_aot_site * site,
                                     const std::uint64_t * argv, std::uint32_t argc,
                                     std::uint64_t receiver, std::uint32_t constructing,
                                     std::uint64_t * out) {
    ++native_calls;

    // THE ARGUMENTS ARE READ BEFORE ct_aot_enter, which the entry signature's
    // comment requires: enter resizes `registers_` and may reallocate it, so
    // `argv` is dangling the moment it returns.
    const value first = argc > 0 ? value::from_bits(argv[0]) : value::undefined();
    const value second = argc > 1 ? value::from_bits(argv[1]) : value::undefined();

    // A body sizes its own frame storage with the number the ABI publishes.
    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    ctbrowser::aot::ct_aot_frame * frame =
        ctbrowser::aot::ct_aot_enter(ctx, site, /*reg_count*/ 4u, storage);
    if (frame == nullptr) {
        // The row's stated failure: NULL on the depth raise, and the caller
        // returns FAILED without leaving.
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed);
    }

    const value sum = value::number(first.as_number() + second.as_number());

    // The status is asked for through the classifier rather than assumed - this
    // is the row every other status is defined against, and a body that skipped
    // it would be a body that cannot notice an unwind.
    const auto status =
        static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_check(frame));
    if (status != ctbrowser::aot::ct_aot_status::ok) {
        if (status != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_leave(frame);
        }
        return static_cast<std::int32_t>(status);
    }

    *out = ctbrowser::aot::ct_aot_return_value(sum.bits(), receiver, constructing);
    ctbrowser::aot::ct_aot_leave(frame);
    return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::ok);
}

constexpr std::string_view fixture = "function addup(a, b) { return a + b; }\n"
                                     "var answer = addup(41, 1);\n";

// Run the fixture, optionally with `addup`'s proto stamped, and report what
// `answer` came out as.
[[nodiscard]] double run_with(bool stamp, std::size_t & calls_after) {
    program compiled = ctbrowser::script::compiler::compile(fixture);
    if (!compiled.ok) { return -1.0; }
    if (stamp) {
        for (function_proto & fn : compiled.functions) {
            if (fn.name == "addup") { fn.aot_entry = &sample_addup; }
        }
    }
    context ctx;
    ctbrowser::script::install_builtins(ctx);
    native_calls = 0;
    const auto result = ctx.run(compiled);
    calls_after = native_calls;
    if (!result.ok) { return -2.0; }
    const value answer = ctx.global("answer");
    return answer.is_number() ? answer.as_number() : -3.0;
}

} // namespace

int main() {
    // THE INTERPRETER'S ANSWER, which is the thing the compiled body has to
    // agree with. It is taken first and from the same source text, so the test
    // compares two runs of one function rather than a run against a constant
    // someone typed.
    std::size_t calls = 0;
    const double interpreted = run_with(false, calls);
    check(interpreted == 42.0, "the interpreted function returns 42");
    check(calls == 0, "and no compiled body ran - the blinded arm really is blind");

    const double compiled = run_with(true, calls);
    check(compiled == interpreted, "THE COMPILED BODY AGREES WITH THE INTERPRETER");
    // THE COUNTER IS THE PROOF. Both arms return 42; only this says which code
    // produced it, and a dispatch that silently never fired would otherwise
    // look exactly like one that works.
    check(calls == 1, "AND IT ACTUALLY RAN - the runtime dispatched to it exactly once");

    // A SECOND CALL GOES THROUGH IT TOO, which catches a one-shot stamp or a
    // frame the first call failed to release: if ct_aot_leave did not restore
    // `frames_` and `registers_`, the second entry would build on a stack that
    // never came back down.
    {
        program twice =
            ctbrowser::script::compiler::compile("function addup(a, b) { return a + b; }\n"
                                                 "var answer = addup(20, 1) + addup(20, 1);\n");
        check(twice.ok, "the two-call fixture compiles");
        for (function_proto & fn : twice.functions) {
            if (fn.name == "addup") { fn.aot_entry = &sample_addup; }
        }
        context ctx;
        ctbrowser::script::install_builtins(ctx);
        native_calls = 0;
        const auto result = ctx.run(twice);
        check(result.ok, "and runs");
        const value answer = ctx.global("answer");
        check(answer.is_number() && answer.as_number() == 42.0, "two compiled calls make 42");
        check(native_calls == 2, "and the body ran twice, so the frame really was released");
    }

    if (ctbrowser_test_failures == 0) {
        std::printf("ok aot_basics (a hand-authored compiled body ran through the real ABI)\n");
    }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
