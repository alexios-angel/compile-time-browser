#include "OwnedGlobalRoots.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

#include <utility>

namespace ctcompile::ctnative {
namespace {
llvm::StringRef keyOf(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    auto key =
        constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue()) : ctjs::StringAttr{};
    return key ? key.getValue() : llvm::StringRef{};
}
} // namespace

OwnedGlobalRoots::OwnedGlobalRoots(mlir::ModuleOp module, const HostContract & contract,
                                   unsigned maxSteps) {
    // The complete environment proof is mandatory, even if its diagnostic
    // reports already contain a usable-looking field edge before a refusal.
    HostContractAnalysis host(module, contract, maxSteps);
    workSteps = host.steps();
    budgetExhausted = host.exhausted();
    if (!host.proved()) {
        refusal = host.reason().str();
        return;
    }
    const auto spend = [&] {
        if (workSteps == maxSteps) {
            budgetExhausted = true;
            refusal = "owned global root analysis work budget exhausted";
            return false;
        }
        ++workSteps;
        return true;
    };
    if (!spend()) { return; }
    if (contract.roots.size() != 1 || contract.roots.front().properties.size() != 1 ||
        host.slots().size() != 1) {
        refusal = "owned global roots require exactly one binding and one fixed field";
        return;
    }
    const HostSlotReport & slot = host.slots().front();
    if (slot.writes.size() != 1 || slot.reads.empty()) {
        refusal = "owned global field requires one initialization and supported reads";
        return;
    }
    for (const std::string & observation : contract.observations) {
        if (!spend()) { return; }
        if (observation == slot.binding) {
            refusal = "owned global binding cannot be a scalar observation";
            return;
        }
    }

    auto field = slot.writes.front();
    if (llvm::isa_and_nonnull<ctjs::CallDirectOp, ctjs::CallOp>(field.getValue().getDefiningOp())) {
        analyzeMethodTable(module, contract, host, maxSteps);
        return;
    }

    const auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    llvm::SmallVector<mlir::Operation *> operations;
    llvm::DenseMap<mlir::Operation *, unsigned> order;
    ctjs::CreateObjectOp owner;
    ctjs::StoreGlobalOp initialization;
    llvm::SmallVector<ctjs::LoadGlobalOp> loads;
    module.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * operation) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        const auto reject = [&](llvm::StringRef reason) {
            refusal = reason.str();
            return mlir::WalkResult::interrupt();
        };
        if (operation == module.getOperation()) { return mlir::WalkResult::advance(); }
        if (operation->hasAttr("ctjs.skipped")) {
            return reject("owned global roots require fully imported source functions");
        }
        if (auto function = llvm::dyn_cast<ctjs::FuncOp>(operation)) {
            if (function != entry || function->getParentOp() != module ||
                !llvm::hasSingleElement(function.getBody())) {
                return reject("owned global roots require one straight-line script entry");
            }
            return mlir::WalkResult::advance();
        }
        if (operation->getParentOp() != entry || operation->getNumRegions() != 0) {
            return reject("owned global roots require unconditional straight-line operations");
        }
        if (llvm::isa<ctjs::CreateClosureOp, ctjs::CallOp, ctjs::CallDirectOp>(operation)) {
            return reject("owned global roots do not support callables or calls");
        }
        order[operation] = static_cast<unsigned>(operations.size());
        operations.push_back(operation);
        if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
            if (owner || made != slot.owner) {
                return reject("owned global roots require one source-created ordinary object");
            }
            owner = made;
        }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation);
            store && store.getName() == slot.binding) {
            if (initialization) { return reject("owned global binding cannot be replaced"); }
            initialization = store;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
            load && load.getName() == slot.binding) {
            loads.push_back(load);
        }
        return mlir::WalkResult::advance();
    });
    if (!refusal.empty()) { return; }
    if (!owner || !initialization || initialization.getValue() != owner.getResult() ||
        order.lookup(owner) >= order.lookup(initialization)) {
        refusal = "owned global binding lacks unconditional source allocation and initialization";
        return;
    }

    llvm::DenseSet<mlir::Value> aliases{owner.getResult()};
    for (ctjs::LoadGlobalOp load : loads) {
        if (!spend()) { return; }
        if (order.lookup(initialization) >= order.lookup(load)) {
            refusal = "owned global load precedes its binding initialization";
            return;
        }
        aliases.insert(load.getResult());
    }
    ctjs::SetPropertyOp fieldInitialization;
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
    for (mlir::Operation * operation : operations) {
        if (!spend()) { return; }
        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            if (!aliases.contains(write.getObject()) || keyOf(write.getKey()) != slot.property ||
                fieldInitialization) {
                refusal = "owned global field cannot be replaced or accompanied by other fields";
                return;
            }
            fieldInitialization = write;
        }
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            if (!aliases.contains(read.getObject()) || keyOf(read.getKey()) != slot.property ||
                !fieldInitialization) {
                refusal = "owned global field read lacks its sole definite initialization";
                return;
            }
            reads.push_back(read);
        }
    }
    if (fieldInitialization != slot.writes.front() || reads.size() != slot.reads.size()) {
        refusal = "owned global field census disagrees with the complete host proof";
        return;
    }
    // Scan every identity use, including otherwise harmless comparisons and
    // return operations accepted by the broader environment proof. Only this
    // root binding may publish the allocation; aliases remain that allocation.
    for (mlir::Value alias : aliases) {
        for (mlir::OpOperand & use : alias.getUses()) {
            if (!spend()) { return; }
            mlir::Operation * user = use.getOwner();
            if (user == initialization.getOperation() || llvm::isa<ctjs::RootOp>(user)) {
                continue;
            }
            if (use.getOperandNumber() == 0 &&
                llvm::isa<ctjs::SetPropertyOp, ctjs::GetPropertyOp>(user)) {
                continue;
            }
            refusal = "owned global allocation has an unsupported alias or publication use";
            return;
        }
    }
    llvm::DenseSet<mlir::Operation *> provedReads;
    for (const HostSlotEdge & edge : slot.edges) {
        if (!spend()) { return; }
        if (edge.write != fieldInitialization) {
            refusal = "owned global read does not refer to its sole field initialization";
            return;
        }
        provedReads.insert(edge.read);
    }
    llvm::DenseMap<mlir::Operation *, unsigned> committedEdges;
    for (ctjs::GetPropertyOp read : reads) {
        if (!spend()) { return; }
        if (!provedReads.contains(read)) {
            refusal = "owned global read is absent from the complete host proof";
            return;
        }
        committedEdges[read] = 0;
    }
    for (ctjs::LoadGlobalOp load : loads) {
        if (!spend()) { return; }
        committedEdges[load] = 0;
    }
    if (!spend()) { return; }
    committedEdges[owner] = 0;
    committedEdges[initialization] = 0;
    committedEdges[fieldInitialization] = 0;
    // Commit only after the complete host proof and the stricter owner census
    // finish within their shared budget. No partial operation index escapes.
    checked.push_back({owner, initialization, std::move(loads), fieldInitialization,
                       std::move(reads), slot.binding, slot.property, std::nullopt});
    edges = std::move(committedEdges);
}

const OwnedGlobalRoot * OwnedGlobalRoots::lookup(mlir::Operation * operation) const {
    const auto found = edges.find(operation);
    return found == edges.end() ? nullptr : &checked[found->second];
}

const HostScalarGlobalRead * OwnedGlobalRoots::scalarRead(ctjs::LoadGlobalOp read) const {
    const auto found = scalarEdges.find(read);
    return found == scalarEdges.end() ? nullptr : &checkedScalarReads[found->second];
}

} // namespace ctcompile::ctnative
