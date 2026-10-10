//===- ctbrowser/unittests/unit/aot/basics.cpp - AOT dispatch basics ------===//
//
// The runtime calls a hand-authored compiled function.
//
// The AOT helper ABI (include/ctbrowser/aot/aot_helpers.def and
// aot/helpers/*.def) declares the helpers that compiled code calls into the
// runtime. This test checks that the runtime actually dispatches to a
// compiled body and that the helpers work through their declared signatures.
// A function is compiled by the engine's own compiler, its proto is stamped
// with a hand-written native body, and it is called from ordinary
// interpreted JavaScript. The native body uses ct_aot_enter,
// ct_aot_return_value, ct_aot_leave and ct_aot_check.
//
// Both runs return 42, so a run that quietly took the interpreter would look
// exactly like one that took the native body. The native body counts its own
// calls, and the test asserts that counter.
//
//===----------------------------------------------------------------------===//

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

/// How many times the compiled body ran. The interpreter produces the same 42,
/// so this is the only evidence that the compiled body was used.
std::size_t native_calls = 0;

/// A hand-authored compiled body for `function addup(a, b) { return a + b; }`.
///
/// This is what a backend would emit, written by hand: enter a frame, do the
/// work, hand the result through ct_aot_return_value, leave. It is not a
/// general `+`: it adds two numbers and nothing else, which is all this sample
/// needs. A real backend would call the generic add helper.
extern "C" std::int32_t sample_addup(ctbrowser::aot::ct_aot_ctx * ctx,
                                     const ctbrowser::aot::ct_aot_site * site,
                                     const std::uint64_t * argv, std::uint32_t argc,
                                     std::uint64_t receiver, std::uint32_t constructing,
                                     std::uint64_t * out) {
    ++native_calls;

    // Read the arguments before ct_aot_enter, as aot_entry.h requires: enter
    // resizes `registers_` and may reallocate it, so `argv` is dangling the
    // moment it returns.
    const value first = argc > 0 ? value::from_bits(argv[0]) : value::undefined();
    const value second = argc > 1 ? value::from_bits(argv[1]) : value::undefined();

    // A body sizes its own frame storage with the number the ABI publishes.
    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    ctbrowser::aot::ct_aot_frame * frame =
        ctbrowser::aot::ct_aot_enter(ctx, site, /*reg_count*/ 4u, receiver, storage);
    if (frame == nullptr) {
        // ct_aot_enter's documented failure: it returns NULL when the call
        // depth limit raises, and the caller returns `failed` without leaving.
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed);
    }

    const value sum = value::number(first.as_number() + second.as_number());

    // Ask ct_aot_check for the status rather than assume it: every other status
    // is defined against it, and a body that skipped it could not notice an
    // unwind.
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

/// Runs the fixture, with `addup`'s proto stamped with sample_addup when
/// \p stamp is set, and returns `answer` (negative on failure). \p calls_after
/// receives the number of compiled-body calls.
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
    // The interpreter's answer, which the compiled body has to agree with. It
    // is taken first and from the same source text, so the test compares two
    // runs of one function rather than a run against a typed constant.
    std::size_t calls = 0;
    const double interpreted = run_with(false, calls);
    check(interpreted == 42.0, "the interpreted function returns 42");
    check(calls == 0, "and no compiled body ran - the blinded arm really is blind");

    const double compiled = run_with(true, calls);
    check(compiled == interpreted, "THE COMPILED BODY AGREES WITH THE INTERPRETER");
    // Both runs return 42, so only the call counter shows that the compiled
    // body ran.
    check(calls == 1, "AND IT ACTUALLY RAN - the runtime dispatched to it exactly once");

    // A second call goes through the compiled body too, which catches a
    // one-shot stamp or a frame the first call failed to release: if
    // ct_aot_leave did not restore `frames_` and `registers_`, the second entry
    // would build on a stack that never came back down.
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
