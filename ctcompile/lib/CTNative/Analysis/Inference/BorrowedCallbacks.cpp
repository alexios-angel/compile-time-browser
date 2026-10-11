//===- BorrowedCallbacks.cpp - Types of borrowed callbacks -----------------===//
//
// Closure lifting (Lowering/ClosureLifting/BorrowedCallbacks.cpp) lifts a
// closure passed to a parameter its callee only calls. The closure stays as a
// value bound to its captures, and the callee's calls through the parameter
// name every callback the parameter can hold. No ctjs.call_direct reaches such
// a callback, so its parameters are typed here, as a direct call types its
// callee's: the captures from the closure's operands, the rest from the
// arguments of every call through the parameter. Such a call answers the join
// of what its callbacks return.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative {

bool TypeInference::isBorrowedCallbackParameter(mlir::Value value) {
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
    if (!argument || !argument.getOwner()->isEntryBlock() ||
        argument.getArgNumber() < ctjs::implicit_arguments) {
        return false;
    }
    auto function = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
    return function && function->hasAttr(kNativeBorrowedCallback);
}

std::optional<mlir::LogicalResult> TypeInference::visitBorrowedCallback(
    mlir::Operation * op, llvm::ArrayRef<const TypeLattice *> operands,
    llvm::ArrayRef<TypeLattice *> results) {
    // Joins operand `from` into the callback's entry argument `index`, as a
    // call joins an operand into its callee's parameter.
    const auto pass = [&](ctjs::FuncOp callback, unsigned index, unsigned from) {
        if (operands[from]->getValue().isUninitialized()) { return; }
        mlir::Block & entry = callback.getBody().front();
        if (index >= entry.getNumArguments()) { return; }
        TypeLattice * parameter = getLatticeElement(entry.getArgument(index));
        propagateIfChanged(parameter, parameter->join(operands[from]->getValue()));
    };
    const auto lookup = [&](llvm::StringRef name) {
        return mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
            op, mlir::StringAttr::get(op->getContext(), name));
    };
    if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
        const llvm::StringRef name = borrowedTarget(op);
        if (name.empty()) { return std::nullopt; }
        ctjs::FuncOp callback = lookup(name);
        if (!callback || callback.getBody().empty()) {
            return mlir::LogicalResult(
                op->emitError("borrowed callback names no function with a body"));
        }
        // The captures are the leading parameters after the implicit three.
        const unsigned first = made.getUpvalues().getBeginOperandIndex();
        for (unsigned i = 0; i < made.getUpvalues().size(); ++i) {
            pass(callback, ctjs::implicit_arguments + i, first + i);
        }
        propagateIfChanged(results[0],
                           results[0]->join(TypeValue{ClosureType::get(op->getContext(), name)}));
        return mlir::success();
    }
    auto callbacks = op->getAttrOfType<mlir::ArrayAttr>(kNativeBorrowedCall);
    auto call = llvm::dyn_cast<ctjs::CallOp>(op);
    if (!callbacks || !call) { return std::nullopt; }
    llvm::SmallVector<mlir::Value> returned;
    const unsigned first = call.getArgs().getBeginOperandIndex();
    for (auto symbol : callbacks.getAsRange<mlir::FlatSymbolRefAttr>()) {
        ctjs::FuncOp callback = lookup(symbol.getValue());
        if (!callback || callback.getBody().empty()) {
            return mlir::LogicalResult(
                op->emitError("borrowed call names no function with a body"));
        }
        const auto captures = callback->getAttrOfType<mlir::IntegerAttr>(kNativeCaptures);
        const unsigned skip =
            ctjs::implicit_arguments + (captures ? static_cast<unsigned>(captures.getInt()) : 0u);
        for (unsigned i = 0; i < call.getArgs().size(); ++i) {
            pass(callback, skip + i, first + i);
        }
        callback.walk([&](ctjs::ReturnOp ret) { returned.push_back(ret.getValue()); });
    }
    if (const mlir::Type result = joinLattices(op, returned, mlir::Type{})) {
        propagateIfChanged(results[0], results[0]->join(TypeValue{result}));
    }
    return mlir::success();
}

} // namespace ctcompile::ctnative
