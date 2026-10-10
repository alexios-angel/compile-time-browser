//===- Rewrite.cpp - Apply a proved class initialization ------------------===//
//
// The mutating half of the class-initialization proof. Once prove() has
// accepted a module's class setup, rewrite() replaces its holder objects,
// getters, captured Maps and calls with direct calls and erases the setup
// code. transportHelperMaps() turns Maps that helpers read through captures
// into ordinary parameters.
//
//===----------------------------------------------------------------------===//

#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {

namespace {

/// Replaces `call` with a direct call of `function`: undefined receiver,
/// new.target and callee, the original arguments padded with undefined, then
/// `extra` as the last parameters.
ctjs::CallDirectOp callDirect(mlir::Operation * call, ctjs::FuncOp function,
                              mlir::ValueRange extra = {}) {
    mlir::OpBuilder at(call);
    auto absent = undefinedAt(at, call->getLoc());
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call);
    llvm::SmallVector<mlir::Value> arguments(direct ? direct.getArgs()
                                                    : llvm::cast<ctjs::CallOp>(call).getArgs());
    arguments.resize(function.getBody().front().getNumArguments() - ctjs::implicit_arguments -
                         extra.size(),
                     absent);
    llvm::append_range(arguments, extra);
    auto replacement =
        ctjs::CallDirectOp::create(at, call->getLoc(), call->getResult(0).getType(),
                                   mlir::FlatSymbolRefAttr::get(function.getSymNameAttr()), absent,
                                   absent, absent, arguments, nullptr, nullptr);
    call->getResult(0).replaceAllUsesWith(replacement.getResult());
    call->erase();
    return replacement;
}

/// Replaces every call of `read`'s result with a direct call of `function`.
/// Other users stay: they are inert ctjs.root uses the caller removes.
void callDirectUsers(mlir::Operation * read, ctjs::FuncOp function) {
    for (mlir::Operation * user : llvm::make_early_inc_range(read->getUsers())) {
        if (llvm::isa<ctjs::CallOp>(user)) { callDirect(user, function); }
    }
}

} // namespace

/// Passes each Map a helper function reads through a capture as an extra
/// trailing parameter instead, and adds that argument at every call. Callers
/// that do not own the Map forward it too (fixpoint below). Returns false
/// when the work budget runs out or the rewrite is refused (`reason`).
bool classInitialization::transportHelperMaps() {
    if (mapClosures.empty()) { return true; }
    // Helper bodies and frames are kept; only their Map captures become
    // parameters. Class constructors and methods keep capture slots, and no
    // new closure is created or duplicated for any caller.
    llvm::DenseMap<mlir::Operation *, ctjs::CreateClosureOp> closures;
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<mlir::Value>> needed;
    llvm::MapVector<mlir::Operation *, ctjs::FuncOp> edges;
    if (!walkSteps(module, [&](ctjs::CreateClosureOp closure) {
            closures[target(closure)] = closure;
            return mlir::WalkResult::advance();
        })) {
        return false;
    }
    if (!walkSteps(module, [&](ctjs::LoadUpvalueOp read) {
            auto function = read->getParentOfType<ctjs::FuncOp>();
            if (auto map = mapCaptures.lookup(read.getResult());
                map && helpers.contains(function)) {
                auto & values = needed[function];
                if (!llvm::is_contained(values, map)) { values.push_back(map); }
            }
            return mlir::WalkResult::advance();
        })) {
        return false;
    }
    if (needed.empty()) { return true; }
    if (!walkSteps(module, [&](mlir::Operation * op) {
            if (!helperCalls.contains(op)) { return mlir::WalkResult::advance(); }
            auto function = helperCallTarget(op, true);
            if (!function) { return refuseWalk("class Map helper call lacks an exact target"); }
            edges[op] = function;
            return mlir::WalkResult::advance();
        })) {
        return false;
    }
    // A caller either creates the Map itself or must receive it too, so
    // propagate the needed Maps up the call edges until nothing changes. This
    // covers helpers that only forward a Map to another helper.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto [op, function] : edges) {
            if (!step()) { return false; }
            auto caller = op->getParentOfType<ctjs::FuncOp>();
            const auto values = needed.lookup(function);
            for (auto map : values) {
                if (!step()) { return false; }
                if (map.getDefiningOp()->getParentOfType<ctjs::FuncOp>() == caller) { continue; }
                auto & carried = needed[caller];
                if (!llvm::is_contained(carried, map)) {
                    carried.push_back(map);
                    changed = true;
                }
            }
        }
    }
    // The new signature must reach every caller, so every symbol reference to
    // a changed helper must be one of the recorded direct calls. Value uses
    // are checked next.
    llvm::DenseSet<mlir::Operation *> referencedCalls;
    if (auto stop = host_detail::forEachSymbolUse(
            module, true, [&] { return step(); },
            [&](const mlir::SymbolTable::SymbolUse & use,
                ctjs::FuncOp function) -> std::optional<bool> {
                if (!helpers.contains(function) || !needed.contains(function)) {
                    return std::nullopt;
                }
                auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
                if (!direct || direct.getTarget() != function || edges.lookup(direct) != function ||
                    use.getSymbolRef() != direct.getCalleeAttr() ||
                    !referencedCalls.insert(direct).second) {
                    return refuse("class Map helper has an unproved symbol reference");
                }
                return std::nullopt;
            })) {
        return *stop ? refuse("class Map helper symbol uses could not be enumerated") : false;
    }
    for (const auto & [operation, values] : needed) {
        (void)values;
        if (!step()) { return false; }
        auto closure = closures.lookup(operation);
        if (!closure) { return refuse("class Map transport lacks its original closure"); }
        if (!helpers.contains(operation)) { continue; }
        for (auto * use : sourceUses(closure.getResult())) {
            if (!step()) { return false; }
            auto * owner = use->getOwner();
            if (llvm::isa<ctjs::RootOp>(owner)) { continue; }
            // The closure is the callee: operand 2 of ctjs.call_direct,
            // operand 0 of ctjs.call.
            if (edges.lookup(owner) == operation &&
                use->getOperandNumber() == (llvm::isa<ctjs::CallDirectOp>(owner) ? 2U : 0U)) {
                continue;
            }
            // Operands from kFirstCapture (2) on are captures.
            if (auto captured = llvm::dyn_cast<ctjs::CreateClosureOp>(owner);
                captured && use->getOperandNumber() >= 2 &&
                llvm::is_contained(capturedClosures, captured)) {
                continue;
            }
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(owner);
            auto holder = store ? localHolders.find(sourceValue(store.getObject()).getDefiningOp())
                                : localHolders.end();
            if (store && use->getOperandNumber() == 2 && holder != localHolders.end() &&
                llvm::is_contained(holder->second.stores, store)) {
                continue;
            }
            return refuse("class Map helper identity escapes its complete call census");
        }
    }
    llvm::DenseMap<mlir::Operation *, llvm::DenseMap<mlir::Value, mlir::Value>> available;
    llvm::DenseSet<mlir::Operation *> transported;
    for (const auto & [operation, values] : needed) {
        if (!step()) { return false; }
        auto function = llvm::cast<ctjs::FuncOp>(operation);
        auto closure = closures.lookup(operation);
        auto & block = function.getBody().front();
        if (helpers.contains(operation)) {
            transported.insert(operation);
            for (auto map : values) {
                if (!step()) { return false; }
                available[operation][map] = block.addArgument(map.getType(), function.getLoc());
            }
            function.setFunctionTypeAttr(mlir::TypeAttr::get(
                mlir::FunctionType::get(module.getContext(), block.getArgumentTypes(),
                                        function.getFunctionType().getResults())));
            if (!walkSteps(function, [&](ctjs::LoadUpvalueOp read) {
                    if (auto value =
                            available[operation].lookup(mapCaptures.lookup(read.getResult()))) {
                        read.getResult().replaceAllUsesWith(value);
                        read.erase();
                    }
                    return mlir::WalkResult::advance();
                })) {
                return false;
            }
            mapClosures.erase(closure);
            mlir::SymbolTable::setSymbolVisibility(function,
                                                   mlir::SymbolTable::Visibility::Private);
            continue;
        }
        if (!constructors.contains(operation) && !methods.contains(operation)) {
            return refuse("class Map transport requires a proved class or helper caller");
        }
        if (llvm::is_contained(staticMethodClosures, closure)) {
            return refuse("static class helper Map captures remain unsupported");
        }
        llvm::SmallVector<mlir::Value> captures(closure.getUpvalues());
        for (auto map : values) {
            if (!step()) { return false; }
            auto * producer = map.getDefiningOp();
            if (producer->getBlock() != closure->getBlock() ||
                !producer->isBeforeInBlock(closure)) {
                return refuse("class helper Map must be initialized before its caller closure");
            }
            size_t index = 0;
            while (index < captures.size() && sourceValue(cells.lookup(captures[index])) != map) {
                if (!step()) { return false; }
                ++index;
            }
            if (index == captures.size()) {
                mlir::Value cell;
                for (auto [candidate, initial] : cells) {
                    if (!step()) { return false; }
                    if (initial == map && mapCells.contains(candidate)) {
                        cell = candidate;
                        break;
                    }
                }
                if (!cell) { return refuse("class helper Map lacks its original immutable cell"); }
                for (auto * user : cell.getUsers()) {
                    if (!step()) { return false; }
                    if (llvm::isa<ctjs::CellSetOp>(user) &&
                        (user->getBlock() != closure->getBlock() ||
                         !user->isBeforeInBlock(closure))) {
                        return refuse(
                            "class helper Map cell is initialized after its caller closure");
                    }
                }
                captures.push_back(cell);
            }
            mlir::OpBuilder at(&block, block.begin());
            auto read = ctjs::LoadUpvalueOp::create(at, function.getLoc(), map.getType(),
                                                    block.getArgument(ctjs::arg_callee),
                                                    static_cast<uint32_t>(index));
            available[operation][map] = read;
        }
        setCaptures(closure, function, captures);
        mapClosures.insert(closure);
        if (!llvm::is_contained(capturedClosures, closure)) { capturedClosures.push_back(closure); }
    }
    for (auto [op, function] : edges) {
        if (!step()) { return false; }
        if (!transported.contains(function)) { continue; }
        auto caller = op->getParentOfType<ctjs::FuncOp>();
        llvm::SmallVector<mlir::Value> maps;
        for (auto map : needed.lookup(function)) {
            if (!step()) { return false; }
            if (map.getDefiningOp()->getParentOfType<ctjs::FuncOp>() == caller) {
                mlir::DominanceInfo dominance(caller);
                if (!dominance.properlyDominates(map, op)) {
                    return refuse("class helper Map does not reach its local call");
                }
                maps.push_back(map);
            } else {
                maps.push_back(available[caller].lookup(map));
            }
        }
        // callDirect() replaces this call, so drop it from inertCalleeCalls,
        // which rewrite() visits later. The spend() pays for the linear scans
        // in llvm::erase and retargetCells.
        if (!spend(inertCalleeCalls.size() + cells.size())) { return false; }
        llvm::erase(inertCalleeCalls, op);
        const auto previous = op->getResult(0);
        retargetCells(previous, callDirect(op, function, maps).getResult());
    }
    return reason.empty();
}

void classInitialization::eraseRooted(mlir::Operation * operation) {
    for (mlir::Operation * root : llvm::make_early_inc_range(operation->getUsers())) {
        if (!llvm::isa<ctjs::RootOp>(root)) {
            llvm::report_fatal_error("proved callable holder retains an observable use");
        }
        root->erase();
    }
    operation->erase();
}

void classInitialization::retireCell(mlir::Value cell) {
    llvm::erase_if(cellReads, [&](ctjs::CellGetOp read) {
        if (read.getCell() != cell) { return false; }
        cellOperations.erase(read);
        eraseRooted(read);
        return true;
    });
    for (mlir::Operation * user : llvm::make_early_inc_range(cell.getUsers())) {
        cellOperations.erase(user);
        user->erase();
    }
    cellOperations.erase(cell.getDefiningOp());
    cells.erase(cell);
    cell.getDefiningOp()->erase();
}

void classInitialization::retireHelper(ctjs::CreateClosureOp closure, ctjs::FuncOp function) {
    earlyCaptures.erase(closure);
    eraseRooted(closure);
    functions.erase(*functionIndex(function));
    function.erase();
}

llvm::SmallVector<ctjs::FuncOp> classInitialization::expandHolders() {
    // Every alias and implicit-argument use was checked before mutation.
    // Slot functions are kept, even unused ones, and only made private: DOM
    // slots still need the typed invocation proof, and rewrite() erases the
    // others once nothing names them.
    llvm::SmallVector<ctjs::FuncOp> targets;
    const auto expand = [&](mlir::Operation * owner, CallableObject & holder) {
        auto publication = llvm::dyn_cast<ctjs::StoreGlobalOp>(owner);
        auto * object = publication ? publication.getValue().getDefiningOp() : owner;
        for (auto [read, closure] : holder.reads) {
            callDirectUsers(read, target(closure));
            eraseRooted(read);
        }
        for (ctjs::SetPropertyOp store : holder.stores) {
            auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            auto function = target(closure);
            targets.push_back(function);
            mlir::SymbolTable::setSymbolVisibility(function,
                                                   mlir::SymbolTable::Visibility::Private);
            store.erase();
            eraseRooted(closure);
        }
        for (ctjs::LoadGlobalOp load : holder.loads) { eraseRooted(load); }
        if (publication) {
            publication.erase();
            eraseRooted(object);
        }
    };
    for (auto & [publication, holder] : globalHolders) { expand(publication, holder); }
    for (auto & [object, holder] : localHolders) { expand(object, holder); }
    return targets;
}

void classInitialization::rewrite() {
    // Every check has already run on the current IR, so the rewrite may now
    // erase setup code. Strip the `ctnative.*` report attributes first: they
    // are diagnostics only and later passes must not treat them as proofs.
    module.walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
    for (auto [operation, answer] : instanceOfResults) {
        auto test = llvm::cast<ctjs::InstanceOfOp>(operation);
        mlir::OpBuilder at(test);
        // The importer boxes the i1 result (ctjs.from_bool) before JavaScript
        // value operations use it; replace those boxes with Boolean constants.
        for (auto * user : llvm::make_early_inc_range(test.getResult().getUsers())) {
            if (auto boxed = llvm::dyn_cast<ctjs::FromBoolOp>(user)) {
                at.setInsertionPoint(boxed);
                auto value = ctjs::ConstantOp::create(
                    at, boxed.getLoc(), ctjs::BooleanAttr::get(module.getContext(), answer));
                boxed.getResult().replaceAllUsesWith(value.getResult());
                boxed.erase();
            }
        }
        if (!test.getResult().use_empty()) {
            at.setInsertionPoint(test);
            auto value = mlir::arith::ConstantIntOp::create(at, test.getLoc(), answer, 1);
            test.getResult().replaceAllUsesWith(value.getResult());
        }
        test.erase(); // The operand producers stay, with their effects.
    }
    // The census proved these inherited methods never change. Copy them onto
    // each constructed subclass's prototype, which nothing has observed yet;
    // ordinary method lowering still checks every call and receiver. The base
    // class is complete before the `extends` call, so every inherited key and
    // closure dominates this point even if the prototype was allocated earlier.
    llvm::StringSet<> readKeys;
    bool dynamicRead = false;
    module.walk([&](ctjs::GetPropertyOp read) {
        auto key = ctjs::constantKey(read.getKey());
        dynamicRead |= key.empty();
        readKeys.insert(key);
    });
    for (auto [inherited, definition] : inheritedSlots) {
        // Copy only methods some read can still reach: a key that no constant
        // read names (and no computed read exists) is never looked up.
        if (!dynamicRead && !readKeys.contains(ctjs::constantKey(definition.getKey()))) {
            continue;
        }
        mlir::OpBuilder at(inherited);
        ctjs::SetPropertyOp::create(at, definition.getLoc(), inherited.getArgs()[2],
                                    definition.getKey(), definition.getValue());
    }
    for (auto & [function, copy] : normalizedMethods) {
        (*copy).walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
        function.getBody().takeBody(copy->getBody());
    }
    for (ctjs::CreateClosureOp closure : inertReceiverClosures) {
        mlir::OpBuilder at(closure);
        closure.getEnclosingThisMutable().assign(undefinedAt(at, closure.getLoc()));
    }
    // expandHolders() erases the slot closures. Clear their captures first;
    // the kept function bodies still contain the recorded capture reads,
    // which are rewritten below.
    for (ctjs::CreateClosureOp closure : capturedClosures) {
        if (!mapClosures.contains(closure)) { setCaptures(closure, target(closure), {}); }
    }
    const auto holderTargets = expandHolders();
    for (auto [read, function] : staticMethodReads) {
        // Call the static method's original function directly instead of
        // cloning its body; getter and capture expansion below removes its
        // implicit-argument uses.
        callDirectUsers(read, function);
        eraseRooted(read);
    }
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::GetPropertyOp>> reads;
    llvm::DenseSet<mlir::Operation *> seenReads;
    for (auto [read, target] : getterReads) {
        if (seenReads.insert(read).second) { reads[target].push_back(read); }
    }
    for (ctjs::FuncOp target : getterOrder) {
        for (ctjs::GetPropertyOp read : reads[target]) {
            mlir::OpBuilder at(read);
            if (throwingGetters.contains(target)) {
                // Keep the throw in its original function: a direct call
                // propagates the exception without cloning a terminator into
                // the middle of the reader's block. Native lowering checks
                // exceptions and Error values separately.
                auto undefined =
                    ctjs::ConstantOp::create(at, read.getLoc(), read.getType(),
                                             ctjs::UndefinedAttr::get(module.getContext()));
                auto scope = read->getParentOfType<ctjs::FuncOp>();
                auto closure = ctjs::CreateClosureOp::create(
                    at, read.getLoc(), read.getType(),
                    scope.getBody().front().getArgument(ctjs::arg_callee), undefined,
                    at.getI32IntegerAttr(static_cast<int32_t>(*functionIndex(target))),
                    mlir::ValueRange{}, mlir::DenseI32ArrayAttr{});
                auto call = ctjs::CallDirectOp::create(
                    at, read.getLoc(), read.getType(),
                    mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()), undefined, undefined,
                    closure, mlir::ValueRange{}, nullptr, nullptr);
                read.getResult().replaceAllUsesWith(call.getResult());
                read.erase();
                continue;
            }
            mlir::IRMapping mapping;
            // Getters this one depends on were expanded first (getterOrder),
            // so the body uses no implicit arguments or outside values. Clone
            // it at each read, which keeps evaluation order and gives each read
            // its own fresh objects.
            for (mlir::Operation & op : target.getBody().front()) {
                if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp>(op)) { continue; }
                if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
                    read.getResult().replaceAllUsesWith(mapping.lookup(returned.getValue()));
                } else {
                    at.clone(op, mapping);
                }
            }
            read.erase();
        }
    }
    for (ctjs::GetPropertyOp read : constructorReads) {
        if (!seenReads.insert(read).second) { continue; }
        for (mlir::Operation * root : llvm::make_early_inc_range(read->getUsers())) {
            root->erase(); // Only ctjs.root uses remain after getter expansion.
        }
        read.erase();
    }
    for (ctjs::LoadUpvalueOp read : captureReads) {
        if (auto helper = callableCaptures.lookup(read)) { callDirectUsers(read, helper); }
        for (mlir::Operation * root : llvm::make_early_inc_range(read->getUsers())) {
            root->erase(); // Only ctjs.root uses remain after getter expansion.
        }
        read.erase();
    }
    for (ctjs::CreateClosureOp closure : capturedClosures) {
        if (!mapClosures.contains(closure)) { continue; }
        llvm::SmallVector<mlir::Value> retained;
        llvm::SmallVector<unsigned> indices;
        for (mlir::Value cell : closure.getUpvalues()) {
            indices.push_back(static_cast<unsigned>(retained.size()));
            if (mapCells.contains(cell)) { retained.push_back(cell); }
        }
        // Other capture reads were rewritten above. Keep only the Map cells,
        // renumbering the reads, for the ordinary closure lifter.
        target(closure).walk([&](ctjs::LoadUpvalueOp read) {
            read.setIndex(indices[static_cast<size_t>(read.getIndex())]);
        });
        setCaptures(closure, target(closure), retained);
    }
    for (ctjs::CellGetOp read : cellReads) {
        read.getResult().replaceAllUsesWith(cells.lookup(read.getCell()));
        read.erase();
    }
    for (auto [value, initial] : cells) {
        (void)initial;
        if (mapCells.contains(value)) { continue; }
        for (mlir::Operation * use : llvm::make_early_inc_range(value.getUsers())) {
            use->erase(); // Stores proved to write the same value, and roots.
        }
        value.getDefiningOp()->erase();
    }
    // Holder objects can be erased only now that the cells and capture reads
    // carrying them are gone. expandHolders() already removed slot calls and
    // closures.
    for (auto & [object, holder] : localHolders) {
        (void)holder;
        eraseRooted(object);
    }
    // Calls whose callee closure is otherwise unused: drop the closure value.
    // The capture proof checked every implicit argument before any mutation.
    for (mlir::Operation * operation : inertCalleeCalls) {
        // A direct call already names its target; its callee value was proved
        // unused, so pass undefined instead.
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
            mlir::OpBuilder at(operation);
            direct.getCalleeValueMutable().assign(undefinedAt(at, operation->getLoc()));
            continue;
        }
        auto call = llvm::cast<ctjs::CallOp>(operation);
        callDirect(call, target(call.getCallee().getDefiningOp<ctjs::CreateClosureOp>()));
    }
    for (mlir::Operation * operation : capturedHelpers) {
        auto closure = llvm::cast<ctjs::CreateClosureOp>(operation);
        mlir::SymbolTable::setSymbolVisibility(target(closure),
                                               mlir::SymbolTable::Visibility::Private);
        eraseIfRooted(closure);
    }
    // Capture reads have now been rewritten, including in unused slots. Erase
    // slot functions nothing names, except DOM entry helpers, which still need
    // the typed invocation proof.
    for (ctjs::FuncOp function : holderTargets) {
        if (!domEntryHelpers.contains(function) &&
            mlir::SymbolTable::symbolKnownUseEmpty(function, module.getOperation()) &&
            mlir::SymbolTable::symbolKnownUseEmpty(function, &module.getBodyRegion())) {
            function.erase();
        }
    }
    for (ctjs::CallOp call : calls) { call.erase(); }
    for (mlir::Operation * op : setup) { op->erase(); }
    for (ctjs::CreateClosureOp closure : staticMethodClosures) {
        auto function = target(closure);
        mlir::SymbolTable::setSymbolVisibility(function, mlir::SymbolTable::Visibility::Private);
        eraseRooted(closure);
        if (mlir::SymbolTable::symbolKnownUseEmpty(function, module.getOperation()) &&
            mlir::SymbolTable::symbolKnownUseEmpty(function, &module.getBodyRegion())) {
            function.erase();
        }
    }
    for (ctjs::CreateClosureOp closure : getterClosures) {
        for (mlir::Operation * root : llvm::make_early_inc_range(closure->getUsers())) {
            root->erase(); // Only ctjs.root uses remain after descriptor removal.
        }
        closure.erase();
    }
    // The original getter closures are gone. A throwing getter is still called
    // directly by symbol, so keep it while referenced. Visit getters in
    // reverse dependency order so a chain of unused throwing getters is erased
    // in one pass.
    // ponytail: one symbol scan per getter; index the uses if large classes
    // make this slow.
    for (ctjs::FuncOp getter : llvm::reverse(getterOrder)) {
        if (!throwingGetters.contains(getter) ||
            mlir::SymbolTable::symbolKnownUseEmpty(getter, &module.getBodyRegion())) {
            getter.erase();
        }
    }
    // Never-constructed classes and shadowed methods may have lost their last
    // use with the setup code. Erase those no closure use or symbol reference
    // keeps alive. Erase all closures before bodies that might contain
    // another closure.
    llvm::SmallVector<std::pair<ctjs::CreateClosureOp, ctjs::FuncOp>> unusedCallables;
    module.walk([&](ctjs::CreateClosureOp closure) {
        auto function = target(closure);
        if ((!baseClasses.contains(closure.getResult()) && !constructors.contains(function) &&
             !methods.contains(function)) ||
            !rootedOnly(closure) ||
            !mlir::SymbolTable::symbolKnownUseEmpty(function, module.getOperation()) ||
            !mlir::SymbolTable::symbolKnownUseEmpty(function, &module.getBodyRegion())) {
            return;
        }
        unusedCallables.emplace_back(closure, function);
    });
    for (auto & callable : unusedCallables) { eraseRooted(callable.first); }
    for (auto & callable : unusedCallables) { callable.second.erase(); }
    // `uncalledHelpers` are functions the proof skipped because nothing calls
    // them. Erase each once no closure or symbol names it any more (repeat
    // until no more are erased); native admission checks any that remain like
    // every other body.
    for (bool erased = true; erased;) {
        erased = false;
        for (auto function : llvm::make_early_inc_range(module.getOps<ctjs::FuncOp>())) {
            if (!uncalledHelpers.contains(function)) { continue; }
            const auto named = module.walk([&](ctjs::CreateClosureOp closure) {
                return target(closure) == function ? mlir::WalkResult::interrupt()
                                                   : mlir::WalkResult::advance();
            });
            if (!named.wasInterrupted() &&
                mlir::SymbolTable::symbolKnownUseEmpty(function, module.getOperation())) {
                functions.erase(*functionIndex(function));
                function.erase();
                erased = true;
            }
        }
    }
    module.walk([&](ctjs::LoadGlobalOp load) {
        if ((load.getName() == host_detail::classDefinedIntrinsic ||
             load.getName() == "__ctbrowser_class_heritage") &&
            load.getResult().use_empty()) {
            load.erase();
        }
    });
}

} // namespace ctcompile::ctnative::class_detail
