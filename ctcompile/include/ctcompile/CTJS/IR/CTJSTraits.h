#pragma once
// THE FOUR SEMANTIC TRAITS, and they are the reason later passes never
// enumerate operation names.
//
// The policy: "These traits are the input to the exception-edge insertion pass,
// the shadow-frame rooting pass, the suspension state-machine pass, and the
// precise-liveness pass. Each of those passes queries traits generically; none
// of them enumerates operation names. That is the whole point. A pass that
// switches on operation names must be revisited every time an operation is
// added. A pass that queries a trait never is."
//
// They are the SAME three obligations aot_helpers.def declares on every helper
// row - may_throw, may_reenter, is_safepoint - plus suspension, which the ABI
// spells as a status rather than a flag. That correspondence is deliberate: the
// dialect and the ABI have to agree about what an operation can do, and the
// cheapest way to keep them agreeing is for the words to be the same.
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

} // namespace ctcompile::ctjs
