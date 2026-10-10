//===- OperatorTables.h - Operator opcode tables ----------------*- C++ -*-===//
//
// The binary, comparison and unary opcodes the bytecode importer handles, as
// tables rather than a switch scattered through the walk. A binary opcode
// missing here is not silently mistranslated: it reaches the default arm and
// the importer abandons the function.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "Importer.h"

namespace ctcompile::js::bytecode_detail {

/// A binary opcode and the ctjs.binary kind it imports as.
struct binary_row {
    op code;
    ctjs::BinaryKind kind;
    /// Whether the operation can run user code (valueOf, toString) and so
    /// re-enter the engine.
    bool re_entering;
};

inline constexpr binary_row binary_rows[] = {
    // `add` is the only static (non-re-entering) row: ct_aot_binary_op_static
    // converts with to_number and runs no user code. The VM's op::add applies
    // ToNumeric, so `x++` on an object calls its valueOf in the interpreter,
    // but the AOT helper does not yet. Until the helper matches, this row
    // stays static rather than claim a re-entry edge the lowered call never
    // takes.
    {op::add, ctjs::BinaryKind::Add, false},
    // The re-entering rows. `add_generic` is what source `+` compiles to;
    // `op::add` comes only from `++` and three internal counters. The six
    // bitwise opcodes apply ToNumeric to both operands, left first, so an
    // object's valueOf runs and a Symbol is a TypeError, and context::binary_op
    // answers them exactly as the interpreter does.
    {op::bit_and, ctjs::BinaryKind::BitAnd, true},
    {op::bit_or, ctjs::BinaryKind::BitOr, true},
    {op::bit_xor, ctjs::BinaryKind::BitXor, true},
    {op::shl, ctjs::BinaryKind::Shl, true},
    {op::shr, ctjs::BinaryKind::Shr, true},
    {op::ushr, ctjs::BinaryKind::UShr, true},
    {op::sub, ctjs::BinaryKind::Sub, true},
    {op::mul, ctjs::BinaryKind::Mul, true},
    {op::div, ctjs::BinaryKind::Div, true},
    {op::mod, ctjs::BinaryKind::Mod, true},
    {op::pow, ctjs::BinaryKind::Pow, true},
    {op::add_generic, ctjs::BinaryKind::Add, true},
    {op::concat, ctjs::BinaryKind::Concat, true},
};

/// A comparison opcode, its ctjs.compare kind, and whether to negate the
/// result (`!==` is a negated StrictEq).
struct compare_row {
    op code;
    ctjs::CompareKind kind;
    bool negate;
};

inline constexpr compare_row compare_rows[] = {
    {op::equal, ctjs::CompareKind::StrictEq, false},
    {op::not_equal, ctjs::CompareKind::StrictEq, true},
    {op::loose_equal, ctjs::CompareKind::Eq, false},
    {op::loose_not_equal, ctjs::CompareKind::Eq, true},
    {op::less, ctjs::CompareKind::Lt, false},
    {op::less_equal, ctjs::CompareKind::Le, false},
    {op::greater, ctjs::CompareKind::Gt, false},
    {op::greater_equal, ctjs::CompareKind::Ge, false},
};

/// A unary opcode and the ctjs.unary kind it imports as.
struct unary_row {
    op code;
    ctjs::UnaryKind kind;
};

inline constexpr unary_row unary_rows[] = {
    {op::negate, ctjs::UnaryKind::Neg},
    {op::bit_not, ctjs::UnaryKind::BitNot},
    {op::logical_not, ctjs::UnaryKind::Not},
    {op::type_of, ctjs::UnaryKind::TypeOf},
    // Unary plus: `+x` compiles to op::to_number.
    {op::to_number, ctjs::UnaryKind::Plus},
};

} // namespace ctcompile::js::bytecode_detail
