#include "Proof.hpp"
#include "llvm/ADT/StringMap.h"

#include <bit>
#include <cmath>

namespace ctcompile::ctnative::class_detail {

bool classInitialization::step() {
    return spend(1);
}

bool classInitialization::spend(uint64_t count) {
    if (count > remaining) {
        remaining = 0;
        return refuse("class initialization work budget exhausted");
    }
    remaining -= static_cast<unsigned>(count);
    return true;
}

bool classInitialization::spendWalk(mlir::Operation * root,
                                    llvm::function_ref<uint64_t(mlir::Operation *)> cost) {
    WorkBudget budget{remaining};
    const bool fits = budget.spendWalk(*root, cost);
    remaining = static_cast<unsigned>(budget.remaining);
    return fits || refuse("class initialization work budget exhausted");
}

bool classInitialization::refuse(llvm::StringRef message) {
    if (reason.empty()) { reason = message.str(); }
    return false;
}

ctjs::FuncOp classInitialization::target(ctjs::CreateClosureOp closure) {
    return closure ? host_detail::closureTarget(closure, functions) : ctjs::FuncOp{};
}

bool classInitialization::proveCells(ctjs::FuncOp entry) {
    // The importer boxes entry locals when one is captured. Only fixed,
    // ordered bindings are transport; their original producers stay live.
    for (ctjs::CreateCellOp cell : entry.getBody().front().getOps<ctjs::CreateCellOp>()) {
        if (!step()) { return false; }
        if (cells.count(cell.getResult())) { continue; }
        ctjs::CellSetOp first;
        const auto readPosition = [&](mlir::Operation * op) {
            return llvm::isa<ctjs::CellGetOp>(op)
                       ? positionIn<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::IndexSwitchOp>(
                             op, cell->getBlock())
                       : op;
        };
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!step()) { return false; }
            auto * op = use.getOwner();
            auto * position = readPosition(op);
            if (position->getBlock() != cell->getBlock() || !cell->isBeforeInBlock(position)) {
                return refuse("class local cell has a nonlocal or unordered use");
            }
            if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(op)) {
                if (use.getOperandNumber() != 0 ||
                    (first && first.getValue() != write.getValue())) {
                    return refuse("class local cell has changing writes");
                }
                if (!first || write->isBeforeInBlock(first)) { first = write; }
            }
        }
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!step()) { return false; }
            auto * op = use.getOwner();
            if (llvm::isa<ctjs::RootOp, ctjs::CellSetOp>(op)) {
                cellOperations.insert(op);
                continue;
            }
            // A fixed cell may be read in a later arm or loop. All
            // writes/captures still belong to its original ordered block.
            if (first && !first->isBeforeInBlock(readPosition(op))) {
                // Capturing the cell does not read it. Only the exact publication
                // helper normalization can discharge this deferred obligation.
                if (!llvm::isa<ctjs::CreateClosureOp>(op)) {
                    return refuse("class local cell is observed before initialization");
                }
                earlyCaptures.insert(op);
            }
            if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(op);
                read && use.getOperandNumber() == 0) {
                cellReads.push_back(read);
                cellOperations.insert(op);
                continue;
            }
            auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
            // A parameter initializes its cell directly. The later capture
            // proof must still establish its value and every consumer.
            if ((!first && !llvm::isa<mlir::BlockArgument>(cell.getInitial())) || !closure ||
                use.getOperandNumber() < 2) {
                return refuse("class local cell escapes its fixed reads and captures");
            }
            // The exact class method and every captured read are checked
            // below. No general immutable-capture rule is widened here.
        }
        cells[cell.getResult()] = first ? first.getValue() : cell.getInitial();
        cellOperations.insert(cell);
    }
    for (auto & [cell, value] : cells) {
        (void)cell;
        value = sourceValue(value);
    }
    return reason.empty();
}

mlir::Value classInitialization::sourceValue(mlir::Value value) {
    if (auto map = mapCaptures.lookup(value)) { return map; }
    if (auto holder = holderCaptures.lookup(value)) { return holder; }
    while (auto read = value.getDefiningOp<ctjs::CellGetOp>()) {
        if (!step()) { return value; }
        auto found = cells.find(read.getCell());
        if (found == cells.end()) { return value; }
        value = found->second;
    }
    return value;
}

llvm::SmallVector<mlir::OpOperand *> classInitialization::sourceUses(mlir::Value value) {
    llvm::SmallVector<mlir::OpOperand *> result;
    llvm::SmallVector<mlir::Value> pending{value};
    llvm::DenseSet<mlir::Value> seen;
    while (!pending.empty()) {
        auto current = pending.pop_back_val();
        if (!seen.insert(current).second) { continue; }
        for (auto [read, object] : holderCaptures) {
            if (!step()) { return {}; }
            if (object == current) { pending.push_back(read); }
        }
        for (auto [read, map] : mapCaptures) {
            if (!step()) { return {}; }
            if (map == current) { pending.push_back(read); }
        }
        for (mlir::OpOperand & use : current.getUses()) {
            if (!step()) { return {}; }
            auto * op = use.getOwner();
            if (cellOperations.contains(op) && llvm::isa<ctjs::CreateCellOp, ctjs::CellSetOp>(op)) {
                auto write = llvm::dyn_cast<ctjs::CellSetOp>(op);
                mlir::Value cell = write ? mlir::Value(write.getCell()) : op->getResult(0);
                for (mlir::OpOperand & cellUse : cell.getUses()) {
                    if (!step()) { return {}; }
                    if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(cellUse.getOwner());
                        read && cells.lookup(cell) == sourceValue(current)) {
                        pending.push_back(read.getResult());
                    }
                }
                continue;
            }
            result.push_back(&use);
        }
    }
    return result;
}

bool classInitialization::proveMaps() {
    // Map keys and payloads are not coerced by these operations. Keep every
    // body and call for native type/ownership admission; this proof establishes
    // only that class setup cannot be observed or changed through the Map.
    for (mlir::Value map : maps) {
        for (mlir::OpOperand * use : sourceUses(map)) {
            if (!step()) { return false; }
            auto * op = use->getOwner();
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op);
                call && use->getOperandNumber() == 1) {
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (read && read.getObject() == call.getReceiver()) { continue; }
            }
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
            if (!read || use->getOperandNumber() != 0) {
                return refuse("class captured Map escapes its exact local operations");
            }
            const auto key = ctjs::constantKey(read.getKey());
            mapOperations.insert(read);
            if (key == "size") { continue; }
            const auto arity = mapMethodArity(key);
            if (!arity) { return refuse("class captured Map has an unproved member"); }
            for (mlir::OpOperand & selected : read.getResult().getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(selected.getOwner())) { continue; }
                auto call = llvm::dyn_cast<ctjs::CallOp>(selected.getOwner());
                if (!call || selected.getOperandNumber() != 0 ||
                    call.getReceiver() != read.getObject() || call.getArgs().size() != *arity) {
                    return refuse("class captured Map method escapes its exact receiver call");
                }
                if (key == "set") {
                    for (mlir::Operation * resultUse : call->getUsers()) {
                        if (!step()) { return false; }
                        if (!llvm::isa<ctjs::RootOp>(resultUse)) {
                            return refuse("class captured Map set result escapes");
                        }
                    }
                }
                mapOperations.insert(call);
            }
        }
    }
    return reason.empty();
}

bool classInitialization::helperCallback(mlir::OpOperand & use) {
    auto callback = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
    auto function = target(callback);
    if (!callback || use.getOperandNumber() != 0 || !function || !callback.getUpvalues().empty() ||
        function.getUpvalueCount() != 0 ||
        llvm::any_of(function.getBody().front().getArguments().take_front(ctjs::implicit_arguments),
                     [](mlir::BlockArgument argument) { return !argument.use_empty(); })) {
        return refuse("captured helper observes its implicit callee");
    }
    for (mlir::OpOperand & callbackUse : callback.getResult().getUses()) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::RootOp>(callbackUse.getOwner())) { continue; }
        // A synchronous leaf cannot retain its enclosing helper. Keep its body
        // for the complete source/DOM proof and bind actuals during expansion.
        auto local = host_detail::jsCallOf(callbackUse.getOwner());
        if (local && callbackUse.getOperandNumber() == (local->direct ? 2u : 0u) &&
            undefined(local->receiver) &&
            (!local->direct ||
             (local->direct.getTarget() == function && undefined(local->direct.getNewTarget()))) &&
            local->args.size() + ctjs::implicit_arguments ==
                function.getBody().front().getNumArguments() &&
            callbackUse.getOwner()->getBlock() == callback->getBlock() &&
            callback->isBeforeInBlock(callbackUse.getOwner())) {
            continue;
        }
        auto call = llvm::dyn_cast<ctjs::CallOp>(callbackUse.getOwner());
        auto read =
            call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
        const bool replacement = call && read && callbackUse.getOperandNumber() == 3 &&
                                 call.getArgs().size() == 2 &&
                                 ctjs::constantKey(read.getKey()) == "replace";
        const bool filter =
            call && read && callbackUse.getOperandNumber() == 2 && call.getArgs().size() == 1 &&
            ctjs::constantKey(read.getKey()) == "filter" &&
            function.getBody().front().getNumArguments() == ctjs::implicit_arguments + 1 &&
            call->getBlock() == callback->getBlock() && callback->isBeforeInBlock(call);
        if ((!replacement && !filter) || read.getObject() != call.getReceiver()) {
            return refuse("captured helper callback escapes its intrinsic call");
        }
    }
    // Retain the original callback for local expansion, the shared no-match
    // or typed Array filter proof, including its complete body and uses.
    helpers.insert(function);
    domEntryHelpers.insert(function);
    return true;
}

bool classInitialization::helperCallbacks(ctjs::FuncOp helper) {
    for (mlir::OpOperand & use : helper.getBody().front().getArgument(ctjs::arg_callee).getUses()) {
        if (!step() || !helperCallback(use)) { return false; }
    }
    return true;
}

bool classInitialization::methodCaptures(ctjs::CreateClosureOp method,
                                         ctjs::CreateClosureOp constructor,
                                         llvm::SmallVectorImpl<ctjs::GetPropertyOp> & reads,
                                         bool domEntry, unsigned depth) {
    if (!step()) { return false; }
    if (earlyCaptures.contains(method)) {
        return refuse("class local cell is observed before initialization");
    }
    // ponytail: bound the proof stack; use an explicit worklist for deeper chains.
    if (depth >= 64) { return refuse("class helper capture nesting limit exceeded"); }
    auto fn = target(method);
    const auto constructorValue = constructor ? constructor.getResult() : mlir::Value{};
    if (fn.getUpvalueCount() != static_cast<int64_t>(method.getUpvalues().size()) ||
        hasOuterCapture(method)) {
        return refuse("class method lacks exact local capture slots");
    }
    for (mlir::Value capture : method.getUpvalues()) {
        if (!step() || !cells.count(capture)) {
            return refuse("class method capture lacks a fixed local cell");
        }
        auto value = sourceValue(cells.lookup(capture));
        if (value == constructorValue) { continue; }
        if (auto literal = value.getDefiningOp<ctjs::ConstantOp>();
            literal && ctjs::isPrimitiveAttr(literal.getValue())) {
            if (literal->getBlock() != method->getBlock() || !literal->isBeforeInBlock(method)) {
                return refuse("class primitive capture requires ordered local initialization");
            }
            continue;
        }
        if (maps.contains(value)) {
            if (value.getDefiningOp()->getBlock() != method->getBlock() ||
                !value.getDefiningOp()->isBeforeInBlock(method)) {
                return refuse("class Map capture requires ordered local initialization");
            }
            mapCells.insert(capture);
            mapClosures.insert(method);
            needsDOMMethodProof |= domEntry;
            continue;
        }
        if (auto object = value.getDefiningOp<ctjs::CreateObjectOp>(); constructor && object) {
            if (object->getBlock() != method->getBlock() || !object->isBeforeInBlock(method)) {
                return refuse("captured holder lacks ordered local initialization");
            }
            // The shared holder census checks every slot and alias below.
            // All original slots must already exist at closure creation.
            for (mlir::Operation * use : object->getUsers()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::SetPropertyOp>(use) &&
                    (use->getBlock() != method->getBlock() || !use->isBeforeInBlock(method))) {
                    return refuse("captured holder slot changes after capture");
                }
            }
            needsDOMMethodProof |= domEntry;
            continue;
        }
        auto closure = value.getDefiningOp<ctjs::CreateClosureOp>();
        auto helper = target(closure);
        if (!helper || closure->getBlock() != method->getBlock() ||
            !closure->isBeforeInBlock(method)) {
            return refuse(
                ("class method capture is not its constructor or an inert sibling helper: " +
                 fn.getSymName() + " / " +
                 (value.getDefiningOp() ? value.getDefiningOp()->getName().getStringRef()
                                        : llvm::StringRef("block argument")))
                    .str());
        }
        // Every edge keeps its fixed cell and source order. Shared helpers
        // record their reads once, only after their entire capture proof succeeds.
        if (capturedHelpers.contains(closure)) { continue; }
        auto & body = helper.getBody().front();
        if (closure.getUpvalues().empty()) {
            if (helper.getUpvalueCount() != 0 || !helperCallbacks(helper)) { return false; }
        } else {
            // A nested helper receives no constructor or holder authority.
            llvm::SmallVector<ctjs::GetPropertyOp> unusedReads;
            if (!methodCaptures(closure, {}, unusedReads, domEntry, depth + 1)) { return false; }
        }
        if (!unusedReceiver(helper) || !body.getArgument(ctjs::arg_new_target).use_empty()) {
            return refuse("captured helper observes its implicit receiver or new.target");
        }
        helpers.insert(helper);
        capturedHelpers.insert(closure);
        // Closed-source helpers retain the complete strict effect census.
        // Only the DOM provider can defer their bodies to its typed proof.
        if (domEntry) {
            domEntryHelpers.insert(helper);
            needsDOMMethodProof = true;
        }
    }
    for (mlir::OpOperand & use :
         llvm::make_early_inc_range(fn.getBody().front().getArgument(ctjs::arg_callee).getUses())) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
        if (domEntry && llvm::isa<ctjs::CreateClosureOp>(use.getOwner())) {
            // Captures and callback enclosures share this original callee.
            // Prove each use separately; retain the callback's body and
            // identity for the complete typed DOM proof after rewriting.
            if (!helperCallback(use)) { return false; }
            needsDOMMethodProof = true;
            continue;
        }
        auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
        if (!load || use.getOperandNumber() != 0 || load.getIndex() < 0 ||
            static_cast<size_t>(load.getIndex()) >= method.getUpvalues().size()) {
            return refuse("class method observes or changes its captured identity");
        }
        auto value = sourceValue(cells.lookup(method.getUpvalues()[load.getIndex()]));
        if (auto literal = value.getDefiningOp<ctjs::ConstantOp>();
            literal && ctjs::isPrimitiveAttr(literal.getValue())) {
            for (mlir::OpOperand & selected : load.getResult().getUses()) {
                (void)selected;
                if (!step()) { return false; }
            }
            if (!spend(cloneCost(literal))) { return false; }
            // Preserve the exact primitive before super/method bodies are copied.
            // The complete source or typed DOM proof still checks every consumer.
            mlir::OpBuilder at(load);
            auto local = ctjs::ConstantOp::create(at, load.getLoc(), literal.getValue());
            load.getResult().replaceAllUsesWith(local.getResult());
            load.erase();
            continue;
        }
        if (maps.contains(value)) {
            mapCaptures[load.getResult()] = value;
            mapOperations.insert(load);
            continue;
        }
        if (value.getDefiningOp<ctjs::CreateObjectOp>()) {
            holderCaptures[load.getResult()] = value;
            captureReads.push_back(load);
            continue; // Every original use goes through the shared holder census.
        }
        auto helper = value != constructorValue
                          ? target(value.getDefiningOp<ctjs::CreateClosureOp>())
                          : ctjs::FuncOp{};
        for (mlir::OpOperand & selected : load.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(selected.getOwner())) { continue; }
            if (helper) {
                auto call = llvm::dyn_cast<ctjs::CallOp>(selected.getOwner());
                if (!call || selected.getOperandNumber() != 0 || !undefined(call.getReceiver()) ||
                    call.getArgs().size() + ctjs::implicit_arguments >
                        helper.getBody().front().getNumArguments()) {
                    return refuse("captured helper escapes its ordinary local call");
                }
                if (!spend(helper.getBody().front().getNumArguments() - call.getArgs().size() -
                           ctjs::implicit_arguments)) {
                    return false;
                }
                helperCalls.insert(call);
                continue;
            }
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(selected.getOwner());
            if (!read || selected.getOperandNumber() != 0 || !ctjs::ordinaryKey(read.getKey())) {
                return refuse("captured class identity escapes its local getter read");
            }
            reads.push_back(read);
        }
        captureReads.push_back(load);
        if (helper) { callableCaptures[load] = helper; }
    }
    if (!method.getUpvalues().empty()) { capturedClosures.push_back(method); }
    return true;
}

ctjs::CreateClosureOp classInitialization::sourceClosure(mlir::Value value, bool entryAliases,
                                                         bool domEntry) {
    if (auto closure = value.getDefiningOp<ctjs::CreateClosureOp>()) { return closure; }
    if (auto read = value.getDefiningOp<ctjs::GetPropertyOp>()) {
        if (auto closure = holderReads.lookup(value)) { return closure; }
        const bool localAliases = entryAliases || holderCaptures.count(read.getObject());
        auto object = (localAliases ? sourceValue(read.getObject()) : read.getObject())
                          .getDefiningOp<ctjs::CreateObjectOp>();
        auto load = read.getObject().getDefiningOp<ctjs::LoadGlobalOp>();
        auto publication = load ? globals.lookup(load.getName()) : ctjs::StoreGlobalOp{};
        if (!object && !publication) { return {}; }
        const bool localHolder = object && localAliases;
        auto proof = localHolder ? analyzeLocalCallableObject(
                                       object, [&] { return step(); },
                                       [&](mlir::Value value) { return sourceUses(value); })
                     : object    ? analyzeLocalCallableObject(object, [&] { return step(); })
                                 : analyzeGlobalCallableObject(publication, [&] { return step(); });
        if (!proof) {
            llvm::consumeError(proof.takeError());
            return {};
        }
        for (ctjs::SetPropertyOp store : proof->stores) {
            if (!step() || !ctjs::ordinaryKey(store.getKey())) { return {}; }
            auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            auto fn = target(closure);
            if (!fn) { return {}; }
            auto & block = fn.getBody().front();
            if (localHolder) {
                if (closure.getUpvalues().empty()) {
                    if (fn.getUpvalueCount() != 0 || !helperCallbacks(fn)) { return {}; }
                } else {
                    // Reuse the exact sibling-function proof. A holder has
                    // no constructor identity or authority for object captures.
                    llvm::SmallVector<ctjs::GetPropertyOp> unusedReads;
                    if (!methodCaptures(closure, {}, unusedReads, domEntry)) { return {}; }
                }
                if (!unusedReceiver(fn) || !block.getArgument(ctjs::arg_new_target).use_empty()) {
                    return {};
                }
            } else if (!closure.getUpvalues().empty() ||
                       !block.getArgument(ctjs::arg_receiver).use_empty() ||
                       !block.getArgument(ctjs::arg_new_target).use_empty() ||
                       !block.getArgument(ctjs::arg_callee).use_empty()) {
                return {};
            }
            for (mlir::OpOperand & use : closure.getResult().getUses()) {
                if (!step()) { return {}; }
                if (!llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                    !(use.getOwner() == store && use.getOperandNumber() == 2)) {
                    return {};
                }
            }
        }
        ctjs::CreateClosureOp selected;
        for (auto [loaded, closure] : proof->reads) {
            if (auto alias = loaded.getObject().getDefiningOp<ctjs::CellGetOp>()) {
                for (ctjs::SetPropertyOp store : proof->stores) {
                    if (!step() || store->getBlock() != alias->getBlock() ||
                        !store->isBeforeInBlock(alias)) {
                        return {};
                    }
                }
            }
            for (mlir::OpOperand & use : loaded.getResult().getUses()) {
                if (!step()) { return {}; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                if (!call || use.getOperandNumber() != 0 ||
                    call.getReceiver() != loaded.getObject()) {
                    return {};
                }
                if (publication || localHolder) {
                    const auto parameters = target(closure).getBody().front().getNumArguments() -
                                            ctjs::implicit_arguments;
                    if (call.getArgs().size() > parameters ||
                        !spend(parameters - call.getArgs().size())) {
                        return {};
                    }
                }
            }
            if (loaded == read) { selected = closure; }
        }
        // Identity and receiver proof only. DOM slots retain the typed body
        // proof; ordinary holders keep the strict source effect census,
        // including every unused slot.
        for (ctjs::SetPropertyOp store : proof->stores) {
            auto fn = target(store.getValue().getDefiningOp<ctjs::CreateClosureOp>());
            helpers.insert(fn);
            if (localHolder && domEntry) {
                // Keep every original local slot for DOM helper expansion.
                // That proof refuses a slot without a source invocation;
                // only actual calls can supply its argument authority.
                domEntryHelpers.insert(fn);
                needsDOMMethodProof = true;
            }
        }
        if (publication || localHolder) {
            for (auto [loaded, closure] : proof->reads) {
                holderReads[loaded.getResult()] = closure;
            }
        }
        if (publication) {
            for (ctjs::LoadGlobalOp loaded : proof->loads) { globalHolderLoads.insert(loaded); }
            globalHolders.try_emplace(publication, std::move(*proof));
        } else if (localHolder) {
            localHolders.try_emplace(object, std::move(*proof));
        }
        return selected;
    }
    auto load = value.getDefiningOp<ctjs::LoadGlobalOp>();
    auto store = load ? globals.lookup(load.getName()) : ctjs::StoreGlobalOp{};
    if (!store) { return {}; }
    auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
    if (store->getBlock() == load->getBlock() && store->isBeforeInBlock(load)) { return closure; }
    auto fn = target(closure);
    if (!fn) { return {}; }
    auto [cached, fresh] = closedGlobals.try_emplace(load.getName(), false);
    if (fresh) {
        // Charge the existing declaration proof's operation/use scans once
        // per binding. Its hoisting and closed-callee rules also cover reads
        // in other functions without trusting resolver annotations.
        cached->second = spendWalk(module, declarationCost) && closedDeclaration(store, fn, module);
    }
    return cached->second ? closure : ctjs::CreateClosureOp{};
}

bool classInitialization::unusedReceiver(ctjs::FuncOp fn) {
    for (mlir::OpOperand & use : fn.getBody().front().getArgument(ctjs::arg_receiver).getUses()) {
        if (!step()) { return false; }
        auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
        // Saving lexical this is inert when the exact helper never reads it.
        if (!closure || use.getOperandNumber() != 1 || !helpers.contains(target(closure))) {
            return false;
        }
    }
    return true;
}

bool classInitialization::borrowedHelperReads(mlir::OpOperand & use,
                                              const llvm::StringSet<> & methodKeys,
                                              llvm::SmallVectorImpl<ctjs::GetPropertyOp> & reads,
                                              unsigned depth) {
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    if ((!direct && !call) || use.getOperandNumber() < (direct ? 3u : 2u)) { return false; }
    if (!step()) { return false; }
    // ponytail: bound recursive proof depth; use a worklist for deeper chains.
    if (depth >= 64) { return refuse("class construction helper proof exceeds its depth bound"); }
    const auto callee = direct ? direct.getCalleeValue() : call.getCallee();
    auto fn = callableCaptures.lookup(callee.getDefiningOp());
    if (!fn) { fn = target(sourceClosure(callee)); }
    if (!fn || !unusedReceiver(fn) ||
        !fn.getBody().front().getArgument(ctjs::arg_new_target).use_empty()) {
        return false;
    }
    if (direct) {
        if (direct.getTarget() != fn || !undefined(direct.getReceiver()) ||
            !undefined(direct.getNewTarget())) {
            return false;
        }
    } else if (!undefined(call.getReceiver())) {
        auto read = callee.getDefiningOp<ctjs::GetPropertyOp>();
        if (!read || call.getReceiver() != read.getObject()) { return false; }
    }
    const unsigned index = use.getOperandNumber() + (direct ? 0u : 1u);
    auto & body = fn.getBody().front();
    if (index >= body.getNumArguments()) { return false; }
    // Every forwarding edge repeats the exact target and borrowed-use proof.
    // The caller checks all leaf reads against its current construction fields.
    // Writes and method/getter dispatch still need separate receiver proofs.
    for (mlir::OpOperand * selected : sourceUses(body.getArgument(index))) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::RootOp>(selected->getOwner())) { continue; }
        if (borrowedHelperReads(*selected, methodKeys, reads, depth + 1)) { continue; }
        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(selected->getOwner());
        const auto key = read ? ctjs::constantKey(read.getKey()) : llvm::StringRef{};
        if (!read || selected->getOperandNumber() != 0 || !ctjs::ordinaryKey(key) ||
            key == "constructor" || methodKeys.contains(key)) {
            return false;
        }
        reads.push_back(read);
    }
    if (!reason.empty()) { return false; }
    helpers.insert(fn);
    helperCalls.insert(use.getOwner());
    return true;
}

bool classInitialization::ownFieldSnapshots(
    ctjs::CreateClosureOp constructor, llvm::ArrayRef<ctjs::ConstructOp> instances,
    llvm::ArrayRef<ctjs::SetPropertyOp> definitions, const llvm::StringSet<> & methodKeys,
    const llvm::StringMap<ctjs::DefineAccessorOp> & staticDefinitions,
    const llvm::StringMap<ctjs::SetPropertyOp> & staticMethods, const HostContract & contract) {
    auto function = target(constructor);
    auto inherited = heritage.lookup(constructor.getResult());
    const auto base = inherited ? sourceValue(inherited.getArgs()[1]) : mlir::Value{};
    const auto parent = [&](mlir::Value derived) {
        auto call = heritage.lookup(derived);
        return call ? sourceValue(call.getArgs()[1]) : mlir::Value{};
    };
    // A deferred ancestor's fields are fixed by the first leaf that folded it.
    auto required = snapshotFields.find(base);
    for (auto ancestor = base;
         required == snapshotFields.end() && deferredSnapshots.contains(ancestor);) {
        if (!step()) { return false; }
        ancestor = parent(ancestor);
        required = snapshotFields.find(ancestor);
    }
    llvm::SetVector<mlir::Value> receivers;
    for (ctjs::ConstructOp made : instances) {
        receivers.insert(made.getResult());
        // Saved Map reads retain the same records across replacement/deletion.
        // Include all their snapshots and writes before folding any own keys.
        for (mlir::OpOperand * use : sourceUses(made.getResult())) {
            if (!step()) { return false; }
            llvm::SmallVector<mlir::Value> aliases;
            if (retainedMapAliases(*use, aliases)) {
                receivers.insert(aliases.begin(), aliases.end());
            }
            if (!reason.empty()) { return false; }
        }
    }
    llvm::SmallVector<ctjs::SetPropertyOp> allDefinitions(definitions);
    if (inherited) { allDefinitions.append(inheritedMethods.lookup(base)); }
    llvm::StringMap<ctjs::FuncOp> selectedMethods;
    for (ctjs::SetPropertyOp definition : allDefinitions) {
        if (!step()) { return false; }
        auto fn = target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
        if (fn) {
            receivers.insert(fn.getBody().front().getArgument(ctjs::arg_receiver));
            selectedMethods.try_emplace(ctjs::constantKey(definition.getKey()), fn);
        }
    }
    llvm::SetVector<mlir::Operation *> snapshots;
    for (mlir::Value receiver : receivers) {
        for (mlir::OpOperand * use : sourceUses(receiver)) {
            if (!step()) { return false; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use->getOwner());
            auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                             : ctjs::GetPropertyOp{};
            auto load =
                read ? read.getObject().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
            if (call && use->getOperandNumber() == 2 && call.getArgs().size() == 1 && load &&
                load.getName() == "Object" && read.getObject() == call.getReceiver() &&
                ctjs::constantKey(read.getKey()) == "getOwnPropertyNames") {
                snapshots.insert(call);
                snapshotMethods.insert(call->getParentOfType<ctjs::FuncOp>());
            }
        }
    }
    if (!reason.empty() || (snapshots.empty() && required == snapshotFields.end())) {
        return reason.empty();
    }
    if (!llvm::is_contained(contract.initialIntrinsics, "Object")) {
        return refuse("class own-key snapshot needs declared Object identity");
    }
    const bool dom = contract.provider == HostContract::Provider::ctbrowserDOM ||
                     contract.provider == HostContract::Provider::ctbrowserDOMSession;
    if (contract.provider != HostContract::Provider::closedSource && !dom) {
        return refuse("class own-key snapshot requires fixed constructor fields");
    }
    // A base whose only constructions are its leaves' super calls (exact
    // `new` sites are the only ones admitted) has no shape of its own: each
    // leaf proves the snapshot after super normalization, where the leaf's
    // actuals can decide the base's arms. Until then fieldsOnly admits the
    // snapshot and its loop writes; the folding leaf rechecks the body.
    // ponytail: the first leaf to fold a shared body fixes its fields for
    // every sibling; per-leaf fields need bodies that only copies reach.
    if (instances.empty() && baseClasses.contains(constructor.getResult()) &&
        required == snapshotFields.end()) {
        deferredSnapshots.insert(constructor.getResult());
        for (mlir::Operation * call : snapshots) {
            pendingSnapshots.insert(call);
            pendingSnapshotMethods.insert(call->getParentOfType<ctjs::FuncOp>());
        }
        return true;
    }
    // ponytail: fixed named fields, with one ordered shape across an inherited
    // snapshot's receivers. Variable presence needs a separate proof.
    if (!function.getBody().hasOneBlock()) {
        return refuse("class own-key snapshot requires fixed constructor fields");
    }
    llvm::SmallVector<llvm::StringRef> fields;
    const auto self = function.getBody().front().getArgument(ctjs::arg_receiver);
    const auto hasField = [&](llvm::ArrayRef<llvm::StringRef> ordered, llvm::StringRef key) {
        for (auto field : ordered) {
            if (!step()) { return false; }
            if (field == key) { return true; }
        }
        return false;
    };
    const auto borrowedFields = [&](mlir::OpOperand & use,
                                    llvm::ArrayRef<llvm::StringRef> ordered) {
        llvm::SmallVector<ctjs::GetPropertyOp> reads;
        if (!borrowedHelperReads(use, methodKeys, reads)) { return false; }
        for (ctjs::GetPropertyOp read : reads) {
            if (!step() || !hasField(ordered, ctjs::constantKey(read.getKey()))) {
                return refuse("class construction helper requires an existing own field");
            }
        }
        return true;
    };
    // A construction-time call keeps its body and arguments. Only its receiver
    // uses need a stricter proof here: the eventual field shape is not present yet.
    const auto inspectMethod = [&](auto && visit, ctjs::FuncOp fn,
                                   llvm::ArrayRef<llvm::StringRef> ordered,
                                   unsigned depth) -> bool {
        if (!step() || depth == 64) {
            return refuse("class construction method proof exceeds its depth bound");
        }
        // A base's snapshot may already have folded. Keep its original identity
        // so a descendant cannot call it with a partially initialized receiver.
        if (!fn || snapshotMethods.contains(fn)) {
            return refuse("class construction method observes an own-key snapshot");
        }
        const auto receiver = fn.getBody().front().getArgument(ctjs::arg_receiver);
        for (mlir::OpOperand * use : sourceUses(receiver)) {
            if (!step()) { return false; }
            auto * op = use->getOwner();
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(op) && borrowedFields(*use, ordered)) {
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                auto callee = read ? selectedMethods.lookup(ctjs::constantKey(read.getKey()))
                                   : ctjs::FuncOp{};
                if (use->getOperandNumber() != 1 || !callee ||
                    sourceValue(read.getObject()) != receiver ||
                    !visit(visit, callee, ordered, depth + 1)) {
                    return refuse("class construction method receiver escapes its fixed fields");
                }
                continue;
            }
            const auto key = ctjs::propertyKey(op);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                // The fresh prototype's constructor backedge exists before
                // any own field. fieldsOnly/staticGetters still prove every
                // use and exact target; inherited target changes remain refused.
                if (use->getOperandNumber() == 0 && ctjs::constantKey(key) == "constructor") {
                    continue;
                }
                if (selectedMethods.count(ctjs::constantKey(key)) && read.getResult().hasOneUse()) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(*read.getResult().getUsers().begin());
                    if (call && call.getCallee() == read.getResult() &&
                        sourceValue(call.getReceiver()) == receiver) {
                        continue;
                    }
                }
            }
            if (use->getOperandNumber() != 0 || !key ||
                !hasField(ordered, ctjs::constantKey(key))) {
                return refuse("class construction method requires an existing own field");
            }
        }
        return reason.empty();
    };
    // An exact class's `this.constructor.K` reads this class, and a static read
    // of K on it runs K's getter with this class as receiver. When that getter
    // always throws, nothing after the read on that path runs: its receiver
    // uses and fields never reach a completed instance.
    // ponytail: exact classes and single-block getters whose first effect is
    // the throwing receiver read; an extended class needs per-leaf getters.
    const auto staticGetter = [&](llvm::StringRef key) -> ctjs::FuncOp {
        if (staticMethods.contains(key)) { return {}; }
        auto definition = staticDefinitions.lookup(key);
        if (!definition && inherited) {
            auto environment = inheritedGetters.find(base);
            if (environment != inheritedGetters.end()) {
                definition = environment->second.lookup(key);
            }
        }
        // A copy made for this receiver answers instead of the shared body.
        if (auto copies = receiverGetters.find(constructor.getResult());
            definition && copies != receiverGetters.end()) {
            if (auto clone = copies->second.lookup(key)) { return clone; }
        }
        return definition ? target(definition.getGetter().getDefiningOp<ctjs::CreateClosureOp>())
                          : ctjs::FuncOp{};
    };
    const auto alwaysThrows = [&](auto && recurse, ctjs::FuncOp fn, unsigned depth) -> bool {
        if (!step() || !fn || depth == 64) { return false; }
        if (throwingGetters.contains(fn)) { return true; }
        if (!fn.getBody().hasOneBlock()) { return false; }
        auto & entry = fn.getBody().front();
        for (mlir::Operation & op : entry) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::RootOp>(op)) { continue; }
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
            return read && read.getObject() == entry.getArgument(ctjs::arg_receiver) &&
                   ctjs::ordinaryKey(read.getKey()) &&
                   recurse(recurse, staticGetter(ctjs::constantKey(read.getKey())), depth + 1);
        }
        return false;
    };
    const auto throwingRead = [&](ctjs::GetPropertyOp read,
                                  llvm::ArrayRef<llvm::StringRef> ordered) {
        auto owner = sourceValue(read.getObject()).getDefiningOp<ctjs::GetPropertyOp>();
        if (baseClasses.contains(constructor.getResult()) || !owner ||
            sourceValue(owner.getObject()) != self ||
            ctjs::constantKey(owner.getKey()) != "constructor" ||
            !ctjs::ordinaryKey(read.getKey()) || hasField(ordered, "constructor")) {
            return false;
        }
        for (mlir::Operation * user : owner.getResult().getUsers()) {
            if (!step() || (user != read && !llvm::isa<ctjs::RootOp>(user))) { return false; }
        }
        return alwaysThrows(alwaysThrows, staticGetter(ctjs::constantKey(read.getKey())), 0);
    };
    const auto inspect = [&](auto && visit, mlir::Block & block,
                             llvm::SmallVector<llvm::StringRef> & ordered, unsigned depth,
                             bool & dead) -> bool {
        if (!step() || depth == 64) {
            return refuse("class own-key snapshot field proof exceeds its depth bound");
        }
        for (mlir::Operation & op : block) {
            if (!step()) { return false; }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                llvm::SmallVector<llvm::StringRef> joined;
                bool live = false;
                for (auto & region : branch->getRegions()) {
                    if (!spend(ordered.size())) { return false; }
                    auto path = ordered;
                    bool throws = false;
                    if (!region.empty() &&
                        (!region.hasOneBlock() ||
                         !llvm::isa<mlir::scf::YieldOp>(region.front().getTerminator()) ||
                         !visit(visit, region.front(), path, depth + 1, throws))) {
                        return refuse("class own-key snapshot requires fixed constructor fields");
                    }
                    if (throws) { continue; }
                    if (!live) {
                        joined = std::move(path);
                        live = true;
                    } else {
                        if (joined.size() != path.size()) {
                            return refuse("class own-key snapshot branches change ordered fields");
                        }
                        for (auto [left, right] : llvm::zip(joined, path)) {
                            if (!step()) { return false; }
                            if (left != right) {
                                return refuse(
                                    "class own-key snapshot branches change ordered fields");
                            }
                        }
                    }
                }
                if (!live) {
                    dead = true;
                    return true;
                }
                ordered = std::move(joined);
                continue;
            }
            if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(op);
                returned && !returned.getValue().getDefiningOp<ctjs::ConstantOp>()) {
                return refuse("class own-key snapshot requires a primitive constructor return");
            }
            if (llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                          ctjs::ReturnOp, ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp,
                          ctjs::CompareOp, ctjs::TruthyOp, ctjs::FromBoolOp,
                          mlir::arith::ConstantOp, mlir::ub::PoisonOp, mlir::scf::YieldOp>(op)) {
                continue;
            }
            if (llvm::isa<ctjs::LoadGlobalOp, ctjs::LoadUpvalueOp, ctjs::CellGetOp,
                          ctjs::GetPropertyOp, ctjs::CallOp, ctjs::CallDirectOp>(op)) {
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                    read && throwingRead(read, ordered)) {
                    dead = true;
                    return true;
                }
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                    read && sourceValue(read.getObject()) == self &&
                    (ctjs::constantKey(read.getKey()) == "constructor" ||
                     selectedMethods.count(ctjs::constantKey(read.getKey())) ||
                     hasField(ordered, ctjs::constantKey(read.getKey())))) {
                    continue;
                }
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(op);
                    call && sourceValue(call.getReceiver()) == self) {
                    auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    auto callee = read ? selectedMethods.lookup(ctjs::constantKey(read.getKey()))
                                       : ctjs::FuncOp{};
                    if (!callee || sourceValue(read.getObject()) != self ||
                        !inspectMethod(inspectMethod, callee, ordered, 0)) {
                        return refuse("class own-key snapshot constructor observes its receiver");
                    }
                    continue;
                }
                // Retain helper computations for the complete callable/body
                // census. Receiver arguments may read only fields present here.
                for (mlir::OpOperand & operand : op.getOpOperands()) {
                    if (!step()) { return false; }
                    if (sourceValue(operand.get()) == self && !borrowedFields(operand, ordered)) {
                        return refuse("class own-key snapshot constructor observes its receiver");
                    }
                }
                continue;
            }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
            auto key = write ? ctjs::constantKey(write.getKey()) : llvm::StringRef{};
            if (!write || write.getObject() != self || !ctjs::ordinaryKey(key) ||
                methodKeys.contains(key) ||
                llvm::all_of(key, [](char c) { return c >= '0' && c <= '9'; })) {
                return refuse("class own-key snapshot requires fixed constructor fields");
            }
            if (!spend(ordered.size())) { return false; }
            if (!llvm::is_contained(ordered, key)) { ordered.push_back(key); }
        }
        return true;
    };
    bool throws = false;
    if (!inspect(inspect, function.getBody().front(), fields, 0, throws)) { return false; }
    llvm::StringSet<> fieldSet;
    for (auto field : fields) {
        if (!step()) { return false; }
        fieldSet.insert(field);
    }
    if (required != snapshotFields.end()) {
        if (fields.size() != required->second.size()) {
            return refuse("inherited own-key snapshot requires the same ordered fields");
        }
        for (auto [field, inheritedField] : llvm::zip(fields, required->second)) {
            if (!step()) { return false; }
            if (field != inheritedField) {
                return refuse("inherited own-key snapshot requires the same ordered fields");
            }
        }
    }
    llvm::MapVector<mlir::Value, mlir::Attribute> replacements;
    for (mlir::Operation * operation : snapshots) {
        auto call = llvm::cast<ctjs::CallOp>(operation);
        if (!clearOwnFieldLoop(call, fields, contract)) { return false; }
        for (mlir::OpOperand & use : call.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            if (!read || use.getOperandNumber() != 0) {
                return refuse("class own-key snapshot requires fixed length or index reads");
            }
            mlir::Attribute value;
            if (ctjs::constantKey(read.getKey()) == "length") {
                value = ctjs::NumberAttr::get(
                    module.getContext(),
                    std::bit_cast<uint64_t>(static_cast<double>(fields.size())));
            } else {
                auto number = ctjs::constantAttr<ctjs::NumberAttr>(read.getKey());
                const auto index = number ? number.getDouble() : -1.0;
                if (!std::isfinite(index) || index < 0 || std::floor(index) != index ||
                    index >= static_cast<double>(fields.size())) {
                    return refuse("class own-key snapshot requires fixed length or index reads");
                }
                value =
                    ctjs::StringAttr::get(module.getContext(), fields[static_cast<size_t>(index)]);
            }
            replacements[read.getResult()] = value;
        }
    }
    // Every receiver still passes fieldsOnly after these exact key reads fold.
    // In addition, no method or external instance use may create a new field.
    // Construction-time calls separately proved the fields present at that point.
    for (mlir::Value receiver : receivers) {
        for (mlir::OpOperand * use : sourceUses(receiver)) {
            if (!step()) { return false; }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use->getOwner());
            if (!write || use->getOperandNumber() != 0) { continue; }
            auto key = replacements.lookup(write.getKey());
            if (!key) {
                auto constant = write.getKey().getDefiningOp<ctjs::ConstantOp>();
                if (constant) { key = constant.getValue(); }
            }
            auto text = llvm::dyn_cast_if_present<ctjs::StringAttr>(key);
            if (!text || !fieldSet.contains(text.getValue())) {
                return refuse("class own-key snapshot field set changes");
            }
        }
    }
    // Check the original Object binding, member and all aliases before erasing
    // any snapshot. Other calls/effects remain for the complete module census.
    if (!spendWalk(module, [](mlir::Operation * op) {
            return uint64_t(2) + 2 * uint64_t(op->getNumOperands());
        })) {
        return false;
    }
    auto binding = dom ? classBindingContract(contract) : contract;
    if (dom) {
        // Recheck every intrinsic consumed by the snapshot and its optional
        // clearing loop. DOM values still owe the complete typed body proof.
        for (const auto & name : contract.initialIntrinsics) {
            if (name == "Object" || name == "Array" || host_detail::iteratorIntrinsicArity(name)) {
                binding.initialIntrinsics.push_back(name);
            }
        }
    }
    if (auto problem = host_detail::initialBindingProblem(module, binding); !problem.empty()) {
        return refuse(problem);
    }
    for (auto [result, value] : replacements) {
        auto * read = result.getDefiningOp();
        mlir::OpBuilder at(read);
        auto constant = ctjs::ConstantOp::create(at, read->getLoc(), value);
        // Entry locals can already have fixed-cell proofs. Preserve their
        // cached producers as well as SSA uses before removing a snapshot read.
        for (auto & cell : cells) {
            if (!step()) { return false; }
            if (cell.second == result) { cell.second = constant; }
        }
        result.replaceAllUsesWith(constant);
        read->erase();
    }
    llvm::SetVector<mlir::Operation *> selections, loads;
    for (mlir::Operation * operation : snapshots) {
        auto call = llvm::cast<ctjs::CallOp>(operation);
        auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        selections.insert(read);
        loads.insert(read.getObject().getDefiningOp());
        pendingSnapshots.erase(call);
        pendingSnapshotMethods.erase(call->getParentOfType<ctjs::FuncOp>());
        eraseRooted(call);
    }
    for (mlir::Operation * operation : selections) { eraseIfRooted(operation); }
    for (mlir::Operation * operation : loads) { eraseIfRooted(operation); }
    // An empty snapshot is a requirement too. Descendants must recheck every
    // inherited body and field write even after its snapshot has been folded.
    for (auto ancestor = base; deferredSnapshots.contains(ancestor); ancestor = parent(ancestor)) {
        if (!step()) { return false; }
        snapshotFields.try_emplace(ancestor, fields);
    }
    snapshotFields[constructor.getResult()] = std::move(fields);
    return true;
}

bool classInitialization::retainedMapAliases(mlir::OpOperand & use,
                                             llvm::SmallVectorImpl<mlir::Value> & reads) {
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    auto owner = sourceValue(use.get()).getDefiningOp<ctjs::ConstructOp>();
    if (!call || use.getOperandNumber() != 3 || !owner ||
        !maps.contains(sourceValue(call.getReceiver()))) {
        return false;
    }
    auto map = sourceValue(call.getReceiver()).getDefiningOp<ctjs::ConstructOp>();
    auto function = owner->getParentOfType<ctjs::FuncOp>();
    if (!map || !function || map->getBlock() != owner->getBlock() ||
        owner->getBlock() != &function.getBody().front() || call->getBlock() != owner->getBlock() ||
        !owner->isBeforeInBlock(call)) {
        return refuse("class retained Map requires completed entry-block record owners");
    }
    if (!proveMaps()) { return false; }
    // Keep the existing full Map identity/use proof. This narrower source proof
    // cannot capture or transport the Map, so every operation shares its owner.
    for (auto [cell, initial] : cells) {
        if (!step()) { return false; }
        if (sourceValue(initial) != map.getResult()) { continue; }
        for (mlir::OpOperand & selected : cell.getUses()) {
            if (!step()) { return false; }
            if (!cellOperations.contains(selected.getOwner())) {
                return refuse("class retained Map requires direct entry-block operations");
            }
        }
    }
    for (mlir::OpOperand * selected : sourceUses(map.getResult())) {
        if (!step()) { return false; }
        auto * op = selected->getOwner();
        if (op->getBlock() != map->getBlock() ||
            (!llvm::isa<ctjs::RootOp>(op) && !mapOperations.contains(op))) {
            return refuse("class retained Map requires direct entry-block operations");
        }
    }
    // ponytail: literal string keys in one block; branch/capture transport
    // needs the shared native Map presence and owner proof before widening.
    llvm::StringMap<mlir::Value> entries;
    for (mlir::Operation & op : *map->getBlock()) {
        if (!step()) { return false; }
        auto operation = llvm::dyn_cast<ctjs::CallOp>(op);
        if (!operation || sourceValue(operation.getReceiver()) != map.getResult()) { continue; }
        auto method = operation.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (!method || !mapOperations.contains(operation)) {
            return refuse("class retained Map has an unproved invocation");
        }
        const auto action = ctjs::constantKey(method.getKey());
        if (action == "clear") {
            entries.clear();
            continue;
        }
        auto text = ctjs::constantAttr<ctjs::StringAttr>(sourceValue(operation.getArgs().front()));
        if (!text) { return refuse("class retained Map requires literal string keys"); }
        if (action == "set") {
            auto record = sourceValue(operation.getArgs()[1]).getDefiningOp<ctjs::ConstructOp>();
            if (!record || record->getBlock() != map->getBlock() ||
                !record->isBeforeInBlock(operation) ||
                sourceValue(record.getCallee()) != sourceValue(owner.getCallee()) ||
                sourceValue(record.getNewTarget()) != sourceValue(owner.getNewTarget())) {
                return refuse("class retained Map requires one completed constructor family");
            }
            entries[text.getValue()] = record.getResult();
        } else if (action == "delete") {
            entries.erase(text.getValue());
        } else if (action == "get") {
            if (!entries.count(text.getValue())) {
                return refuse("class retained Map get requires a present record");
            }
            reads.push_back(operation.getResult());
            retainedRecordOrigins[operation.getResult()] = entries.lookup(text.getValue());
        }
    }
    return reason.empty();
}

bool classInitialization::retainedMapReads(
    mlir::OpOperand & use, const llvm::StringSet<> & methodKeys,
    llvm::SmallVectorImpl<ctjs::GetPropertyOp> & staticReads) {
    llvm::SmallVector<mlir::Value> reads;
    if (!retainedMapAliases(use, reads)) { return false; }
    // Recheck saved aliases for every receiver census: inherited method keys
    // can change. The native stage still owes the typed carrier/lifetime proof.
    for (mlir::Value read : reads) {
        for (mlir::OpOperand * selected : sourceUses(read)) {
            if (!step()) { return false; }
            if (auto key = ctjs::propertyKey(selected->getOwner());
                key && ctjs::constantKey(key) == "constructor") {
                return refuse("class retained Map alias requires data field reads");
            }
        }
        if (!step() || !fieldsOnly(read, methodKeys, staticReads, true)) { return false; }
    }
    return reason.empty();
}

bool classInitialization::fieldsOnly(mlir::Value object, const llvm::StringSet<> & methodKeys,
                                     llvm::SmallVectorImpl<ctjs::GetPropertyOp> & staticReads,
                                     bool methodsAvailable, bool unconstructed) {
    for (mlir::OpOperand * sourceUse : sourceUses(object)) {
        auto & use = *sourceUse;
        if (!step()) { return false; }
        auto * op = use.getOwner();
        if (llvm::isa<ctjs::RootOp>(op)) { continue; }
        if (llvm::isa<ctjs::InstanceOfOp>(op) && use.getOperandNumber() == 0) {
            continue; // The complete instance/constructor proof runs after class setup.
        }
        if (use.getOperandNumber() == 3 &&
            deferredPublications.lookup(op->getParentOfType<ctjs::FuncOp>()) == op) {
            continue; // Only the sole leaf can discharge this exact base receiver edge.
        }
        if (retainedMapReads(use, methodKeys, staticReads)) { continue; }
        if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(op)) {
            llvm::SmallVector<ctjs::GetPropertyOp> reads;
            if (borrowedHelperReads(use, methodKeys, reads)) { continue; }
        }
        // A deferred snapshot and its loop's computed null writes wait for the
        // leaf that folds them; it rechecks this body once they are fields.
        if (pendingSnapshotMethods.contains(op->getParentOfType<ctjs::FuncOp>())) {
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
            auto value =
                write ? write.getValue().getDefiningOp<ctjs::ConstantOp>() : ctjs::ConstantOp{};
            if ((pendingSnapshots.contains(op) && use.getOperandNumber() == 2) ||
                (value && use.getOperandNumber() == 0 &&
                 llvm::isa<ctjs::NullAttr>(value.getValue()) &&
                 !write.getKey().getDefiningOp<ctjs::ConstantOp>())) {
                continue;
            }
        }
        if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
            closure && use.getOperandNumber() == 1 && helpers.contains(target(closure)) &&
            target(closure).getBody().front().getArgument(ctjs::arg_receiver).use_empty()) {
            // The confined helper was checked before this receiver census.
            // Saving lexical this is inert when its body never reads it.
            inertReceiverClosures.push_back(closure);
            continue;
        }
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
            read && use.getOperandNumber() == 0 &&
            ctjs::constantKey(read.getKey()) == "constructor") {
            // The exact fresh prototype owns this backedge. Its identity
            // may only select a proved local getter; no write or escape.
            for (mlir::OpOperand & selected : read.getResult().getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(selected.getOwner())) { continue; }
                auto getter = llvm::dyn_cast<ctjs::GetPropertyOp>(selected.getOwner());
                if (!getter || selected.getOperandNumber() != 0 ||
                    !ctjs::ordinaryKey(getter.getKey())) {
                    if (unconstructed) { continue; }
                    return refuse("class constructor identity escapes its local getter read");
                }
                staticReads.push_back(getter);
            }
            constructorReads.push_back(read);
            continue;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op); call && methodsAvailable) {
            auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (use.getOperandNumber() == 1 && read &&
                sourceValue(read.getObject()) == sourceValue(object) &&
                read.getResult().hasOneUse() &&
                methodKeys.contains(ctjs::constantKey(read.getKey()))) {
                methodCalls.insert(call);
                continue;
            }
        }
        const auto key = ctjs::propertyKey(op);
        // A body that never runs keeps its records for the census; the uses
        // themselves are proved on every copy that does run.
        if (unconstructed) { continue; }
        if (use.getOperandNumber() != 0 || !key || !ctjs::ordinaryKey(key)) {
            return refuse("class receiver escapes or observes a prototype/descriptor");
        }
        if (methodKeys.contains(ctjs::constantKey(key))) {
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
            auto call = read && read.getResult().hasOneUse()
                            ? llvm::dyn_cast<ctjs::CallOp>(*read.getResult().getUsers().begin())
                            : ctjs::CallOp{};
            if (!methodsAvailable || !call || call.getCallee() != read.getResult() ||
                sourceValue(call.getReceiver()) != sourceValue(object)) {
                return refuse("class method is observed or shadowed");
            }
        }
    }
    return reason.empty();
}

bool classInitialization::freshFunctionIndex(unsigned & next) {
    next = 0;
    for (auto function : module.getOps<ctjs::FuncOp>()) {
        if (!step()) { return false; }
        if (auto index = functionIndex(function)) { next = std::max(next, *index + 1); }
    }
    return true;
}

ctjs::FuncOp classInitialization::cloneFunction(ctjs::FuncOp fn, unsigned index) {
    auto copy = llvm::cast<ctjs::FuncOp>(fn->clone());
    copy.setSymName((fn.getSymName().rsplit('$').first + "$" + llvm::Twine(index)).str());
    module.getBody()->push_back(copy);
    return functions[index] = copy;
}

bool classInitialization::cloneReceiverMethods(
    ctjs::CreateClosureOp leaf, ctjs::CreateObjectOp prototype, ctjs::CallOp helper,
    const llvm::StringSet<> & shadowed, llvm::SmallVectorImpl<ctjs::SetPropertyOp> & selected,
    llvm::SmallVectorImpl<ctjs::SetPropertyOp> & definitions, bool perLeaf, bool proofOnly) {
    // An inherited method reading an overridden or copied getter through
    // this.constructor selects other getters for this class's instances. Put a
    // copy on this class's prototype, as if the source overrode it with the
    // same body: the own-method proof checks it in this class's environment,
    // and base instances keep the original. DOM entries use the same copies
    // for every inherited body, so each leaf gets its own typed method probes.
    const auto copies = receiverGetters.lookup(leaf.getResult());
    llvm::SmallVector<ctjs::SetPropertyOp> kept;
    for (ctjs::SetPropertyOp definition : selected) {
        if (!step()) { return false; }
        auto closure = definition.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        auto fn = target(closure);
        // Parameter observations are checked with all other method probes;
        // an unused formal supplies no argument or type authority.
        if (!fn) { return refuse("inherited method lacks a source body"); }
        bool reads = perLeaf;
        if (fn) {
            const auto receiver = fn.getBody().front().getArgument(ctjs::arg_receiver);
            for (mlir::Operation * user : receiver.getUsers()) {
                if (!step()) { return false; }
                auto constructor = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                if (!constructor || constructor.getObject() != receiver ||
                    ctjs::constantKey(constructor.getKey()) != "constructor") {
                    continue;
                }
                for (mlir::Operation * selector : constructor.getResult().getUsers()) {
                    if (!step()) { return false; }
                    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(selector);
                    const auto key = read ? ctjs::constantKey(read.getKey()) : llvm::StringRef{};
                    reads |= read && (shadowed.contains(key) || copies.contains(key));
                }
            }
        }
        if (!reads) {
            kept.push_back(definition);
            continue;
        }
        unsigned index = 0;
        if (perLeaf && !spendWalk(fn, cloneCost)) { return false; }
        if (!freshFunctionIndex(index)) { return false; }
        auto copy = cloneFunction(fn, index);
        if (snapshotMethods.contains(fn)) { snapshotMethods.insert(copy); }
        mlir::OpBuilder at(helper);
        mlir::Value key = definition.getKey();
        if (proofOnly) {
            // Keep ordinary dispatch unchanged, including calls from this
            // body back to the overridden key. Avoid all source string keys.
            llvm::StringSet<> keys;
            if (!walkSteps(module, [&](ctjs::ConstantOp constant) {
                    if (auto text = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())) {
                        keys.insert(text.getValue());
                    }
                    return mlir::WalkResult::advance();
                })) {
                return false;
            }
            std::string name = "__ctnative_shadow_" + std::to_string(index);
            while (keys.contains(name)) {
                if (!step()) { return false; }
                name += '_';
            }
            key = ctjs::ConstantOp::create(at, definition.getLoc(),
                                           ctjs::StringAttr::get(module.getContext(), name));
        }
        auto made = ctjs::CreateClosureOp::create(
            at, closure.getLoc(), closure.getType(), closure.getEnclosingClosure(),
            closure.getEnclosingThis(), at.getI32IntegerAttr(static_cast<int32_t>(index)),
            closure.getUpvalues(), closure.getEnclosingIndicesAttr());
        definitions.push_back(ctjs::SetPropertyOp::create(
            at, definition.getLoc(), prototype.getResult(), key, made.getResult()));
        if (proofOnly) {
            proofOnlyMethods.insert(definitions.back());
            needsDOMMethodProof = true;
        }
        auto home = ctjs::ConstantOp::create(at, definition.getLoc(),
                                             ctjs::StringAttr::get(module.getContext(), "__home"));
        ctjs::SetPropertyOp::create(at, definition.getLoc(), made.getResult(), home.getResult(),
                                    prototype.getResult());
    }
    selected.assign(kept.begin(), kept.end());
    return true;
}

bool classInitialization::cloneReceiverGetters(
    mlir::Value leaf, const llvm::StringMap<ctjs::DefineAccessorOp> & environment,
    const llvm::StringSet<> & shadowed) {
    // An inherited getter whose receiver reads reach a key this class shadows
    // selects different bodies when this class is the receiver. Give the class
    // its own copy; every other inherited body resolves as in its base.
    auto & clones = receiverGetters[leaf];
    llvm::DenseMap<mlir::Operation *, bool> reaches;
    const auto inherited = [&](llvm::StringRef key) -> ctjs::FuncOp {
        auto definition = environment.lookup(key);
        if (!definition || sourceValue(definition.getTarget()) == leaf) { return {}; }
        return target(definition.getGetter().getDefiningOp<ctjs::CreateClosureOp>());
    };
    const auto depends = [&](auto && self, llvm::StringRef key, unsigned depth) -> bool {
        if (!step()) { return false; }
        if (depth == 64) { return refuse("static getter dependency cycle"); }
        if (shadowed.contains(key)) { return true; }
        auto fn = inherited(key);
        if (!fn) { return false; } // Own getters are proved in this environment.
        if (auto known = reaches.find(fn); known != reaches.end()) { return known->second; }
        reaches[fn] = false; // A cycle is refused by the dependency fixpoint.
        bool result = false;
        for (mlir::OpOperand & use :
             fn.getBody().front().getArgument(ctjs::arg_receiver).getUses()) {
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            if (!read || !ctjs::ordinaryKey(read.getKey())) { continue; } // Body proof refuses.
            result |= self(self, ctjs::constantKey(read.getKey()), depth + 1);
            if (!reason.empty()) { return false; }
        }
        return reaches[fn] = result;
    };
    unsigned next = 0;
    if (!freshFunctionIndex(next)) { return false; }
    for (const auto & item : environment) {
        const auto key = item.first();
        auto source = inherited(key);
        if (!source) { continue; }
        const bool reached = depends(depends, key, 0);
        if (!reason.empty()) { return false; }
        if (!reached) { continue; }
        clones[key] = cloneFunction(source, next++);
    }
    return true;
}

bool classInitialization::staticGetters(const llvm::StringMap<ctjs::DefineAccessorOp> & definitions,
                                        llvm::ArrayRef<ctjs::GetPropertyOp> reads,
                                        ctjs::CallOp helper) {
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> dependencies;
    const auto firstRead = getterReads.size();
    const auto clones = receiverGetters.lookup(helper.getArgs().front());
    const auto resolve = [&](ctjs::GetPropertyOp read) -> ctjs::FuncOp {
        const auto key = ctjs::constantKey(read.getKey());
        auto definition = definitions.lookup(key);
        if (!ctjs::ordinaryKey(read.getKey()) || !definition) { return {}; }
        if (auto clone = clones.lookup(key)) { return clone; }
        return target(definition.getGetter().getDefiningOp<ctjs::CreateClosureOp>());
    };
    const auto proveBody = [&](ctjs::FuncOp fn) {
        auto & entry = fn.getBody().front();
        if (!llvm::hasSingleElement(fn.getBody()) || entry.getNumArguments() != 3 ||
            !entry.getArgument(ctjs::arg_callee).use_empty() ||
            !entry.getArgument(ctjs::arg_new_target).use_empty()) {
            return refuse("static getter observes its callable identity or parameters");
        }
        auto & required = dependencies[fn];
        if (auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(entry.getTerminator())) {
            if (!thrown.getValue().getDefiningOp<ctjs::ConstantOp>() &&
                !errorOperations.contains(thrown.getOperation())) {
                return refuse("static getter throw needs a literal or declared Error payload");
            }
            throwingGetters.insert(fn);
        }
        for (mlir::Operation & op : entry) {
            if (!step()) { return false; }
            if (errorOperations.contains(&op) ||
                (llvm::isa<ctjs::ThrowOp>(op) && throwingGetters.contains(fn))) {
                continue;
            }
            if (!llvm::isa<ctjs::ConstantOp, ctjs::CreateObjectOp, ctjs::BinaryOp, ctjs::UnaryOp,
                           ctjs::CompareOp, ctjs::TruthyOp, ctjs::FromBoolOp, ctjs::FrameEnterOp,
                           ctjs::FrameExitOp, ctjs::ReturnOp>(op)) {
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                if (!read || read.getObject() != entry.getArgument(ctjs::arg_receiver)) {
                    return refuse("static getter body is not a closed expression");
                }
            }
        }
        for (mlir::OpOperand & use : entry.getArgument(ctjs::arg_receiver).getUses()) {
            if (!step()) { return false; }
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            auto callee = read ? resolve(read) : ctjs::FuncOp{};
            if (use.getOperandNumber() != 0 || !callee) {
                return refuse("static getter receiver lacks an exact local getter dependency");
            }
            required.push_back(callee);
            getterReads.emplace_back(read, callee);
        }
        getters.insert(fn);
        return true;
    };
    for (const auto & item : definitions) {
        // Closure metadata and the compiler's home field precede accessors
        // in the interpreter. Keep their source reads until that boundary
        // has one semantics across the interpreter and native output.
        const auto key = item.first();
        if (closureMetadataKey(key)) { return refuse("static getter shadows closure metadata"); }
        auto definition = item.second;
        auto closure = definition.getGetter().getDefiningOp<ctjs::CreateClosureOp>();
        auto fn = target(closure);
        // An inherited environment contains the exact already-proved
        // definitions. Its source bodies and dependency targets stay unchanged.
        if (setup.contains(definition) && getters.contains(fn) && getterExpansion.contains(fn)) {
            continue;
        }
        if (!step() || !fn || !ctjs::ordinaryKey(item.first()) ||
            !undefined(definition.getSetter()) || closure->getBlock() != helper->getBlock() ||
            !closure->isBeforeInBlock(definition) || !undefined(closure.getEnclosingThis()) ||
            !closure.getUpvalues().empty()) {
            return refuse(
                "static getter needs a unique local capture-free getter without a setter");
        }
        ctjs::SetPropertyOp getterHome;
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (use.getOwner() == definition && use.getOperandNumber() == 1) { continue; }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            if (getterHome || !write || use.getOperandNumber() != 0 ||
                ctjs::constantKey(write.getKey()) != "__home" ||
                write.getValue() != helper.getArgs().front() ||
                write->getBlock() != helper->getBlock() || !closure->isBeforeInBlock(write) ||
                !write->isBeforeInBlock(helper)) {
                return refuse("static getter callable identity escapes its definition");
            }
            getterHome = write;
        }
        // The body census below forbids observing this metadata. Keep the
        // exact source assignment in the setup proof before erasing it.
        if (getterHome) { setup.insert(getterHome); }
        if (!proveBody(fn)) { return false; }
        getterClosures.push_back(closure);
        setup.insert(definition);
    }
    // A receiver-specific copy is proved like an own getter, in this class's
    // environment, and has no definition, closure or home of its own.
    for (const auto & item : clones) {
        if (!step() || !proveBody(item.second)) { return false; }
    }
    // ponytail: bounded fixpoint over fixed getter environments; overrides
    // require separate receiver-specific bodies before widening.
    llvm::DenseSet<mlir::Operation *> completed;
    while (completed.size() != dependencies.size()) {
        const auto before = completed.size();
        for (const auto & [fn, required] : dependencies) {
            if (!step()) { return false; }
            if (completed.contains(fn)) { continue; }
            bool ready = true;
            for (mlir::Operation * callee : required) {
                if (!step()) { return false; }
                ready &= getterExpansion.contains(callee);
            }
            if (!ready) { continue; }
            // A dependency call uses this function's closure argument.
            // Keep its callers too, so no such argument is copied into a
            // different function when expanding a getter chain.
            if (llvm::any_of(required, [&](mlir::Operation * callee) {
                    return throwingGetters.contains(callee);
                })) {
                throwingGetters.insert(fn);
            }
            unsigned cost = 0;
            for (mlir::Operation & op : llvm::cast<ctjs::FuncOp>(fn).getBody().front()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp>(op)) {
                    continue;
                }
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                const unsigned added = read ? getterExpansion.lookup(resolve(read)) : 1;
                if (cost > remaining || added > remaining - cost) {
                    return refuse("class initialization work budget exhausted");
                }
                cost += added;
            }
            if (throwingGetters.contains(fn)) {
                cost = 3; // Undefined, a fresh capture-free closure, and its direct call.
                if (cost > remaining) {
                    return refuse("class initialization work budget exhausted");
                }
            }
            getterExpansion[fn] = cost;
            completed.insert(fn);
            getterOrder.push_back(llvm::cast<ctjs::FuncOp>(fn));
        }
        if (completed.size() == before) { return refuse("static getter dependency cycle"); }
    }
    // ponytail: a throwing copy would need its own closure transport in the
    // throwing expansion; only returning copies are expanded inline.
    for (const auto & item : clones) {
        if (!step()) { return false; }
        if (throwingGetters.contains(item.second)) {
            return refuse("derived class requires receiver-preserving super normalization");
        }
    }
    for (ctjs::GetPropertyOp read : reads) {
        if (!step()) { return false; }
        auto callee = resolve(read);
        if (!callee) { return refuse("constructor read lacks an exact local static getter"); }
        getterReads.emplace_back(read, callee);
    }
    // Charge every clone before any mutation, including repeated reads and
    // the transitive expansion copied from already-expanded getter bodies.
    for (const auto & item : llvm::drop_begin(getterReads, firstRead)) {
        if (!spend(getterExpansion.lookup(item.second))) { return false; }
    }
    return true;
}

} // namespace ctcompile::ctnative::class_detail
