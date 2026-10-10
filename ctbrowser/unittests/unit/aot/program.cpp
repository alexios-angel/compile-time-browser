//===- unittests/unit/aot/program.cpp - Hand-compiled function vs VM ------===//
//
// A whole function hand-compiled against the AOT ABI. It is the first test that
// uses the whole ABI at once (frame, rooted slots, calls, binary operations,
// comparison, truthiness, property and index reads, interned names and the
// failure poll) to answer one question: could a backend emit a real function
// against it today?
//
//     function total(items, scale) {
//       var sum = 0;
//       for (var i = 0; i < items.length; i = i + 1) {
//         sum = sum + scale(items[i].width);
//       }
//       return sum;
//     }
//
// `scale` is interpreted and recurses on purpose. Without a nested call in the
// loop nothing grows the register file, so a body that forgot to reload its
// slot pointer after a safepoint would still work: the pointer only goes stale
// when the vector reallocates.
//
// Written by hand the way a backend would emit it, and checked against the
// interpreter running the same source on the same input: the same differential
// the compiler's test oracle uses.
//
// It also runs under forced GC, because a compiled body that keeps a live value
// where the collector cannot see it is the failure this ABI is arranged around.
// Every value that must survive a safepoint is parked in a frame slot and
// reloaded afterwards, which is the discipline generated code must follow.
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

/// How many times compiled_total ran; proves the compiled body was used.
std::size_t compiled_calls = 0;

/// The frame's slots, named the way a backend would name its virtual registers.
enum slot : std::size_t {
    slot_items = 0,
    slot_scale = 1,
    slot_sum = 2,
    slot_index = 3,
    slot_length = 4,
    slot_element = 5,
    slot_count = 6,
};

/// The hand-compiled body of `total`, standing in for code-generator output.
///
/// Every helper that can collect is followed by a reload of the slot pointer,
/// and every live value crosses those calls in a slot rather than in a C++
/// local.
extern "C" std::int32_t compiled_total(ctbrowser::aot::ct_aot_ctx * ctx,
                                       const ctbrowser::aot::ct_aot_site * site,
                                       const std::uint64_t * argv, std::uint32_t argc,
                                       std::uint64_t receiver, std::uint32_t constructing,
                                       std::uint64_t * out) {
    ++compiled_calls;
    // Read argv before ct_aot_enter: enter may resize the register file (see
    // its entry in aot_helpers.def), which leaves argv dangling.
    const std::uint64_t items = argc > 0 ? argv[0] : value::undefined().bits();
    const std::uint64_t scale = argc > 1 ? argv[1] : value::undefined().bits();

    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    ctbrowser::aot::ct_aot_frame * frame =
        ctbrowser::aot::ct_aot_enter(ctx, site, slot_count, receiver, storage);
    if (frame == nullptr) {
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed);
    }

    const auto leave_with = [&](ctbrowser::aot::ct_aot_status status) {
        // `unwound` means the frame is already popped, so leaving again would
        // pop someone else's. Every exit goes through this one lambda so frame
        // unregistration is written once rather than at each return.
        if (status != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_leave(frame);
        }
        return static_cast<std::int32_t>(status);
    };

    std::uint64_t * slots = ctbrowser::aot::ct_aot_slots(frame);
    slots[slot_items] = items;
    slots[slot_scale] = scale;
    slots[slot_sum] = value::number(0).bits();
    slots[slot_index] = value::number(0).bits();

    // `items.length` - one interned name, looked up once and reused, exactly as
    // a backend would hoist it to image init.
    static const ctbrowser::aot::ct_aot_name * const name_length =
        ctbrowser::aot::ct_aot_intern_name("length", 6u);
    static const ctbrowser::aot::ct_aot_name * const name_width =
        ctbrowser::aot::ct_aot_intern_name("width", 5u);

    {
        std::uint64_t length = value::undefined().bits();
        const auto status =
            static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_get_prop(
                frame, slots[slot_items], name_length, nullptr, &length));
        if (status != ctbrowser::aot::ct_aot_status::ok) { return leave_with(status); }
        // This reload is correct but not enforced: removing it leaves the test
        // green, because the register file does not reallocate across this
        // read. It grows geometrically and settles at a high-water mark, and
        // nothing reachable inside the 512-frame guard pushes past the mark
        // this fixture has already reached when the loop starts. Deepening the
        // getter until it crossed also tripped the frame guard in the control
        // arm, so the test would fail for the wrong reason. The reload after
        // the nested call below is enforced.
        slots = ctbrowser::aot::ct_aot_slots(frame);
        slots[slot_length] = length;
    }

    for (;;) {
        // Loop condition i < length: compare the ordering ct_aot_compare writes
        // against `less` and nothing else. An unordered result (either side
        // NaN) must exit the loop, which makes `for (i = 0; i < NaN; ...)` run
        // zero times.
        std::int32_t ordering = 0;
        {
            const auto status =
                static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_compare(
                    frame, slots[slot_index], slots[slot_length], &ordering));
            if (status != ctbrowser::aot::ct_aot_status::ok) { return leave_with(status); }
            slots = ctbrowser::aot::ct_aot_slots(frame);
        }
        if (ordering != static_cast<std::int32_t>(ctbrowser::aot::ct_aot_ordering::less)) { break; }

        // items[i]
        {
            std::uint64_t element = value::undefined().bits();
            const auto status =
                static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_get_index(
                    frame, slots[slot_items], slots[slot_index], nullptr, &element));
            if (status != ctbrowser::aot::ct_aot_status::ok) { return leave_with(status); }
            slots = ctbrowser::aot::ct_aot_slots(frame);
            slots[slot_element] = element;
        }

        // .width
        std::uint64_t width = value::undefined().bits();
        {
            const auto status =
                static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_get_prop(
                    frame, slots[slot_element], name_width, nullptr, &width));
            if (status != ctbrowser::aot::ct_aot_status::ok) { return leave_with(status); }
            slots = ctbrowser::aot::ct_aot_slots(frame);
        }

        // scale(width): a compiled body calling an interpreted function, the
        // AOT -> VM transition that makes the reloads below matter: the
        // callee's frame grows the register file, so every pointer into it
        // taken before this call is stale afterwards.
        std::uint64_t scaled = value::undefined().bits();
        {
            const std::uint64_t forwarded[1] = {width};
            const auto status = static_cast<ctbrowser::aot::ct_aot_status>(
                ctbrowser::aot::ct_aot_call(frame, slots[slot_scale], value::undefined().bits(),
                                            forwarded, 1u, 0u, site, &scaled));
            if (status != ctbrowser::aot::ct_aot_status::ok) { return leave_with(status); }
            slots = ctbrowser::aot::ct_aot_slots(frame);
        }

        // sum = sum + scaled, through the re-entering `+` because the operands
        // are not provably numbers.
        {
            std::uint64_t sum = value::undefined().bits();
            const auto status =
                static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_binary_op(
                    frame, static_cast<std::uint32_t>(ctbrowser::script::op::add_generic),
                    slots[slot_sum], scaled, &sum));
            if (status != ctbrowser::aot::ct_aot_status::ok) { return leave_with(status); }
            slots = ctbrowser::aot::ct_aot_slots(frame);
            slots[slot_sum] = sum;
        }

        // i = i + 1, through the static family: the counter is a number this
        // body produced, so nothing here can run user code.
        {
            std::uint64_t next = value::undefined().bits();
            const auto status =
                static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_binary_op_static(
                    frame, static_cast<std::uint32_t>(ctbrowser::script::op::add),
                    slots[slot_index], value::number(1).bits(), &next));
            if (status != ctbrowser::aot::ct_aot_status::ok) { return leave_with(status); }
            slots = ctbrowser::aot::ct_aot_slots(frame);
            slots[slot_index] = next;
        }

        // Back-edge poll. Uncatchable failures (allocation ceiling, depth
        // guard) set a flag no try/catch sees; a compiled loop that never
        // checks it would run past them. ct_aot_failed is one bool load,
        // cheaper than a status test after every allocation.
        if (ctbrowser::aot::ct_aot_failed(frame) != 0u) {
            return leave_with(ctbrowser::aot::ct_aot_status::failed);
        }
    }

    *out = ctbrowser::aot::ct_aot_return_value(slots[slot_sum], receiver, constructing);
    return leave_with(ctbrowser::aot::ct_aot_status::ok);
}

constexpr std::string_view source =
    // Deep and wide. Depth alone does not force the register file to move: it
    // grows geometrically and settles at a high-water mark, so 200 narrow
    // frames fit in capacity something earlier already asked for. Each frame
    // here carries thirty-odd locals, so the getter below is far and away the
    // largest thing in the program and its read really does reallocate.
    "function deep(n) {\n"
    "  var a1=1,a2=2,a3=3,a4=4,a5=5,a6=6,a7=7,a8=8,a9=9,a10=10;\n"
    "  var b1=1,b2=2,b3=3,b4=4,b5=5,b6=6,b7=7,b8=8,b9=9,b10=10;\n"
    "  var c1=1,c2=2,c3=3,c4=4,c5=5,c6=6,c7=7,c8=8,c9=9,c10=10;\n"
    "  if (n <= 0) { return a1 - a1; }\n"
    "  return deep(n - 1) + (c10 - c10);\n"
    "}\n"
    "function scale(w) { return deep(10) + w * 2; }\n"
    "function total(items, scale) {\n"
    "  var sum = 0;\n"
    "  for (var i = 0; i < items.length; i = i + 1) {\n"
    "    sum = sum + scale(items[i].width);\n"
    "  }\n"
    "  return sum;\n"
    "}\n"
    // `width` is a getter, because reading a property can run a page's own code
    // (which is why ct_aot_get_prop is may_reenter). With a plain data
    // property nothing nests, the register file never grows, and a body that
    // forgot to reload its slot pointer after the read still worked.
    "class Item {\n"
    "  constructor(w) { this.w = w; }\n"
    "  get width() { return deep(200) + this.w; }\n"
    "}\n"
    "function makeItems(n) {\n"
    "  var out = [];\n"
    "  for (var i = 0; i < n; i = i + 1) { out.push(new Item(i * 3)); }\n"
    "  return out;\n"
    "}\n";

/// Runs `total(makeItems(count), scale)` on a fresh context, with `total`
/// either interpreted or stamped with the hand-compiled body. `stress` forces a
/// collection at every safepoint; `calls` receives how often the compiled body
/// ran and `ok` whether everything ran cleanly.
[[nodiscard]] value run_total(bool stamp, int count, bool stress, std::size_t & calls, bool & ok) {
    program compiled = ctbrowser::script::compiler::compile(source);
    if (!compiled.ok) {
        ok = false;
        return value::undefined();
    }
    if (stamp) {
        for (function_proto & fn : compiled.functions) {
            if (fn.name == "total") { fn.aot_entry = &compiled_total; }
        }
    }
    context ctx;
    ctbrowser::script::install_builtins(ctx);
    const auto ran = ctx.run(compiled);
    if (!ran.ok) {
        ok = false;
        return value::undefined();
    }
    const value size[1] = {value::number(count)};
    const value items = ctx.call(ctx.global("makeItems"), size, value::undefined());

    ctx.set_gc_stress(stress);
    compiled_calls = 0;
    const value args[2] = {items, ctx.global("scale")};
    const value answer = ctx.call(ctx.global("total"), args, value::undefined());
    ctx.set_gc_stress(false);
    calls = compiled_calls;
    ok = !ctx.failed();
    return answer;
}

} // namespace

int main() {
    // The interpreter's answer first, from the same source. The compiled body
    // must agree with it; a constant written here would only check the body
    // against hand arithmetic.
    std::size_t calls = 0;
    bool ok = true;
    const value interpreted = run_total(false, 12, false, calls, ok);
    check(ok, "the interpreted arm runs cleanly");
    // Check for a finite, known number, not just "a number". Two arms that both produce
    // NaN agree perfectly, and NaN is what this fixture gives if the getter
    // never runs - so a differential alone cannot tell working from broken
    // here. width is i * 3, scale doubles it, so the sum is 6 * (0 + 1 + ... +
    // 11) = 396.
    check(interpreted.is_number() && interpreted.as_number() == 396.0,
          "and returns 396, which is what this program computes");
    check(calls == 0, "with no compiled body involved - the blinded arm really is blind");

    const value from_body = run_total(true, 12, false, calls, ok);
    check(ok, "the compiled arm runs cleanly");
    check(calls == 1, "AND THE HAND-COMPILED BODY RAN");
    check(from_body.is_number() && interpreted.is_number() &&
              from_body.as_number() == interpreted.as_number(),
          "AND IT AGREES WITH THE INTERPRETER");

    // Empty and one-element inputs, because a loop that is wrong at its
    // boundaries can still pass a test with twelve elements.
    for (const int count : {0, 1, 2, 37}) {
        const value expected = run_total(false, count, false, calls, ok);
        const value got = run_total(true, count, false, calls, ok);
        check(calls == 1, "the body ran for " + std::to_string(count) + " elements");
        check(got.is_number() && expected.is_number() && got.as_number() == expected.as_number(),
              "and agrees on " + std::to_string(count) + " elements");
    }

    // Under forced GC every safepoint in that body collects the whole heap,
    // so a value parked anywhere but a frame slot is freed while it is still
    // needed. Under asan this is where a missing reload shows up.
    {
        const value expected = run_total(false, 12, true, calls, ok);
        const value got = run_total(true, 12, true, calls, ok);
        check(calls == 1, "the body ran under stress");
        check(got.is_number() && expected.is_number() && got.as_number() == expected.as_number(),
              "AND AGREES WITH THE INTERPRETER WITH A COLLECTOR RUNNING AT EVERY SAFEPOINT");
    }

    if (ctbrowser_test_failures == 0) {
        std::printf("ok aot_program (a hand-compiled function agrees with the interpreter)\n");
    }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
