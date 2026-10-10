//===- CTNativeAttrNames.h - Shared ctnative attribute names ----*- C++ -*-===//
//
// The discardable `ctnative.*` attribute names one pass writes and another
// (or the C++ emitter) reads. A typo in a bare string is a silent miss (the
// reader falls back and nothing fails), so both sides must spell them from
// here.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

/// Written by closure lifting (lib/CTNative/Lowering/ClosureLifting) and read
/// by the admission check.
inline constexpr llvm::StringLiteral kNativeReceiver = "ctnative.receiver";
inline constexpr llvm::StringLiteral kNativeObjectArgs = "ctnative.object_args";
inline constexpr llvm::StringLiteral kNativeCellArgs = "ctnative.cell_args";
inline constexpr llvm::StringLiteral kNativeCaptures = "ctnative.captures";
inline constexpr llvm::StringLiteral kNativeLifted = "ctnative.lifted";
inline constexpr llvm::StringLiteral kNativeUnboxed = "ctnative.unboxed";
inline constexpr llvm::StringLiteral kNativeCarried = "ctnative.carried";
inline constexpr llvm::StringLiteral kNativeClosureReason = "ctnative.closure_reason";
inline constexpr llvm::StringLiteral kNativeCellReason = "ctnative.cell_reason";
inline constexpr llvm::StringLiteral kNativeObjectReason = "ctnative.object_reason";
inline constexpr llvm::StringLiteral kNativeMethod = "ctnative.method";
inline constexpr llvm::StringLiteral kNativeMethodRefusal = "ctnative.method_refusal";
/// A carried cell of a function that runs at most once per program run, stored
/// in a namespace-scope variable whose name is the attribute's value. Read by
/// type inference (which joins every cell naming the same variable) and by the
/// lowering (which declares the variable).
inline constexpr llvm::StringLiteral kNativeStaticCell = "ctnative.static_cell";
/// On a static cell: this op is not the binding's box but a name for it in
/// another function, created where a captured upvalue used to be read.
inline constexpr llvm::StringLiteral kNativeStaticReference = "ctnative.static_reference";
/// On a ctjs.func whose boxed bindings could not become static: why its body
/// is not proved to run at most once.
inline constexpr llvm::StringLiteral kNativeNotRunOnce = "ctnative.not_run_once";
/// On the operations that set up a proved JavaScript class (its prototype,
/// constructor and `__home` edges, member definitions and the class helper
/// calls): they lower to nothing. Written by the class census
/// (ClosureLifting/Classes.cpp).
inline constexpr llvm::StringLiteral kNativeClassSetup = "ctnative.class_setup";
/// On a class constructor's closure: why the class census did not prove it.
inline constexpr llvm::StringLiteral kNativeClassReason = "ctnative.class_reason";
/// On the object literal that stands for an instance of a JavaScript class
/// (the constructor lift's `new K(...)`): the class's name, which the lowering
/// gives the C++ class it emits for that shape.
inline constexpr llvm::StringLiteral kNativeClassName = "ctnative.class_name";
/// On the static box of a binding that holds an object literal: why calls of
/// its methods stay calls through the literal (ClosureLifting/Singletons.cpp).
inline constexpr llvm::StringLiteral kNativeSingletonReason = "ctnative.singleton_reason";

/// Written by the lowering and read by the C++ emitter (lib/Target/Cpp).
inline constexpr llvm::StringLiteral kNativeCallableBody = "ctnative.callable_body";
inline constexpr llvm::StringLiteral kNativeCallableCreate = "ctnative.callable_create";
inline constexpr llvm::StringLiteral kNativeProvenance = "ctnative.provenance";
inline constexpr llvm::StringLiteral kNativeDeduced = "ctnative.deduced";
inline constexpr llvm::StringLiteral kNativeStatement = "ctnative.statement";
inline constexpr llvm::StringLiteral kNativePinned = "ctnative.pinned";
inline constexpr llvm::StringLiteral kNativeConstBindings = "ctnative.const_bindings";
inline constexpr llvm::StringLiteral kNativeReadableLiterals = "ctnative.readable_literals";
inline constexpr llvm::StringLiteral kNativeNumericAlias = "ctnative.numeric_alias";
inline constexpr llvm::StringLiteral kNativeParameterSuppression = "ctnative.parameter_suppression";
inline constexpr llvm::StringLiteral kNativeConstOperands = "ctnative.const_operands";
inline constexpr llvm::StringLiteral kNativeTemplateParams = "ctnative.template_params";

} // namespace ctcompile::ctnative
