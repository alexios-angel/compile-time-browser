#pragma once

//===- Body.h - Callable printing -------------------------------*- C++ -*-===//
//
// Decisions the C++ emitter makes about a native callable: a function the
// lowering marked `ctnative.callable_body` is printed as a binder function
// that takes the captured values and returns a lambda, and a call marked
// `ctnative.callable_create` creates one. These helpers decide whether the
// lambda inlines the lifted function's body, and whether the lifted function
// and the binder still need their own definitions.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/SmallVector.h"

#include <string>

namespace ctcompile::cpp {

/// How one `ctnative.callable_body` function is printed. A default-constructed
/// value (empty binder) describes an ordinary, unmarked function.
struct CallableBody {
    /// The C++ type of the callable, `ctnative::ctn_env_*`.
    std::string type;
    /// The binder's name (`ctn_bind_...`): takes the captures, returns the lambda.
    std::string binder;
    /// The name of the lambda variable inside the binder (`ctn_...`).
    std::string name;
    /// Names for the function's arguments: the captures first, then the
    /// lambda's own parameters.
    llvm::SmallVector<std::string> parameters;
    /// How many leading function arguments are captures.
    unsigned captures = 0;
    /// The lambda contains the function's body instead of calling the lifted
    /// function; possible only when every capture can be bound const.
    bool inlineBody = false;
    /// The lifted function still needs its own definition.
    bool retainFunction = true;
    /// Creation sites expand the lambda in place instead of calling the binder.
    bool inlineCreation = false;
    /// The binder still needs its own definition.
    bool retainBinder = true;

    /// What a definition prints: the lifted function, the binder, or both. An
    /// unmarked function (empty binder) is only ever the function.
    [[nodiscard]] bool emitsFunction() const { return binder.empty() || retainFunction; }
    [[nodiscard]] bool emitsBinder() const { return !binder.empty() && retainBinder; }
};

/// Validates `function`'s `ctnative.callable_body` description and decides how
/// to print it. The lifted function is retained if another emitted use names
/// it, or if a capture cannot be bound const and so cannot host its body.
/// Returns a default CallableBody for an unmarked function, and fails with a
/// diagnostic on a malformed description.
mlir::FailureOr<CallableBody> callableBodyPlan(mlir::emitc::FuncOp function);

/// The function a `ctnative.callable_create` mark names, or null when unmarked
/// or unresolved. No validation: see callableCreationTarget.
mlir::emitc::FuncOp callableCreationSymbol(mlir::emitc::CallOpaqueOp call);

/// The function a marked creation call creates, after checking the call
/// against the function's callable_body description (binder name, result
/// type, capture count and types). Returns null for an unmarked call and fails
/// with a diagnostic on a mismatch. Only such a checked creation can become a
/// lambda initializer.
mlir::FailureOr<mlir::emitc::FuncOp> callableCreationTarget(mlir::emitc::CallOpaqueOp call);

/// True when anything other than its declaration or its own callable-creation
/// call refers to `function`, including textual mentions inside opaque C++
/// (verbatim text, literals, opaque types and attributes).
bool callableFunctionReferenced(mlir::emitc::FuncOp function);

/// True when the binder named `binder` must still be defined: no marked
/// creation of `function` exists, or opaque C++ text names the binder.
bool callableBinderNeeded(mlir::emitc::FuncOp function, llvm::StringRef binder);

/// True when expanding `function`'s creations inline stays small: nested
/// creations at most 8 deep, not recursive, and at most 4096 operations
/// visited in total, counting each repeated child body again.
bool boundedCallableExpansion(mlir::emitc::FuncOp function);

} // namespace ctcompile::cpp
