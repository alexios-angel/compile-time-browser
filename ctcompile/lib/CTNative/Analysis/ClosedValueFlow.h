#pragma once

#include "OwnedGlobalRoots.h"
#include "OwnedMethodTableSlots.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::ctnative {

// These are possible value flows, not runtime aliases. Distinct invocations
// of one factory share a target/schema but keep separate environments.
struct closedValueFlow {
    llvm::EquivalenceClasses<mlir::Value> classes;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::ReturnOp>> returns;

    void add(mlir::Value value) { classes.insert(value); }
    // The family's leader, or null for a value never added. Consumers report
    // the first failing member in insertion order, which nodes() preserves.
    mlir::Value find(mlir::Value value) const {
        const auto leader = classes.findLeader(value);
        return leader == classes.member_end() ? mlir::Value{} : *leader;
    }
    void join(mlir::Value a, mlir::Value b) { classes.unionSets(a, b); }
    auto nodes() const {
        return llvm::map_range(classes, [](const auto * node) { return node->getData(); });
    }
    static bool closed(ctjs::FuncOp fn) {
        return fn && !fn.getBody().empty() &&
               mlir::SymbolTable::getSymbolVisibility(fn) == mlir::SymbolTable::Visibility::Private;
    }
    static bool exactCall(ctjs::CallDirectOp call, ctjs::FuncOp fn) {
        return closed(fn) && call->getNumOperands() == fn.getBody().front().getNumArguments();
    }
    // Only the returned-table census requests these structural storage edges.
    // Other flow consumers must not acquire new escape permissions implicitly.
    void connectOwnedMethodTableSlots(const OwnedMethodTableSlots & slots) {
        for (const OwnedMethodTableSlot & slot : slots.slots()) {
            auto initialization = slot.initialization;
            for (ctjs::GetPropertyOp read : slot.reads) {
                join(initialization.getValue(), read.getResult());
            }
        }
    }
    // Only a complete live global-owner query can connect an exported
    // field. This does not turn arbitrary global loads into local aliases.
    void connectOwnedGlobalMethodTables(const OwnedGlobalRoots & roots) {
        for (const OwnedGlobalRoot & root : roots.roots()) {
            if (!root.methodTable) { continue; }
            auto initialization = root.fieldInitialization;
            for (ctjs::GetPropertyOp read : root.reads) {
                join(initialization.getValue(), read.getResult());
            }
        }
    }
    void build(mlir::ModuleOp module) {
        module.walk([&](ctjs::FuncOp fn) {
            fn.getBody().walk([&](ctjs::ReturnOp ret) { returns[fn].push_back(ret); });
            for (ctjs::ReturnOp ret : returns[fn]) {
                join(returns[fn].front().getValue(), ret.getValue());
            }
        });
        module.walk([&](ctjs::CallDirectOp call) {
            auto fn = call.getTarget();
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

} // namespace ctcompile::ctnative
