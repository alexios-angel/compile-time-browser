#pragma once
// The generated enum classes and their stringification. Separate from
// CTJSAttrs.h because the enums are what the attributes are ABOUT: an
// EnumAttr's storage is the enum, so anything including the attributes needs
// these first.
#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/StringRef.h"

#include "ctcompile/CTJS/IR/CTJSEnums.h.inc"

namespace ctcompile::ctjs {

// THE SIX BITWISE KINDS, the ones both binary families implement. The importer
// emits them as ctjs.binary, whose ToNumeric can run an operand's valueOf; a
// ctjs.binary_static of one says the operands are numeric already. A pass may
// treat the two alike only once it has proved the operands primitive: on a
// primitive, ToNumeric is the identity and both reach the same operator.
constexpr bool isBitwise(BinaryKind kind) {
    switch (kind) {
    case BinaryKind::Shl:
    case BinaryKind::Shr:
    case BinaryKind::UShr:
    case BinaryKind::BitAnd:
    case BinaryKind::BitOr:
    case BinaryKind::BitXor: return true;
    default: return false;
    }
}

// THE SIX ARITHMETIC KINDS: every kind that is neither bitwise nor Concat.
constexpr bool isArithmetic(BinaryKind kind) {
    return !isBitwise(kind) && kind != BinaryKind::Concat;
}

} // namespace ctcompile::ctjs
