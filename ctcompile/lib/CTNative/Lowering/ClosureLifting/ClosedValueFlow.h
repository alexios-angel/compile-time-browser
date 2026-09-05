#pragma once

#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

// These are possible value flows, not runtime aliases. Distinct invocations
// of one factory share a target/schema but keep separate environments.
struct closedValueFlow {
    llvm::DenseMap<mlir::Value, mlir::Value> parent;
    llvm::SmallVector<mlir::Value> nodes;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::ReturnOp>> returns;

    void add(mlir::Value value) {
        if (parent.try_emplace(value, value).second) { nodes.push_back(value); }
    }
    mlir::Value find(mlir::Value value) {
        auto root = parent.lookup(value);
        if (!root) { return {}; }
        while (parent.lookup(root) != root) { root = parent.lookup(root); }
        while (value != root) {
            auto next = parent.lookup(value);
            parent[value] = root;
            value = next;
        }
        return root;
    }
    void join(mlir::Value a, mlir::Value b) {
        add(a);
        add(b);
        parent[find(b)] = find(a);
    }
    static ctjs::FuncOp target(ctjs::CallDirectOp call) {
        return mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
    }
    static bool closed(ctjs::FuncOp fn) {
        return fn && !fn.getBody().empty() &&
               mlir::SymbolTable::getSymbolVisibility(fn) == mlir::SymbolTable::Visibility::Private;
    }
    void build(mlir::ModuleOp module) {
        module.walk([&](ctjs::FuncOp fn) {
            fn.getBody().walk([&](ctjs::ReturnOp ret) { returns[fn].push_back(ret); });
            for (ctjs::ReturnOp ret : returns[fn]) {
                join(returns[fn].front().getValue(), ret.getValue());
            }
        });
        module.walk([&](ctjs::CallDirectOp call) {
            auto fn = target(call);
            if (!fn || fn.getBody().empty()) { return; }
            callers[fn].push_back(call);
            auto & entry = fn.getBody().front();
            for (unsigned i = 3; i < call->getNumOperands() && i < entry.getNumArguments(); ++i) {
                join(call->getOperand(i), entry.getArgument(i));
            }
            for (ctjs::ReturnOp ret : returns[fn]) { join(call.getResult(), ret.getValue()); }
        });
    }
};

} // namespace ctcompile::ctnative::lowering_detail
