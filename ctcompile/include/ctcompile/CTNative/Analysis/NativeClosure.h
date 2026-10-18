//===- NativeClosure.h - Closures native code can own -----------*- C++ -*-===//
//
// Attribute names the returned-closure proof writes, and small accessors for
// them. A closure with one known target keeps its code in the call graph and
// carries its immutable captures by value, so native code needs no heap
// closure object.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"
#include "ctcompile/CTNative/IR/CTNativeTypes.h"

#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative {

/// Attribute names written only by the returned-closure proof.
inline constexpr llvm::StringLiteral kNativeEnvironment = "ctnative.environment";
inline constexpr llvm::StringLiteral kNativeEnvironmentRead = "ctnative.environment_read";
inline constexpr llvm::StringLiteral kNativeStoredCallable = "ctnative.stored_callable";
inline constexpr llvm::StringLiteral kNativeStoredCall = "ctnative.stored_call";
inline constexpr llvm::StringLiteral kNativeStoredRead = "ctnative.stored_capture_read";
inline constexpr llvm::StringLiteral kNativeMethodTable = "ctnative.method_table";
inline constexpr llvm::StringLiteral kNativeTableField = "ctnative.table_field";

/// A borrowed callback (ClosureLifting/BorrowedCallbacks.cpp): a closure
/// passed to a parameter its callee only calls. On the ctjs.create_closure it
/// is a symbol reference to the lifted callback, whose captures are now the
/// closure's operands; on that callback's ctjs.func it is a unit attribute.
inline constexpr llvm::StringLiteral kNativeBorrowedCallback = "ctnative.borrowed_callback";
/// On a ctjs.call through such a parameter: the callbacks it may call, as an
/// array of symbol references.
inline constexpr llvm::StringLiteral kNativeBorrowedCall = "ctnative.borrowed_call";

/// A closure set (ClosureLifting/ClosureSets.cpp): on a member's
/// ctjs.create_closure, every member's lifted function, sorted, as an array of
/// symbol references; on a ctjs.call through the set, a unit attribute. The
/// member also carries kNativeBorrowedCallback and the call
/// kNativeBorrowedCall, which type inference and admission read as for a
/// borrowed callback.
inline constexpr llvm::StringLiteral kNativeClosureSet = "ctnative.closure_set";

/// Whether `type` is a closure set every target of which the closure-set lift
/// made a member (kNativeClosureSet on the target's ctjs.func), possibly
/// absent (`opt`), looked up from `at`. Any other `closure_set` (callers passing different
/// callbacks to one borrowed parameter, say) has no closure-set representation.
inline bool isProvedClosureSet(mlir::Operation * at, mlir::Type type) {
    if (auto optional = llvm::dyn_cast_or_null<OptType>(type)) { type = optional.getElementType(); }
    auto set = llvm::dyn_cast_or_null<ClosureSetType>(type);
    if (!set) { return false; }
    return llvm::all_of(set.getTargets(), [&](mlir::StringAttr target) {
        mlir::Operation * fn = mlir::SymbolTable::lookupNearestSymbolFrom(at, target);
        return fn && fn->hasAttr(kNativeClosureSet);
    });
}

/// The callback a borrowed ctjs.create_closure names, or empty.
inline llvm::StringRef borrowedTarget(mlir::Operation * op) {
    auto target = op->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeBorrowedCallback);
    return target ? target.getValue() : llvm::StringRef{};
}

/// Whether `value` is a parameter that only borrowed calls call through.
inline bool isBorrowedParameter(mlir::Value value) {
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
    return argument && argument.getOwner()->isEntryBlock() && !argument.use_empty() &&
           llvm::all_of(argument.getUses(), [](mlir::OpOperand & use) {
               return use.getOperandNumber() == 0 && use.getOwner()->hasAttr(kNativeBorrowedCall);
           });
}

/// Whether `value` is the entry argument through which a listener that removes
/// itself receives its own ctnative::callable_identity (kNativeListenerSelf on
/// its ctjs.func, ClosureLifting/HostCallbacks.cpp). Its only uses name the
/// listener of a removal, so it needs no value carrier.
inline bool isListenerSelfParameter(mlir::Value value) {
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
    if (!argument || !argument.getOwner()->isEntryBlock()) { return false; }
    auto function = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
    auto self = function ? function->getAttrOfType<mlir::IntegerAttr>(kNativeListenerSelf)
                         : mlir::IntegerAttr{};
    return self && argument.getArgNumber() == static_cast<unsigned>(self.getInt());
}

/// The kNativeMethodTable name on `op`, or empty.
inline llvm::StringRef methodTableName(mlir::Operation * op) {
    return ctjs::stringAttrOf(op, kNativeMethodTable);
}

/// The kNativeEnvironment target on `op`, or empty.
inline llvm::StringRef environmentTarget(mlir::Operation * op) {
    return ctjs::stringAttrOf(op, kNativeEnvironment);
}

/// The slot list `name` (kNativeObjectArgs, kNativeCellArgs, ...) on `op`, or
/// empty.
inline llvm::ArrayRef<int32_t> listedSlots(mlir::Operation * op, llvm::StringRef name) {
    auto listed = op->getAttrOfType<mlir::DenseI32ArrayAttr>(name);
    return listed ? listed.asArrayRef() : llvm::ArrayRef<int32_t>{};
}

/// True when the slot list `name` on `op` contains `index`.
inline bool listsSlot(mlir::Operation * op, llvm::StringRef name, unsigned index) {
    return llvm::is_contained(listedSlots(op, name), static_cast<int32_t>(index));
}

} // namespace ctcompile::ctnative
