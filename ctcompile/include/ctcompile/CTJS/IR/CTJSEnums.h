//===- CTJSEnums.h - CTJS operation kind enums ------------------*- C++ -*-===//
//
// The generated enum classes and their stringification, plus classification
// helpers. Separate from CTJSAttrs.h because an EnumAttr's storage is the enum,
// so anything including the attributes needs these first.
//
//===----------------------------------------------------------------------===//

#pragma once
#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/StringRef.h"

#include "ctcompile/CTJS/IR/CTJSEnums.h.inc"

namespace ctcompile::ctjs {

/// Returns true for the six bitwise kinds, which both binary families
/// implement. The importer emits them as ctjs.binary, whose ToNumeric can run
/// an operand's valueOf; a ctjs.binary_static of one says the operands are
/// numeric already. A pass may treat the two alike only once it has proved the
/// operands primitive: on a primitive, ToNumeric is the identity and both
/// reach the same operator.
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

/// Returns true for the six arithmetic kinds. They are listed rather than
/// derived so that a kind outside the enum (tests forge 255) answers false
/// here, as it does in isBitwise.
constexpr bool isArithmetic(BinaryKind kind) {
    switch (kind) {
    case BinaryKind::Add:
    case BinaryKind::Sub:
    case BinaryKind::Mul:
    case BinaryKind::Div:
    case BinaryKind::Mod:
    case BinaryKind::Pow: return true;
    default: return false;
    }
}

} // namespace ctcompile::ctjs
