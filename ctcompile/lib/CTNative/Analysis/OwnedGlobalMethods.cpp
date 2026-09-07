#include "OwnedGlobalRoots.h"

#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {
namespace {
llvm::StringRef keyOf(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    auto key =
        constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue()) : ctjs::StringAttr{};
    return key ? key.getValue() : llvm::StringRef{};
}
} // namespace

void OwnedGlobalRoots::analyzeMethodTable(mlir::ModuleOp module, const HostContract & contract,
                                          const HostContractAnalysis & host, unsigned maxSteps) {
    const auto spend = [&] {
        if (workSteps == maxSteps) {
            budgetExhausted = true;
            refusal = "owned global method analysis work budget exhausted";
            return false;
        }
        ++workSteps;
        return true;
    };
    const auto reject = [&](llvm::StringRef why) {
        if (refusal.empty()) { refusal = why.str(); }
    };
    const auto & slot = host.slots().front();
    auto owner = slot.owner;
    auto field = slot.writes.front();
    auto factoryCall = field.getValue().getDefiningOp<ctjs::CallDirectOp>();
    auto factory = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
        factoryCall, factoryCall.getCalleeAttr());
    auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    const auto capture = host.callables().empty() ? std::optional<HostCapturedMap>{}
                                                  : host.callables().front().capturedMap;
    auto publicationScope = field->getParentOfType<ctjs::FuncOp>();
    auto wrapper = publicationScope != entry ? publicationScope : ctjs::FuncOp{};
    if (!spend()) { return; }
    if (!factory || factory == entry || factory.getBody().empty() ||
        factory.getUpvalueCount() != 0 || factoryCall->getNumOperands() != 3 ||
        factory.getBody().front().getNumArguments() != 3 ||
        owner->getParentOfType<ctjs::FuncOp>() != entry || (!capture && wrapper) ||
        (capture && !wrapper) || factoryCall->getParentOfType<ctjs::FuncOp>() != publicationScope ||
        host.callables().empty()) {
        reject("owned global method table requires one uncaptured factory and visible calls");
        return;
    }

    auto first = host.callables().front();
    auto method = first.function;
    auto closure = first.closure;
    auto methodInitialization = first.write;
    auto table = methodInitialization.getObject().getDefiningOp<ctjs::CreateObjectOp>();
    if (!table || method == entry || method == factory ||
        table->getParentOfType<ctjs::FuncOp>() != factory ||
        closure->getParentOfType<ctjs::FuncOp>() != factory ||
        methodInitialization->getParentOfType<ctjs::FuncOp>() != factory) {
        reject("owned global method table requires a fresh factory-local table and closure");
        return;
    }
    if (capture &&
        (capture->allocation->getParentOfType<ctjs::FuncOp>() != factory || wrapper == method ||
         wrapper == factory || wrapper.getUpvalueCount() != 0 || wrapper.getBody().empty() ||
         (wrapper.getBody().front().getNumArguments() != 3 &&
          wrapper.getBody().front().getNumArguments() != 4))) {
        reject("owned global captured table requires one wrapper, factory and Map environment");
        return;
    }

    llvm::SmallVector<mlir::Operation *> operations;
    unsigned functions = 0;
    module.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * operation) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        if (operation == module.getOperation()) { return mlir::WalkResult::advance(); }
        if (auto function = llvm::dyn_cast<ctjs::FuncOp>(operation)) {
            ++functions;
            if ((function != entry && function != factory && function != method &&
                 function != wrapper) ||
                function->getParentOp() != module || !llvm::hasSingleElement(function.getBody()) ||
                function->hasAttr("ctjs.skipped")) {
                reject("owned global method table requires its exact straight-line source "
                       "functions");
            }
        } else {
            if (!llvm::isa<ctjs::FuncOp>(operation->getParentOp()) ||
                operation->getNumRegions() != 0) {
                reject("owned global method table requires unconditional straight-line operations");
            }
            operations.push_back(operation);
        }
        return refusal.empty() ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
    });
    if (!refusal.empty()) { return; }
    if (functions != (wrapper ? 4u : 3u)) {
        reject("owned global method table requires its exact source function chain");
        return;
    }

    ctjs::StoreGlobalOp initialization;
    llvm::SmallVector<ctjs::LoadGlobalOp> loads;
    ctjs::ReturnOp factoryReturn;
    ctjs::CallDirectOp wrapperCall;
    ctjs::ReturnOp wrapperReturn;
    llvm::DenseSet<mlir::Operation *> methodReads;
    llvm::DenseSet<mlir::Operation *> methodCalls;
    for (const HostCallableEdge & edge : host.callables()) {
        if (!spend()) { return; }
        if (edge.function != method || edge.closure != closure ||
            edge.write != methodInitialization ||
            edge.capturedMap.has_value() != capture.has_value() ||
            (capture && (edge.capturedMap->allocation != capture->allocation ||
                         edge.capturedMap->cell != capture->cell ||
                         edge.capturedMap->size != capture->size)) ||
            edge.call->getParentOfType<ctjs::FuncOp>() != entry) {
            reject("owned global method table has another callable or invocation context");
            return;
        }
        methodReads.insert(edge.read);
        methodCalls.insert(edge.call);
    }
    for (mlir::Operation * operation : operations) {
        if (!spend()) { return; }
        if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
            if (made != owner && made != table) {
                reject("owned global method table has another allocation");
            }
        }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation);
            store && store.getName() == slot.binding) {
            if (initialization || store.getValue() != owner.getResult() ||
                store->getParentOp() != entry) {
                reject("owned global method binding must publish its sole source allocation once");
            }
            initialization = store;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
            load && load.getName() == slot.binding) {
            if (load->getParentOp() != entry && load->getParentOp() != wrapper) {
                reject("owned global method binding cannot be read by another activation");
            }
            loads.push_back(load);
        }
        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            if (write != field && write != methodInitialization) {
                reject("owned global method fields cannot be replaced or extended");
            }
        }
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            if (!llvm::is_contained(slot.reads, read) && !methodReads.contains(read) &&
                (!capture || read != capture->size)) {
                reject("owned global method field read lacks a complete live callable edge");
            }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
            wrapper && call && call.getCallee() == wrapper.getSymName()) {
            if (wrapperCall || call->getParentOp() != entry ||
                call->getNumOperands() != wrapper.getBody().front().getNumArguments()) {
                reject("owned global wrapper requires exactly one entry invocation");
            }
            wrapperCall = call;
        }
        if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(operation) &&
            operation != factoryCall.getOperation() && operation != wrapperCall.getOperation() &&
            !methodCalls.contains(operation)) {
            reject("owned global method table has another call or factory invocation");
        }
        if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation);
            returned && returned->getParentOp() == factory) {
            if (factoryReturn || returned.getValue() != table.getResult()) {
                reject("owned global factory must return its initialized table directly");
            }
            factoryReturn = returned;
        }
        if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation);
            wrapper && returned && returned->getParentOp() == wrapper) {
            auto value = returned.getValue().getDefiningOp<ctjs::ConstantOp>();
            if (wrapperReturn || !value || !llvm::isa<ctjs::UndefinedAttr>(value.getValue())) {
                reject("owned global wrapper must return undefined after publication");
            }
            wrapperReturn = returned;
        }
        if (!refusal.empty()) { return; }
    }
    if (!initialization || !factoryReturn || !owner->isBeforeInBlock(initialization) ||
        !table->isBeforeInBlock(methodInitialization) ||
        !closure->isBeforeInBlock(methodInitialization) ||
        !methodInitialization->isBeforeInBlock(factoryReturn) ||
        !factoryCall->isBeforeInBlock(field) ||
        (wrapper &&
         (!wrapperCall || !wrapperReturn || !initialization->isBeforeInBlock(wrapperCall) ||
          !field->isBeforeInBlock(wrapperReturn)))) {
        reject("owned global method table lacks unconditional allocation and initialization order");
        return;
    }

    llvm::DenseSet<mlir::Value> owners{owner.getResult()};
    for (ctjs::LoadGlobalOp load : loads) {
        if (!spend()) { return; }
        auto * position =
            load->getParentOp() == wrapper ? wrapperCall.getOperation() : load.getOperation();
        if (!initialization->isBeforeInBlock(position) ||
            (load->getParentOp() == wrapper && !load->isBeforeInBlock(field))) {
            reject("owned global method load precedes binding initialization");
            return;
        }
        owners.insert(load.getResult());
    }
    if (!owners.contains(field.getObject()) || keyOf(field.getKey()) != slot.property) {
        reject("owned global method publication does not name the checked root field");
        return;
    }
    llvm::DenseSet<mlir::Value> tables{table.getResult(), factoryCall.getResult()};
    for (ctjs::GetPropertyOp read : slot.reads) {
        if (!spend()) { return; }
        const auto * edge = host.property(read);
        if (read->getParentOp() != entry || !owners.contains(read.getObject()) || !edge ||
            edge->write != field ||
            !(wrapper ? wrapperCall.getOperation() : field.getOperation())->isBeforeInBlock(read)) {
            reject("owned global method read lacks its sole definite publication");
            return;
        }
        tables.insert(read.getResult());
    }
    for (mlir::Value alias : owners) {
        for (mlir::OpOperand & use : alias.getUses()) {
            if (!spend()) { return; }
            auto * user = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(user) || user == initialization.getOperation()) {
                continue;
            }
            if (use.getOperandNumber() == 0 &&
                (user == field.getOperation() ||
                 llvm::is_contained(slot.reads, llvm::dyn_cast<ctjs::GetPropertyOp>(user)))) {
                continue;
            }
            reject("owned global method owner has another alias or publication use");
            return;
        }
    }
    for (mlir::Value alias : tables) {
        for (mlir::OpOperand & use : alias.getUses()) {
            if (!spend()) { return; }
            auto * user = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(user) || user == factoryReturn.getOperation()) { continue; }
            if (user == field.getOperation() && use.getOperandNumber() == 2) { continue; }
            if (user == methodInitialization.getOperation() && use.getOperandNumber() == 0 &&
                alias == table.getResult()) {
                continue;
            }
            if (methodReads.contains(user) && use.getOperandNumber() == 0) { continue; }
            if (methodCalls.contains(user) &&
                use.getOperandNumber() == (llvm::isa<ctjs::CallOp>(user) ? 1u : 0u)) {
                continue;
            }
            reject("owned global method table has another alias, inspection or publication use");
            return;
        }
    }
    for (const HostCallableEdge & edge : host.callables()) {
        if (!spend()) { return; }
        auto read = edge.read;
        if (!tables.contains(read.getObject())) {
            reject("owned global callable receiver is outside its owning table family");
            return;
        }
    }

    // The query exposes only source ownership. A later consumer must prove a
    // callable carrier and native call component; no annotation closes them.
    OwnedGlobalRoot result{owner, initialization, std::move(loads), field,
                           {},    slot.binding,   slot.property,    std::nullopt};
    result.reads.append(slot.reads.begin(), slot.reads.end());
    result.methodTable.emplace(OwnedGlobalMethodTable{factoryCall,
                                                      factory,
                                                      table,
                                                      methodInitialization,
                                                      closure,
                                                      method,
                                                      {},
                                                      wrapper,
                                                      wrapperCall,
                                                      capture});
    result.methodTable->calls.append(host.callables().begin(), host.callables().end());
    llvm::DenseMap<mlir::Operation *, unsigned> committed;
    for (ctjs::LoadGlobalOp load : result.loads) {
        if (!spend()) { return; }
        committed[load] = 0;
    }
    for (ctjs::GetPropertyOp read : result.reads) {
        if (!spend()) { return; }
        committed[read] = 0;
    }
    if (!spend()) { return; }
    committed[owner] = 0;
    committed[initialization] = 0;
    committed[field] = 0;
    checked.push_back(std::move(result));
    edges = std::move(committed);
}

} // namespace ctcompile::ctnative
