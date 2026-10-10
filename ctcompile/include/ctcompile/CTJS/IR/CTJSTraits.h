//===- CTJSTraits.h - CTJS semantic op traits -------------------*- C++ -*-===//
//
// The four semantic traits. Later passes (exception-edge insertion,
// shadow-frame rooting, the suspension state machine, precise liveness) query
// these traits instead of switching on operation names, so adding an
// operation never requires revisiting those passes.
//
// They match the three obligations aot_helpers.def declares on every helper
// row (may_throw, may_reenter, is_safepoint), plus suspension, which the ABI
// spells as a status rather than a flag. Using the same words keeps the
// dialect and the ABI in agreement about what an operation can do.
//
//===----------------------------------------------------------------------===//

#pragma once
#include "mlir/IR/OpDefinition.h"

namespace ctcompile::ctjs {

/// The operation can complete abruptly with a thrown JavaScript value.
template <typename ConcreteType>
class CTJSMayThrow : public mlir::OpTrait::TraitBase<ConcreteType, CTJSMayThrow> {};

/// The operation can transfer control into arbitrary user JavaScript
/// (accessors, proxy traps, valueOf/toString, user callbacks).
template <typename ConcreteType>
class CTJSMayReenterJS : public mlir::OpTrait::TraitBase<ConcreteType, CTJSMayReenterJS> {};

/// The operation may trigger garbage collection. Live GC-managed values must
/// be rooted in the AOT shadow frame across it.
template <typename ConcreteType>
class CTJSSafepoint : public mlir::OpTrait::TraitBase<ConcreteType, CTJSSafepoint> {};

/// The operation can suspend the enclosing resumable function.
template <typename ConcreteType>
class CTJSMaySuspend : public mlir::OpTrait::TraitBase<ConcreteType, CTJSMaySuspend> {};

/// Whether an operation's ODS declaration matches the ABI row it names:
/// operand count, result count, the return register's type, and the attributes
/// that stand for parameters with no SSA source. Implemented in
/// CTJSABIShape.cpp.
mlir::LogicalResult verifyABIShape(mlir::Operation * op);

/// Runs verifyABIShape() on every runtime operation. CTJS_RuntimeOp attaches
/// this trait to its base class, so no operation can forget the check, as a
/// per-operation verifier could.
template <typename ConcreteType>
class CTJSABIShaped : public mlir::OpTrait::TraitBase<ConcreteType, CTJSABIShaped> {
public:
    static mlir::LogicalResult verifyTrait(mlir::Operation * op) { return verifyABIShape(op); }
};

} // namespace ctcompile::ctjs
