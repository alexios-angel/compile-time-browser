//===- OwnedMethodTableSlots.cpp - Method table slots -----------*- C++ -*-===//
//
// Finds object fields that are written exactly once, right after the object
// is created, and then only read, by an object that never leaves its
// function. These are candidate slots of a method table.
//
//===----------------------------------------------------------------------===//
#include "OwnedMethodTableSlots.h"

#include "NativeObject/Fields.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

#include <utility>

namespace ctcompile::ctnative {
namespace {
/// The writes and reads of one field of one object.
struct accesses {
    llvm::SmallVector<ctjs::SetPropertyOp> writes;
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
};
} // namespace

OwnedMethodTableSlots::OwnedMethodTableSlots(mlir::ModuleOp module, unsigned maxSteps)
    : budget{maxSteps} {
    llvm::SmallVector<ctjs::CreateObjectOp> owners;
    unsigned operations = 0;
    module.walk([&](mlir::Operation * op) {
        if (!budget.spend()) { return mlir::WalkResult::interrupt(); }
        ++operations;
        if (auto owner = llvm::dyn_cast<ctjs::CreateObjectOp>(op)) { owners.push_back(owner); }
        return mlir::WalkResult::advance();
    });
    if (budget.exhausted || owners.empty()) { return; }
    // Refuse the whole module if any operation could change a descriptor or
    // prototype, access a property by a dynamic key, or reflect: keeping one
    // object local does not protect it from those. This does not summarise
    // what calls do; the normal callable, Map and native admission still runs.
    if (!budget.spend(operations) || !object_detail::scalarFieldEnvironment(module)) { return; }

    mlir::DominanceInfo dominance(module);
    for (ctjs::CreateObjectOp owner : owners) {
        auto function = owner->getParentOfType<ctjs::FuncOp>();
        if (!function || function->getParentOp() != module) { continue; }
        llvm::StringMap<accesses> fields;
        bool confined = true;
        for (mlir::OpOperand & use : owner.getResult().getUses()) {
            if (!budget.spend()) { break; }
            auto * operation = use.getOwner();
            if (operation->getParentOfType<ctjs::FuncOp>() != function) {
                confined = false;
                break;
            }
            if (llvm::isa<ctjs::RootOp>(operation)) { continue; }
            // Only field accesses with ordinary keys are allowed. A block
            // argument, cell, call argument, capture or store of the object
            // itself is an escape.
            if (!object_detail::scalarFieldUse(use)) {
                confined = false;
                break;
            }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                fields[ctjs::constantKey(store.getKey())].writes.push_back(store);
            } else {
                auto read = llvm::cast<ctjs::GetPropertyOp>(operation);
                fields[ctjs::constantKey(read.getKey())].reads.push_back(read);
            }
        }
        if (budget.exhausted) { break; }
        if (!confined) { continue; }
        for (auto & field : fields) {
            auto & uses = field.second;
            if (!budget.spend()) { break; }
            if (uses.writes.size() != 1) { continue; }
            auto initialization = uses.writes.front();
            // The initialising store must run exactly once per new object, so
            // it must sit in the object's own block. A store inside a nested
            // conditional or loop does not count, even when a later read is
            // in the same arm.
            if (initialization->getBlock() != owner->getBlock() ||
                !owner->isBeforeInBlock(initialization.getOperation())) {
                continue;
            }
            bool initialized = true;
            for (ctjs::GetPropertyOp read : uses.reads) {
                if (!budget.spend()) { break; }
                if (!dominance.properlyDominates(initialization.getOperation(),
                                                 read.getOperation())) {
                    initialized = false;
                    break;
                }
            }
            if (budget.exhausted) { break; }
            if (!initialized) { continue; }
            checked.push_back({owner, initialization, std::move(uses.reads)});
        }
        if (budget.exhausted) { break; }
    }
    // Running out of budget publishes nothing, not the slots found so far.
    if (budget.exhausted) {
        checked.clear();
        return;
    }
    for (auto [index, slot] : llvm::enumerate(checked)) {
        indices[slot.initialization] = static_cast<unsigned>(index);
        for (ctjs::GetPropertyOp read : slot.reads) {
            indices[read] = static_cast<unsigned>(index);
        }
    }
}

const OwnedMethodTableSlot * OwnedMethodTableSlots::lookup(mlir::Operation * operation) const {
    const auto found = indices.find(operation);
    return found == indices.end() ? nullptr : &checked[found->second];
}

} // namespace ctcompile::ctnative
