// ctbrowser.aot: a compiled `try`, which the ABI said could not be written.
//
// aot_helpers.def records, dated and by name, that ct_aot_catch_land "CANNOT BE
// IMPLEMENTED AS WRITTEN, found on 2026-08-22 by trying": it says to read back
// registers_[call_frame::base + handler::slot], and unwind_to_handler pops the
// handler BEFORE it writes, so by the time a compiled body could ask, which
// register the thrown value went into is unknowable. The row wrote down two
// possible fixes and deliberately took neither, because "taking one without a
// compiled `try` to test it would be inventing on no evidence".
//
// This file is that evidence. The fix taken is a third: call_frame records the
// slot at the moment unwind_to_handler writes it, which keeps the helper's
// signature the one the table declares.
//
// COMPLETIONS, NOT UNWINDING, which is Phase 6's whole instruction. A compiled
// body never has a C++ exception thrown through it: a helper RETURNS
// CT_AOT_CAUGHT and the body branches to its own pad, exactly as the plan says
// - "generated functions are nounwind".
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

std::size_t body_calls = 0;
std::size_t pads_taken = 0;

enum slot : std::size_t {
    // NOT SLOT ZERO, deliberately. It was, and that made the runtime's
    // recording of WHICH slot the thrown value went into untestable: a
    // `landed_slot` left at its default of 0 is indistinguishable from one
    // correctly recorded as 0. Blinding the recording left this file green.
    slot_arg = 0,
    slot_caught = 2, // where the thrown value lands
    slot_count = 4,
};

constexpr std::uint32_t pad_catch = 7; // any id the body likes; it is its own label

// A COMPILED
//
//     function guarded(f) {
//       try { return f(); }
//       catch (e) { return "caught:" + e; }
//     }
//
// written as a backend would emit it: push a handler naming a pad and a slot,
// make the call, and branch on the STATUS rather than on an exception.
extern "C" std::int32_t compiled_guarded(ctbrowser::aot::ct_aot_ctx * ctx,
                                         const ctbrowser::aot::ct_aot_site * site,
                                         const std::uint64_t * argv, std::uint32_t argc,
                                         std::uint64_t receiver, std::uint32_t constructing,
                                         std::uint64_t * out) {
    ++body_calls;
    const std::uint64_t callee = argc > 0 ? argv[0] : value::undefined().bits();

    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    ctbrowser::aot::ct_aot_frame * frame =
        ctbrowser::aot::ct_aot_enter(ctx, site, slot_count, receiver, storage);
    if (frame == nullptr) { return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed); }

    std::uint64_t * slots = ctbrowser::aot::ct_aot_slots(frame);
    slots[slot_arg] = callee;

    // try {
    ctbrowser::aot::ct_aot_handler_push(frame, pad_catch, slot_caught);
    std::uint64_t produced = value::undefined().bits();
    const auto status = static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_call(
        frame, slots[slot_arg], value::undefined().bits(), nullptr, 0u, 0u, site, &produced));

    if (status == ctbrowser::aot::ct_aot_status::ok) {
        // No throw: the handler this body pushed must come off, or it stays
        // live for the caller. Balance is the compiler's obligation and
        // ct_aot_handler_pop does not check it for us.
        ctbrowser::aot::ct_aot_handler_pop(frame);
        *out = ctbrowser::aot::ct_aot_return_value(produced, receiver, constructing);
        ctbrowser::aot::ct_aot_leave(frame);
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::ok);
    }

    if (status != ctbrowser::aot::ct_aot_status::caught) {
        // UNWOUND means this frame is already gone - no epilogue, no leave.
        // FAILED is the uncatchable tier and no `catch` may see it.
        if (status != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_handler_pop(frame);
            ctbrowser::aot::ct_aot_leave(frame);
        }
        return static_cast<std::int32_t>(status);
    }

    // } catch (e) {
    //
    // THE HANDLER IS ALREADY OFF: unwind_to_handler pops as it searches. A
    // ct_aot_handler_pop here would take the CALLER's.
    std::uint64_t thrown = value::undefined().bits();
    const std::uint32_t pad = ctbrowser::aot::ct_aot_catch_land(frame, &thrown);
    slots = ctbrowser::aot::ct_aot_slots(frame);
    if (pad != pad_catch) {
        ctbrowser::aot::ct_aot_leave(frame);
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed);
    }
    ++pads_taken;
    slots[slot_caught] = thrown;

    // "caught:" + e, through the re-entering concatenation, which can itself
    // throw if `e` has a hostile toString - so its status is tested too.
    auto & cx = *reinterpret_cast<context *>(ctx);
    const value prefix = cx.string("caught:");
    std::uint64_t message = value::undefined().bits();
    const auto joined = static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_binary_op(
        frame, static_cast<std::uint32_t>(ctbrowser::script::op::add_generic), prefix.bits(),
        slots[slot_caught], &message));
    if (joined != ctbrowser::aot::ct_aot_status::ok) {
        if (joined != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_leave(frame);
        }
        return static_cast<std::int32_t>(joined);
    }

    *out = ctbrowser::aot::ct_aot_return_value(message, receiver, constructing);
    ctbrowser::aot::ct_aot_leave(frame);
    return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::ok);
}

// A COMPILED BODY THAT POPS A HANDLER IT NEVER PUSHED, which is what a
// mis-balanced emission looks like. ct_aot_handler_pop takes the GLOBALLY
// innermost handler without consulting handler_base, so without a guard this
// silently takes its CALLER'S catch and nothing reports it - the row says so.
extern "C" std::int32_t compiled_sloppy(ctbrowser::aot::ct_aot_ctx * ctx,
                                        const ctbrowser::aot::ct_aot_site * site,
                                        const std::uint64_t * argv, std::uint32_t argc,
                                        std::uint64_t receiver, std::uint32_t constructing,
                                        std::uint64_t * out) {
    (void)argv;
    (void)argc;
    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    ctbrowser::aot::ct_aot_frame * frame =
        ctbrowser::aot::ct_aot_enter(ctx, site, slot_count, receiver, storage);
    if (frame == nullptr) { return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed); }
    ctbrowser::aot::ct_aot_handler_pop(frame); // never pushed one
    *out = ctbrowser::aot::ct_aot_return_value(value::number(1).bits(), receiver, constructing);
    ctbrowser::aot::ct_aot_leave(frame);
    return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::ok);
}

constexpr std::string_view source =
    "function guarded(f) {\n"
    "  try { return f(); }\n"
    "  catch (e) { return 'caught:' + e; }\n"
    "}\n"
    "function fine() { return 'no problem'; }\n"
    "function boom() { throw 'a string'; }\n"
    "function boomObject() { throw { toString: function () { return 'an object'; } }; }\n"
    "function outer(f) {\n"
    "  try { return guarded(f); }\n"
    "  catch (e) { return 'outer:' + e; }\n"
    "}\n"
    "function rethrower() { throw 'inner'; }\n"
    "function sloppy() { return 1; }\n"
    "function keepsCatch(f) {\n"
    "  try { f(); throw 'after'; }\n"
    "  catch (e) { return 'kept:' + e; }\n"
    "}\n";

[[nodiscard]] std::string run(bool stamp, const char * entry, const char * argument,
                              std::size_t & calls, bool & failed) {
    program compiled = ctbrowser::script::compiler::compile(source);
    if (!compiled.ok) {
        failed = true;
        return "<did not compile>";
    }
    if (stamp) {
        for (function_proto & fn : compiled.functions) {
            if (fn.name == "guarded") { fn.aot_entry = &compiled_guarded; }
            if (fn.name == "sloppy") { fn.aot_entry = &compiled_sloppy; }
        }
    }
    context ctx;
    ctbrowser::script::install_builtins(ctx);
    const auto ran = ctx.run(compiled);
    if (!ran.ok) {
        failed = true;
        return "<did not run>";
    }
    body_calls = 0;
    const value args[1] = {ctx.global(argument)};
    const value answer = ctx.call(ctx.global(entry), args, value::undefined());
    calls = body_calls;
    failed = ctx.failed();
    return ctx.to_string(answer);
}

// Both arms, compared. The interpreted one is the specification here.
void agrees(const char * entry, const char * argument, bool expect_body) {
    std::size_t calls = 0;
    bool failed = false;
    const std::string interpreted = run(false, entry, argument, calls, failed);
    check(calls == 0, std::string{"no compiled body ran for "} + entry + "(" + argument + ")");
    const bool interpreted_failed = failed;

    const std::string compiled = run(true, entry, argument, calls, failed);
    check(!expect_body || calls == 1,
          std::string{"the compiled body ran for "} + entry + "(" + argument + ")");
    check(compiled == interpreted, std::string{entry} + "(" + argument + ") is \"" + compiled +
                                       "\" compiled and \"" + interpreted + "\" interpreted");
    check(failed == interpreted_failed,
          std::string{"and both arms agree about failing for "} + entry + "(" + argument + ")");
}

} // namespace

int main() {
    // NOTHING THROWN: the handler must come off again, and the answer is the
    // callee's. A body that forgot the pop leaves a live handler pointing at a
    // frame that has returned - which the next throw anywhere would find.
    agrees("guarded", "fine", true);

    // A STRING THROWN THROUGH A COMPILED try, caught by its own catch.
    agrees("guarded", "boom", true);
    check(pads_taken >= 1, "AND THE COMPILED BODY REACHED ITS LANDING PAD");

    // AN OBJECT THROWN, whose toString runs during the concatenation in the
    // catch block - page JavaScript running inside a compiled handler.
    agrees("guarded", "boomObject", true);

    // A THROW THAT PASSES THROUGH the compiled frame to a handler BELOW it.
    // The compiled body must return UNWOUND without running its epilogue: its
    // frame is already gone, and calling ct_aot_leave would pop somebody else's.
    agrees("outer", "rethrower", true);

    // A MIS-BALANCED BODY MUST NOT TAKE ITS CALLER'S CATCH. `keepsCatch` has a
    // live handler when it calls the compiled `sloppy`, which pops one it never
    // pushed; if that pop is allowed, the `throw 'after'` on the next line is
    // uncaught instead of caught.
    agrees("keepsCatch", "sloppy", false);

    // AND UNCAUGHT, which is the failure tier no `catch` may see: the compiled
    // body must not treat it as caught.
    {
        std::size_t calls = 0;
        bool failed = false;
        const std::string interpreted = run(false, "guarded", "boom", calls, failed);
        (void)interpreted;
        // `boom` inside `guarded` IS caught, so uncaught needs the throw to
        // escape both. Called directly, nothing is guarding it.
        const std::string bare_interpreted = run(false, "boom", "fine", calls, failed);
        const bool interpreted_failed = failed;
        const std::string bare_compiled = run(true, "boom", "fine", calls, failed);
        check(bare_compiled == bare_interpreted, "an unguarded throw reads the same either way");
        check(failed == interpreted_failed && interpreted_failed,
              "and both arms record the uncatchable failure");
    }

    if (ctbrowser_test_failures == 0) {
        std::printf("ok aot_throw (a compiled try/catch, %zu pads taken)\n", pads_taken);
    }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
