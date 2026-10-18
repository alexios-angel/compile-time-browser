//===- ClosureLifting/SpreadParameters.h - Spread object parameters -*- C++ -*-===//
//
// argumentCensus gives a parameter a pointer to the caller's object literal
// when the function reads it only through constant keys. A configuration
// parameter is often also spread into a fresh literal and tested first,
// `{...defaults, ...(typeof config === "object" ? config : {})}`, which is
// neither. When every call passes a literal of one shape, both are known
// before the call runs: `typeof` answers "object", and the spread copies that
// shape's own keys in property order. rewriteSpreadParameters writes them out
// that way, so the object-parameter rule sees only constant-key reads.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

#include <utility>

namespace ctcompile::ctnative::lowering_detail {

/// A function and every call of it, each call with the JavaScript arguments
/// it passes. The list must be complete: a call it leaves out could pass
/// anything.
struct calledFunction {
    ctjs::FuncOp function;
    llvm::SmallVector<std::pair<mlir::Operation *, llvm::SmallVector<mlir::Value>>> calls;
};

/// Rewrites each parameter of `functions` that every call passes an object
/// literal of one shape, or forwards from a parameter rewritten the same way.
/// The shape is the literal's own keys at the call, all written by the
/// literal itself before the call in the call's block, none an array index
/// or a reserved name. The parameter must not be written, and the function
/// must not read `arguments`. Then `typeof` of the parameter becomes the
/// constant "object", an scf.if that tests it against a constant string keeps
/// the arm that test selects, and each spread of it into a fresh literal
/// becomes one constant-key read and one own-property definition per key.
/// Any other parameter is left as it is. A test is folded only when the arm it
/// drops holds nothing but constants and fresh literals, whose operations it
/// adds to `erasedLiterals` for the caller's census. Returns how many
/// parameters it rewrote.
unsigned rewriteSpreadParameters(llvm::ArrayRef<calledFunction> functions,
                                 llvm::SmallVectorImpl<mlir::Operation *> & erasedLiterals);

/// The same rewrite for a value made in the function itself: a fresh object
/// literal whose own keys are all defined directly in its block before any
/// other use, so they never change once it is read, or the result of an
/// scf.if whose two arms each make it as such a literal, both of one shape.
/// Its uses must be `typeof`, constant-key reads, spreads into fresh literals
/// and the scf.if arms that test it. Any other value is left as it is. Returns
/// how many values it rewrote; `erasedLiterals` as above.
unsigned rewriteLiteralLocals(mlir::ModuleOp module,
                              llvm::SmallVectorImpl<mlir::Operation *> & erasedLiterals);

} // namespace ctcompile::ctnative::lowering_detail
