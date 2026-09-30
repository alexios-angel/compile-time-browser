#pragma once

#include "OwnedGlobalRoots.h"
#include "OwnedMethodTableSlots.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

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
    static bool closed(ctjs::FuncOp fn) { return fn && !fn.getBody().empty() && fn.isPrivate(); }
    static bool exactCall(ctjs::CallDirectOp call, ctjs::FuncOp fn) {
        return closed(fn) && call->getNumOperands() == fn.getBody().front().getNumArguments();
    }
    // A declared entry parameter of a closed function with visible callers.
    bool closedParameter(mlir::BlockArgument arg) {
        auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
        return closed(fn) && arg.getOwner() == &fn.getBody().front() && arg.getArgNumber() >= 3 &&
               !callers[fn].empty();
    }
    // Every visible call of a closedParameter's function is exact.
    bool exactCallers(mlir::BlockArgument arg) {
        auto fn = llvm::cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
        return llvm::all_of(callers[fn],
                            [&](ctjs::CallDirectOp call) { return exactCall(call, fn); });
    }
    // An exact call of a closed function with visible returns.
    bool closedResult(ctjs::CallDirectOp call) {
        auto fn = call.getTarget();
        return exactCall(call, fn) && !returns[fn].empty();
    }
    // An erased capture, or a capture into a proved native closure environment.
    static bool captured(mlir::OpOperand & use) {
        return erasedCapture(use) ||
               (llvm::isa<ctjs::CreateClosureOp>(use.getOwner()) && use.getOperandNumber() >= 2 &&
                !environmentTarget(use.getOwner()).empty());
    }
    // Only the returned-table census requests these structural storage edges.
    // Other flow consumers must not acquire new escape permissions implicitly.
    void connectOwnedMethodTableSlots(const OwnedMethodTableSlots & slots) {
        for (const OwnedMethodTableSlot & slot : slots.slots()) {
            joinReads(slot.initialization, slot.reads);
        }
    }
    // Only a complete live global-owner query can connect an exported
    // field. This does not turn arbitrary global loads into local aliases.
    void connectOwnedGlobalMethodTables(const OwnedGlobalRoots & roots) {
        for (const OwnedGlobalRoot & root : roots.roots()) {
            if (root.methodTable) { joinReads(root.fieldInitialization, root.reads); }
        }
    }
    // Every read of a slot yields the value its initialization stored.
    void joinReads(ctjs::SetPropertyOp initialization, llvm::ArrayRef<ctjs::GetPropertyOp> reads) {
        for (ctjs::GetPropertyOp read : reads) {
            join(initialization.getValue(), read.getResult());
        }
    }
    // Opt-in edges from the native closure proof, not generic flow permission.
    // Identity consumers also need the exact reads recognized by this census.
    void connectEnvironmentReads(mlir::ModuleOp module,
                                 llvm::DenseSet<mlir::Operation *> * reads = nullptr) {
        llvm::StringMap<ctjs::CreateClosureOp> environments;
        module.walk([&](ctjs::CreateClosureOp made) {
            if (!environmentTarget(made).empty()) { environments[environmentTarget(made)] = made; }
        });
        module.walk([&](ctjs::LoadUpvalueOp read) {
            if (!read->hasAttr(kNativeEnvironmentRead)) { return; }
            auto made = environments.lookup(environmentTarget(read));
            if (made && read.getIndex() >= 0 &&
                static_cast<size_t>(read.getIndex()) < made.getUpvalues().size()) {
                join(read.getResult(), made.getUpvalues()[static_cast<size_t>(read.getIndex())]);
                if (reads) { reads->insert(read); }
            }
        });
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
