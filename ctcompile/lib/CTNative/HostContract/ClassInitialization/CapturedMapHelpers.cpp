#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {

bool classInitialization::normalizeCapturedMapHelpers(ctjs::FuncOp scope) {
    // ponytail: straight-line helpers called in their owning entry block.
    // Nested calls and regions need a wider capture/lifetime proof.
    auto & entry = scope.getBody().front();
    llvm::SmallVector<ctjs::CreateClosureOp> closures;
    for (auto closure : entry.getOps<ctjs::CreateClosureOp>()) {
        if (!step()) { return false; }
        closures.push_back(closure);
    }
    const auto expand = [&](ctjs::CreateClosureOp helper) {
        auto function = target(helper);
        if (!function || helper.getUpvalues().size() != 1 || function.getUpvalueCount() != 1 ||
            !function.getBody().hasOneBlock() || !undefined(helper.getEnclosingThis()) ||
            (helper.getEnclosingIndicesAttr() &&
             llvm::any_of(helper.getEnclosingIndicesAttr().asArrayRef(),
                          [](int32_t index) { return index >= 0; }))) {
            return true;
        }
        auto mapCell = helper.getUpvalues().front();
        auto map = sourceValue(cells.lookup(mapCell)).getDefiningOp<ctjs::ConstructOp>();
        if (!map || !maps.contains(map.getResult()) || map->getBlock() != &entry) { return true; }
        auto & body = function.getBody().front();
        if (!body.getArgument(ctjs::arg_receiver).use_empty() ||
            !body.getArgument(ctjs::arg_new_target).use_empty()) {
            return true;
        }
        auto returned = llvm::dyn_cast<ctjs::ReturnOp>(body.getTerminator());
        if (!returned) { return true; }
        llvm::DenseSet<mlir::Value> captures;
        for (mlir::Operation & op : body) {
            if (!step()) { return false; }
            if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                if (read.getIndex() != 0 ||
                    read.getClosure() != body.getArgument(ctjs::arg_callee)) {
                    return true;
                }
                captures.insert(read.getResult());
                continue;
            }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                if (!captures.contains(read.getObject())) { return true; }
                const auto key = ctjs::constantKey(read.getKey());
                if (key != "size" && key != "set" && key != "get" && key != "has" &&
                    key != "delete" && key != "clear") {
                    return true;
                }
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!read || !captures.contains(call.getReceiver()) ||
                    read.getObject() != call.getReceiver()) {
                    return true;
                }
                const auto key = ctjs::constantKey(read.getKey());
                const unsigned arity = key == "set" ? 2u : key == "clear" ? 0u : 1u;
                if (key == "size" || call.getArgs().size() != arity) { return true; }
                continue;
            }
            if (!llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                           ctjs::ReturnOp>(op)) {
                return true;
            }
        }
        for (mlir::OpOperand & use : body.getArgument(ctjs::arg_callee).getUses()) {
            if (!step()) { return false; }
            if (!llvm::isa<ctjs::LoadUpvalueOp, ctjs::RootOp>(use.getOwner())) { return true; }
        }
        mlir::Value helperCell;
        for (auto [cell, initial] : cells) {
            if (!step()) { return false; }
            if (sourceValue(initial) != helper.getResult()) { continue; }
            if (helperCell) { return true; }
            helperCell = cell;
        }
        if (helperCell) {
            for (mlir::Operation * user : helperCell.getUsers()) {
                if (!step()) { return false; }
                // sourceUses omits capture transport: a second observer must
                // not survive retirement of this binding.
                if (!cellOperations.contains(user)) { return true; }
            }
        }
        llvm::SmallVector<ctjs::GetPropertyOp> holderReads;
        ctjs::SetPropertyOp publication;
        auto uses = sourceUses(helper.getResult());
        llvm::SmallVector<mlir::Operation *> invocations;
        for (size_t i = 0; i < uses.size(); ++i) {
            mlir::OpOperand * use = uses[i];
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use->getOwner())) { continue; }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use->getOwner())) {
                auto object = store.getObject().getDefiningOp<ctjs::CreateObjectOp>();
                if (publication || use->getOperandNumber() != 2 || !object ||
                    object->getBlock() != &entry) {
                    return true;
                }
                // Include the original constructor's captured reads in the
                // holder census. Its registration slot stays intact because
                // this expansion only accepts calls in the owning entry block.
                auto holderUses = sourceUses(object.getResult());
                llvm::DenseMap<mlir::Value, mlir::Operation *> captureSites;
                for (auto [cell, initial] : cells) {
                    if (!step()) { return false; }
                    if (sourceValue(initial) != object.getResult()) { continue; }
                    for (mlir::OpOperand & selected : cell.getUses()) {
                        if (!step()) { return false; }
                        if (cellOperations.contains(selected.getOwner())) { continue; }
                        auto constructor =
                            llvm::dyn_cast<ctjs::CreateClosureOp>(selected.getOwner());
                        auto fn = target(constructor);
                        if (!fn || constructor->getBlock() != &entry ||
                            constructor.getUpvalues().size() != 1 ||
                            constructor.getUpvalues().front() != cell ||
                            fn.getUpvalueCount() != 1 || !fn.getBody().hasOneBlock()) {
                            return true;
                        }
                        bool classConstructor = false;
                        for (ctjs::CallOp setup : calls) {
                            if (!step()) { return false; }
                            classConstructor |= setup.getArgs().size() == 1 &&
                                                setup.getArgs().front() == constructor.getResult();
                        }
                        if (!classConstructor) { return true; }
                        auto & constructorBody = fn.getBody().front();
                        for (mlir::OpOperand & use :
                             constructorBody.getArgument(ctjs::arg_callee).getUses()) {
                            if (!step()) { return false; }
                            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                            auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
                            if (!load || load.getIndex() != 0 ||
                                load->getBlock() != &constructorBody) {
                                return true;
                            }
                            captureSites[load.getResult()] = constructor;
                            for (mlir::OpOperand & capturedUse : load.getResult().getUses()) {
                                if (!step()) { return false; }
                                holderUses.push_back(&capturedUse);
                            }
                        }
                    }
                }
                auto holder = analyzeLocalCallableObject(
                    object, [&] { return step(); }, [&](mlir::Value) { return holderUses; });
                if (!holder) {
                    llvm::consumeError(holder.takeError());
                    return reason.empty();
                }
                for (ctjs::SetPropertyOp slot : holder->stores) {
                    if (!step()) { return false; }
                    if (slot.getValue() == helper.getResult() && slot != store) { return true; }
                }
                for (auto [read, closure] : holder->reads) {
                    if (!step()) { return false; }
                    auto * position = captureSites.lookup(read.getObject());
                    if (!position) { position = read; }
                    for (ctjs::SetPropertyOp slot : holder->stores) {
                        if (!step()) { return false; }
                        if (position->getBlock() != &entry || !slot->isBeforeInBlock(position)) {
                            return true;
                        }
                    }
                    if (closure != helper) { continue; }
                    holderReads.push_back(read);
                    for (mlir::OpOperand & selected : read.getResult().getUses()) {
                        if (!step()) { return false; }
                        uses.push_back(&selected);
                    }
                }
                publication = store;
                continue;
            }
            auto * call = use->getOwner();
            mlir::ValueRange arguments;
            if (auto dynamic = llvm::dyn_cast<ctjs::CallOp>(call)) {
                auto read = dynamic.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                const bool holderCall = read && llvm::is_contained(holderReads, read);
                if (use->getOperandNumber() != 0 ||
                    (holderCall ? dynamic.getReceiver() != read.getObject()
                                : !undefined(dynamic.getReceiver()))) {
                    return true;
                }
                arguments = dynamic.getArgs();
            } else if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call)) {
                if (direct.getCalleeValue().getDefiningOp<ctjs::GetPropertyOp>() ||
                    use->getOperandNumber() != 2 || direct.getTarget() != function ||
                    !undefined(direct.getReceiver()) || !undefined(direct.getNewTarget())) {
                    return true;
                }
                arguments = direct.getArgs();
            } else {
                return true;
            }
            if (call->getBlock() != &entry || !helper->isBeforeInBlock(call) ||
                !map->isBeforeInBlock(call) ||
                arguments.size() != body.getNumArguments() - ctjs::implicit_arguments) {
                return true;
            }
            // Capturing a hoisted cell does not read it. Every invocation
            // must follow initialization of this exact captured Map cell.
            bool initialized =
                mapCell.getDefiningOp<ctjs::CreateCellOp>().getInitial() == map.getResult();
            for (mlir::Operation * user : mapCell.getUsers()) {
                if (!step()) { return false; }
                auto write = llvm::dyn_cast<ctjs::CellSetOp>(user);
                initialized |= write && write->getBlock() == &entry &&
                               write->isBeforeInBlock(call) &&
                               sourceValue(write.getValue()) == map.getResult();
            }
            if (!initialized) { return true; }
            invocations.push_back(call);
        }
        if (invocations.empty()) { return true; }
        for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                                  mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
            if (!uses) { return true; }
            for (const auto & use : *uses) {
                if (!step()) { return false; }
                if (mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                        use.getUser(), use.getSymbolRef()) == function &&
                    !llvm::is_contained(invocations, use.getUser())) {
                    return true;
                }
            }
        }
        // Precharge expansion and cleanup before the first mutation. The
        // enclosing pass owns a disposable module if a later proof refuses.
        for (mlir::Operation * call : invocations) {
            if (!step()) { return false; }
            for (mlir::OpOperand & use : call->getResult(0).getUses()) {
                (void)use;
                if (!step()) { return false; }
            }
            for (mlir::Operation & op : body) {
                const uint64_t cost = uint64_t(1) + op.getNumOperands() + op.getNumResults();
                if (cost > remaining) {
                    return refuse("class initialization work budget exhausted");
                }
                remaining -= static_cast<unsigned>(cost);
            }
        }
        for (ctjs::CellGetOp read : cellReads) {
            if (!step()) { return false; }
            if (read.getCell() != helperCell) { continue; }
            for (mlir::Operation * user : read->getUsers()) {
                (void)user;
                if (!step()) { return false; }
            }
        }
        for (mlir::Value value : {mlir::Value(helper.getResult()), helperCell}) {
            if (!value) { continue; }
            for (mlir::Operation * user : value.getUsers()) {
                (void)user;
                if (!step()) { return false; }
            }
        }
        if (publication) {
            for (mlir::Operation * user : publication.getObject().getUsers()) {
                (void)user;
                if (!step()) { return false; }
            }
        }
        if (!reason.empty()) { return false; }
        for (mlir::Operation * call : invocations) {
            mlir::IRMapping mapping;
            auto dynamic = llvm::dyn_cast<ctjs::CallOp>(call);
            auto arguments =
                dynamic ? dynamic.getArgs() : llvm::cast<ctjs::CallDirectOp>(call).getArgs();
            mapping.map(body.getArguments().drop_front(ctjs::implicit_arguments), arguments);
            mlir::OpBuilder at(call);
            for (mlir::Operation & op : body) {
                if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                    mapping.map(read.getResult(), map.getResult());
                } else if (!llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                                      ctjs::ReturnOp>(op)) {
                    at.clone(op, mapping);
                }
            }
            call->getResult(0).replaceAllUsesWith(mapping.lookup(returned.getValue()));
            call->erase();
        }
        for (ctjs::GetPropertyOp read : holderReads) { eraseRooted(read); }
        if (publication) {
            auto object = publication.getObject();
            publication.erase();
            if (llvm::all_of(object.getUsers(), [](mlir::Operation * user) {
                    return llvm::isa<ctjs::RootOp>(user);
                })) {
                eraseRooted(object.getDefiningOp());
            }
        }
        if (helperCell) {
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
        }
        earlyCaptures.erase(helper);
        eraseRooted(helper);
        functions.erase(*functionIndex(function));
        function.erase();
        return true;
    };
    for (ctjs::CreateClosureOp helper : closures) {
        if (!step() || !expand(helper)) { return false; }
    }
    return reason.empty();
}

} // namespace ctcompile::ctnative::class_detail
