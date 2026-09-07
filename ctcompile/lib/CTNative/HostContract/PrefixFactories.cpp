#include "Prefix.h"

#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "llvm/ADT/ScopeExit.h"

namespace ctcompile::ctnative::host_detail {

prefixValue prefixAnalysis::factory(ctjs::CallOp call, ctjs::FuncOp function) {
    if (!followPublication || !call.getArgs().empty() || !function ||
        function.getUpvalueCount() != 0 || !llvm::hasSingleElement(function.getBody()) ||
        function.getBody().front().getNumArguments() != 3) {
        return {};
    }
    // Build fresh abstract identities for this invocation. Nothing here runs
    // at compile time: allocation, failure and stores remain in source order.
    // A failed summary cannot leak partial table contents into the prefix.
    const auto originalObjects = objects.size();
    bool accepted = false;
    const llvm::scope_exit rollback([&] {
        if (!accepted) { objects.resize(originalObjects); }
    });
    environment values, cells;
    llvm::DenseMap<mlir::Value, unsigned> cellWrites;
    llvm::DenseMap<unsigned, ctjs::CreateObjectOp> tables;
    llvm::DenseSet<mlir::Operation *> closures;
    HostPrefixFactory proof{call, function, {}, {}, {}};
    prefixValue returned;
    for (mlir::Operation & operation : function.getBody().front()) {
        if (!step()) { return {}; }
        if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
            values[constant.getResult()] = prefixValue::constant(constant.getValue());
        } else if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(operation)) {
            const auto initial = values.lookup(cell.getInitial());
            if (initial.kind == prefixValue::Kind::unknown ||
                initial.kind == prefixValue::Kind::closure) {
                return {};
            }
            cells[cell.getResult()] = initial;
            cellWrites[cell.getResult()] = 0;
        } else if (auto store = llvm::dyn_cast<ctjs::CellSetOp>(operation)) {
            auto slot = cells.find(store.getCell());
            const auto value = values.lookup(store.getValue());
            if (slot == cells.end() || ++cellWrites[store.getCell()] != 1 ||
                value.kind == prefixValue::Kind::unknown ||
                value.kind == prefixValue::Kind::closure) {
                return {};
            }
            slot->second = value;
        } else if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(operation)) {
            auto slot = cells.find(read.getCell());
            if (slot == cells.end()) { return {}; }
            values[read.getResult()] = slot->second;
        } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            if (load.getName() != "Map" || !llvm::is_contained(contract.initialIntrinsics, "Map")) {
                return {};
            }
        } else if (auto construct = llvm::dyn_cast<ctjs::ConstructOp>(operation)) {
            auto load = construct.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
            if (!load || load.getName() != "Map" || construct.getNewTarget() != load.getResult() ||
                !construct.getArgs().empty()) {
                return {};
            }
            const auto id = static_cast<unsigned>(proof.resources.size());
            proof.resources.push_back(construct);
            values[construct.getResult()] = {prefixValue::Kind::resource, {}, {}, id};
        } else if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
            const auto id = static_cast<unsigned>(objects.size());
            objects.emplace_back();
            tables[id] = made;
            values[made.getResult()] = {prefixValue::Kind::object, {}, {}, id};
        } else if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
            // Reuse the independent capture proof: exact source target and
            // parent, local capture indices, unused receiver/new.target,
            // no binding writes, forwarding closures or callee publication.
            // Charge its whole-module scans before asking the shared query.
            for (unsigned scan = 0; scan < 6; ++scan) {
                if (!spend(operationCount)) { return {}; }
            }
            if (!immutableClosureTarget(closure, module)) { return {}; }
            for (mlir::Value capture : closure.getUpvalues()) {
                auto cell = cells.find(capture);
                if (cell == cells.end() || cell->second.kind != prefixValue::Kind::resource) {
                    return {};
                }
            }
            closures.insert(closure);
            values[closure.getResult()] = {prefixValue::Kind::closure,
                                           {},
                                           closure,
                                           0,
                                           static_cast<unsigned>(factories.size()) + 1};
        } else if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            const auto owner = values.lookup(store.getObject());
            const auto value = values.lookup(store.getValue());
            const auto key = keyOf(store.getKey());
            if (owner.kind != prefixValue::Kind::object || !tables.contains(owner.object) ||
                !ordinaryKey(key) ||
                (value.kind != prefixValue::Kind::closure &&
                 value.kind != prefixValue::Kind::primitive)) {
                return {};
            }
            objects[owner.object][key] = value;
        } else if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
            returned = values.lookup(result.getValue());
        } else if (!llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) {
            // No arbitrary call, descriptor/prototype operation, global
            // mutation, branch, coercion or exception is summarized.
            return {};
        }
    }
    if (returned.kind != prefixValue::Kind::object || !tables.contains(returned.object) ||
        proof.resources.empty()) {
        return {};
    }
    // Cells cannot escape through an unmodeled path. Even a cell that is not
    // in the returned table must have only these local uses. Stored Map values
    // likewise cannot escape separately through a property or return.
    for (const auto & entry : cells) {
        const mlir::Value cell = entry.first;
        for (mlir::OpOperand & use : cell.getUses()) {
            if (!step()) { return {}; }
            auto * user = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(user)) { continue; }
            if (llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) {
                continue;
            }
            if (llvm::isa<ctjs::CreateClosureOp>(user) && closures.contains(user) &&
                use.getOperandNumber() >= 2) {
                continue;
            }
            return {};
        }
    }
    for (auto resource : proof.resources) {
        for (mlir::OpOperand & use : resource.getResult().getUses()) {
            if (!step()) { return {}; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (auto store = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
                store && use.getOperandNumber() == 1 && cells.contains(store.getCell())) {
                continue;
            }
            if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner());
                cell && cells.contains(cell.getResult())) {
                continue;
            }
            return {};
        }
    }
    llvm::DenseSet<mlir::Operation *> retained;
    for (const auto & field : objects[returned.object]) {
        auto value = field.second;
        if (value.kind != prefixValue::Kind::closure) { continue; }
        for (auto [index, capture] : llvm::enumerate(value.made.getUpvalues())) {
            const auto final = cells.find(capture);
            // A cell's creation-time value is insufficient: its one explicit
            // store may follow closure creation and replace that resource.
            if (final == cells.end() || final->second.kind != prefixValue::Kind::resource ||
                final->second.object >= proof.resources.size()) {
                return {};
            }
            const auto resource = proof.resources[final->second.object];
            proof.captures.push_back({field.first().str(), value.made, static_cast<unsigned>(index),
                                      capture.getDefiningOp<ctjs::CreateCellOp>(), resource});
            retained.insert(resource);
        }
    }
    if (retained.size() != proof.resources.size()) { return {}; }
    llvm::sort(proof.captures, [](const auto & left, const auto & right) {
        return left.property < right.property ||
               (left.property == right.property && left.index < right.index);
    });
    proof.table = tables.lookup(returned.object);
    factoryTables[returned.object] = static_cast<unsigned>(factories.size());
    factories.push_back(std::move(proof));
    factoryClosures.insert(closures.begin(), closures.end());
    accepted = true;
    return returned;
}

void prefixAnalysis::publication(ctjs::SetPropertyOp write, prefixValue owner, prefixValue value) {
    if (!followPublication || value.kind != prefixValue::Kind::object) { return; }
    const auto factory = factoryTables.find(value.object);
    if (factory == factoryTables.end()) { return; }
    for (const auto & root : contract.roots) {
        const auto binding = globals.find(root.binding);
        if (binding == globals.end() || binding->second.kind != owner.kind ||
            (owner.kind != prefixValue::Kind::realm &&
             (owner.kind != prefixValue::Kind::object || binding->second.object != owner.object))) {
            continue;
        }
        const auto key = keyOf(write.getKey());
        if (llvm::is_contained(root.properties, key)) {
            publications.push_back(
                {write, factories[factory->second].operation, root.binding, key.str()});
        }
    }
}

} // namespace ctcompile::ctnative::host_detail
