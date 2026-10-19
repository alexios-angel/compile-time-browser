//===- ClosureLifting/ClassInheritance.h - Flattening `extends` -*- C++ -*-===//
//
// Declares the rewrite that turns each JavaScript class that extends another
// class of the same module into a class that extends nothing, so the class
// census (Classes.cpp) and the constructor, method and static-member rules
// treat it like any other class. ClassInheritance.cpp describes the rewrite.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative::lowering_detail {

/// Whether `call` calls the engine helper `name`.
bool callsIntrinsic(ctjs::CallOp call, llvm::StringRef name);

/// The constructor closure a binding read names when every store into the
/// binding stores that one closure, or null.
ctjs::CreateClosureOp boundConstructor(mlir::Value read);

/// On the constructor closure of a class that extends another and was left
/// as it is: why, as a clause that completes "it extends another class: ...".
inline constexpr llvm::StringLiteral kNativeInheritanceReason = "ctnative.inheritance_reason";

/// On the module: {copies, erased}, how many functions flattening added as
/// per-class copies and erased as unreachable, over every run.
inline constexpr llvm::StringLiteral kNativeInheritanceSummary = "ctnative.inheritance_summary";

/// On each copy while flattening runs: the symbol of the source function it
/// stands for (never another copy's).
inline constexpr llvm::StringLiteral kNativeCopyOf = "ctnative.copy_of";

/// On the module: {"<copy>" = "<source>"} for every copy flattening made,
/// including copies later passes prune or remove.
inline constexpr llvm::StringLiteral kNativeCopies = "ctnative.copies";

/// On the module: the symbols of the source functions flattening erased as
/// unreachable, whose copies stand for them.
inline constexpr llvm::StringLiteral kNativeErasedSources = "ctnative.inheritance_erased";

/// Flattens every class whose parent is a class this module defines, parents
/// first, and records kNativeInheritanceReason on each class it leaves alone.
/// Runs before any closure-lifting census. Running it again changes nothing.
void flattenClassInheritance(mlir::ModuleOp module);

} // namespace ctcompile::ctnative::lowering_detail
