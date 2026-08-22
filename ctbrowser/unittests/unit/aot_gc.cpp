// ctbrowser.aot: a compiled body under a collector that runs at every safepoint.
//
// Phase 4's gate is "forced-GC mixed-mode tests pass under sanitizers", and the
// master plan calls a forced-GC mode the highest-value test in the phase. This
// is why: in an ordinary run NOTHING COLLECTS WHILE SCRIPT IS RUNNING. The only
// production trigger is `collect_if_due`, once per tick, from the browser's
// frame loop, under a comment that says "collect between callbacks, never
// inside one".
//
// So every `is_safepoint` flag in aot_helpers.def is an obligation nothing has
// ever enforced, and a compiled body keeping a live value where the precise
// collector cannot see it would run correctly for as long as that stayed true.
// `context::set_gc_stress` makes it stop being true.
//
// WHAT THIS FOUND, before a line of it was written: a compiled body's receiver
// was in no root at all. The interpreted path stores it in
// call_frame::receiver and the collector marks that for every live frame; the
// bridge never set the field. For an ordinary call it survived by accident,
// because the receiver is usually still in the caller's register. For `new` it
// did not - op::construct's fresh instance lives in a C++ local until the body
// returns - so a collection anywhere inside a compiled constructor freed the
// object being constructed.
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

// A COMPILED BODY THAT ALLOCATES, KEEPS THE RESULT ACROSS A SAFEPOINT, AND
// THEN READS IT BACK.
//
// It builds a string, puts it in its own frame slot - which is a register, and
// registers are traced in full - then calls whatever it was handed. That call
// is `ct_aot_call`, which the table declares is_safepoint, so under stress the
// whole heap is collected inside it. Afterwards the body reads its slot again
// and returns the string.
//
// THE POINTER IS RE-FETCHED AFTER THE CALL, deliberately and not as a style
// choice: `registers_` is a std::vector and a nested call resizes it, so the
// base moves. This is the same rule the interpreter's own reg() follows.
extern "C" std::int32_t sample_keeps(ctbrowser::aot::ct_aot_ctx * ctx,
                                     const ctbrowser::aot::ct_aot_site * site,
                                     const std::uint64_t * argv, std::uint32_t argc,
                                     std::uint64_t receiver, std::uint32_t constructing,
                                     std::uint64_t * out) {
    ++body_calls;
    const std::uint64_t callee = argc > 0 ? argv[0] : value::undefined().bits();

    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    ctbrowser::aot::ct_aot_frame * frame =
        ctbrowser::aot::ct_aot_enter(ctx, site, /*reg_count*/ 4u, receiver, storage);
    if (frame == nullptr) {
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed);
    }

    // Allocated INSIDE the frame's lifetime and parked in a slot, which is the
    // whole discipline this phase exists to make possible.
    auto & cx = *reinterpret_cast<context *>(ctx);
    const value kept = cx.string("a string this body must not lose");
    ctbrowser::aot::ct_aot_slots(frame)[0] = kept.bits();

    std::uint64_t ignored = value::undefined().bits();
    const auto called = static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_call(
        frame, callee, value::undefined().bits(), nullptr, 0u, 0u, site, &ignored));
    if (called != ctbrowser::aot::ct_aot_status::ok) {
        if (called != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_leave(frame);
        }
        return static_cast<std::int32_t>(called);
    }

    // RE-FETCHED, because the call may have moved the register file.
    const value recovered = value::from_bits(ctbrowser::aot::ct_aot_slots(frame)[0]);
    *out = ctbrowser::aot::ct_aot_return_value(recovered.bits(), receiver, constructing);
    ctbrowser::aot::ct_aot_leave(frame);
    return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::ok);
}

// A COMPILED CONSTRUCTOR that allocates before it returns, so a collection
// inside it has something to free and its receiver has to survive one.
extern "C" std::int32_t sample_ctor(ctbrowser::aot::ct_aot_ctx * ctx,
                                    const ctbrowser::aot::ct_aot_site * site,
                                    const std::uint64_t * argv, std::uint32_t argc,
                                    std::uint64_t receiver, std::uint32_t constructing,
                                    std::uint64_t * out) {
    ++body_calls;
    const std::uint64_t callee = argc > 0 ? argv[0] : value::undefined().bits();

    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    ctbrowser::aot::ct_aot_frame * frame =
        ctbrowser::aot::ct_aot_enter(ctx, site, /*reg_count*/ 4u, receiver, storage);
    if (frame == nullptr) {
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed);
    }

    // A SAFEPOINT WITH THE INSTANCE LIVE. `this` is reachable only through the
    // frame the bridge pushed; if that field is not set, this collection frees
    // the object being constructed and everything after it is reading freed
    // memory.
    std::uint64_t ignored = value::undefined().bits();
    const auto called = static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_call(
        frame, callee, value::undefined().bits(), nullptr, 0u, 0u, site, &ignored));
    if (called != ctbrowser::aot::ct_aot_status::ok) {
        if (called != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_leave(frame);
        }
        return static_cast<std::int32_t>(called);
    }

    // Write a property on the receiver AFTER the collection, which is what
    // touches it: a freed instance is a use-after-free here, and a live one
    // keeps the property.
    auto & cx = *reinterpret_cast<context *>(ctx);
    const value self = value::from_bits(receiver);
    if (self.is_object()) { cx.store_property(self, "marker", value::number(1234)); }

    *out = ctbrowser::aot::ct_aot_return_value(value::number(7).bits(), receiver, constructing);
    ctbrowser::aot::ct_aot_leave(frame);
    return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::ok);
}

constexpr std::string_view fixture =
    "function keeps(f) { return f(); }\n"
    "function Thing(f) { this.made = f(); }\n"
    "function makeThing(f) { var t = new Thing(f); return t; }\n"
    // A CLASS WITH A FIELD INITIALISER, which is the only shape that opens
    // op::construct's own window: the instance exists, the initialiser runs
    // USER JAVASCRIPT, and nothing has pushed a frame carrying it yet.
    "class Held { made = churn(); }\n"
    "function makeHeld() { return new Held(); }\n"
    "function churn() { var s = ''; for (var i = 0; i < 40; i++) { s = s + i; } return s.length; "
    "}\n";

} // namespace

int main() {
    program compiled = ctbrowser::script::compiler::compile(fixture);
    check(compiled.ok, "the fixture compiles");
    if (!compiled.ok) { return 1; }
    for (function_proto & fn : compiled.functions) {
        if (fn.name == "keeps") { fn.aot_entry = &sample_keeps; }
        if (fn.name == "Thing") { fn.aot_entry = &sample_ctor; }
    }

    context ctx;
    ctbrowser::script::install_builtins(ctx);
    const auto ran = ctx.run(compiled);
    check(ran.ok, "and runs");

    const value churn = ctx.global("churn");
    check(churn.is_callable(), "the interpreted allocator is reachable");

    // ---- WITHOUT STRESS, which is what every other test in this suite sees --
    {
        body_calls = 0;
        const std::size_t before = ctx.collections();
        const value args[1] = {churn};
        const value out = ctx.call(ctx.global("keeps"), args, value::undefined());
        check(body_calls == 1, "the compiled body ran");
        check(out.is_string(), "and returned the string it parked in its slot");
        check(ctx.collections() == before,
              "and NOTHING COLLECTED - which is why this mode has to exist at all");
    }

    // ---- WITH STRESS ------------------------------------------------------
    ctx.set_gc_stress(true);
    {
        body_calls = 0;
        const std::size_t before = ctx.collections();
        const value args[1] = {churn};
        const value out = ctx.call(ctx.global("keeps"), args, value::undefined());
        check(body_calls == 1, "the compiled body ran under stress");
        // THE COUNTER, NOT THE ANSWER. A stress mode that silently stopped
        // collecting would produce exactly the same string.
        check(ctx.collections() > before, "AND A COLLECTION ACTUALLY RAN AT THE SAFEPOINT");
        check(out.is_string(), "the body still returned a string");
        check(ctx.to_string(out) == "a string this body must not lose",
              "AND IT IS THE STRING IT PARKED - the slot kept it alive across the collection");
    }

    // ---- A COMPILED CONSTRUCTOR, WHOSE RECEIVER IS THE THING AT RISK -------
    {
        body_calls = 0;
        const std::size_t before = ctx.collections();
        const value args[1] = {churn};
        const value made = ctx.construct(ctx.global("Thing"), args);
        check(body_calls == 1, "the compiled constructor ran");
        check(ctx.collections() > before, "with a collection inside it");
        check(made.is_object_like(), "`new` evaluated to the instance, not to the body's 7");
        const value marker = ctx.lookup_property(made, "marker");
        check(marker.is_number() && marker.as_number() == 1234,
              "AND THE INSTANCE SURVIVED THE COLLECTION - its receiver was rooted");
    }

    // ---- AND `new` FROM JAVASCRIPT, which is a different opcode -----------
    //
    // The arm above reached the constructor through context::construct, the
    // C++ way in. This one goes through `op::construct`, which builds the
    // instance itself and holds it in a C++ local of its own while field
    // initialisers run - the same hazard in a second place, which is why it
    // needed a second fix and needs a second arm.
    {
        body_calls = 0;
        const std::size_t before = ctx.collections();
        const value args[1] = {churn};
        const value made = ctx.call(ctx.global("makeThing"), args, value::undefined());
        check(body_calls == 1, "the compiled constructor ran from JavaScript's `new`");
        check(ctx.collections() > before, "with a collection inside it");
        check(made.is_object_like(), "and `new` evaluated to the instance");
        const value marker = ctx.lookup_property(made, "marker");
        check(marker.is_number() && marker.as_number() == 1234,
              "AND THE INSTANCE SURVIVED - op::construct rooted it too");
    }

    // ---- A FIELD INITIALISER, which is op::construct's own window ---------
    //
    // The arm above did not reach it: `Thing` is a plain function, so
    // `run_field_initialisers` had nothing to run and could not collect. A
    // CLASS FIELD runs user JavaScript with the instance in a C++ local and no
    // frame yet carrying it - which is the whole hazard, and this arm exists
    // because removing op::construct's root left the suite green without it.
    {
        const std::size_t before = ctx.collections();
        const value held = ctx.call(ctx.global("makeHeld"), {}, value::undefined());
        check(ctx.collections() > before, "the field initialiser collected");
        check(held.is_object_like(), "`new Held()` evaluated to an instance");
        const value made = ctx.lookup_property(held, "made");
        check(made.is_number(),
              "AND THE INSTANCE SURVIVED ITS OWN FIELD INITIALISER - op::construct rooted it");
    }

    // ---- AND THE INTERPRETER ITSELF, under the same stress -----------------
    //
    // Worth doing because it is nearly free and because the mode is only
    // trustworthy if the paths that were ALREADY correct stay correct under it.
    // A failure here would be a rooting bug in the VM, not in the AOT bridge.
    //
    // THE ANSWER IS COMPARED AGAINST ITSELF, not against a number written here.
    // A constant is a thing to get wrong - this file already did - and the
    // question is not "what does this program compute" but "does stress change
    // it", which a differential answers exactly.
    {
        constexpr std::string_view work =
            "function build(n) {\n"
            "  var acc = [];\n"
            "  for (var i = 0; i < n; i++) { acc.push({ n: i, s: 'v' + i }); }\n"
            "  var total = 0;\n"
            "  for (var j = 0; j < acc.length; j++) { total = total + acc[j].n + acc[j].s.length; "
            "}\n"
            "  return total;\n"
            "}\n";
        program builder = ctbrowser::script::compiler::compile(work);
        check(builder.ok, "the interpreted fixture compiles");

        context calm;
        ctbrowser::script::install_builtins(calm);
        (void)calm.run(builder);
        const value thirty[1] = {value::number(30)};
        const value without = calm.call(calm.global("build"), thirty, value::undefined());
        check(without.is_number(), "it computes a number without stress");

        context stressed;
        ctbrowser::script::install_builtins(stressed);
        (void)stressed.run(builder);
        stressed.set_gc_stress(true);
        const std::size_t before = stressed.collections();
        const value with = stressed.call(stressed.global("build"), thirty, value::undefined());
        check(stressed.collections() > before,
              "entering a function collects under stress, so the interpreter really was exercised");
        check(
            with.is_number() && without.is_number() && with.as_number() == without.as_number(),
            "AND THE INTERPRETER COMPUTES THE SAME ANSWER WITH A COLLECTOR RUNNING AT EVERY CALL");
    }

    ctx.set_gc_stress(false);
    if (ctbrowser_test_failures == 0) {
        std::printf("ok aot_gc (%zu collections, all at safepoints)\n", ctx.collections());
    }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
