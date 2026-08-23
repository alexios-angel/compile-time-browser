#pragma once
// WHICH BYTECODE OPCODE A CTJS KIND MEANS.
//
// `uint32_t op_kind` in the ABI is a `ctbrowser::script::op` - aot_bridge.cpp
// does `static_cast<op>(op_kind)` and dispatches the interpreter's own switch on
// it. So this file is the difference between a compiler that calls the right
// operator and one that calls whatever `op(5)` happens to be.
//
// EVERY ANSWER IS SPELLED BY NAME. Not one numeric literal appears below, which
// is the whole point: Phases 13 and 14 renumber `enum class op` deliberately,
// and a renumbering must be a build error here rather than a silent change of
// meaning. A table of integers would survive it and compile `**` into `%`.
#include <ctbrowser/script/bytecode.hpp>

#include "ctcompile/CTJS/IR/CTJSEnums.h"

namespace ctcompile::ctjs {

// THE SAME KIND MEANS TWO DIFFERENT OPCODES, which is why this is two functions
// rather than one table. `ctjs.binary add` is source `+` and compiles to
// op::add_generic - which runs ToPrimitive and can call a user valueOf.
// `ctjs.binary_static add` is op::add, which uses the static conversions and
// cannot run user code at all; the runtime reaches it from `++` and three
// internal counters. Folding them would make `x + y` and `x++` the same call.
[[nodiscard]] constexpr ctbrowser::script::op opcode_for_binary(BinaryKind kind) {
    using op = ctbrowser::script::op;
    switch (kind) {
    case BinaryKind::Add: return op::add_generic;
    case BinaryKind::Sub: return op::sub;
    case BinaryKind::Mul: return op::mul;
    case BinaryKind::Div: return op::div;
    case BinaryKind::Mod: return op::mod;
    case BinaryKind::Pow: return op::pow;
    case BinaryKind::Concat: return op::concat;
    // THE SEVEN STATIC-ONLY KINDS ARE NOT VALID HERE. ct_aot_binary_op's switch
    // has no arm for a bitwise opcode, so passing one would fall through to a
    // default that returns undefined - an answer, silently wrong. The caller
    // checks this rather than the callee guessing.
    case BinaryKind::Shl:
    case BinaryKind::Shr:
    case BinaryKind::UShr:
    case BinaryKind::BitAnd:
    case BinaryKind::BitOr:
    case BinaryKind::BitXor: break;
    }
    return op::halt; // never valid as a binary operator; see is_valid_binary
}

[[nodiscard]] constexpr ctbrowser::script::op opcode_for_binary_static(BinaryKind kind) {
    using op = ctbrowser::script::op;
    switch (kind) {
    case BinaryKind::Add: return op::add;
    case BinaryKind::Shl: return op::shl;
    case BinaryKind::Shr: return op::shr;
    case BinaryKind::UShr: return op::ushr;
    case BinaryKind::BitAnd: return op::bit_and;
    case BinaryKind::BitOr: return op::bit_or;
    case BinaryKind::BitXor: return op::bit_xor;
    // And the seven re-entering ones are not valid here, symmetrically.
    case BinaryKind::Sub:
    case BinaryKind::Mul:
    case BinaryKind::Div:
    case BinaryKind::Mod:
    case BinaryKind::Pow:
    case BinaryKind::Concat: break;
    }
    return op::halt;
}

[[nodiscard]] constexpr bool is_valid_binary(BinaryKind kind) {
    return opcode_for_binary(kind) != ctbrowser::script::op::halt;
}

[[nodiscard]] constexpr bool is_valid_binary_static(BinaryKind kind) {
    return opcode_for_binary_static(kind) != ctbrowser::script::op::halt;
}

// THE ASSERTIONS ARE THE TEST, and deliberately: "Invariants become tests under
// tests/lint/ or ctcompile/test/, or static_asserts. PREFER A BUILD ERROR TO A
// TEST." Every claim below is decidable at compile time, so it is decided at
// compile time, in every translation unit that includes this - which is every
// one that could get it wrong.
//
// It also keeps the claim where the mapping is. A runtime test would have to
// live in a file that links MLIR, because BinaryKind is generated from the ODS;
// the assertions cost nothing and travel with the header.

// THE ONE KIND BOTH FAMILIES ANSWER, AND THEY ANSWER DIFFERENTLY. This is the
// pair that would be invisible in a table of integers.
static_assert(opcode_for_binary(BinaryKind::Add) == ctbrowser::script::op::add_generic,
              "source `+` is add_generic - it runs ToPrimitive and can call a user valueOf");
static_assert(opcode_for_binary_static(BinaryKind::Add) == ctbrowser::script::op::add,
              "and the static family's `add` is op::add, which cannot run user code at all");
static_assert(opcode_for_binary(BinaryKind::Add) != opcode_for_binary_static(BinaryKind::Add),
              "so folding the two families would make `x + y` and `x++` the same call");

// AND EVERY OTHER KIND, so a renumbering of `enum class op` - which Phases 13
// and 14 do deliberately - cannot quietly change what any of them means.
static_assert(opcode_for_binary(BinaryKind::Sub) == ctbrowser::script::op::sub);
static_assert(opcode_for_binary(BinaryKind::Mul) == ctbrowser::script::op::mul);
static_assert(opcode_for_binary(BinaryKind::Div) == ctbrowser::script::op::div);
static_assert(opcode_for_binary(BinaryKind::Mod) == ctbrowser::script::op::mod);
static_assert(opcode_for_binary(BinaryKind::Pow) == ctbrowser::script::op::pow,
              "`**` is context::exponentiate, not libm's pow - the specification's edge cases "
              "and C's do not agree");
static_assert(opcode_for_binary(BinaryKind::Concat) == ctbrowser::script::op::concat);
static_assert(opcode_for_binary_static(BinaryKind::Shl) == ctbrowser::script::op::shl);
static_assert(opcode_for_binary_static(BinaryKind::Shr) == ctbrowser::script::op::shr);
static_assert(opcode_for_binary_static(BinaryKind::UShr) == ctbrowser::script::op::ushr);
static_assert(opcode_for_binary_static(BinaryKind::BitAnd) == ctbrowser::script::op::bit_and);
static_assert(opcode_for_binary_static(BinaryKind::BitOr) == ctbrowser::script::op::bit_or);
static_assert(opcode_for_binary_static(BinaryKind::BitXor) == ctbrowser::script::op::bit_xor);

// A KIND THE FAMILY DOES NOT SERVE IS REFUSED rather than guessed:
// ct_aot_binary_op's switch has no arm for a bitwise opcode, so passing one
// would return undefined - an answer, silently wrong.
static_assert(!is_valid_binary(BinaryKind::BitAnd), "the bitwise kinds are static-only");
static_assert(!is_valid_binary_static(BinaryKind::Concat),
              "and concat is re-entering only - it must never reach the BigInt arm");

} // namespace ctcompile::ctjs
