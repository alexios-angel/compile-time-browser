#include "OwnedMethodTableSlots.h"

#include "NativeObject/Fields.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

#include <utility>

namespace ctcompile::ctnative {
namespace {
llvm::StringRef keyOf(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    auto key =
        constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue()) : ctjs::StringAttr{};
    return key ? key.getValue() : llvm::StringRef{};
}

struct accesses {
    llvm::SmallVector<ctjs::SetPropertyOp> writes;
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
};
} // namespace

OwnedMethodTableSlots::OwnedMethodTableSlots(mlir::ModuleOp module, unsigned maxSteps) {
    unsigned steps = 0;
    const auto spend = [&](unsigned amount = 1) {
        if (amount > maxSteps - steps) {
            budgetExhausted = true;
            return false;
        }
        steps += amount;
        return true;
    };
    llvm::SmallVector<ctjs::CreateObjectOp> owners;
    unsigned operations = 0;
    module.walk([&](mlir::Operation * op) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        ++operations;
        if (auto owner = llvm::dyn_cast<ctjs::CreateObjectOp>(op)) { owners.push_back(owner); }
        return mlir::WalkResult::advance();
    });
    if (budgetExhausted || owners.empty()) { return; }
    // Reuse the existing ordinary-field boundary. Descriptor/prototype changes,
    // dynamic property access and reflective operations anywhere in the source
    // cannot be hidden by keeping the particular owner local. This is not an
    // effect summary for calls: normal callable/Map/native admission still runs.
    if (!spend(operations) || !object_detail::scalarFieldEnvironment(module)) { return; }

    mlir::DominanceInfo dominance(module);
    for (ctjs::CreateObjectOp owner : owners) {
        auto function = owner->getParentOfType<ctjs::FuncOp>();
        if (!function || function->getParentOp() != module) { continue; }
        llvm::StringMap<accesses> fields;
        bool confined = true;
        for (mlir::OpOperand & use : owner.getResult().getUses()) {
            if (!spend()) { break; }
            auto * operation = use.getOwner();
            if (operation->getParentOfType<ctjs::FuncOp>() != function) {
                confined = false;
                break;
            }
            if (llvm::isa<ctjs::RootOp>(operation)) { continue; }
            // SSA moves preserve the same value. A real phi, cell, parameter,
            // capture or object/global store is an escape, not another owner.
            if (!object_detail::scalarFieldUse(use)) {
                confined = false;
                break;
            }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                fields[keyOf(store.getKey())].writes.push_back(store);
            } else {
                auto read = llvm::cast<ctjs::GetPropertyOp>(operation);
                fields[keyOf(read.getKey())].reads.push_back(read);
            }
        }
        if (budgetExhausted) { break; }
        if (!confined) { continue; }
        for (auto & field : fields) {
            auto & uses = field.second;
            if (!spend()) { break; }
            if (uses.writes.size() != 1) { continue; }
            auto initialization = uses.writes.front();
            // A source initializer executes once per fresh owner. A store in a
            // child conditional/loop is not initialization of this fixed slot,
            // even when a later read happens to be in the same selected arm.
            if (initialization->getBlock() != owner->getBlock() ||
                !owner->isBeforeInBlock(initialization.getOperation())) {
                continue;
            }
            bool initialized = true;
            for (ctjs::GetPropertyOp read : uses.reads) {
                if (!spend()) { break; }
                if (!dominance.properlyDominates(initialization.getOperation(),
                                                 read.getOperation())) {
                    initialized = false;
                    break;
                }
            }
            if (budgetExhausted) { break; }
            if (!initialized) { continue; }
            checked.push_back({owner, initialization, std::move(uses.reads)});
        }
        if (budgetExhausted) { break; }
    }
    // Exhaustion cannot expose a successful prefix of an unfinished census.
    if (budgetExhausted) {
        checked.clear();
        return;
    }
    for (auto [index, slot] : llvm::enumerate(checked)) {
        stores[slot.initialization] = static_cast<unsigned>(index);
        for (ctjs::GetPropertyOp read : slot.reads) { loads[read] = static_cast<unsigned>(index); }
    }
}

const OwnedMethodTableSlot * OwnedMethodTableSlots::lookup(ctjs::SetPropertyOp store) const {
    const auto found = stores.find(store);
    return found == stores.end() ? nullptr : &checked[found->second];
}

const OwnedMethodTableSlot * OwnedMethodTableSlots::lookup(ctjs::GetPropertyOp read) const {
    const auto found = loads.find(read);
    return found == loads.end() ? nullptr : &checked[found->second];
}

} // namespace ctcompile::ctnative
