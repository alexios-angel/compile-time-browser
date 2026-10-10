//===- ctbrowser/unittests/unit/aot/dispatch.cpp - Mixed-mode dispatch ----===//
//
// All six mixed-mode transitions, and the counter for each.
//
// Every call path between C++, the interpreter (VM) and compiled (AOT) bodies
// must dispatch to a compiled body when one exists. A path that does not still
// returns the right answer through the interpreter, so the failure only shows
// up as a slowdown under some callback (a timer, a promise job, a getter),
// long after the code that caused it ran.
//
// So the answers here prove nothing: every arm returns the same number whether
// it took the compiled body or the interpreter, and the only evidence that
// dispatch happened is the transition counter. That is the same discipline
// aot_basics uses for one transition, applied to all six transitions:
//
//   C++ -> VM    C++ -> AOT    VM -> AOT    AOT -> VM    AOT -> AOT    AOT -> C++
//
// The three with an AOT source go through ct_aot_call, the ABI's own call
// helper, rather than reaching around it into the runtime: a compiled body
// that called context::call directly would be testing C++, not the contract
// the code generators emit against.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/dispatch.hpp>
#include <ctbrowser/script/vm.hpp>

#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <new>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::script::context;
using ctbrowser::script::function_proto;
using ctbrowser::script::program;
using ctbrowser::script::transition;
using ctbrowser::script::transition_name;
using ctbrowser::script::transitions;
using ctbrowser::script::value;

namespace {

std::size_t leaf_calls = 0;
std::size_t caller_calls = 0;
std::size_t native_calls = 0;
std::size_t point_calls = 0;
std::uint32_t saw_constructing = 0;

/// A compiled `function leaf(a, b) { return a + b; }`, as a backend would emit
/// it: enter, work, hand the result through ct_aot_return_value, leave.
extern "C" std::int32_t sample_leaf(ctbrowser::aot::ct_aot_ctx * ctx,
                                    const ctbrowser::aot::ct_aot_site * site,
                                    const std::uint64_t * argv, std::uint32_t argc,
                                    std::uint64_t receiver, std::uint32_t constructing,
                                    std::uint64_t * out) {
    ++leaf_calls;
    const value first = argc > 0 ? value::from_bits(argv[0]) : value::undefined();
    const value second = argc > 1 ? value::from_bits(argv[1]) : value::undefined();

    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    ctbrowser::aot::ct_aot_frame * frame =
        ctbrowser::aot::ct_aot_enter(ctx, site, 4u, receiver, storage);
    if (frame == nullptr) {
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed);
    }

    const value sum = value::number(first.as_number() + second.as_number());
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

/// A compiled `function caller(f, a, b) { return f(a, b); }`. Whatever it is
/// handed (an interpreted function, another compiled one, or a native
/// built-in), it calls through ct_aot_call and the dispatcher decides. Three of
/// the six transitions are this function being handed three different things.
extern "C" std::int32_t sample_caller(ctbrowser::aot::ct_aot_ctx * ctx,
                                      const ctbrowser::aot::ct_aot_site * site,
                                      const std::uint64_t * argv, std::uint32_t argc,
                                      std::uint64_t receiver, std::uint32_t constructing,
                                      std::uint64_t * out) {
    ++caller_calls;
    // Read argv before ct_aot_enter: enter resizes the register file and may
    // reallocate it, which leaves argv dangling (see aot_entry.h).
    const std::uint64_t callee = argc > 0 ? argv[0] : value::undefined().bits();
    std::vector<std::uint64_t> forwarded;
    for (std::uint32_t i = 1; i < argc; ++i) { forwarded.push_back(argv[i]); }

    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    ctbrowser::aot::ct_aot_frame * frame =
        ctbrowser::aot::ct_aot_enter(ctx, site, 4u, receiver, storage);
    if (frame == nullptr) {
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed);
    }

    std::uint64_t produced = value::undefined().bits();
    const auto called = static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_call(
        frame, callee, value::undefined().bits(), forwarded.data(),
        static_cast<std::uint32_t>(forwarded.size()), 0u, site, &produced));
    if (called != ctbrowser::aot::ct_aot_status::ok) {
        if (called != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_leave(frame);
        }
        return static_cast<std::int32_t>(called);
    }

    *out = ctbrowser::aot::ct_aot_return_value(produced, receiver, constructing);
    ctbrowser::aot::ct_aot_leave(frame);
    return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::ok);
}

/// A compiled `function Point(x, y)`. It returns a number on purpose: only
/// ct_aot_return_value with `constructing` set makes `new` yield the instance
/// instead of the sum. `constructing` is what makes a constructor with no
/// explicit return evaluate to its receiver, and the ABI hands that decision to
/// ct_aot_return_value.
extern "C" std::int32_t sample_point(ctbrowser::aot::ct_aot_ctx * ctx,
                                     const ctbrowser::aot::ct_aot_site * site,
                                     const std::uint64_t * argv, std::uint32_t argc,
                                     std::uint64_t receiver, std::uint32_t constructing,
                                     std::uint64_t * out) {
    ++point_calls;
    const value first = argc > 0 ? value::from_bits(argv[0]) : value::undefined();
    const value second = argc > 1 ? value::from_bits(argv[1]) : value::undefined();
    saw_constructing = constructing;

    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    ctbrowser::aot::ct_aot_frame * frame =
        ctbrowser::aot::ct_aot_enter(ctx, site, 4u, receiver, storage);
    if (frame == nullptr) {
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed);
    }

    const value total = value::number(first.as_number() + second.as_number());
    const auto status =
        static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_check(frame));
    if (status != ctbrowser::aot::ct_aot_status::ok) {
        if (status != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_leave(frame);
        }
        return static_cast<std::int32_t>(status);
    }
    *out = ctbrowser::aot::ct_aot_return_value(total.bits(), receiver, constructing);
    ctbrowser::aot::ct_aot_leave(frame);
    return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::ok);
}

constexpr std::string_view fixture = "function leaf(a, b) { return a + b; }\n"
                                     "function caller(f, a, b) { return f(a, b); }\n"
                                     "function timesTwo(a, b) { return (a + b) * 2; }\n"
                                     "function Point(x, y) { this.x = x; this.y = y; }\n"
                                     "var fromScript = leaf(20, 22);\n"
                                     "var built = new Point(3, 4);\n";

/// One arm: the transition it is supposed to cross and the counter before it.
struct arm {
    transition crossing;
    std::uint64_t before = 0;
};

} // namespace

int main() {
    program compiled = ctbrowser::script::compiler::compile(fixture);
    check(compiled.ok, "the fixture compiles");
    if (!compiled.ok) { return 1; }

    for (function_proto & fn : compiled.functions) {
        if (fn.name == "leaf") { fn.aot_entry = &sample_leaf; }
        if (fn.name == "caller") { fn.aot_entry = &sample_caller; }
        if (fn.name == "Point") { fn.aot_entry = &sample_point; }
    }

    context ctx;
    ctbrowser::script::install_builtins(ctx);
    ctx.define_native("nativeAdd", [](context &, std::span<value> args) {
        ++native_calls;
        const double a = args.size() > 0 ? context::to_number(args[0]) : 0;
        const double b = args.size() > 1 ? context::to_number(args[1]) : 0;
        return value::number(a + b);
    });

    // ---- VM -> AOT, and C++ -> VM ----------------------------------------
    //
    // Running the program is itself a C++ entry into the interpreter, and the
    // `leaf(20, 22)` inside it is the interpreter reaching a compiled body.
    ctbrowser::script::reset_transitions();
    const auto ran = ctx.run(compiled);
    check(ran.ok, "the program runs");
    check(ctx.global("fromScript").as_number() == 42.0, "and computes 42 at its top level");
    check(transitions(transition::cxx_to_vm) >= 1, "running a program is C++ reaching the VM");
    // Two crossings: `leaf(20, 22)` and `new Point(3, 4)`. Both are the
    // interpreter reaching a compiled body, through two different opcodes
    // (op::call and op::construct) that must each check for one.
    check(transitions(transition::vm_to_aot) == 2,
          "AND THE SCRIPT REACHED BOTH COMPILED BODIES - two VM -> AOT crossings");
    check(leaf_calls == 1, "which is also what the body's own counter says");

    const value leaf = ctx.global("leaf");
    const value caller = ctx.global("caller");
    const value times_two = ctx.global("timesTwo");
    const value native_add = ctx.global("nativeAdd");
    check(leaf.is_callable() && caller.is_callable() && times_two.is_callable() &&
              native_add.is_callable(),
          "all four callables are reachable as globals");

    // ---- C++ -> AOT -------------------------------------------------------
    //
    // Every entry from C++ (a DOM event, a timer, a promise job) must check for
    // a compiled body instead of always pushing an interpreter frame.
    {
        ctbrowser::script::reset_transitions();
        leaf_calls = 0;
        const value args[2] = {value::number(41), value::number(1)};
        const value out = ctx.call(leaf, args, value::undefined());
        check(out.is_number() && out.as_number() == 42.0,
              "C++ calling a compiled function gets 42");
        check(transitions(transition::cxx_to_aot) == 1, "and it crossed C++ -> AOT exactly once");
        check(transitions(transition::cxx_to_vm) == 0, "without touching the interpreter at all");
        check(leaf_calls == 1, "the body ran");
    }

    // ---- C++ -> VM, on its own -------------------------------------------
    {
        ctbrowser::script::reset_transitions();
        const value args[2] = {value::number(3), value::number(4)};
        const value out = ctx.call(times_two, args, value::undefined());
        check(out.is_number() && out.as_number() == 14.0, "an interpreted function still works");
        check(transitions(transition::cxx_to_vm) == 1, "counted as C++ -> VM");
        check(transitions(transition::cxx_to_aot) == 0, "and not as anything else");
    }

    // ---- AOT -> VM --------------------------------------------------------
    {
        ctbrowser::script::reset_transitions();
        caller_calls = 0;
        const value args[3] = {times_two, value::number(3), value::number(4)};
        const value out = ctx.call(caller, args, value::undefined());
        check(out.is_number() && out.as_number() == 14.0,
              "a compiled body calling an interpreted function gets its answer");
        check(caller_calls == 1, "the compiled caller ran");
        check(transitions(transition::cxx_to_aot) == 1, "C++ reached the compiled caller");
        check(transitions(transition::aot_to_vm) == 1, "AND IT REACHED THE INTERPRETER FROM THERE");
    }

    // ---- AOT -> AOT -------------------------------------------------------
    {
        ctbrowser::script::reset_transitions();
        caller_calls = 0;
        leaf_calls = 0;
        const value args[3] = {leaf, value::number(41), value::number(1)};
        const value out = ctx.call(caller, args, value::undefined());
        check(out.is_number() && out.as_number() == 42.0, "one compiled body calls another");
        check(caller_calls == 1 && leaf_calls == 1, "and both bodies ran");
        check(transitions(transition::aot_to_aot) == 1, "counted as AOT -> AOT");
        check(transitions(transition::aot_to_vm) == 0,
              "and the interpreter was not involved in the inner call");
    }

    // ---- AOT -> C++ -------------------------------------------------------
    {
        ctbrowser::script::reset_transitions();
        caller_calls = 0;
        native_calls = 0;
        const value args[3] = {native_add, value::number(41), value::number(1)};
        const value out = ctx.call(caller, args, value::undefined());
        check(out.is_number() && out.as_number() == 42.0,
              "a compiled body calls a native built-in");
        check(native_calls == 1, "the native ran");
        check(transitions(transition::aot_to_cxx) == 1, "counted as AOT -> C++");
    }

    // ---- `new` on a compiled constructor ---------------------------------
    //
    // `new` is a separate entry into a function (op::construct and
    // context::construct); both must dispatch to a compiled constructor and
    // pass `constructing`.
    {
        check(point_calls == 1, "the script's `new Point(3, 4)` reached the compiled constructor");
        check(saw_constructing == 1u, "AND IT WAS TOLD IT WAS CONSTRUCTING");
        const value built = ctx.global("built");
        check(built.is_object_like(),
              "so `new` evaluates to the instance, not to the number the body returned");

        // From C++, through context::construct, the other way in.
        ctbrowser::script::reset_transitions();
        point_calls = 0;
        saw_constructing = 0;
        const value args[2] = {value::number(20), value::number(22)};
        const value made = ctx.construct(ctx.global("Point"), args);
        check(point_calls == 1, "constructing from C++ reaches it too");
        check(saw_constructing == 1u, "and that path passes `constructing` as well");
        check(made.is_object_like(), "and evaluates to the instance");
        check(transitions(transition::cxx_to_aot) == 1, "counted as C++ -> AOT");
    }

    // ---- Nested mixed-mode calls -----------------------------------------
    //
    // Arbitrarily nested mixed-mode calls must work. One call goes
    // C++ -> VM -> AOT -> AOT: an interpreted relay is handed the compiled
    // caller, which calls the compiled leaf.
    {
        ctbrowser::script::reset_transitions();
        leaf_calls = 0;
        caller_calls = 0;
        // The relay is interpreted and forwards to whatever it is given, so
        // `relay(caller, leaf, 41, 1)` is one expression that crosses three
        // boundaries: C++ into the interpreter, the interpreter into the
        // compiled caller, and that caller into the compiled leaf.
        program nested = ctbrowser::script::compiler::compile(
            "function relay(f, g, a, b) { return f(g, a, b); }\n");
        check(nested.ok, "the relay compiles");
        const auto relay_ran = ctx.run(nested);
        check(relay_ran.ok, "and runs");
        const value relay = ctx.global("relay");

        ctbrowser::script::reset_transitions();
        leaf_calls = 0;
        caller_calls = 0;
        const value chain[4] = {caller, leaf, value::number(41), value::number(1)};
        const value out = ctx.call(relay, chain, value::undefined());
        check(out.is_number() && out.as_number() == 42.0, "C++ -> VM -> AOT -> AOT produces 42");
        check(transitions(transition::cxx_to_vm) == 1, "C++ reached the interpreted relay");
        check(transitions(transition::vm_to_aot) == 1, "the relay reached the compiled caller");
        check(transitions(transition::aot_to_aot) == 1, "which reached the compiled leaf");
        check(caller_calls == 1 && leaf_calls == 1, "and both compiled bodies ran once");
    }

    // ---- A compiled top level --------------------------------------------
    //
    // The case ctcompile actually produces. A page's `<script>` is a program's
    // top level, so a backend that compiles anything compiles this. Running a
    // program must dispatch its entry function to a compiled body too;
    // otherwise a whole compiled script would be interpreted silently.
    {
        program top = ctbrowser::script::compiler::compile("var ignored = 1;\n");
        check(top.ok, "the top-level fixture compiles");
        check(!top.functions.empty(), "and has an entry function");
        top.functions[0].aot_entry = &sample_leaf;

        ctbrowser::script::reset_transitions();
        leaf_calls = 0;
        const auto out = ctx.run(top);
        check(out.ok, "a program with a compiled top level runs");
        check(leaf_calls == 1, "AND ITS TOP LEVEL WAS THE COMPILED BODY");
        check(transitions(transition::cxx_to_aot) == 1, "counted as C++ -> AOT");
        check(transitions(transition::cxx_to_vm) == 0,
              "and the interpreter was never entered at all, which is the point");
    }

    // ---- A module evaluating inside its importer --------------------------
    //
    // The third place that enters a top level. `run` cannot be re-entered (it
    // clears `frames_`, because it is the entry point for a whole turn), so a
    // module evaluated from inside a running program goes through
    // `run_reentrant` instead, which must also dispatch to a compiled body.
    {
        program module_body = ctbrowser::script::compiler::compile("var inModule = 1;\n");
        check(module_body.ok, "the module fixture compiles");
        module_body.functions[0].aot_entry = &sample_leaf;

        // A frame must be on the stack, or run_module takes `run` and this
        // tests the path that is already covered. The compiled `caller` gives
        // one: it is entered from C++, and from inside it the module is
        // evaluated, which is the shape a dynamic import has.
        ctbrowser::script::reset_transitions();
        leaf_calls = 0;
        ctbrowser::script::module_record record;
        ctx.define_native("evaluateModule",
                          [&module_body, &record](context & cx, std::span<value>) {
                              (void)cx.run_module(module_body, record);
                              return value::undefined();
                          });
        const value args[1] = {ctx.global("evaluateModule")};
        (void)ctx.call(caller, args, value::undefined());
        check(leaf_calls == 1, "AND THE MODULE'S COMPILED TOP LEVEL RAN");
        check(transitions(transition::aot_to_cxx) == 1, "the compiled caller reached the native");
        check(transitions(transition::cxx_to_aot) >= 1, "which reached the module's compiled body");
        check(transitions(transition::cxx_to_vm) == 0,
              "and the interpreter was not entered for it");
    }

    // ---- Generators are not dispatched to AOT ----------------------------
    //
    // Deliberate: calling a generator only builds a coroutine, and resuming
    // one restores a register window copied out of the interpreter's register
    // file, which a compiled frame does not have. This pins the current
    // boundary; if compiled generators are supported later, this test should
    // fail and be updated.
    {
        program gen = ctbrowser::script::compiler::compile(
            "function* counter(n) { for (var i = 0; i < n; i++) { yield i; } }\n");
        check(gen.ok, "the generator fixture compiles");
        for (function_proto & fn : gen.functions) {
            if (fn.name == "counter") { fn.aot_entry = &sample_leaf; }
        }
        (void)ctx.run(gen);

        ctbrowser::script::reset_transitions();
        leaf_calls = 0;
        const value args[1] = {value::number(3)};
        const value made = ctx.call(ctx.global("counter"), args, value::undefined());
        check(made.is_object_like(), "calling a generator still produces a coroutine");
        check(leaf_calls == 0,
              "AND THE COMPILED BODY WAS NOT ENTERED - generators are Phase 14's, not Phase 3's");
        check(transitions(transition::cxx_to_aot) == 0, "so nothing was counted as a crossing");
    }

    // ---- All six, over the whole run --------------------------------------
    //
    // Each of the six transitions must be crossed at least once in this run;
    // otherwise every assertion above could pass without testing that
    // transition.
    {
        ctbrowser::script::reset_transitions();
        leaf_calls = 0;
        const value two[2] = {value::number(20), value::number(22)};
        const value three_vm[3] = {times_two, value::number(3), value::number(4)};
        const value three_aot[3] = {leaf, value::number(41), value::number(1)};
        const value three_native[3] = {native_add, value::number(41), value::number(1)};
        (void)ctx.run(compiled);                                  // C++ -> VM, VM -> AOT
        (void)ctx.call(leaf, two, value::undefined());            // C++ -> AOT
        (void)ctx.call(caller, three_vm, value::undefined());     // AOT -> VM
        (void)ctx.call(caller, three_aot, value::undefined());    // AOT -> AOT
        (void)ctx.call(caller, three_native, value::undefined()); // AOT -> C++
        for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(transition::count); ++i) {
            const auto which = static_cast<transition>(i);
            if (transitions(which) == 0) {
                std::printf("FAIL the transition %s was never exercised\n", transition_name(which));
                ++ctbrowser_test_failures;
            }
        }
    }

    if (ctbrowser_test_failures == 0) {
        std::printf("ok aot_dispatch (all six mixed-mode transitions, each asserted by counter)\n");
    }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
