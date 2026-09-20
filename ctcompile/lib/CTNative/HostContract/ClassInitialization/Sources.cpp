#include "Proof.hpp"

#include <bit>
#include <cmath>

namespace ctcompile::ctnative::class_detail {

bool classInitialization::step() {
    if (!remaining) { return refuse("class initialization work budget exhausted"); }
    --remaining;
    return true;
}

bool classInitialization::refuse(llvm::StringRef message) {
    if (reason.empty()) { reason = message.str(); }
    return false;
}

bool classInitialization::undefined(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    return constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
}

ctjs::FuncOp classInitialization::target(ctjs::CreateClosureOp closure) {
    if (!closure || closure.getFunction() < 0) { return {}; }
    // Numeric function indices belong to the enclosing source program.
    auto scope = closure->getParentOfType<ctjs::FuncOp>();
    if (!scope || scope.getBody().empty() || scope.getBody().front().getNumArguments() < 3 ||
        closure.getEnclosingClosure() != scope.getBody().front().getArgument(ctjs::arg_callee)) {
        return {};
    }
    return functions.lookup(static_cast<unsigned>(closure.getFunction()));
}

bool classInitialization::proveCells(ctjs::FuncOp entry) {
    // The importer boxes entry locals when one is captured. Only fixed,
    // ordered bindings are transport; their original producers stay live.
    for (ctjs::CreateCellOp cell : entry.getBody().front().getOps<ctjs::CreateCellOp>()) {
        if (!step()) { return false; }
        if (cells.count(cell.getResult())) { continue; }
        ctjs::CellSetOp first;
        const auto readPosition = [&](mlir::Operation * op) {
            if (llvm::isa<ctjs::CellGetOp>(op)) {
                while (op->getBlock() != cell->getBlock() &&
                       llvm::isa_and_nonnull<mlir::scf::IfOp, mlir::scf::WhileOp,
                                             mlir::scf::IndexSwitchOp>(op->getParentOp())) {
                    if (!step()) { break; }
                    op = op->getParentOp();
                }
            }
            return op;
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
                return refuse("class local cell is observed before initialization");
            }
            if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(op);
                read && use.getOperandNumber() == 0) {
                cellReads.push_back(read);
                cellOperations.insert(op);
                continue;
            }
            auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
            if (!first || !closure || use.getOperandNumber() < 2) {
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
    // Retain the original callback for the shared no-match or typed
    // Array filter proof, including its complete body and uses.
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
    // ponytail: bound the proof stack; use an explicit worklist for deeper chains.
    if (depth >= 64) { return refuse("class helper capture nesting limit exceeded"); }
    auto fn = target(method);
    const auto constructorValue = constructor ? constructor.getResult() : mlir::Value{};
    if (fn.getUpvalueCount() != static_cast<int64_t>(method.getUpvalues().size()) ||
        (method.getEnclosingIndicesAttr() &&
         llvm::any_of(method.getEnclosingIndicesAttr().asArrayRef(),
                      [](int32_t index) { return index >= 0; }))) {
        return refuse("class method lacks exact local capture slots");
    }
    for (mlir::Value capture : method.getUpvalues()) {
        if (!step() || !cells.count(capture)) {
            return refuse("class method capture lacks a fixed local cell");
        }
        auto value = sourceValue(cells.lookup(capture));
        if (value == constructorValue) { continue; }
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
            return refuse("class method capture is not its constructor or an inert sibling helper");
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
    for (mlir::OpOperand & use : fn.getBody().front().getArgument(ctjs::arg_callee).getUses()) {
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
                for (auto i = call.getArgs().size() + ctjs::implicit_arguments;
                     i < helper.getBody().front().getNumArguments(); ++i) {
                    if (!step()) { return false; }
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
                    if (call.getArgs().size() > parameters) { return {}; }
                    for (auto i = call.getArgs().size(); i < parameters; ++i) {
                        if (!step()) { return {}; }
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
        auto walked = module.walk([&](mlir::Operation * op) {
            const uint64_t cost = uint64_t(2) + op->getNumOperands();
            if (cost > remaining) {
                refuse("class initialization work budget exhausted");
                return mlir::WalkResult::interrupt();
            }
            remaining -= static_cast<unsigned>(cost);
            return mlir::WalkResult::advance();
        });
        cached->second = !walked.wasInterrupted() && closedDeclaration(store, fn, module);
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
                                              llvm::SmallVectorImpl<ctjs::GetPropertyOp> & reads) {
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    if ((!direct && !call) || use.getOperandNumber() < (direct ? 3u : 2u)) { return false; }
    if (!step()) { return false; }
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
    // ponytail: read-only named fields. Writes, forwarding and method/getter
    // dispatch need their own construction-point and shared-receiver proofs.
    for (mlir::OpOperand * selected : sourceUses(body.getArgument(index))) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::RootOp>(selected->getOwner())) { continue; }
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

bool classInitialization::ownFieldSnapshots(ctjs::CreateClosureOp constructor,
                                            llvm::ArrayRef<ctjs::ConstructOp> instances,
                                            llvm::ArrayRef<ctjs::SetPropertyOp> definitions,
                                            const llvm::StringSet<> & methodKeys,
                                            const HostContract & contract) {
    auto function = target(constructor);
    auto inherited = heritage.lookup(constructor.getResult());
    const auto base = inherited ? sourceValue(inherited.getArgs()[1]) : mlir::Value{};
    auto required = snapshotFields.find(base);
    llvm::SmallVector<mlir::Value> receivers;
    for (ctjs::ConstructOp made : instances) { receivers.push_back(made.getResult()); }
    llvm::SmallVector<ctjs::SetPropertyOp> allDefinitions(definitions);
    if (inherited) { allDefinitions.append(inheritedMethods.lookup(base)); }
    llvm::StringMap<ctjs::FuncOp> selectedMethods;
    for (ctjs::SetPropertyOp definition : allDefinitions) {
        if (!step()) { return false; }
        auto fn = target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
        if (fn) {
            receivers.push_back(fn.getBody().front().getArgument(ctjs::arg_receiver));
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
    if (contract.provider != HostContract::Provider::closedSource) {
        return refuse("class own-key snapshot requires fixed constructor fields");
    }
    if (inherited) {
        auto baseFunction = target(base.getDefiningOp<ctjs::CreateClosureOp>());
        if (!constructors.contains(baseFunction) ||
            !normalizeSuper(function, baseFunction, contract)) {
            return false;
        }
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
            mlir::Value key;
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                key = read.getKey();
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
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) { key = write.getKey(); }
            if (use->getOperandNumber() != 0 || !key ||
                !hasField(ordered, ctjs::constantKey(key))) {
                return refuse("class construction method requires an existing own field");
            }
        }
        return reason.empty();
    };
    const auto inspect = [&](auto && visit, mlir::Block & block,
                             llvm::SmallVector<llvm::StringRef> & ordered, unsigned depth) -> bool {
        if (!step() || depth == 64) {
            return refuse("class own-key snapshot field proof exceeds its depth bound");
        }
        for (mlir::Operation & op : block) {
            if (!step()) { return false; }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                llvm::SmallVector<llvm::StringRef> joined;
                for (auto [i, region] : llvm::enumerate(branch->getRegions())) {
                    for (auto field : ordered) {
                        (void)field;
                        if (!step()) { return false; }
                    }
                    auto path = ordered;
                    if (!region.empty() &&
                        (!region.hasOneBlock() ||
                         !llvm::isa<mlir::scf::YieldOp>(region.front().getTerminator()) ||
                         !visit(visit, region.front(), path, depth + 1))) {
                        return refuse("class own-key snapshot requires fixed constructor fields");
                    }
                    if (i == 0) {
                        joined = std::move(path);
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
            bool found = false;
            for (auto field : ordered) {
                if (!step()) { return false; }
                found |= field == key;
            }
            if (!found) { ordered.push_back(key); }
        }
        return true;
    };
    if (!inspect(inspect, function.getBody().front(), fields, 0)) { return false; }
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
                auto key = read.getKey().getDefiningOp<ctjs::ConstantOp>();
                auto number =
                    key ? llvm::dyn_cast<ctjs::NumberAttr>(key.getValue()) : ctjs::NumberAttr{};
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
    const auto counted = module.walk([&](mlir::Operation * op) {
        const uint64_t cost = uint64_t(2) + 2 * uint64_t(op->getNumOperands());
        if (cost > remaining) {
            refuse("class initialization work budget exhausted");
            return mlir::WalkResult::interrupt();
        }
        remaining -= static_cast<unsigned>(cost);
        return mlir::WalkResult::advance();
    });
    if (counted.wasInterrupted()) { return false; }
    if (auto problem = host_detail::initialBindingProblem(module, contract); !problem.empty()) {
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
        eraseRooted(call);
    }
    for (mlir::Operation * operation : selections) {
        if (llvm::all_of(operation->getUsers(),
                         [](mlir::Operation * op) { return llvm::isa<ctjs::RootOp>(op); })) {
            eraseRooted(operation);
        }
    }
    for (mlir::Operation * operation : loads) {
        if (llvm::all_of(operation->getUsers(),
                         [](mlir::Operation * op) { return llvm::isa<ctjs::RootOp>(op); })) {
            eraseRooted(operation);
        }
    }
    // An empty snapshot is a requirement too. Descendants must recheck every
    // inherited body and field write even after its snapshot has been folded.
    snapshotFields[constructor.getResult()] = std::move(fields);
    return true;
}

bool classInitialization::fieldsOnly(mlir::Value object, const llvm::StringSet<> & methodKeys,
                                     llvm::SmallVectorImpl<ctjs::GetPropertyOp> & staticReads,
                                     bool methodsAvailable) {
    for (mlir::OpOperand * sourceUse : sourceUses(object)) {
        auto & use = *sourceUse;
        if (!step()) { return false; }
        auto * op = use.getOwner();
        if (llvm::isa<ctjs::RootOp>(op)) { continue; }
        if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(op)) {
            llvm::SmallVector<ctjs::GetPropertyOp> reads;
            if (borrowedHelperReads(use, methodKeys, reads)) { continue; }
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
        mlir::Value key;
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) { key = read.getKey(); }
        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) { key = write.getKey(); }
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

bool classInitialization::staticGetters(const llvm::StringMap<ctjs::DefineAccessorOp> & definitions,
                                        llvm::ArrayRef<ctjs::GetPropertyOp> reads,
                                        ctjs::CallOp helper) {
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> dependencies;
    const auto firstRead = getterReads.size();
    const auto resolve = [&](ctjs::GetPropertyOp read) -> ctjs::FuncOp {
        auto definition = definitions.lookup(ctjs::constantKey(read.getKey()));
        if (!ctjs::ordinaryKey(read.getKey()) || !definition) { return {}; }
        return target(definition.getGetter().getDefiningOp<ctjs::CreateClosureOp>());
    };
    for (const auto & item : definitions) {
        // Closure metadata and the compiler's home field precede accessors
        // in the interpreter. Keep their source reads until that boundary
        // has one semantics across the interpreter and native output.
        const auto key = item.first();
        if (key == "name" || key == "length" || key == "__home" || key == "caller" ||
            key == "arguments") {
            return refuse("static getter shadows closure metadata");
        }
        auto definition = item.second;
        auto closure = definition.getGetter().getDefiningOp<ctjs::CreateClosureOp>();
        auto fn = target(closure);
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
        getterClosures.push_back(closure);
        setup.insert(definition);
    }
    // ponytail: bounded fixpoint over local getters; inherited/dynamic
    // receivers need a separate provenance proof before widening.
    llvm::DenseSet<mlir::Operation *> completed;
    llvm::DenseMap<mlir::Operation *, unsigned> expansion;
    while (completed.size() != dependencies.size()) {
        const auto before = completed.size();
        for (const auto & [fn, required] : dependencies) {
            if (!step()) { return false; }
            if (completed.contains(fn)) { continue; }
            bool ready = true;
            for (mlir::Operation * callee : required) {
                if (!step()) { return false; }
                ready &= completed.contains(callee);
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
                const unsigned added = read ? expansion.lookup(resolve(read)) : 1;
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
            expansion[fn] = cost;
            completed.insert(fn);
            getterOrder.push_back(llvm::cast<ctjs::FuncOp>(fn));
        }
        if (completed.size() == before) { return refuse("static getter dependency cycle"); }
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
        const unsigned cost = expansion.lookup(item.second);
        if (cost > remaining) { return refuse("class initialization work budget exhausted"); }
        remaining -= cost;
    }
    return true;
}

} // namespace ctcompile::ctnative::class_detail
