//===- OpcodeMapping.hpp - CTJS kinds to bytecode opcodes -------*- C++ -*-===//
//
// Maps each CTJS operator kind to the bytecode opcode it means.
//
// `uint32_t op_kind` in the ABI is a `ctbrowser::script::op`:
// aot_bridge/operators.cpp does `static_cast<op>(op_kind)` and dispatches the
// interpreter's own switch on it. So this file decides whether compiled code
// calls the right operator or whatever `op(5)` happens to be.
//
// Every opcode is spelled by name, never as a number: `enum class op` is
// renumbered whenever opcodes are added or reordered, and a renumbering must
// be a build error here rather than a silent change of meaning. A table of
// integers would survive it and compile `**` into `%`.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <ctbrowser/script/bytecode.hpp>

#include "ctcompile/CTJS/IR/CTJSEnums.h"

#include <iterator>
#include <string_view>

namespace ctcompile::ctjs {

/// Every opcode's name, expanded from the same .def the VM decodes with, so the
/// table cannot disagree with the enum.
#define CT_OPCODE(name_, ...) #name_,
inline constexpr std::string_view opcode_names[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};
#undef CT_OPCODE

static_assert(std::size(opcode_names) == ctbrowser::script::opcode_count,
              "the opcode-name table and `enum class op` disagree");

/// The name of `code`, or "<unknown>" for a value outside the enum.
[[nodiscard]] constexpr std::string_view opcode_name(ctbrowser::script::op code) {
    const auto index = static_cast<std::size_t>(code);
    return index < std::size(opcode_names) ? opcode_names[index] : "<unknown>";
}

/// Returns true for the six bitwise kinds.
///
/// The same kind can mean two different opcodes, which is why there are two
/// mapping functions rather than one table. `ctjs.binary add` is source `+` and
/// compiles to op::add_generic, which runs ToPrimitive and can call a user
/// valueOf. `ctjs.binary_static add` is op::add, which uses the static
/// conversions and cannot run user code; the runtime reaches it from `++` and
/// three internal counters. Folding them would make `x + y` and `x++` the same
/// call.
///
/// The six bitwise kinds are valid in both families and mean the same opcode in
/// each. The importer emits them as ctjs.binary, because the VM ToNumerics
/// their operands and context::binary_op does the same. ctjs.binary_static of
/// one is still valid IR, meaning "the operands are already numeric":
/// binary_op_static converts nothing, which is right only once something has
/// proved that.
[[nodiscard]] constexpr bool is_bitwise(BinaryKind kind) {
    return kind == BinaryKind::Shl || kind == BinaryKind::Shr || kind == BinaryKind::UShr ||
           kind == BinaryKind::BitAnd || kind == BinaryKind::BitOr || kind == BinaryKind::BitXor;
}

/// The opcode a `ctjs.binary` of `kind` lowers to (op::halt if none).
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
    case BinaryKind::Shl: return op::shl;
    case BinaryKind::Shr: return op::shr;
    case BinaryKind::UShr: return op::ushr;
    case BinaryKind::BitAnd: return op::bit_and;
    case BinaryKind::BitOr: return op::bit_or;
    case BinaryKind::BitXor: return op::bit_xor;
    }
    return op::halt; // Never valid as a binary operator; see is_valid_binary.
}

/// The opcode a `ctjs.binary_static` of `kind` lowers to, or op::halt for the
/// six kinds that exist only in the re-entering family. binary_op_static's
/// switch has no arm for those, so passing one would fall through to a default
/// that silently returns undefined. The caller checks for op::halt rather than
/// the callee guessing.
[[nodiscard]] constexpr ctbrowser::script::op opcode_for_binary_static(BinaryKind kind) {
    if (kind == BinaryKind::Add) { return ctbrowser::script::op::add; }
    return is_bitwise(kind) ? opcode_for_binary(kind) : ctbrowser::script::op::halt;
}

[[nodiscard]] constexpr bool is_valid_binary(BinaryKind kind) {
    return opcode_for_binary(kind) != ctbrowser::script::op::halt;
}

[[nodiscard]] constexpr bool is_valid_binary_static(BinaryKind kind) {
    return opcode_for_binary_static(kind) != ctbrowser::script::op::halt;
}

// These static_asserts are the test for this mapping: every claim is
// decidable at compile time, so it is checked in every translation unit that
// includes this header. A runtime test would need to link MLIR, because
// BinaryKind is generated from ODS.

// Add is the one kind the two families map differently, the pair that would be
// invisible in a table of integers.
static_assert(opcode_for_binary(BinaryKind::Add) == ctbrowser::script::op::add_generic,
              "source `+` is add_generic - it runs ToPrimitive and can call a user valueOf");
static_assert(opcode_for_binary_static(BinaryKind::Add) == ctbrowser::script::op::add,
              "and the static family's `add` is op::add, which cannot run user code at all");
static_assert(opcode_for_binary(BinaryKind::Add) != opcode_for_binary_static(BinaryKind::Add),
              "so folding the two families would make `x + y` and `x++` the same call");

// Every other kind is pinned too, so renumbering `enum class op` cannot
// quietly change what any of them means. The six bitwise kinds map to the
// same opcode in both families; only the helper, and so whether operands are
// converted first, differs.
struct pinned_opcode {
    BinaryKind kind;
    ctbrowser::script::op code;
};
inline constexpr pinned_opcode pinned_opcodes[] = {
    {BinaryKind::Sub, ctbrowser::script::op::sub},
    {BinaryKind::Mul, ctbrowser::script::op::mul},
    {BinaryKind::Div, ctbrowser::script::op::div},
    {BinaryKind::Mod, ctbrowser::script::op::mod},
    {BinaryKind::Pow, ctbrowser::script::op::pow},
    {BinaryKind::Concat, ctbrowser::script::op::concat},
    {BinaryKind::Shl, ctbrowser::script::op::shl},
    {BinaryKind::Shr, ctbrowser::script::op::shr},
    {BinaryKind::UShr, ctbrowser::script::op::ushr},
    {BinaryKind::BitAnd, ctbrowser::script::op::bit_and},
    {BinaryKind::BitOr, ctbrowser::script::op::bit_or},
    {BinaryKind::BitXor, ctbrowser::script::op::bit_xor}};
constexpr bool every_opcode_pinned() {
    for (const pinned_opcode & row : pinned_opcodes) {
        if (opcode_for_binary(row.kind) != row.code ||
            opcode_for_binary_static(row.kind) !=
                (is_bitwise(row.kind) ? row.code : ctbrowser::script::op::halt)) {
            return false;
        }
    }
    return true;
}
// Pow is op::pow: `**` is context::exponentiate, not libm's pow - the
// specification's edge cases and C's do not agree.
static_assert(every_opcode_pinned());

// A kind the family does not serve is refused rather than guessed:
// binary_op_static's switch has no arm for concat, so passing one would
// silently return undefined.
static_assert(is_valid_binary(BinaryKind::BitAnd),
              "the bitwise kinds re-enter: context::binary_op ToNumerics them");
static_assert(!is_valid_binary_static(BinaryKind::Concat),
              "and concat is re-entering only - it must never reach the BigInt arm");

} // namespace ctcompile::ctjs
