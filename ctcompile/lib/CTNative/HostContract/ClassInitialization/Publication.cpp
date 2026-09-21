#include "Proof.hpp"
#include "llvm/ADT/ScopeExit.h"

namespace ctcompile::ctnative::class_detail {

bool classInitialization::normalizePublicationKey(ctjs::CreateClosureOp constructor,
                                                  const HostContract & contract) {
    // ponytail: one Map and one immutable String key. Distinct caller keys need
    // per-call specialization; nested Maps still need their own lifetime proof.
    auto function = target(constructor);
    auto scope = constructor->getParentOfType<ctjs::FuncOp>();
    if (contract.provider != HostContract::Provider::closedSource ||
        constructor.getUpvalues().size() != 2 || function.getUpvalueCount() != 2 ||
        !function.getBody().hasOneBlock() || !scope.getBody().hasOneBlock() ||
        constructor->getBlock() != &scope.getBody().front() ||
        earlyCaptures.contains(constructor) ||
        (constructor.getEnclosingIndicesAttr() &&
         llvm::any_of(constructor.getEnclosingIndicesAttr().asArrayRef(),
                      [](int32_t index) { return index >= 0; }))) {
        return true;
    }
    unsigned mapIndex = 0;
    if (!maps.contains(sourceValue(cells.lookup(constructor.getUpvalues()[0])))) { mapIndex = 1; }
    const unsigned keyIndex = 1 - mapIndex;
    auto mapCell = constructor.getUpvalues()[mapIndex];
    auto keyCell = constructor.getUpvalues()[keyIndex];
    if (!cells.contains(mapCell) || !cells.contains(keyCell) ||
        !maps.contains(sourceValue(cells.lookup(mapCell)))) {
        return true;
    }
    auto key = sourceValue(cells.lookup(keyCell));
    auto literal = key.getDefiningOp<ctjs::ConstantOp>();
    auto value =
        literal ? llvm::dyn_cast<ctjs::StringAttr>(literal.getValue()) : ctjs::StringAttr{};
    auto parameter = llvm::dyn_cast<mlir::BlockArgument>(key);
    if (!value) {
        if (!parameter || parameter.getOwner() != &scope.getBody().front() ||
            parameter.getArgNumber() < ctjs::implicit_arguments ||
            scope.getSymName() == contract.entry || scope.getUpvalueCount() != 0 ||
            mlir::SymbolTable::getSymbolVisibility(scope) !=
                mlir::SymbolTable::Visibility::Private) {
            return true;
        }
        ctjs::CreateClosureOp source;
        auto scanned = module.walk([&](ctjs::CreateClosureOp closure) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (target(closure) == scope) { source = closure; }
            return mlir::WalkResult::advance();
        });
        if (scanned.wasInterrupted()) { return false; }
        if (!source || !source.getUpvalues().empty()) { return true; }
        ctjs::StoreGlobalOp declaration;
        for (mlir::OpOperand & use : source.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            if (direct && use.getOperandNumber() == 2 && direct.getTarget() == scope) { continue; }
            auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(use.getOwner());
            if (!store || declaration || globals.lookup(store.getName()) != store) { return true; }
            declaration = store;
        }
        if (declaration) {
            // Precharge the shared hoisting/declaration proof's module/use scans.
            scanned = module.walk([&](mlir::Operation * op) {
                const uint64_t cost = uint64_t(2) + op->getNumOperands();
                if (cost > remaining) {
                    refuse("class initialization work budget exhausted");
                    return mlir::WalkResult::interrupt();
                }
                remaining -= static_cast<unsigned>(cost);
                return mlir::WalkResult::advance();
            });
            if (scanned.wasInterrupted()) { return false; }
            if (!closedDeclaration(declaration, scope, module)) { return true; }
            for (const auto & root : contract.roots) {
                if (!step()) { return false; }
                if (root.binding == declaration.getName()) { return true; }
            }
        }
        llvm::DenseSet<mlir::Operation *> callers;
        bool exact = true;
        scanned = module.walk([&](ctjs::CallDirectOp call) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (call.getTarget() != scope) { return mlir::WalkResult::advance(); }
            auto loaded = call.getCalleeValue().getDefiningOp<ctjs::LoadGlobalOp>();
            auto actual =
                call->getNumOperands() == scope.getBody().front().getNumArguments()
                    ? call->getOperand(parameter.getArgNumber()).getDefiningOp<ctjs::ConstantOp>()
                    : ctjs::ConstantOp{};
            auto text =
                actual ? llvm::dyn_cast<ctjs::StringAttr>(actual.getValue()) : ctjs::StringAttr{};
            exact &= (call.getCalleeValue() == source.getResult() ||
                      (loaded && declaration && loaded.getName() == declaration.getName())) &&
                     text && (!value || value == text);
            value = text;
            callers.insert(call);
            return mlir::WalkResult::advance();
        });
        if (scanned.wasInterrupted()) { return false; }
        if (!exact || callers.empty()) { return true; }
        // The resolver's diagnostic table is not a caller. Every actual
        // declaration/callee was rechecked above; all other symbol attributes
        // still participate. Restore the report even when this proof refuses.
        auto report = module->removeAttr("ctjs.globals");
        llvm::scope_exit restoreReport([&] {
            if (report) { module->setAttr("ctjs.globals", report); }
        });
        for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                                  mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
            if (!uses) { return true; }
            for (const auto & use : *uses) {
                if (!step()) { return false; }
                if (mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                        use.getUser(), use.getSymbolRef()) != scope) {
                    continue;
                }
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
                if (!call || !callers.contains(call) ||
                    use.getSymbolRef() != call.getCalleeAttr()) {
                    return true;
                }
            }
        }
    }
    llvm::SmallVector<ctjs::LoadUpvalueOp> keys, mapReads;
    auto & body = function.getBody().front();
    for (mlir::OpOperand & use : body.getArgument(ctjs::arg_callee).getUses()) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
        auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
        if (!load || load->getBlock() != &body ||
            (load.getIndex() != keyIndex && load.getIndex() != mapIndex)) {
            return true;
        }
        (load.getIndex() == keyIndex ? keys : mapReads).push_back(load);
    }
    if (keys.empty() || mapReads.empty()) { return true; }
    // No symbolic caller may retain the old environment layout.
    for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                              mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
        if (!uses) { return true; }
        for (const auto & use : *uses) {
            if (!step()) { return false; }
            if (mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                    use.getUser(), use.getSymbolRef()) == function) {
                return true;
            }
        }
    }
    // Charge substitutions/cleanup before mutation; the enclosing pass rolls
    // back this private candidate if any later source or ownership proof fails.
    for (mlir::Value old : {key, mlir::Value(body.getArgument(ctjs::arg_callee))}) {
        for (mlir::OpOperand & use : old.getUses()) {
            (void)use;
            if (!step()) { return false; }
        }
    }
    for (ctjs::LoadUpvalueOp load : keys) {
        for (mlir::OpOperand & use : load.getResult().getUses()) {
            (void)use;
            if (!step()) { return false; }
        }
    }
    const uint64_t cost = uint64_t(2) * (keys.size() + 1) + cells.size();
    if (cost > remaining) { return refuse("class initialization work budget exhausted"); }
    remaining -= static_cast<unsigned>(cost);
    if (parameter) {
        mlir::OpBuilder at(&scope.getBody().front(), scope.getBody().front().begin());
        auto constant = ctjs::ConstantOp::create(at, scope.getLoc(), value);
        parameter.replaceAllUsesWith(constant);
        for (auto & [cell, initial] : cells) {
            (void)cell;
            if (initial == key) { initial = constant; }
        }
    }
    for (ctjs::LoadUpvalueOp load : keys) {
        mlir::OpBuilder at(load);
        auto constant = ctjs::ConstantOp::create(at, load.getLoc(), value);
        load.getResult().replaceAllUsesWith(constant);
        load.erase();
    }
    for (ctjs::LoadUpvalueOp load : mapReads) { load.setIndex(0); }
    constructor.getUpvaluesMutable().assign(mapCell);
    constructor.removeEnclosingIndicesAttr();
    function.setUpvalueCount(1);
    return true;
}

bool classInitialization::normalizePublicationHelper(ctjs::CreateClosureOp constructor,
                                                     const HostContract & contract) {
    if (!normalizePublicationKey(constructor, contract)) { return false; }
    // ponytail: one pure registration helper, direct or in one exact holder slot.
    // Shared helpers and earlier publication need a complete observer proof.
    auto function = target(constructor);
    if (contract.provider != HostContract::Provider::closedSource ||
        constructor.getUpvalues().size() != 1 || !function.getBody().hasOneBlock()) {
        return true;
    }
    auto helperCell = constructor.getUpvalues().front();
    if (!cells.contains(helperCell)) { return true; }
    auto source = sourceValue(cells.lookup(helperCell));
    auto helper = source.getDefiningOp<ctjs::CreateClosureOp>();
    auto holder = source.getDefiningOp<ctjs::CreateObjectOp>();
    auto & body = function.getBody().front();
    ctjs::LoadUpvalueOp selectedHelper;
    for (mlir::OpOperand & use : body.getArgument(ctjs::arg_callee).getUses()) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
        auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
        if (!load || load.getIndex() != 0 || selectedHelper || load->getBlock() != &body) {
            return true;
        }
        selectedHelper = load;
    }
    if (!selectedHelper) { return true; }
    ctjs::SetPropertyOp holderSlot;
    ctjs::GetPropertyOp holderRead;
    ctjs::CallOp invocation;
    if (holder) {
        if (holder->getBlock() != constructor->getBlock() ||
            !holder->isBeforeInBlock(constructor)) {
            return true;
        }
        // The raw cell/alias census below proves every omitted transport.
        // Project only this constructor's sole captured holder read.
        auto proof = analyzeLocalCallableObject(
            holder, [&] { return step(); },
            [&](mlir::Value value) {
                auto uses = sourceUses(value);
                for (mlir::OpOperand & use : selectedHelper.getResult().getUses()) {
                    if (!step()) { break; }
                    uses.push_back(&use);
                }
                return uses;
            });
        if (!proof) {
            llvm::consumeError(proof.takeError());
            return reason.empty();
        }
        if (proof->stores.size() != 1 || proof->reads.size() != 1 || proof->calls.size() != 1) {
            return true;
        }
        holderSlot = proof->stores.front();
        holderRead = proof->reads.front().first;
        helper = proof->reads.front().second;
        invocation = llvm::dyn_cast<ctjs::CallOp>(proof->calls.front());
        if (!ctjs::ordinaryKey(holderSlot.getKey()) || !holderSlot->isBeforeInBlock(constructor) ||
            holderRead.getObject() != selectedHelper.getResult() || !invocation ||
            invocation.getCallee() != holderRead.getResult() ||
            invocation.getReceiver() != selectedHelper.getResult()) {
            return true;
        }
        for (mlir::Operation * user : holderRead->getUsers()) {
            if (!step()) { return false; }
            if (user != invocation && !llvm::isa<ctjs::RootOp>(user)) { return true; }
        }
    }
    auto targetFunction = target(helper);
    if (!targetFunction || helper.getUpvalues().size() != 1 ||
        helper->getBlock() != constructor->getBlock() || !helper->isBeforeInBlock(constructor) ||
        !undefined(helper.getEnclosingThis()) || !targetFunction.getBody().hasOneBlock() ||
        targetFunction.getUpvalueCount() != 1 || function.getUpvalueCount() != 1 ||
        helpers.contains(targetFunction) || capturedHelpers.contains(helper) ||
        mapClosures.contains(helper) || llvm::is_contained(capturedClosures, helper) ||
        (helper.getEnclosingIndicesAttr() &&
         llvm::any_of(helper.getEnclosingIndicesAttr().asArrayRef(),
                      [](int32_t index) { return index >= 0; })) ||
        (constructor.getEnclosingIndicesAttr() &&
         llvm::any_of(constructor.getEnclosingIndicesAttr().asArrayRef(),
                      [](int32_t index) { return index >= 0; }))) {
        return true;
    }
    auto mapCell = helper.getUpvalues().front();
    if (!cells.contains(mapCell)) { return true; }
    auto map = sourceValue(cells.lookup(mapCell)).getDefiningOp<ctjs::ConstructOp>();
    if (!map || !maps.contains(map.getResult()) || map->getBlock() != helper->getBlock() ||
        !map->isBeforeInBlock(constructor)) {
        return true;
    }
    auto & helperBody = targetFunction.getBody().front();
    if (helperBody.getNumArguments() < ctjs::implicit_arguments + 1) { return true; }
    const auto parameters = helperBody.getArguments().drop_front(ctjs::implicit_arguments);
    if ((parameters.size() != 1 && parameters.size() != 2) ||
        !helperBody.getArgument(ctjs::arg_receiver).use_empty() ||
        !helperBody.getArgument(ctjs::arg_new_target).use_empty()) {
        return true;
    }
    ctjs::LoadUpvalueOp captured;
    ctjs::CallOp publication;
    ctjs::GetPropertyOp selection;
    for (mlir::Operation & op : helperBody) {
        if (!step()) { return false; }
        if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
            if (captured || load.getIndex() != 0 ||
                load.getClosure() != helperBody.getArgument(ctjs::arg_callee)) {
                return true;
            }
            captured = load;
        } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            if (selection) { return true; }
            selection = read;
        } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
            if (publication) { return true; }
            publication = call;
        } else if (!llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                              ctjs::ReturnOp>(op)) {
            return true;
        }
    }
    auto returned = llvm::dyn_cast<ctjs::ReturnOp>(helperBody.getTerminator());
    if (!captured || !selection || !publication || !returned || !undefined(returned.getValue()) ||
        selection.getObject() != captured.getResult() ||
        ctjs::constantKey(selection.getKey()) != "set" ||
        publication.getCallee() != selection.getResult() ||
        publication.getReceiver() != captured.getResult() || publication.getArgs().size() != 2 ||
        publication.getArgs()[1] != parameters.back()) {
        return true;
    }
    auto key = publication.getArgs()[0].getDefiningOp<ctjs::ConstantOp>();
    const bool keyed = parameters.size() == 2;
    if (keyed ? publication.getArgs()[0] != parameters.front()
              : !key || !llvm::isa<ctjs::StringAttr>(key.getValue())) {
        return true;
    }
    for (mlir::Operation * user : publication->getUsers()) {
        if (!step()) { return false; }
        if (!llvm::isa<ctjs::RootOp>(user)) { return true; }
    }
    if (!holder) {
        for (mlir::OpOperand & use : selectedHelper.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            if (!call || invocation || use.getOperandNumber() != 0 ||
                !undefined(call.getReceiver())) {
                return true;
            }
            invocation = call;
        }
    }
    if (!invocation || invocation->getBlock() != &body ||
        invocation.getArgs().size() != parameters.size() ||
        invocation.getArgs().back() != body.getArgument(ctjs::arg_receiver)) {
        return true;
    }
    for (mlir::Operation * user : invocation->getUsers()) {
        if (!step()) { return false; }
        if (!llvm::isa<ctjs::RootOp>(user)) { return true; }
    }
    // Raw cell uses matter: sourceUses intentionally omits capture transport.
    // No second helper/Map observer can survive retirement of this helper.
    bool initialized = mapCell.getDefiningOp<ctjs::CreateCellOp>().getInitial() == map.getResult();
    for (auto [cell, owner] : {std::pair{helperCell, constructor}, std::pair{mapCell, helper}}) {
        for (mlir::OpOperand & use : cell.getUses()) {
            if (!step()) { return false; }
            if (auto store = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
                cell == mapCell && store && store->getBlock() == constructor->getBlock() &&
                store->isBeforeInBlock(constructor)) {
                initialized = true;
            }
            if (use.getOwner() != owner && !cellOperations.contains(use.getOwner())) {
                return true;
            }
        }
    }
    if (!initialized) { return true; }
    for (mlir::Operation * user : helper->getUsers()) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::RootOp>(user) || (holder && user == holderSlot)) { continue; }
        auto store = llvm::dyn_cast<ctjs::CellSetOp>(user);
        auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(user);
        if (!cellOperations.contains(user) ||
            (store ? store.getCell() != helperCell : !cell || cell.getResult() != helperCell)) {
            return true;
        }
    }
    for (mlir::OpOperand * use : sourceUses(helper.getResult())) {
        if (!step()) { return false; }
        if (!llvm::isa<ctjs::RootOp>(use->getOwner()) &&
            !(holder && use->getOwner() == holderSlot && use->getOperandNumber() == 2)) {
            return true;
        }
    }
    for (auto [cell, initial] : cells) {
        if (!step()) { return false; }
        auto value = sourceValue(initial);
        if ((cell != helperCell && value == source) || (holder && value == helper.getResult())) {
            return true;
        }
    }
    for (ctjs::CellGetOp read : cellReads) {
        // Precharge the cleanup scan as well; it cannot fail after mutation.
        if (!step() || !step()) { return false; }
        if (read.getCell() != helperCell) { continue; }
        for (mlir::Operation * user : read->getUsers()) {
            if (!step()) { return false; }
            if (!llvm::isa<ctjs::RootOp>(user)) { return true; }
        }
    }
    for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                              mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
        if (!uses) { return true; }
        for (const auto & use : *uses) {
            if (!step()) { return false; }
            if (mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                    use.getUser(), use.getSymbolRef()) == targetFunction) {
                return true;
            }
        }
    }
    if (!reason.empty()) { return false; }
    // Count operations, operands and results before allocation. A forwarded
    // key keeps its original evaluation; the publication proof checks its type.
    const unsigned expansionCost = keyed ? 12 : 14;
    if (remaining < expansionCost) { return refuse("class initialization work budget exhausted"); }
    remaining -= expansionCost;
    // The helper has no other effects or callers. Replace its exact call,
    // then let the existing terminal-publication and owner proofs run afresh.
    mlir::OpBuilder at(invocation);
    auto name = ctjs::ConstantOp::create(at, selection.getLoc(),
                                         ctjs::StringAttr::get(module.getContext(), "set"));
    auto select = ctjs::GetPropertyOp::create(at, selection.getLoc(), selectedHelper, name);
    mlir::Value registrationKey = invocation.getArgs().front();
    if (!keyed) { registrationKey = ctjs::ConstantOp::create(at, key.getLoc(), key.getValue()); }
    ctjs::CallOp::create(at, invocation.getLoc(), invocation.getType(), select, selectedHelper,
                         mlir::ValueRange{registrationKey, body.getArgument(ctjs::arg_receiver)});
    eraseRooted(invocation);
    if (holder) {
        eraseRooted(holderRead);
        holderSlot.erase();
    }
    constructor.getUpvaluesMutable().assign(mapCell);
    constructor.removeEnclosingIndicesAttr();
    llvm::erase_if(cellReads, [&](ctjs::CellGetOp read) {
        if (read.getCell() != helperCell) { return false; }
        cellOperations.erase(read);
        eraseRooted(read);
        return true;
    });
    for (mlir::Operation * user : llvm::make_early_inc_range(helperCell.getUsers())) {
        cellOperations.erase(user);
        user->erase();
    }
    cellOperations.erase(helperCell.getDefiningOp());
    cells.erase(helperCell);
    helperCell.getDefiningOp()->erase();
    if (holder) { eraseRooted(holder); }
    earlyCaptures.erase(helper);
    eraseRooted(helper);
    functions.erase(*functionIndex(targetFunction));
    targetFunction.erase();
    return true;
}

bool classInitialization::sinkConstructorPublication(ctjs::CreateClosureOp constructor,
                                                     llvm::ArrayRef<ctjs::ConstructOp> instances,
                                                     const HostContract & contract) {
    // ponytail: a terminal leaf, or one unconstructed base with one leaf whose
    // remaining work only stores literals or proved Numbers. General partial
    // publication needs exception/reentry and complete family observer proofs.
    auto function = target(constructor);
    const bool isBase = baseClasses.contains(constructor.getResult());
    if (contract.provider != HostContract::Provider::closedSource ||
        (instances.empty() && !isBase) || !function.getBody().hasOneBlock() ||
        constructor.getUpvalues().size() != 1) {
        return true;
    }
    ctjs::CreateClosureOp baseClosure;
    ctjs::CallOp basePublication;
    if (auto inherited = heritage.lookup(constructor.getResult())) {
        baseClosure = sourceValue(inherited.getArgs()[1]).getDefiningOp<ctjs::CreateClosureOp>();
        basePublication = deferredPublications.lookup(target(baseClosure));
    }
    if (isBase) {
        if (!instances.empty() || heritage.contains(constructor.getResult())) { return true; }
        ctjs::CreateClosureOp leaf;
        for (auto [derived, inherited] : heritage) {
            if (!step()) { return false; }
            if (sourceValue(inherited.getArgs()[1]) != constructor.getResult()) { continue; }
            auto next = derived.getDefiningOp<ctjs::CreateClosureOp>();
            if (leaf || !next || baseClasses.contains(derived) || !next.getUpvalues().empty()) {
                return true;
            }
            leaf = next;
        }
        if (!leaf) { return true; }
    }
    auto & body = function.getBody().front();
    auto self = body.getArgument(ctjs::arg_receiver);
    auto cell = constructor.getUpvalues().front();
    auto map = sourceValue(cells.lookup(cell)).getDefiningOp<ctjs::ConstructOp>();
    if (!map || !maps.contains(map.getResult()) || map->getBlock() != constructor->getBlock() ||
        map->getBlock() != &constructor->getParentOfType<ctjs::FuncOp>().getBody().front()) {
        return true;
    }
    ctjs::CallOp publication;
    for (mlir::OpOperand & use : self.getUses()) {
        if (!step()) { return false; }
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        if (!call || use.getOperandNumber() != 3 || call.getArgs().size() != 2 ||
            sourceValue(call.getReceiver()) != map.getResult()) {
            continue;
        }
        if (publication || call->getBlock() != &body) { return true; }
        publication = call;
    }
    if (!publication) { return true; }
    auto selection = publication.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    auto captured = publication.getReceiver().getDefiningOp<ctjs::LoadUpvalueOp>();
    auto key = publication.getArgs()[0].getDefiningOp<ctjs::ConstantOp>();
    auto keyParameter = llvm::dyn_cast<mlir::BlockArgument>(publication.getArgs()[0]);
    auto returned = llvm::dyn_cast<ctjs::ReturnOp>(body.getTerminator());
    if (!selection || selection.getObject() != publication.getReceiver() ||
        ctjs::constantKey(selection.getKey()) != "set" || !captured ||
        mapCaptures.lookup(captured.getResult()) != map.getResult() || !returned ||
        !undefined(returned.getValue())) {
        return true;
    }
    if (key ? !llvm::isa<ctjs::StringAttr>(key.getValue())
            : !keyParameter || keyParameter.getOwner() != &body ||
                  keyParameter.getArgNumber() < ctjs::implicit_arguments) {
        return true;
    }
    // A base parameter remains a deferred obligation: super normalization
    // substitutes it into the sole leaf, which checks every exact new below.
    // ponytail: literal String actuals; computed keys need a wider type proof.
    llvm::SmallVector<ctjs::ConstantOp> keys;
    for (ctjs::ConstructOp made : instances) {
        if (!step()) { return false; }
        auto actual = key;
        if (!actual) {
            const auto index = keyParameter.getArgNumber() - ctjs::implicit_arguments;
            if (index >= made.getArgs().size()) { return true; }
            actual = made.getArgs()[index].getDefiningOp<ctjs::ConstantOp>();
        }
        if (!actual || !llvm::isa<ctjs::StringAttr>(actual.getValue())) { return true; }
        keys.push_back(actual);
    }
    llvm::DenseSet<mlir::Value> numbers;
    if (basePublication) {
        // A generic arithmetic opcode can invoke user coercion. Prove its
        // operands from every exact construction, never from an observed run.
        // ponytail: literal Number arguments; wider callers need typed source proof.
        for (auto [index, argument] :
             llvm::enumerate(body.getArguments().drop_front(ctjs::implicit_arguments))) {
            bool numeric = !instances.empty();
            for (ctjs::ConstructOp made : instances) {
                if (!step()) { return false; }
                auto actual = index < made.getArgs().size()
                                  ? made.getArgs()[index].getDefiningOp<ctjs::ConstantOp>()
                                  : ctjs::ConstantOp{};
                numeric &= actual && llvm::isa<ctjs::NumberAttr>(actual.getValue());
            }
            if (numeric) { numbers.insert(argument); }
        }
        for (mlir::Operation & op : body) {
            if (!step()) { return false; }
            if (auto literal = llvm::dyn_cast<ctjs::ConstantOp>(op)) {
                if (llvm::isa<ctjs::NumberAttr>(literal.getValue())) {
                    numbers.insert(literal.getResult());
                }
                continue;
            }
            bool numeric = llvm::isa<ctjs::BinaryStaticOp>(op);
            if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op)) {
                switch (binary.getKind()) {
                case ctjs::BinaryKind::Add:
                case ctjs::BinaryKind::Sub:
                case ctjs::BinaryKind::Mul:
                case ctjs::BinaryKind::Div:
                case ctjs::BinaryKind::Mod:
                case ctjs::BinaryKind::Pow: numeric = true; break;
                default: break;
                }
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
                numeric = unary.getKind() == ctjs::UnaryKind::Neg ||
                          unary.getKind() == ctjs::UnaryKind::Plus ||
                          unary.getKind() == ctjs::UnaryKind::BitNot;
            }
            if (numeric && llvm::all_of(op.getOperands(), [&](mlir::Value operand) {
                    return numbers.contains(operand);
                })) {
                numbers.insert(op.getResult(0));
            }
        }
    }
    for (mlir::Operation * op = publication->getNextNode(); op; op = op->getNextNode()) {
        if (!step()) { return false; }
        if (op->getNumResults() == 1 && numbers.contains(op->getResult(0))) { continue; }
        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op); basePublication && write) {
            auto value = write.getValue().getDefiningOp<ctjs::ConstantOp>();
            if (write.getObject() == self && ctjs::ordinaryKey(write.getKey()) &&
                (numbers.contains(write.getValue()) ||
                 (value && llvm::isa<ctjs::UndefinedAttr, ctjs::NullAttr, ctjs::BooleanAttr,
                                     ctjs::NumberAttr, ctjs::StringAttr>(value.getValue())))) {
                continue;
            }
        }
        if (!llvm::isa<ctjs::ConstantOp, ctjs::RootOp, ctjs::FrameExitOp, ctjs::ReturnOp>(op)) {
            return true;
        }
    }
    // This sole capture must have no other observer, including another class
    // or helper. Every remaining Map use stays in the completed-owner census.
    for (auto [alias, initial] : cells) {
        if (!step()) { return false; }
        if (sourceValue(initial) != map.getResult()) { continue; }
        for (mlir::OpOperand & use : alias.getUses()) {
            if (!step()) { return false; }
            if (use.getOwner() != constructor &&
                (!basePublication || use.getOwner() != baseClosure) &&
                !cellOperations.contains(use.getOwner())) {
                return true;
            }
        }
    }
    for (mlir::Operation & op : body) {
        if (!step()) { return false; }
        if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op); load && load != captured) {
            return true;
        }
    }
    for (mlir::Operation * user : captured->getUsers()) {
        if (!step()) { return false; }
        if (user != selection && user != publication && !llvm::isa<ctjs::RootOp>(user)) {
            return true;
        }
    }
    for (mlir::Operation * user : selection->getUsers()) {
        if (!step()) { return false; }
        if (user != publication && !llvm::isa<ctjs::RootOp>(user)) { return true; }
    }
    for (mlir::Operation * user : publication->getUsers()) {
        if (!step()) { return false; }
        if (!llvm::isa<ctjs::RootOp>(user)) { return true; }
    }
    for (ctjs::ConstructOp made : instances) {
        if (!step()) { return false; }
        if (made->getBlock() != map->getBlock() || !map->isBeforeInBlock(made)) { return true; }
    }
    // Numeric closure uses were completely enumerated by examine. A symbol
    // call would bypass that census and must keep the original constructor.
    for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                              mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
        if (!uses) { return true; }
        for (const auto & use : *uses) {
            if (!step()) { return false; }
            if (mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                    use.getUser(), use.getSymbolRef()) == function) {
                return true;
            }
        }
    }
    if (!proveMaps()) { return false; }
    if (auto problem = host_detail::initialBindingProblem(module, contract); !problem.empty()) {
        return refuse(problem);
    }
    if (isBase) {
        deferredPublications[function] = publication;
        return true;
    }
    // There is no observable work between the original set and return. Keep
    // registration immediately after each exact new, before its caller resumes.
    // A throwing prefix still skips it; a throwing set still skips the caller.
    for (auto [made, actual] : llvm::zip(instances, keys)) {
        constexpr unsigned expansionCost = 14;
        if (remaining < expansionCost) {
            return refuse("class initialization work budget exhausted");
        }
        remaining -= expansionCost;
        mlir::OpBuilder at(made);
        at.setInsertionPointAfter(made);
        auto name = ctjs::ConstantOp::create(at, selection.getLoc(),
                                             ctjs::StringAttr::get(module.getContext(), "set"));
        auto select = ctjs::GetPropertyOp::create(at, selection.getLoc(), map.getResult(), name);
        auto literal = ctjs::ConstantOp::create(at, actual.getLoc(), actual.getValue());
        ctjs::CallOp::create(at, publication.getLoc(), publication.getType(), select,
                             map.getResult(), mlir::ValueRange{literal, made->getResult(0)});
    }
    const auto retire = [&](ctjs::CreateClosureOp closure, ctjs::CallOp call) {
        auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        auto load = call.getReceiver().getDefiningOp<ctjs::LoadUpvalueOp>();
        mapCaptures.erase(load.getResult());
        mapOperations.erase(call);
        mapOperations.erase(read);
        mapOperations.erase(load);
        eraseRooted(call);
        eraseRooted(read);
        eraseRooted(load);
        closure.getUpvaluesMutable().clear();
        closure.removeEnclosingIndicesAttr();
        target(closure).setUpvalueCount(0);
        mapClosures.erase(closure);
    };
    retire(constructor, publication);
    if (basePublication) {
        deferredPublications.erase(target(baseClosure));
        retire(baseClosure, basePublication);
    }
    mapCells.erase(cell);
    return true;
}

} // namespace ctcompile::ctnative::class_detail
