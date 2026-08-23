// ctbrowser.aot: an extracted helper computes what the code it replaced did.
//
// Phase 5 moves the VM's semantics into helpers the AOT backend shares, and its
// discipline is "the VM's observable behaviour must not change at all". The
// full suite passing is the first half of that. This is the second: a
// REFERENCE IMPLEMENTATION, transcribed from the handlers as they were before
// the extraction, run against the extracted function over a matrix of operands.
//
// It exists because of what a bad extraction of SEVEN BYTE-IDENTICAL HANDLERS
// actually looks like. They differ only in one operator each - `&` against `|`,
// `<<` against `>>`, to_int32 against to_uint32 - so the plausible mistake is
// not a crash or a missing case, it is a transposed line in the switch. A
// program that never shifts by a negative amount, or never ORs, passes a whole
// suite over it.
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

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++ctbrowser_test_failures;
    }
}

// THE SEVEN HANDLERS AS THEY WERE, minus the BigInt arm - which is a private
// member the extraction did not touch and this cannot call. Every operand below
// is chosen so that arm does not fire, which leaves exactly the part that MOVED:
// the static conversion and the operator.
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

// AND THE SEVEN RE-ENTERING ONES AS THEY WERE. Same rule as above: the BigInt
// arm is private, so every operand pair below is chosen to miss it, leaving
// exactly the part that moved. `concat` and `add_generic` are transcribed
// whole, because their shapes are the two this extraction could most easily
// have flattened into each other.
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
        // NO ToPrimitive HERE, and the omission is the reason the operand
        // matrix contains no objects: `context::to_primitive` is private, and
        // adding a test-only accessor to the engine to reach it would be
        // changing the thing under test to suit the test. On primitives
        // ToPrimitive is the identity, so this arm is the extracted one exactly.
        //
        // WHAT THAT LEAVES UNCOVERED, said rather than glossed: the
        // valueOf/toString walk that makes this family re-entering at all.
        // `[] - 0` against `-[]`, `{valueOf: () => 42} + 1` and their kin are
        // covered by the JavaScript suite, which runs them as source.
        if (lhs.is_string() || rhs.is_string()) {
            return cx.string(cx.to_string(lhs) + cx.to_string(rhs));
        }
        return value::number(context::to_number(lhs) + context::to_number(rhs));
    }
    default: break;
    }
    return value::undefined();
}

// NaN-aware, because two NaNs are not `==` and both arms produce them.
bool same(value a, value b) {
    if (a.is_number() && b.is_number()) {
        const double x = a.as_number();
        const double y = b.as_number();
        if (std::isnan(x) && std::isnan(y)) { return true; }
        // -0 and 0 compare equal as doubles and are DIFFERENT values here, so
        // the sign bit is compared too: `-0 | 0` is 0 and `-0 + -0` is -0.
        if (x == 0.0 && y == 0.0) { return std::signbit(x) == std::signbit(y); }
        return x == y;
    }
    return a.bits() == b.bits();
}

std::string describe(context & cx, value v) {
    if (v.is_string()) { return "\"" + cx.to_string(v) + "\""; }
    // -0 PRINTS AS "0", so a failure between the two zeroes reads "is 0, was 0"
    // - which is the least useful message a differential can produce. It came
    // up on the first run of the mod case.
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
    context ctx;
    ctbrowser::script::install_builtins(ctx);

    // OPERANDS CHOSEN TO SEPARATE THE SEVEN, not to be representative. The
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
                    return 1; // one failure is the whole story; 5,488 are not
                }
            }
        }
    }
    check(compared == 7 * operands.size() * operands.size(), "every pair was compared");

    // ---- AND THE RE-ENTERING FAMILY, over the same matrix ------------------
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

        // THE THREE `+` OPCODES STAY THREE, pinned on the one input that
        // separates them. `add` is the static immediate that never runs user
        // code, `add_generic` lets the operands decide, and `concat`
        // unconditionally ToStrings - so a flattening of any two into one shows
        // up here and nowhere else in this file.
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

    // AND THE SEVEN ARE ACTUALLY DIFFERENT FUNCTIONS. A switch whose arms were
    // all accidentally `add` would pass everything above, because the reference
    // would be wrong in exactly the same way - it is transcribed from the same
    // source. This pins the answers themselves, on one pair chosen so that no
    // two operators agree on it.
    {
        const value lhs = value::number(-9);
        const value rhs = value::number(34); // a shift count of 2 after & 31
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

    // ---- AND THE ABI HELPER IS THE SAME FUNCTION --------------------------
    //
    // Not a second implementation agreeing with the first: ct_aot_binary_op_static
    // calls context::binary_op_static. What this checks is the PLUMBING - that
    // an op_kind out of an image reaches the right arm, and that an out-of-range
    // one is refused rather than indexing a switch with it.
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

        // AN OP KIND AN IMAGE MADE UP, AND THE VALUE IS CHOSEN WITH CARE.
        //
        // `op` is an enum class with underlying type uint8_t, so casting a
        // uint32 to it TRUNCATES. An out-of-range kind is therefore not
        // harmlessly out of range - it becomes some OTHER opcode, and the
        // helper computes a real answer for an operation the image did not ask
        // for. 256 + bit_or truncates to exactly bit_or, so without the range
        // check this returns 3 for `1 | 2` and looks perfectly reasonable.
        //
        // A first attempt at this used 60000, which truncates to 96 - past the
        // end of the opcode list - so the switch fell through to undefined and
        // the check looked load-bearing when it was not.
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

    // ---- THE ROWS THAT ARE SHIMS OVER FUNCTIONS THAT ALREADY EXISTED ------
    //
    // Each of these adds a symbol and touches no handler, so it cannot change
    // the interpreter - but it can still be wired to the WRONG function, or
    // encode its answer differently from the row it implements. That is what is
    // checked here, and for ct_aot_compare it is the whole substance: the four
    // relational opcodes are constant comparisons against the ordering it
    // writes, so the numbers are part of the contract.
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

        // TRUTHINESS, over the cases that are not the obvious ones.
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

        // ORDERING, and the whole point is NaN. It is unordered with
        // everything INCLUDING ITSELF, which is what makes all four relational
        // operators false against it - so they cannot be lowered as negations
        // of one another, and a helper that returned `equivalent` or `greater`
        // for it would make `NaN <= NaN` true.
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
            // TWO STRINGS COMPARE AS BYTES, not as numbers. A numeric-only fast
            // path in a backend silently reintroduces the all-false string
            // comparison this pins.
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
        // AND THE NUMBERS THEMSELVES, because a backend derives `<=` as
        // (ord == -1 || ord == 0) from them.
        check(static_cast<std::int32_t>(ctbrowser::aot::ct_aot_ordering::less) == -1 &&
                  static_cast<std::int32_t>(ctbrowser::aot::ct_aot_ordering::equivalent) == 0 &&
                  static_cast<std::int32_t>(ctbrowser::aot::ct_aot_ordering::greater) == 1 &&
                  static_cast<std::int32_t>(ctbrowser::aot::ct_aot_ordering::unordered) == 2,
              "the ordering constants are the ones the row writes down");

        // STRICT AGAINST LOOSE, on the three pairs where they disagree.
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

        // typeof, WITHOUT ALLOCATING. The row's whole argument for splitting
        // this into two helpers is that `typeof x === 'function'` should be a
        // length compare and a memcmp.
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

        // THE ALLOCATING ONES produce the kinds they say they do.
        {
            const value made = value::from_bits(ctbrowser::aot::ct_aot_new_object(frame));
            check(made.is_object(), "new_object makes an object");
            const value list = value::from_bits(ctbrowser::aot::ct_aot_new_array(frame, 8u));
            check(list.is_object_like(), "new_array makes an array");
            const value cell =
                value::from_bits(ctbrowser::aot::ct_aot_cell_new(frame, value::number(7).bits()));
            // A CELL IS NOT "OBJECT-LIKE". It is a box a closure shares, not
            // anything a page can see, so it answers no to every is_object
            // question - which is why this asks for its kind instead.
            check(cell.is_kind(ctbrowser::script::heap_kind::cell), "cell_new makes a cell");
        }

        // AN INDEXED READ, which needs no interned name and so is reachable
        // before ct_aot_intern_name exists.
        //
        // THE ARRAY IS BUILT IN THE FIXTURE ABOVE, before the frame is entered.
        // The first version ran a second program HERE, which fails: `run` is
        // the entry point for a whole TURN and clears frames_, so it destroyed
        // the frame this block is holding and ct_aot_check correctly reported
        // it unwound. That is the hazard run_reentrant exists for, met from the
        // other side.
        {
            std::uint64_t out = value::undefined().bits();
            const auto status =
                static_cast<ctbrowser::aot::ct_aot_status>(ctbrowser::aot::ct_aot_get_index(
                    frame, ctx.global("arr").bits(), value::number(1).bits(), nullptr, &out));
            check(status == ctbrowser::aot::ct_aot_status::ok, "get_index succeeds");
            check(value::from_bits(out).is_number() && value::from_bits(out).as_number() == 20.0,
                  "and reads arr[1]");
        }

        // ---- INTERNED NAMES, WHICH EVERY PROPERTY HELPER IS KEYED BY -----
        //
        // The row that blocked the whole property family: every key parameter
        // in the ABI is a `const ct_aot_name *`, and nothing produced one. It
        // is an OWNING immortal record rather than the runtime's own
        // `prehashed_name`, which is a {string_view, hash} - non-owning, so a
        // pool built out of one dangles the moment the characters go away.
        {
            const char * text = "colour";
            const ctbrowser::aot::ct_aot_name * first =
                ctbrowser::aot::ct_aot_intern_name(text, 6u);
            check(first != nullptr, "a name interns");

            // IDEMPOTENT, and by ADDRESS. That is the property a backend
            // depends on: two sites naming the same property must be able to
            // compare their handles as pointers.
            const ctbrowser::aot::ct_aot_name * again =
                ctbrowser::aot::ct_aot_intern_name(text, 6u);
            check(first == again, "AND INTERNING IT AGAIN GIVES THE SAME POINTER");

            std::string elsewhere = "colour";
            check(ctbrowser::aot::ct_aot_intern_name(elsewhere.data(), 6u) == first,
                  "interning the same text from another buffer gives the same pointer");

            // THE CALLER'S BUFFER IS NOT THE RECORD, and this is the case that
            // proves it: a name is interned FIRST from a string that is then
            // overwritten, and interning the same text again must still find
            // it. An index keyed by the characters the caller supplied - rather
            // than by a view into the record's own copy - holds a key that has
            // silently changed underneath it, so the second interning misses
            // and produces a second record for one name.
            //
            // A backend compares name handles by ADDRESS, so two records for
            // one name is two properties that are not the same property.
            const ctbrowser::aot::ct_aot_name * from_temporary = nullptr;
            {
                std::string temporary = "ephemeral-property-name";
                from_temporary = ctbrowser::aot::ct_aot_intern_name(
                    temporary.data(), static_cast<std::uint32_t>(temporary.size()));
                temporary.assign(std::string(64, 'x')); // reallocates and overwrites
            }
            check(ctbrowser::aot::ct_aot_intern_name("ephemeral-property-name", 23u) ==
                      from_temporary,
                  "A NAME OUTLIVES THE BUFFER IT WAS INTERNED FROM");

            check(ctbrowser::aot::ct_aot_intern_name("colours", 7u) != first,
                  "a longer name is a different name");
            check(ctbrowser::aot::ct_aot_intern_name("colour", 5u) != first,
                  "and so is a prefix - the length is honoured, not a NUL");

            // A NAME WITH AN EMBEDDED NUL, which a JavaScript property may have
            // and a C string cannot express. This is why the row takes a length.
            const ctbrowser::aot::ct_aot_name * nulled =
                ctbrowser::aot::ct_aot_intern_name("a\0b", 3u);
            check(nulled != ctbrowser::aot::ct_aot_intern_name("a", 1u),
                  "an embedded NUL does not truncate the name");
        }

        // ---- AND A COMPILED BODY READING AND WRITING A PROPERTY ----------
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
            // ASKED THROUGH THE INTERPRETER, not through the helper that just
            // wrote it: a get and a set that agree with each other and with
            // nothing else would look identical to a pair that works.
            check(ctx.lookup_property(obj, "width").as_number() == 800.0,
                  "AND THE INTERPRETER SEES THE NEW VALUE");

            // A MISSING PROPERTY IS undefined, not a failure.
            const ctbrowser::aot::ct_aot_name * absent =
                ctbrowser::aot::ct_aot_intern_name("nope", 4u);
            status = static_cast<ctbrowser::aot::ct_aot_status>(
                ctbrowser::aot::ct_aot_get_prop(frame, obj.bits(), absent, nullptr, &out));
            check(status == ctbrowser::aot::ct_aot_status::ok,
                  "a missing property is not an error");
            check(value::from_bits(out).is_undefined(), "and reads undefined");
        }

        // ct_aot_failed IS THE BACK-EDGE POLL, so it has to be false when
        // nothing has gone wrong - a helper wired to the wrong bool would make
        // every compiled loop exit on its first iteration.
        check(ctbrowser::aot::ct_aot_failed(frame) == 0u, "nothing has failed");

        ctbrowser::aot::ct_aot_leave(frame);
    }

    if (ctbrowser_test_failures == 0) {
        std::printf("ok aot_helpers (%zu operand pairs across seven operations)\n", compared);
    }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
