//===- ctbrowser/unittests/unit/aot/helpers.cpp - AOT helper differential -===//
//
// An extracted helper computes what the code it replaced did.
//
// The VM's operator semantics were moved into helpers that the AOT backend
// shares, and that move must not change the VM's observable behaviour. The
// full suite passing is the first check. This file is the second: a
// reference implementation, transcribed from the interpreter handlers before
// the move, run against the extracted functions over a matrix of operands.
//
// Seven of the handlers are byte-identical except for one operator each (`&`
// against `|`, `<<` against `>>`, to_int32 against to_uint32), so the likely
// mistake is a transposed line in the switch, not a crash or a missing case.
// A program that never shifts by a negative amount, or never ORs, would pass a
// whole suite over it.
//
// The file also checks the ABI helpers that wrap existing runtime functions:
// truthiness, ordering, equality, typeof, allocation, interned names and
// property access.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/vm.hpp>

#include "check.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::script::context;
using ctbrowser::script::op;
using ctbrowser::script::value;

namespace {

/// The seven static handlers as they were before the extraction, minus the
/// BigInt arm, which is a private member the extraction did not touch and this
/// cannot call. Every operand is chosen so that arm does not fire, which leaves
/// exactly the part that moved: the static conversion and the operator.
value reference(op kind, value lhs, value rhs) {
    switch (kind) {
    case op::add: return value::number(context::to_number(lhs) + context::to_number(rhs));
    case op::bit_and: return value::number(context::to_int32(lhs) & context::to_int32(rhs));
    case op::bit_or: return value::number(context::to_int32(lhs) | context::to_int32(rhs));
    case op::bit_xor: return value::number(context::to_int32(lhs) ^ context::to_int32(rhs));
    case op::shl:
        return value::number(static_cast<std::int32_t>(
            static_cast<std::uint32_t>(context::to_int32(lhs)) << (context::to_uint32(rhs) & 31U)));
    case op::shr: return value::number(context::to_int32(lhs) >> (context::to_uint32(rhs) & 31U));
    case op::ushr:
        return value::number(
            static_cast<double>(context::to_uint32(lhs) >> (context::to_uint32(rhs) & 31U)));
    default: break;
    }
    return value::undefined();
}

/// The seven re-entering handlers (which may run user code) as they were. The
/// same rule applies: the BigInt arm is private, so every operand pair is
/// chosen to miss it. `concat` and `add_generic` are transcribed whole,
/// because they are the two shapes an extraction could most easily flatten
/// into each other.
value reference_reentering(context & cx, op kind, value lhs, value rhs) {
    switch (kind) {
    case op::sub: return value::number(cx.to_number_value(lhs) - cx.to_number_value(rhs));
    case op::mul: return value::number(cx.to_number_value(lhs) * cx.to_number_value(rhs));
    case op::div: return value::number(cx.to_number_value(lhs) / cx.to_number_value(rhs));
    case op::mod: return value::number(std::fmod(cx.to_number_value(lhs), cx.to_number_value(rhs)));
    case op::pow:
        return value::number(
            context::exponentiate(cx.to_number_value(lhs), cx.to_number_value(rhs)));
    case op::concat: return cx.string(cx.to_string(lhs) + cx.to_string(rhs));
    case op::add_generic: {
        // No ToPrimitive here, which is why the operand matrix contains no
        // objects: `context::to_primitive` is private, and adding a test-only
        // accessor would change the thing under test to suit the test. On
        // primitives ToPrimitive is the identity, so this arm matches the
        // extracted one exactly.
        //
        // That leaves the valueOf/toString walk, which makes this family
        // re-entering, uncovered here. `[] - 0`, `{valueOf: () => 42} + 1` and
        // their kin are covered by the JavaScript suite, which runs them as
        // source.
        if (lhs.is_string() || rhs.is_string()) {
            return cx.string(cx.to_string(lhs) + cx.to_string(rhs));
        }
        return value::number(context::to_number(lhs) + context::to_number(rhs));
    }
    default: break;
    }
    return value::undefined();
}

/// Value equality for the differential: NaN equals NaN (two NaNs are not `==`
/// and both arms produce them), and 0 and -0 differ.
bool same(value a, value b) {
    if (a.is_number() && b.is_number()) {
        const double x = a.as_number();
        const double y = b.as_number();
        if (std::isnan(x) && std::isnan(y)) { return true; }
        // -0 and 0 compare equal as doubles and are different values here, so
        // the sign bit is compared too: `-0 | 0` is 0 and `-0 + -0` is -0.
        if (x == 0.0 && y == 0.0) { return std::signbit(x) == std::signbit(y); }
        return x == y;
    }
    return a.bits() == b.bits();
}

/// Prints \p v for a failure message, quoting strings and spelling out -0.
std::string describe(context & cx, value v) {
    if (v.is_string()) { return "\"" + cx.to_string(v) + "\""; }
    // to_string prints -0 as "0"; spell it out so a mismatch between the two
    // zeroes is readable.
    if (v.is_number() && v.as_number() == 0.0 && std::signbit(v.as_number())) { return "-0"; }
    return cx.to_string(v);
}

const char * name_of(op kind) {
    switch (kind) {
    case op::add: return "add";
    case op::bit_and: return "bit_and";
    case op::bit_or: return "bit_or";
    case op::bit_xor: return "bit_xor";
    case op::shl: return "shl";
    case op::shr: return "shr";
    case op::ushr: return "ushr";
    case op::sub: return "sub";
    case op::mul: return "mul";
    case op::div: return "div";
    case op::mod: return "mod";
    case op::pow: return "pow";
    case op::add_generic: return "add_generic";
    case op::concat: return "concat";
    default: return "?";
    }
}

} // namespace

int main() {
    // The AOT bridge has the same TDZ even for typeof and sloppy assignment.
    for (int access = 0; access != 3; ++access) {
        context cx;
        ctbrowser::script::install_builtins(cx);
        const auto first = ctbrowser::script::compiler::compile("throw 0; let pending = 1;");
        check(!cx.run(first).ok, "the lexical initializer is not reached");
        const auto probe = ctbrowser::script::compiler::compile("0;");
        check(cx.run(probe).ok, "the next script clears the prior failure");
        const auto * site =
            reinterpret_cast<const ctbrowser::aot::ct_aot_site *>(&probe.functions[0]);
        alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
        auto * frame =
            ctbrowser::aot::ct_aot_enter(reinterpret_cast<ctbrowser::aot::ct_aot_ctx *>(&cx), site,
                                         1u, value::undefined().bits(), storage);
        check(frame != nullptr, "the TDZ probe frame enters");
        if (frame == nullptr) { return 1; }
        ctbrowser::aot::ct_aot_handler_push(frame, 11u, 0u);
        std::uint64_t out = value::number(123).bits();
        std::int32_t checked = 0;
        if (access == 0) {
            checked = ctbrowser::aot::ct_aot_global_get(frame, "pending", 7u, &out);
            check(out == value::number(123).bits(), "a failed hard read preserves its out slot");
        } else if (access == 1) {
            (void)ctbrowser::aot::ct_aot_global_get_soft(frame, "pending", 7u);
            checked = ctbrowser::aot::ct_aot_check(frame);
        } else {
            ctbrowser::aot::ct_aot_global_set(frame, "pending", 7u, value::number(9).bits());
            checked = ctbrowser::aot::ct_aot_check(frame);
        }
        const auto status = static_cast<ctbrowser::aot::ct_aot_status>(checked);
        check(status == ctbrowser::aot::ct_aot_status::caught,
              "an AOT lexical access reaches its catch edge");
        if (status == ctbrowser::aot::ct_aot_status::caught) {
            std::uint64_t thrown = value::undefined().bits();
            check(ctbrowser::aot::ct_aot_catch_land(frame, &thrown) == 11u,
                  "the TDZ exception reaches the expected pad");
            check(cx.to_string(cx.lookup_property(value::from_bits(thrown), "name")) ==
                      "ReferenceError",
                  "the TDZ exception is a ReferenceError");
        } else if (status != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_handler_pop(frame);
        }
        if (status != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_leave(frame);
        }
        const auto still = ctbrowser::script::compiler::compile("typeof pending;");
        check(!cx.run(still).ok, "an AOT assignment does not initialize the stranded binding");
    }
    // An uncaught TDZ error is described through its prototype, which can run
    // page code even though assignment itself calls no accessor.
    {
        context cx;
        ctbrowser::script::install_builtins(cx);
        const auto first = ctbrowser::script::compiler::compile("throw 0; let pending = 1;");
        check(!cx.run(first).ok, "the reentrant store fixture strands its lexical");
        const auto setup = ctbrowser::script::compiler::compile(
            "var described = 0; Object.defineProperty(ReferenceError.prototype, 'name',"
            "{get: function() { described++; return 'ReferenceError'; }});");
        check(cx.run(setup).ok, "the error prototype has an observable getter");
        const auto * site =
            reinterpret_cast<const ctbrowser::aot::ct_aot_site *>(&setup.functions[0]);
        alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
        auto * frame =
            ctbrowser::aot::ct_aot_enter(reinterpret_cast<ctbrowser::aot::ct_aot_ctx *>(&cx), site,
                                         1u, value::undefined().bits(), storage);
        check(frame != nullptr, "the uncaught TDZ store frame enters");
        if (frame == nullptr) { return 1; }
        ctbrowser::aot::ct_aot_global_set(frame, "pending", 7u, value::number(9).bits());
        check(static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_check(frame)) ==
                  ctbrowser::aot::ct_aot_status::failed,
              "an uncaught TDZ store fails the turn");
        check(cx.global("described").is_number() && cx.global("described").as_number() == 1,
              "describing the TDZ failure reenters the getter exactly once");
        ctbrowser::aot::ct_aot_leave(frame);
    }
    // Both Construct helpers must let a compiled handler catch a non-constructor
    // TypeError, including the global object's Proxy forwarding path.
    for (const bool spread : {false, true}) {
        context cx;
        ctbrowser::script::install_builtins(cx);
        const auto tiny = ctbrowser::script::compiler::compile("0;");
        check(cx.run(tiny).ok, "construction fixture runs");
        const auto * site =
            reinterpret_cast<const ctbrowser::aot::ct_aot_site *>(&tiny.functions[0]);
        alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
        auto * frame =
            ctbrowser::aot::ct_aot_enter(reinterpret_cast<ctbrowser::aot::ct_aot_ctx *>(&cx), site,
                                         1u, value::undefined().bits(), storage);
        check(frame != nullptr, "construction frame enters");
        if (frame == nullptr) { return 1; }
        ctbrowser::aot::ct_aot_handler_push(frame, 7u, 0u);
        std::uint64_t out = value::number(123).bits();
        const auto callee = cx.global("globalThis").bits();
        const auto status = static_cast<ctbrowser::aot::ct_aot_status>(
            spread ? ctbrowser::aot::ct_aot_construct_spread(frame, callee, cx.make_array().bits(),
                                                             site, &out)
                   : ctbrowser::aot::ct_aot_construct(frame, callee, nullptr, 0u, site, &out));
        check(status == ctbrowser::aot::ct_aot_status::caught, "non-constructor is catchable");
        check(out == value::number(123).bits(), "failed construction leaves output unchanged");
        if (status == ctbrowser::aot::ct_aot_status::caught) {
            std::uint64_t thrown = value::undefined().bits();
            check(ctbrowser::aot::ct_aot_catch_land(frame, &thrown) == 7u,
                  "construction reaches its catch pad");
            check(cx.to_string(cx.lookup_property(value::from_bits(thrown), "name")) == "TypeError",
                  "construction throws TypeError");
        } else if (status != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_handler_pop(frame);
        }
        if (status != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_leave(frame);
        }
    }

    // ct_aot_define_data defines, as op::define_data does: an inherited setter
    // does not run, and a non-extensible target is a catchable TypeError.
    {
        context cx;
        ctbrowser::script::install_builtins(cx);
        const auto fixture = ctbrowser::script::compiler::compile(
            "var hits = 0, child = Object.create({set x(v) { hits++; }}),"
            " closed = Object.preventExtensions({});");
        check(cx.run(fixture).ok, "definition fixture runs");
        const auto * site =
            reinterpret_cast<const ctbrowser::aot::ct_aot_site *>(&fixture.functions[0]);
        alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
        auto * frame =
            ctbrowser::aot::ct_aot_enter(reinterpret_cast<ctbrowser::aot::ct_aot_ctx *>(&cx), site,
                                         1u, value::undefined().bits(), storage);
        check(frame != nullptr, "definition frame enters");
        if (frame == nullptr) { return 1; }
        const value child = cx.global("child");
        auto status = static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_define_data(
            frame, child.bits(), cx.string("x").bits(), value::number(7).bits()));
        check(status == ctbrowser::aot::ct_aot_status::ok, "define_data succeeds");
        check(cx.global("hits").as_number() == 0.0, "the inherited setter does not run");
        context::property_descriptor own;
        check(cx.own_property(child, "x", own) && own.is_data() && own.writable && own.enumerable &&
                  own.configurable && own.held.as_number() == 7.0,
              "an own writable, enumerable, configurable data property");
        ctbrowser::aot::ct_aot_handler_push(frame, 9u, 0u);
        status = static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_define_data(
            frame, cx.global("closed").bits(), cx.string("y").bits(), value::number(1).bits()));
        check(status == ctbrowser::aot::ct_aot_status::caught,
              "a non-extensible target is catchable");
        if (status == ctbrowser::aot::ct_aot_status::caught) {
            std::uint64_t thrown = value::undefined().bits();
            check(ctbrowser::aot::ct_aot_catch_land(frame, &thrown) == 9u,
                  "definition reaches its catch pad");
            check(cx.to_string(cx.lookup_property(value::from_bits(thrown), "name")) == "TypeError",
                  "definition throws TypeError");
        } else if (status != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_handler_pop(frame);
        }
        if (status != ctbrowser::aot::ct_aot_status::unwound) {
            ctbrowser::aot::ct_aot_leave(frame);
        }
    }
    context ctx;
    ctbrowser::script::install_builtins(ctx);

    // Operands chosen to separate the seven, not to be representative. The
    // interesting ones are where two of the operators agree and a third does
    // not: a shift count above 31, a negative left operand (where >> and >>>
    // differ), a value above 2^31 (where to_int32 and to_uint32 differ), and
    // the two zeroes.
    const std::vector<value> operands = {
        value::number(0),
        value::number(-0.0),
        value::number(1),
        value::number(-1),
        value::number(2.5),
        value::number(-2.5),
        value::number(31),
        value::number(32),
        value::number(33),
        value::number(255),
        value::number(-255),
        value::number(2147483647),
        value::number(-2147483648.0),
        value::number(2147483648.0),
        value::number(4294967295.0),
        value::number(4294967296.0),
        value::number(std::nan("")),
        value::number(HUGE_VAL),
        value::number(-HUGE_VAL),
        value::boolean(true),
        value::boolean(false),
        value::undefined(),
        value::null(),
        ctx.string("3"),
        ctx.string("-7"),
        ctx.string("abc"),
        ctx.string(""),
        ctx.string("0x10"),
    };
    const op kinds[] = {op::add, op::bit_and, op::bit_or, op::bit_xor, op::shl, op::shr, op::ushr};

    std::size_t compared = 0;
    for (const op kind : kinds) {
        for (const value lhs : operands) {
            for (const value rhs : operands) {
                const value expected = reference(kind, lhs, rhs);
                const value got = ctx.binary_op_static(kind, lhs, rhs);
                ++compared;
                if (!same(expected, got)) {
                    check(false, std::string{name_of(kind)} + "(" + describe(ctx, lhs) + ", " +
                                     describe(ctx, rhs) + ") is " + describe(ctx, got) + ", was " +
                                     describe(ctx, expected));
                    return 1; // Report the first failure only, not thousands.
                }
            }
        }
    }
    check(compared == 7 * operands.size() * operands.size(), "every pair was compared");

    // ---- The re-entering family, over the same matrix ---------------------
    {
        const op reentering[] = {op::sub, op::mul,         op::div,   op::mod,
                                 op::pow, op::add_generic, op::concat};
        std::size_t pairs = 0;
        for (const op kind : reentering) {
            for (const value lhs : operands) {
                for (const value rhs : operands) {
                    const value expected = reference_reentering(ctx, kind, lhs, rhs);
                    const value got = ctx.binary_op(kind, lhs, rhs);
                    ++pairs;
                    const bool agree = (expected.is_string() && got.is_string())
                                           ? ctx.to_string(expected) == ctx.to_string(got)
                                           : same(expected, got);
                    if (!agree) {
                        check(false, std::string{name_of(kind)} + "(" + describe(ctx, lhs) + ", " +
                                         describe(ctx, rhs) + ") is " + describe(ctx, got) +
                                         ", was " + describe(ctx, expected));
                        return 1;
                    }
                }
            }
        }
        check(pairs == 7 * operands.size() * operands.size(),
              "every re-entering pair was compared");
        compared += pairs;

        // The three `+` opcodes stay distinct, pinned on the inputs that
        // separate them. `add` is the static form that never runs user code,
        // `add_generic` lets the operands decide, and `concat` unconditionally
        // ToStrings, so merging any two into one shows up here and nowhere
        // else in this file.
        const value one = value::number(1);
        const value two = value::number(2);
        check(ctx.to_string(ctx.binary_op_static(op::add, one, two)) == "3", "add is numeric");
        check(ctx.to_string(ctx.binary_op(op::add_generic, one, two)) == "3",
              "add_generic on two numbers is numeric too");
        check(ctx.to_string(ctx.binary_op(op::concat, one, two)) == "12",
              "AND concat IS NOT - it ToStrings both sides unconditionally");
        const value text = ctx.string("x");
        check(ctx.to_string(ctx.binary_op(op::add_generic, one, text)) == "1x",
              "add_generic concatenates when either side is a string");
        check(ctx.binary_op_static(op::add, one, text).is_number(),
              "and `add` does not - it is ToNumber on both sides, which is NaN here");
    }

    // The seven are actually different functions. A switch whose arms were all
    // accidentally `add` would pass everything above if the reference were
    // wrong in the same way, since it is transcribed from the same source. This
    // pins the answers themselves, on one pair on which no two operators agree.
    {
        const value lhs = value::number(-9);
        const value rhs = value::number(34); // A shift count of 2 after & 31.
        struct expectation {
            op kind;
            double answer;
        };
        const expectation pinned[] = {
            {op::add, 25.0},  {op::bit_and, 34.0}, {op::bit_or, -9.0},       {op::bit_xor, -43.0},
            {op::shl, -36.0}, {op::shr, -3.0},     {op::ushr, 1073741821.0},
        };
        for (const expectation & e : pinned) {
            const value got = ctx.binary_op_static(e.kind, lhs, rhs);
            check(got.is_number() && got.as_number() == e.answer,
                  std::string{"-9 "} + name_of(e.kind) + " 34 is " + describe(ctx, got) + ", not " +
                      std::to_string(e.answer));
        }
    }

    // ---- The ABI helper is the same function -----------------------------
    //
    // ct_aot_binary_op_static calls context::binary_op_static, so this checks
    // the plumbing: an op_kind out of an image reaches the right arm, and an
    // out-of-range one is refused rather than indexing a switch with it.
    {
        ctbrowser::script::program tiny =
            ctbrowser::script::compiler::compile("function host() { return 1; }\n");
        check(tiny.ok, "the host fixture compiles");
        (void)ctx.run(tiny);

        // A frame is needed, and ct_aot_enter is how one is made.
        alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
        ctbrowser::aot::ct_aot_frame * frame = ctbrowser::aot::ct_aot_enter(
            reinterpret_cast<ctbrowser::aot::ct_aot_ctx *>(&ctx),
            reinterpret_cast<const ctbrowser::aot::ct_aot_site *>(&tiny.functions[0]), 4u,
            value::undefined().bits(), storage);
        check(frame != nullptr, "a frame is entered");
        if (frame == nullptr) { return 1; }

        for (const op kind : kinds) {
            std::uint64_t out = value::undefined().bits();
            const auto status =
                static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_binary_op_static(
                    frame, static_cast<std::uint32_t>(kind), value::number(-9).bits(),
                    value::number(34).bits(), &out));
            check(status == ctbrowser::aot::ct_aot_status::ok,
                  std::string{"the helper succeeds for "} + name_of(kind));
            check(same(value::from_bits(out),
                       ctx.binary_op_static(kind, value::number(-9), value::number(34))),
                  std::string{"the helper agrees with the interpreter for "} + name_of(kind));
        }

        // An out-of-range op kind. `op` has underlying type uint8_t, so a
        // careless cast would truncate 256 + bit_or to bit_or and compute
        // `1 | 2` = 3. The value is chosen to truncate to a real opcode; one
        // that truncated past the end of the opcode list would pass even
        // without the range check.
        const auto disguised = 256u + static_cast<std::uint32_t>(op::bit_or);
        std::uint64_t out = value::number(1234).bits();
        const auto status =
            static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_binary_op_static(
                frame, disguised, value::number(1).bits(), value::number(2).bits(), &out));
        check(status == ctbrowser::aot::ct_aot_status::ok, "an unknown op kind does not fail");
        check(value::from_bits(out).is_undefined(),
              "AND IT IS REFUSED RATHER THAN TRUNCATED INTO A REAL OPCODE");
        ctbrowser::aot::ct_aot_leave(frame);
    }

    // ---- Helpers that wrap existing runtime functions --------------------
    //
    // They cannot change the interpreter, but they can be wired to the wrong
    // function or encode their answer differently from what
    // include/ctbrowser/aot/aot.hpp declares. For ct_aot_compare the encoding
    // is the contract: backends lower the four relational operators as
    // comparisons against the ordering it writes.
    {
        ctbrowser::script::program tiny =
            ctbrowser::script::compiler::compile("function host() { return 1; }\n"
                                                 "var arr = [10, 20, 30];\n"
                                                 "var holder = { width: 640 };\n");
        check(tiny.ok, "the host fixture compiles");
        (void)ctx.run(tiny);
        alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
        ctbrowser::aot::ct_aot_frame * frame = ctbrowser::aot::ct_aot_enter(
            reinterpret_cast<ctbrowser::aot::ct_aot_ctx *>(&ctx),
            reinterpret_cast<const ctbrowser::aot::ct_aot_site *>(&tiny.functions[0]), 4u,
            value::undefined().bits(), storage);
        check(frame != nullptr, "a frame is entered");
        if (frame == nullptr) { return 1; }

        // Truthiness, over the cases that are not the obvious ones.
        struct truth {
            value v;
            bool expected;
        };
        const truth truths[] = {
            {value::number(0), false},
            {value::number(-0.0), false},
            {value::number(std::nan("")), false},
            {value::number(1), true},
            {value::number(-1), true},
            {value::undefined(), false},
            {value::null(), false},
            {value::boolean(false), false},
            {value::boolean(true), true},
            {ctx.string(""), false},
            {ctx.string("0"), true},
            {ctx.string("false"), true},
        };
        for (const truth & t : truths) {
            const bool got = ctbrowser::aot::ct_aot_truthy(t.v.bits()) != 0u;
            check(got == t.expected,
                  "truthy(" + describe(ctx, t.v) + ") is " + (got ? "true" : "false"));
        }

        // Ordering, where NaN is the point. It is unordered with everything,
        // including itself, which makes all four relational operators false
        // against it. So they cannot be lowered as negations of one another,
        // and a helper that returned `equivalent` or `greater` for it would
        // make `NaN <= NaN` true.
        struct comparison {
            value lhs, rhs;
            ctbrowser::aot::ct_aot_ordering expected;
        };
        const value nan = value::number(std::nan(""));
        const comparison comparisons[] = {
            {value::number(1), value::number(2), ctbrowser::aot::ct_aot_ordering::less},
            {value::number(2), value::number(1), ctbrowser::aot::ct_aot_ordering::greater},
            {value::number(2), value::number(2), ctbrowser::aot::ct_aot_ordering::equivalent},
            {nan, value::number(1), ctbrowser::aot::ct_aot_ordering::unordered},
            {value::number(1), nan, ctbrowser::aot::ct_aot_ordering::unordered},
            {nan, nan, ctbrowser::aot::ct_aot_ordering::unordered},
            // Two strings compare as strings, not as numbers ("10" < "9").
            // A numeric-only fast path in a backend would make every string
            // comparison false.
            {ctx.string("a"), ctx.string("b"), ctbrowser::aot::ct_aot_ordering::less},
            {ctx.string("10"), ctx.string("9"), ctbrowser::aot::ct_aot_ordering::less},
            {value::number(10), value::number(9), ctbrowser::aot::ct_aot_ordering::greater},
        };
        for (const comparison & c : comparisons) {
            std::int32_t ordering = 99;
            const auto status = static_cast<ctbrowser::aot::ct_aot_status>(
                ctbrowser::aot::ct_aot_compare(frame, c.lhs.bits(), c.rhs.bits(), &ordering));
            check(status == ctbrowser::aot::ct_aot_status::ok, "compare succeeds");
            check(ordering == static_cast<std::int32_t>(c.expected),
                  "compare(" + describe(ctx, c.lhs) + ", " + describe(ctx, c.rhs) + ") is " +
                      std::to_string(ordering));
        }
        // The numeric values themselves, because a backend derives `<=` as
        // (ord == -1 || ord == 0) from them.
        check(static_cast<std::int32_t>(ctbrowser::aot::ct_aot_ordering::less) == -1 &&
                  static_cast<std::int32_t>(ctbrowser::aot::ct_aot_ordering::equivalent) == 0 &&
                  static_cast<std::int32_t>(ctbrowser::aot::ct_aot_ordering::greater) == 1 &&
                  static_cast<std::int32_t>(ctbrowser::aot::ct_aot_ordering::unordered) == 2,
              "the ordering constants are the ones the row writes down");

        // Strict against loose equality, on the pairs where they disagree.
        struct equality {
            value a, b;
            bool strict, loose;
        };
        const equality equalities[] = {
            {value::number(1), ctx.string("1"), false, true},
            {value::null(), value::undefined(), false, true},
            {nan, nan, false, false},
            {value::number(0), value::number(-0.0), true, true},
            {value::number(1), value::number(1), true, true},
        };
        for (const equality & e : equalities) {
            const bool strict = ctbrowser::aot::ct_aot_strict_equals(e.a.bits(), e.b.bits()) != 0u;
            std::uint32_t loose = 99;
            (void)ctbrowser::aot::ct_aot_loose_equals(frame, e.a.bits(), e.b.bits(), &loose);
            check(strict == e.strict, describe(ctx, e.a) + " === " + describe(ctx, e.b));
            check((loose != 0u) == e.loose, describe(ctx, e.a) + " == " + describe(ctx, e.b));
        }

        // typeof without allocating: ct_aot_type_of_name returns a pointer and
        // a length so that `typeof x === 'function'` compiles to a length
        // compare and a memcmp.
        {
            const char * text = nullptr;
            const std::uint32_t length =
                ctbrowser::aot::ct_aot_type_of_name(value::number(1).bits(), &text);
            check(text != nullptr && length == 6 && std::string_view{text, length} == "number",
                  "typeof 1 is \"number\", as a length and a pointer");
            const std::uint32_t undef =
                ctbrowser::aot::ct_aot_type_of_name(value::undefined().bits(), &text);
            check(std::string_view{text, undef} == "undefined", "and typeof undefined");
        }

        // The allocating helpers produce the kinds they say they do.
        {
            const value made = value::from_bits(ctbrowser::aot::ct_aot_new_object(frame));
            check(made.is_object(), "new_object makes an object");
            const value list = value::from_bits(ctbrowser::aot::ct_aot_new_array(frame, 8u));
            check(list.is_object_like(), "new_array makes an array");
            const value cell =
                value::from_bits(ctbrowser::aot::ct_aot_cell_new(frame, value::number(7).bits()));
            // A cell is not "object-like": it is a box a closure shares, not
            // anything a page can see, so it answers no to every is_object
            // question. This asks for its kind instead.
            check(cell.is_kind(ctbrowser::script::heap_kind::cell), "cell_new makes a cell");
        }

        // An indexed read, which needs no interned name. The array comes from
        // the fixture above: do not call `run` here, because it starts a new
        // turn and clears frames_, destroying the frame this block holds
        // (run_reentrant exists for that case).
        {
            std::uint64_t out = value::undefined().bits();
            const auto status =
                static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_get_index(
                    frame, ctx.global("arr").bits(), value::number(1).bits(), nullptr, &out));
            check(status == ctbrowser::aot::ct_aot_status::ok, "get_index succeeds");
            check(value::from_bits(out).is_number() && value::from_bits(out).as_number() == 20.0,
                  "and reads arr[1]");
        }

        // ---- Interned names, which key every property helper --------------
        //
        // Every property helper in the ABI takes its key as a
        // `const ct_aot_name *`. A name is an owning, immortal record, not the
        // runtime's non-owning `prehashed_name` ({string_view, hash}), so it
        // cannot dangle when the caller's characters go away.
        {
            const char * text = "colour";
            const ctbrowser::aot::ct_aot_name * first =
                ctbrowser::aot::ct_aot_intern_name(text, 6u);
            check(first != nullptr, "a name interns");

            // Interning is idempotent by address: two sites naming the same
            // property must be able to compare their handles as pointers.
            const ctbrowser::aot::ct_aot_name * again =
                ctbrowser::aot::ct_aot_intern_name(text, 6u);
            check(first == again, "AND INTERNING IT AGAIN GIVES THE SAME POINTER");

            std::string elsewhere = "colour";
            check(ctbrowser::aot::ct_aot_intern_name(elsewhere.data(), 6u) == first,
                  "interning the same text from another buffer gives the same pointer");

            // The caller's buffer is not the record. A name is interned first
            // from a string that is then overwritten, and interning the same
            // text again must still find it. An index keyed by the caller's
            // characters, rather than by a view into the record's own copy,
            // would hold a key that silently changed, so the second interning
            // would miss and make a second record. Since backends compare
            // handles by address, that would split one property into two.
            const ctbrowser::aot::ct_aot_name * from_temporary = nullptr;
            {
                std::string temporary = "ephemeral-property-name";
                from_temporary = ctbrowser::aot::ct_aot_intern_name(
                    temporary.data(), static_cast<std::uint32_t>(temporary.size()));
                temporary.assign(std::string(64, 'x')); // Reallocates and overwrites.
            }
            check(ctbrowser::aot::ct_aot_intern_name("ephemeral-property-name", 23u) ==
                      from_temporary,
                  "A NAME OUTLIVES THE BUFFER IT WAS INTERNED FROM");

            check(ctbrowser::aot::ct_aot_intern_name("colours", 7u) != first,
                  "a longer name is a different name");
            check(ctbrowser::aot::ct_aot_intern_name("colour", 5u) != first,
                  "and so is a prefix - the length is honoured, not a NUL");

            // A name with an embedded NUL, which a JavaScript property may have
            // and a C string cannot express; this is why ct_aot_intern_name
            // takes a length.
            const ctbrowser::aot::ct_aot_name * nulled =
                ctbrowser::aot::ct_aot_intern_name("a\0b", 3u);
            check(nulled != ctbrowser::aot::ct_aot_intern_name("a", 1u),
                  "an embedded NUL does not truncate the name");
        }

        // ---- A compiled body reading and writing a property ---------------
        {
            const value obj = ctx.global("holder");
            check(obj.is_object(), "the fixture object exists");

            const ctbrowser::aot::ct_aot_name * width =
                ctbrowser::aot::ct_aot_intern_name("width", 5u);
            std::uint64_t out = value::undefined().bits();
            auto status = static_cast<ctbrowser::aot::ct_aot_status>(
                ctbrowser::aot::ct_aot_get_prop(frame, obj.bits(), width, nullptr, &out));
            check(status == ctbrowser::aot::ct_aot_status::ok, "get_prop succeeds");
            check(value::from_bits(out).is_number() && value::from_bits(out).as_number() == 640.0,
                  "and reads holder.width");

            status = static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_set_prop(
                frame, obj.bits(), width, value::number(800).bits(), nullptr));
            check(status == ctbrowser::aot::ct_aot_status::ok, "set_prop succeeds");
            // Read back through the interpreter, not through the helper that
            // just wrote it: a get and a set that agree only with each other
            // would look identical to a pair that works.
            check(ctx.lookup_property(obj, "width").as_number() == 800.0,
                  "AND THE INTERPRETER SEES THE NEW VALUE");

            // A missing property is undefined, not a failure.
            const ctbrowser::aot::ct_aot_name * absent =
                ctbrowser::aot::ct_aot_intern_name("nope", 4u);
            status = static_cast<ctbrowser::aot::ct_aot_status>(
                ctbrowser::aot::ct_aot_get_prop(frame, obj.bits(), absent, nullptr, &out));
            check(status == ctbrowser::aot::ct_aot_status::ok,
                  "a missing property is not an error");
            check(value::from_bits(out).is_undefined(), "and reads undefined");
        }

        // ct_aot_failed is the loop back-edge poll, so it has to be false when
        // nothing has gone wrong: a helper wired to the wrong bool would make
        // every compiled loop exit on its first iteration.
        check(ctbrowser::aot::ct_aot_failed(frame) == 0u, "nothing has failed");

        ctbrowser::aot::ct_aot_leave(frame);
    }

    if (ctbrowser_test_failures == 0) {
        std::printf("ok aot_helpers (%zu operand pairs across seven operations)\n", compared);
    }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
