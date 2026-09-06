#pragma once

#include "Const/Bindings.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::cpp {

inline mlir::BlockArgument suppressedParameter(mlir::Operation & operation) {
    auto call = llvm::dyn_cast<mlir::emitc::CallOpaqueOp>(&operation);
    if (!call || !call->hasAttrOfType<mlir::UnitAttr>("ctnative.parameter_suppression") ||
        call.getCallee() != "static_cast<void>" || call->getNumOperands() != 1 ||
        call->getNumResults() != 0 || call.getArgs() || call.getTemplateArgs()) {
        return {};
    }
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(call->getOperand(0));
    auto function = call->getParentOfType<mlir::emitc::FuncOp>();
    // Native carriers cannot hide a volatile binding in an opaque spelling.
    // Arbitrary external types retain their discard, even with a forged tag.
    if (!argument || !supportsConstBinding(argument.getType()) || !function ||
        function.getBody().empty() || argument.getOwner() != &function.getBody().front()) {
        return {};
    }
    return argument;
}

// Lowering adds this marker before canonicalization can remove a parameter's
// last real use. Expression captures are aliases, so follow the corresponding
// block argument rather than mistaking an unused capture for printed C++.
// Explicit opaque argument lists can also omit an SSA operand entirely.
// Ordinary EmitC calls and all unmarked void casts remain intact.
inline bool omitParameterSuppression(mlir::Operation & operation) {
    const auto parameter = suppressedParameter(operation);
    if (!parameter) { return false; }
    llvm::SmallVector<mlir::Value> work{parameter};
    llvm::DenseSet<mlir::Value> seen;
    while (!work.empty()) {
        const mlir::Value value = work.pop_back_val();
        if (!seen.insert(value).second) { continue; }
        for (mlir::OpOperand & use : value.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (suppressedParameter(*user) == parameter) { continue; }
            if (auto expression = llvm::dyn_cast<mlir::emitc::ExpressionOp>(user)) {
                auto & body = expression.getRegion().front();
                work.push_back(body.getArgument(use.getOperandNumber()));
                continue;
            }
            if (auto call = llvm::dyn_cast<mlir::emitc::CallOpaqueOp>(user)) {
                const auto args = call.getArgs();
                if (!args) { return true; }
                bool printed = false;
                for (mlir::Attribute argument : *args) {
                    auto index = llvm::dyn_cast<mlir::IntegerAttr>(argument);
                    printed |= index && index.getType().isIndex() &&
                               index.getInt() == static_cast<int64_t>(use.getOperandNumber());
                }
                if (!printed) { continue; }
            }
            return true;
        }
    }
    return false;
}

} // namespace ctcompile::cpp
