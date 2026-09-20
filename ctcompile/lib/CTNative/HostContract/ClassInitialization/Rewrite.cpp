#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {

bool classInitialization::transportHelperMaps() {
    if (mapClosures.empty()) { return true; }
    // Keep helper bodies and frames, passing their exact Maps as ordinary
    // parameters. Only real class closures retain capture slots; no closure
    // is synthesized at a cross-frame call or duplicated for another caller.
    llvm::DenseMap<mlir::Operation *, ctjs::CreateClosureOp> closures;
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<mlir::Value>> needed;
    llvm::MapVector<mlir::Operation *, ctjs::FuncOp> edges;
    auto scanned = module.walk([&](ctjs::CreateClosureOp closure) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        closures[target(closure)] = closure;
        return mlir::WalkResult::advance();
    });
    if (scanned.wasInterrupted()) { return false; }
    scanned = module.walk([&](ctjs::LoadUpvalueOp read) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        auto function = read->getParentOfType<ctjs::FuncOp>();
        if (auto map = mapCaptures.lookup(read.getResult()); map && helpers.contains(function)) {
            auto & values = needed[function];
            if (!llvm::is_contained(values, map)) { values.push_back(map); }
        }
        return mlir::WalkResult::advance();
    });
    if (scanned.wasInterrupted()) { return false; }
    if (needed.empty()) { return true; }
    scanned = module.walk([&](mlir::Operation * op) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (!helperCalls.contains(op)) { return mlir::WalkResult::advance(); }
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(op);
        auto call = llvm::dyn_cast<ctjs::CallOp>(op);
        auto callee = direct ? direct.getCalleeValue() : call.getCallee();
        auto function =
            direct ? direct.getTarget() : callableCaptures.lookup(callee.getDefiningOp());
        if (!function) { function = target(sourceClosure(sourceValue(callee))); }
        if (!function) {
            refuse("class Map helper call lacks an exact target");
            return mlir::WalkResult::interrupt();
        }
        edges[op] = function;
        return mlir::WalkResult::advance();
    });
    if (scanned.wasInterrupted()) { return false; }
    // A caller either owns the original Map or carries the same identity.
    // The bounded fixpoint includes helpers which only forward another helper.
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
    // A changed signature must cover every caller and cannot remain externally
    // observable. Check both symbol-attribute locations as well as value aliases.
    llvm::DenseSet<mlir::Operation *> referencedCalls;
    for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                              mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
        if (!uses) { return refuse("class Map helper symbol uses could not be enumerated"); }
        for (const auto & use : *uses) {
            if (!step()) { return false; }
            auto function = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                use.getUser(), use.getSymbolRef());
            if (helpers.contains(function) && needed.contains(function)) {
                auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
                if (!direct || direct.getTarget() != function || edges.lookup(direct) != function ||
                    use.getSymbolRef() != direct.getCalleeAttr() ||
                    !referencedCalls.insert(direct).second) {
                    return refuse("class Map helper has an unproved symbol reference");
                }
            }
        }
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
            if (edges.lookup(owner) == operation &&
                use->getOperandNumber() == (llvm::isa<ctjs::CallDirectOp>(owner) ? 2U : 0U)) {
                continue;
            }
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
    llvm::DenseMap<mlir::Operation *, unsigned> originalParameters;
    for (const auto & [operation, values] : needed) {
        if (!step()) { return false; }
        auto function = llvm::cast<ctjs::FuncOp>(operation);
        auto closure = closures.lookup(operation);
        auto & block = function.getBody().front();
        if (helpers.contains(operation)) {
            originalParameters[operation] = block.getNumArguments() - ctjs::implicit_arguments;
            for (auto map : values) {
                if (!step()) { return false; }
                available[operation][map] = block.addArgument(map.getType(), function.getLoc());
            }
            function.setFunctionTypeAttr(mlir::TypeAttr::get(
                mlir::FunctionType::get(module.getContext(), block.getArgumentTypes(),
                                        function.getFunctionType().getResults())));
            auto rewritten = function.walk([&](ctjs::LoadUpvalueOp read) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                if (auto value =
                        available[operation].lookup(mapCaptures.lookup(read.getResult()))) {
                    read.getResult().replaceAllUsesWith(value);
                    read.erase();
                }
                return mlir::WalkResult::advance();
            });
            if (rewritten.wasInterrupted()) { return false; }
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
        closure.getUpvaluesMutable().assign(captures);
        closure.removeEnclosingIndicesAttr();
        function.setUpvalueCount(static_cast<uint32_t>(captures.size()));
        mapClosures.insert(closure);
        if (!llvm::is_contained(capturedClosures, closure)) { capturedClosures.push_back(closure); }
    }
    for (auto [op, function] : edges) {
        if (!step()) { return false; }
        if (!originalParameters.contains(function)) { continue; }
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(op);
        auto call = llvm::dyn_cast<ctjs::CallOp>(op);
        auto caller = op->getParentOfType<ctjs::FuncOp>();
        mlir::OpBuilder at(op);
        auto absent = ctjs::ConstantOp::create(at, op->getLoc(),
                                               ctjs::UndefinedAttr::get(module.getContext()));
        llvm::SmallVector<mlir::Value> arguments(direct ? direct.getArgs() : call.getArgs());
        arguments.resize(originalParameters.lookup(function), absent);
        for (auto map : needed.lookup(function)) {
            if (!step()) { return false; }
            if (map.getDefiningOp()->getParentOfType<ctjs::FuncOp>() == caller) {
                mlir::DominanceInfo dominance(caller);
                if (!dominance.properlyDominates(map, op)) {
                    return refuse("class helper Map does not reach its local call");
                }
                arguments.push_back(map);
            } else {
                arguments.push_back(available[caller].lookup(map));
            }
        }
        auto replacement =
            ctjs::CallDirectOp::create(at, op->getLoc(), op->getResult(0).getType(),
                                       mlir::FlatSymbolRefAttr::get(function.getSymNameAttr()),
                                       absent, absent, absent, arguments, nullptr, nullptr);
        op->getResult(0).replaceAllUsesWith(replacement);
        op->erase();
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

llvm::SmallVector<ctjs::FuncOp> classInitialization::expandHolders() {
    // Every alias and implicit-argument use was checked before mutation.
    // Local DOM slots remain as functions until the complete invocation/type
    // proof; no unused body may disappear merely because its holder does.
    llvm::SmallVector<ctjs::FuncOp> targets;
    const auto expand = [&](mlir::Operation * owner, CallableObject & holder) {
        auto publication = llvm::dyn_cast<ctjs::StoreGlobalOp>(owner);
        auto * object = publication ? publication.getValue().getDefiningOp() : owner;
        for (auto [read, closure] : holder.reads) {
            auto function = target(closure);
            for (mlir::Operation * user : llvm::make_early_inc_range(read->getUsers())) {
                auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                if (!call) { continue; } // Inert roots are removed below.
                mlir::OpBuilder at(call);
                auto undefined =
                    ctjs::ConstantOp::create(at, call.getLoc(), call.getType(),
                                             ctjs::UndefinedAttr::get(module.getContext()));
                llvm::SmallVector<mlir::Value> arguments(call.getArgs());
                arguments.resize(function.getBody().front().getNumArguments() -
                                     ctjs::implicit_arguments,
                                 undefined);
                auto direct = ctjs::CallDirectOp::create(
                    at, call.getLoc(), call.getType(),
                    mlir::FlatSymbolRefAttr::get(function.getSymNameAttr()), undefined, undefined,
                    undefined, arguments, nullptr, nullptr);
                call.getResult().replaceAllUsesWith(direct.getResult());
                call.erase();
            }
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
    // Complete current-IR checks precede setup erasure. Normalized super
    // bodies exist only on the private candidate; reports grant no authority.
    module.walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
    // The complete source census proved these immutable callable identities.
    // Reuse them on each constructed leaf's already unobservable prototype;
    // ordinary method lowering still proves every call and borrowed receiver.
    // Base completion precedes heritage, so every inherited key and closure
    // dominates this point even if the leaf prototype was allocated earlier.
    llvm::StringSet<> readKeys;
    bool dynamicRead = false;
    module.walk([&](ctjs::GetPropertyOp read) {
        auto key = ctjs::constantKey(read.getKey());
        dynamicRead |= key.empty();
        readKeys.insert(key);
    });
    for (auto [inherited, definition] : inheritedSlots) {
        // A lexical call may have consumed the only lookup. All original
        // bodies passed the source census; retain slots for ordinary reads.
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
        closure.getEnclosingThisMutable().assign(ctjs::ConstantOp::create(
            at, closure.getLoc(), ctjs::UndefinedAttr::get(module.getContext())));
    }
    // Holder expansion erases its slot closures. Clear their proved capture
    // metadata first; the retained bodies still own all recorded reads.
    for (ctjs::CreateClosureOp closure : capturedClosures) {
        if (mapClosures.contains(closure)) { continue; }
        target(closure).setUpvalueCount(0);
        closure.getUpvaluesMutable().clear();
        closure.removeEnclosingIndicesAttr();
    }
    const auto holderTargets = expandHolders();
    for (auto [read, function] : staticMethodReads) {
        for (mlir::Operation * user : llvm::make_early_inc_range(read->getUsers())) {
            auto call = llvm::dyn_cast<ctjs::CallOp>(user);
            if (!call) { continue; }
            mlir::OpBuilder at(call);
            auto absent = ctjs::ConstantOp::create(at, call.getLoc(),
                                                   ctjs::UndefinedAttr::get(module.getContext()));
            llvm::SmallVector<mlir::Value> arguments(call.getArgs());
            arguments.resize(
                function.getBody().front().getNumArguments() - ctjs::implicit_arguments, absent);
            // Getter/capture expansion removes the proved implicit uses.
            // Keep the original body and frame rather than cloning its code.
            auto direct =
                ctjs::CallDirectOp::create(at, call.getLoc(), call.getType(),
                                           mlir::FlatSymbolRefAttr::get(function.getSymNameAttr()),
                                           absent, absent, absent, arguments, nullptr, nullptr);
            call.getResult().replaceAllUsesWith(direct.getResult());
            call.erase();
        }
        eraseRooted(read);
    }
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::GetPropertyOp>> reads;
    for (auto [read, target] : getterReads) { reads[target].push_back(read); }
    for (ctjs::FuncOp target : getterOrder) {
        for (ctjs::GetPropertyOp read : reads[target]) {
            mlir::OpBuilder at(read);
            if (throwingGetters.contains(target)) {
                // Keep the throw in its original function. A direct call
                // preserves abrupt completion without cloning a terminator
                // into the middle of the reader's block. Native completion
                // and Error representation remain separate admission proofs.
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
            // Dependencies have already been expanded. This closed body
            // has no remaining implicit-argument or external-value uses.
            // Clone at each original read, preserving evaluation order and
            // fresh object identity, including through getter dependencies.
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
        for (mlir::Operation * root : llvm::make_early_inc_range(read->getUsers())) {
            root->erase(); // Only inert roots remain after getter expansion.
        }
        read.erase();
    }
    for (ctjs::LoadUpvalueOp read : captureReads) {
        if (auto helper = callableCaptures.lookup(read)) {
            for (mlir::Operation * user : llvm::make_early_inc_range(read->getUsers())) {
                auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                if (!call) { continue; } // Only inert roots remain below.
                mlir::OpBuilder at(call);
                auto absent =
                    ctjs::ConstantOp::create(at, call.getLoc(), call.getType(),
                                             ctjs::UndefinedAttr::get(module.getContext()));
                llvm::SmallVector<mlir::Value> arguments(call.getArgs());
                arguments.resize(
                    helper.getBody().front().getNumArguments() - ctjs::implicit_arguments, absent);
                auto direct = ctjs::CallDirectOp::create(
                    at, call.getLoc(), call.getType(),
                    mlir::FlatSymbolRefAttr::get(helper.getSymNameAttr()), absent, absent, absent,
                    arguments, nullptr, nullptr);
                call.getResult().replaceAllUsesWith(direct.getResult());
                call.erase();
            }
        }
        for (mlir::Operation * root : llvm::make_early_inc_range(read->getUsers())) {
            root->erase(); // Only roots remain after captured getter expansion.
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
        // Other captured reads were consumed above. Preserve the original
        // Map cells for the ordinary closure lifter and its call-site proof.
        target(closure).walk([&](ctjs::LoadUpvalueOp read) {
            read.setIndex(indices[static_cast<size_t>(read.getIndex())]);
        });
        closure.getUpvaluesMutable().assign(retained);
        closure.removeEnclosingIndicesAttr();
        target(closure).setUpvalueCount(static_cast<uint32_t>(retained.size()));
    }
    for (ctjs::CellGetOp read : cellReads) {
        read.getResult().replaceAllUsesWith(cells.lookup(read.getCell()));
        read.erase();
    }
    for (auto [value, initial] : cells) {
        (void)initial;
        if (mapCells.contains(value)) { continue; }
        for (mlir::Operation * use : llvm::make_early_inc_range(value.getUsers())) {
            use->erase(); // Proved identical stores and inert cell roots.
        }
        value.getDefiningOp()->erase();
    }
    // Captured holders remain alive until all proved cell/capture transport
    // is gone. Slot calls and closures were removed by expandHolders.
    for (auto & [object, holder] : localHolders) {
        (void)holder;
        eraseRooted(object);
    }
    // The original capture proof checked every implicit argument before any
    // mutation. Only an unobserved sibling closure can disappear here; a
    // remaining entry call still goes through the ordinary closure lift.
    for (mlir::Operation * operation : inertCalleeCalls) {
        // Entry-local calls survive method normalization. Their original
        // callee uses were proved to be only inert callback enclosures.
        mlir::OpBuilder at(operation);
        auto absent = ctjs::ConstantOp::create(at, operation->getLoc(),
                                               ctjs::UndefinedAttr::get(module.getContext()));
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
            direct.getCalleeValueMutable().assign(absent);
            continue;
        }
        auto call = llvm::cast<ctjs::CallOp>(operation);
        auto helper = target(call.getCallee().getDefiningOp<ctjs::CreateClosureOp>());
        llvm::SmallVector<mlir::Value> arguments(call.getArgs());
        arguments.resize(helper.getBody().front().getNumArguments() - ctjs::implicit_arguments,
                         absent);
        auto direct =
            ctjs::CallDirectOp::create(at, call.getLoc(), call.getType(),
                                       mlir::FlatSymbolRefAttr::get(helper.getSymNameAttr()),
                                       absent, absent, absent, arguments, nullptr, nullptr);
        call.getResult().replaceAllUsesWith(direct.getResult());
        call.erase();
    }
    for (mlir::Operation * operation : capturedHelpers) {
        auto closure = llvm::cast<ctjs::CreateClosureOp>(operation);
        mlir::SymbolTable::setSymbolVisibility(target(closure),
                                               mlir::SymbolTable::Visibility::Private);
        if (llvm::any_of(closure->getUsers(),
                         [](mlir::Operation * user) { return !llvm::isa<ctjs::RootOp>(user); })) {
            continue;
        }
        for (mlir::Operation * root : llvm::make_early_inc_range(closure->getUsers())) {
            root->erase();
        }
        closure.erase();
    }
    // Captured reads have now been rewritten, including in unused slots.
    // DOM slots still need the complete typed invocation/effect proof.
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
            root->erase(); // Only inert roots remain after descriptor removal.
        }
        closure.erase();
    }
    // Original getter closures are gone; every new numeric closure has a
    // matching direct symbol call. Remove callers before their dependencies
    // so unused throwing chains disappear in one pass.
    // ponytail: one symbol scan per getter; index uses if large classes need it.
    for (ctjs::FuncOp getter : llvm::reverse(getterOrder)) {
        if (!throwingGetters.contains(getter) ||
            mlir::SymbolTable::symbolKnownUseEmpty(getter, &module.getBodyRegion())) {
            getter.erase();
        }
    }
    // Unconstructed bases and shadowed methods can lose their last setup
    // use. Complete original bodies passed the census before this check;
    // a remaining callable or symbol reference keeps them alive.
    // Erase all closures before bodies that might contain another closure.
    llvm::SmallVector<std::pair<ctjs::CreateClosureOp, ctjs::FuncOp>> unusedCallables;
    module.walk([&](ctjs::CreateClosureOp closure) {
        auto function = target(closure);
        if ((!baseClasses.contains(closure.getResult()) && !methods.contains(function)) ||
            !llvm::all_of(closure->getUsers(),
                          [](mlir::Operation * use) { return llvm::isa<ctjs::RootOp>(use); }) ||
            !mlir::SymbolTable::symbolKnownUseEmpty(function, module.getOperation()) ||
            !mlir::SymbolTable::symbolKnownUseEmpty(function, &module.getBodyRegion())) {
            return;
        }
        unusedCallables.emplace_back(closure, function);
    });
    for (auto & callable : unusedCallables) { eraseRooted(callable.first); }
    for (auto & callable : unusedCallables) { callable.second.erase(); }
    module.walk([&](ctjs::LoadGlobalOp load) {
        if ((load.getName() == host_detail::classDefinedIntrinsic ||
             load.getName() == "__ctbrowser_class_heritage") &&
            load.getResult().use_empty()) {
            load.erase();
        }
    });
}

} // namespace ctcompile::ctnative::class_detail
