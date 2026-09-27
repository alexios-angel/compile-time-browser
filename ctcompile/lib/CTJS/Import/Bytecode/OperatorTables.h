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
    // THE STATIC FAMILY, which is now `add` alone: ct_aot_binary_op_static
    // converts with to_number and runs no user code. The VM's own op::add
    // ToNumerics its operands since ec1b5a89b, so `x++` on an object runs its
    // valueOf in the interpreter; the helper still does not, and until that
    // browser-side fix lands this row stays static rather than claim an edge
    // the call it lowers to never takes.
    {op::add, ctjs::BinaryKind::Add, false},
    // AND THE RE-ENTERING ONES. `add_generic` is what source `+` compiles to;
    // `op::add` comes only from `++` and three internal counters. The six
    // bitwise opcodes ToNumeric both operands, left first, so an object's
    // valueOf runs and a Symbol is a TypeError - and context::binary_op
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
