//===- ClosedValueFlow.h - Value flow across direct calls -------*- C++ -*-===//
//
// A union-find over SSA values that may carry the same value through direct
// calls and returns in a closed program, plus opt-in edges for proved storage
// (method-table slots, owned globals, native closure environments).
//
//===----------------------------------------------------------------------===//
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
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

namespace ctcompile::ctnative {

/// Union-find over SSA values that may carry the same value across direct
/// calls: call arguments join callee parameters, every return of a function
/// joins its other returns and each caller's result. These are possible
/// value flows, not runtime aliases; distinct invocations of one factory
/// share a target and schema but keep separate environments.
struct closedValueFlow {
    llvm::EquivalenceClasses<mlir::Value> classes;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::ReturnOp>> returns;

    /// Adds \p value as its own family.
    void add(mlir::Value value) { classes.insert(value); }
    /// The family's leader, or null for a value never added. Consumers report
    /// the first failing member in insertion order, which nodes() preserves.
    mlir::Value find(mlir::Value value) const {
        const auto leader = classes.findLeader(value);
        return leader == classes.member_end() ? mlir::Value{} : *leader;
    }
    /// Merges the families of \p a and \p b.
    void join(mlir::Value a, mlir::Value b) { classes.unionSets(a, b); }
    /// Every value added, in insertion order.
    auto nodes() const {
        return llvm::map_range(classes, [](const auto * node) { return node->getData(); });
    }
    /// A private function with a body: every caller is visible in the module.
    static bool closed(ctjs::FuncOp fn) { return fn && !fn.getBody().empty() && fn.isPrivate(); }
    /// A call into a closed function that passes its whole entry-argument window.
    static bool exactCall(ctjs::CallDirectOp call, ctjs::FuncOp fn) {
        return closed(fn) && call->getNumOperands() == fn.getBody().front().getNumArguments();
    }
    /// A declared entry parameter of a closed function with visible callers.
    /// Entry arguments 0-2 are the implicit receiver, new.target and callee;
    /// declared parameters start at 3.
    bool closedParameter(mlir::BlockArgument arg) {
        auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
        return closed(fn) && arg.getOwner() == &fn.getBody().front() && arg.getArgNumber() >= 3 &&
               !callers[fn].empty();
    }
    /// Every visible call of a closedParameter's function is exact.
    bool exactCallers(mlir::BlockArgument arg) {
        auto fn = llvm::cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
        return llvm::all_of(callers[fn],
                            [&](ctjs::CallDirectOp call) { return exactCall(call, fn); });
    }
    /// An exact call of a closed function with visible returns.
    bool closedResult(ctjs::CallDirectOp call) {
        auto fn = call.getTarget();
        return exactCall(call, fn) && !returns[fn].empty();
    }
    /// An erased capture, or a capture into a proved native closure environment.
    static bool captured(mlir::OpOperand & use) {
        return erasedCapture(use) ||
               (llvm::isa<ctjs::CreateClosureOp>(use.getOwner()) && use.getOperandNumber() >= 2 &&
                !environmentTarget(use.getOwner()).empty());
    }
    /// Joins each owned method-table slot's reads to its initialization. Only
    /// the returned-table census requests these structural storage edges;
    /// other flow consumers must not acquire new escape permissions implicitly.
    void connectOwnedMethodTableSlots(const OwnedMethodTableSlots & slots) {
        for (const OwnedMethodTableSlot & slot : slots.slots()) {
            joinReads(slot.initialization, slot.reads);
        }
    }
    /// Joins the reads of each owned global method table to its field
    /// initialization. Only a complete live global-owner query can connect an
    /// exported field; arbitrary global loads do not become local aliases.
    void connectOwnedGlobalMethodTables(const OwnedGlobalRoots & roots) {
        for (const OwnedGlobalRoot & root : roots.roots()) {
            if (root.methodTable) { joinReads(root.fieldInitialization, root.reads); }
        }
    }
    /// Every read of a slot yields the value its initialization stored.
    void joinReads(ctjs::SetPropertyOp initialization, llvm::ArrayRef<ctjs::GetPropertyOp> reads) {
        for (ctjs::GetPropertyOp read : reads) {
            join(initialization.getValue(), read.getResult());
        }
    }
    /// Joins each native environment read to the captured value it reads.
    /// These are opt-in edges from the native closure proof, not generic flow
    /// permission. When \p reads is given, it receives the reads recognized,
    /// which identity consumers also need.
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
    /// The reads and the writes connectRunOnceStatics joined.
    llvm::DenseSet<mlir::Value> staticReads;
    llvm::DenseSet<mlir::Operation *> staticWrites;
    /// Joins the one write of each run-once static (the variable a binding
    /// of a function that runs once becomes, RunOnce.cpp) to every read of
    /// it, when every read runs after that write: a read that could run first
    /// finds the undefined the binding was hoisted with. A read runs after the
    /// write when it sits after the write in the write's block, or in a
    /// function that only operations running after the write can start: a
    /// creation of one of its closures, or a use of its symbol, climbed the
    /// same way. A function nothing in the module starts (the script entry,
    /// one the host calls) never qualifies. Opt-in, like the storage edges
    /// above: only the native Map proof asks for it.
    void connectRunOnceStatics(mlir::ModuleOp module) {
        llvm::MapVector<mlir::Attribute, llvm::SmallVector<ctjs::CreateCellOp>> statics;
        module.walk([&](ctjs::CreateCellOp cell) {
            if (auto name = cell->getAttr(kNativeStaticCell)) { statics[name].push_back(cell); }
        });
        if (statics.empty()) { return; }
        llvm::DenseMap<unsigned, mlir::Operation *> byIndex;
        module.walk([&](ctjs::FuncOp fn) {
            if (const auto index = ctjs::functionIndex(fn)) { byIndex[*index] = fn; }
        });
        llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> starters;
        module.walk([&](ctjs::CreateClosureOp made) {
            const auto index = ctjs::closureFunction(made);
            if (mlir::Operation * fn = index ? byIndex.lookup(*index) : nullptr) {
                starters[fn].push_back(made);
            }
        });
        if (auto uses = mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())) {
            for (const mlir::SymbolTable::SymbolUse & use : *uses) {
                if (mlir::Operation * fn = mlir::SymbolTable::lookupNearestSymbolFrom(
                        module.getOperation(), use.getSymbolRef())) {
                    starters[fn].push_back(use.getUser());
                }
            }
        }
        const auto runsAfter = [&](mlir::Operation * op, mlir::Operation * write) {
            mlir::Block * body = write->getBlock();
            auto owner = write->getParentOfType<ctjs::FuncOp>();
            llvm::SmallVector<mlir::Operation *> pending{op};
            llvm::DenseSet<mlir::Operation *> seen;
            while (!pending.empty()) {
                mlir::Operation * at = pending.pop_back_val();
                if (!seen.insert(at).second) { continue; }
                if (seen.size() > 256) { return false; }
                auto holder = at->getParentOfType<ctjs::FuncOp>();
                if (!holder) { return false; }
                if (holder == owner) {
                    mlir::Operation * level = at;
                    while (level && level->getBlock() != body) { level = level->getParentOp(); }
                    if (!level || level == write || !write->isBeforeInBlock(level)) {
                        return false;
                    }
                    continue;
                }
                const auto found = starters.find(holder.getOperation());
                if (found == starters.end()) { return false; }
                pending.append(found->second.begin(), found->second.end());
            }
            return true;
        };
        for (auto & [name, cells] : statics) {
            ctjs::CellSetOp write;
            llvm::SmallVector<ctjs::CellGetOp> reads;
            const auto owners = llvm::count_if(cells, [](ctjs::CreateCellOp cell) {
                return !cell->hasAttr(kNativeStaticReference);
            });
            bool usable = owners == 1;
            for (ctjs::CreateCellOp cell : cells) {
                for (mlir::OpOperand & use : cell.getResult().getUses()) {
                    if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(use.getOwner())) {
                        reads.push_back(read);
                    } else if (auto set = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
                               set && use.getOperandNumber() == 0 && !write &&
                               !cell->hasAttr(kNativeStaticReference)) {
                        write = set;
                    } else if (!llvm::isa<ctjs::RootOp>(use.getOwner())) {
                        usable = false;
                    }
                }
            }
            const auto afterWrite = [&](ctjs::CellGetOp read) { return runsAfter(read, write); };
            if (!usable || !write || !llvm::all_of(reads, afterWrite)) { continue; }
            staticWrites.insert(write);
            for (ctjs::CellGetOp read : reads) {
                join(write.getValue(), read.getResult());
                staticReads.insert(read.getResult());
            }
        }
    }
    /// The capture parameters and the call operands
    /// connectArrayCallbackCaptures joined.
    llvm::DenseSet<mlir::Value> callbackCaptures;
    llvm::DenseSet<mlir::OpOperand *> callbackCaptureUses;
    /// Joins what an array-method call passes for its callback's captures
    /// (ClosureLifting/ArrayCallbacks.cpp: the call's leading arguments, in
    /// capture order) to the callback's capture parameters. The runtime binds
    /// those values to the callback, and nothing else calls it. Opt-in: only
    /// the native Map proof asks for it.
    void connectArrayCallbackCaptures(mlir::ModuleOp module) {
        module.walk([&](ctjs::CallOp call) {
            auto name = call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeArrayCallback);
            auto callback =
                name ? mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, name)
                     : ctjs::FuncOp{};
            const auto captures =
                callback ? callback->getAttrOfType<mlir::IntegerAttr>(kNativeCaptures) : nullptr;
            if (!captures || callback.getBody().empty()) { return; }
            mlir::Block & entry = callback.getBody().front();
            const auto count = static_cast<unsigned>(captures.getInt());
            for (unsigned k = 0; k < count && k < call.getArgs().size() &&
                                 ctjs::implicit_arguments + k < entry.getNumArguments();
                 ++k) {
                const mlir::BlockArgument parameter =
                    entry.getArgument(ctjs::implicit_arguments + k);
                join(call.getArgs()[k], parameter);
                callbackCaptures.insert(parameter);
                callbackCaptureUses.insert(&call.getArgsMutable()[k]);
            }
        });
    }
    /// Records every function's returns and every direct call's edges.
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
