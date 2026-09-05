#pragma once

#include "Importer.h"

namespace ctcompile::js::bytecode_detail {

// THE OPCODES THIS CUT UNDERSTANDS, as a table rather than a switch scattered
// through the walk. A binary opcode that is not here is not silently wrong: it
// reaches the default arm and abandons the function.
struct binary_row {
    op code;
    ctjs::BinaryKind kind;
    bool re_entering;
};

inline constexpr binary_row binary_rows[] = {
    // THE STATIC FAMILY, which cannot run user code - to_number and to_int32
    // rather than to_number_value. A backend that proves both operands are
    // numbers may drop the call, the exception edge and the safepoint.
    {op::add, ctjs::BinaryKind::Add, false},
    {op::bit_and, ctjs::BinaryKind::BitAnd, false},
    {op::bit_or, ctjs::BinaryKind::BitOr, false},
    {op::bit_xor, ctjs::BinaryKind::BitXor, false},
    {op::shl, ctjs::BinaryKind::Shl, false},
    {op::shr, ctjs::BinaryKind::Shr, false},
    {op::ushr, ctjs::BinaryKind::UShr, false},
    // AND THE RE-ENTERING ONE. `add_generic` is what source `+` compiles to;
    // `op::add` comes only from `++` and three internal counters.
    {op::sub, ctjs::BinaryKind::Sub, true},
    {op::mul, ctjs::BinaryKind::Mul, true},
    {op::div, ctjs::BinaryKind::Div, true},
    {op::mod, ctjs::BinaryKind::Mod, true},
    {op::pow, ctjs::BinaryKind::Pow, true},
    {op::add_generic, ctjs::BinaryKind::Add, true},
    {op::concat, ctjs::BinaryKind::Concat, true},
};

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

struct unary_row {
    op code;
    ctjs::UnaryKind kind;
};

inline constexpr unary_row unary_rows[] = {
    {op::negate, ctjs::UnaryKind::Neg},
    {op::bit_not, ctjs::UnaryKind::BitNot},
    {op::logical_not, ctjs::UnaryKind::Not},
    {op::type_of, ctjs::UnaryKind::TypeOf},
    // UNARY PLUS, which was the only operation in this dialect that was fully
    // lowered and completely unreachable. ctjs.unary plus emits ct_aot_to_number
    // and boxes the double, operators.mlir has exercised it from hand-written
    // IR since it was written, and the helper has had a body all along - the
    // table simply had no row, so no JavaScript could ever produce it.
    {op::to_number, ctjs::UnaryKind::Plus},
};

} // namespace ctcompile::js::bytecode_detail
