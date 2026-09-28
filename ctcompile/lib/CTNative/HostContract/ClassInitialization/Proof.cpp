#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {

bool classInitialization::proveInstanceOf(const HostContract & contract) {
    llvm::DenseMap<mlir::Value, ctjs::CallOp> completed;
    for (ctjs::CallOp call : calls) {
        if (!step()) { return false; }
        auto [at, fresh] = completed.try_emplace(sourceValue(call.getArgs().front()), call);
        if (!fresh) { at->second = {}; }
    }
    const auto walked = module.walk([&](ctjs::InstanceOfOp test) {
        const auto reject = [&](llvm::StringRef message) {
            refuse(message);
            return mlir::WalkResult::interrupt();
        };
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (llvm::count(contract.initialIntrinsics, "Function") != 1) {
            return reject("class instanceof requires the standard Function identity");
        }
        // The full source census excludes hook/prototype mutation and reentry.
        // Function supplies its initial inherited @@hasInstance. Only an exact
        // source construction carries nominal identity: equal C++ shapes do not.
        auto constructor =
            sourceValue(test.getConstructor()).getDefiningOp<ctjs::CreateClosureOp>();
        auto made = sourceValue(test.getObject()).getDefiningOp<ctjs::ConstructOp>();
        auto original = made ? sourceValue(made.getCallee()).getDefiningOp<ctjs::CreateClosureOp>()
                             : ctjs::CreateClosureOp{};
        auto wanted = constructor ? completed.lookup(constructor.getResult()) : ctjs::CallOp{};
        auto initialized = original ? completed.lookup(original.getResult()) : ctjs::CallOp{};
        auto scope = test->getParentOfType<ctjs::FuncOp>();
        if (!constructor || !made || !original || !wanted || !initialized ||
            !constructors.contains(target(constructor)) ||
            !constructors.contains(target(original)) ||
            sourceValue(made.getNewTarget()) != original.getResult() ||
            wanted->getBlock() != test->getBlock() || initialized->getBlock() != test->getBlock() ||
            made->getBlock() != test->getBlock() || !wanted->isBeforeInBlock(test) ||
            !initialized->isBeforeInBlock(made) || !made->isBeforeInBlock(test) ||
            methods.contains(scope) || constructors.contains(scope) || getters.contains(scope)) {
            return reject("class instanceof requires a completed local constructor and exact "
                          "same-block construction");
        }
        bool returned = false;
        const auto returns = target(original).walk([&](ctjs::ReturnOp result) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            returned = true;
            // Derived super normalization has already proved and preserved its
            // receiver. Other replacement-return proofs can extend this slice.
            if (!result.getValue().getDefiningOp<ctjs::ConstantOp>()) {
                refuse("class instanceof constructor may replace its instance");
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        });
        if (returns.wasInterrupted()) { return mlir::WalkResult::interrupt(); }
        if (!returned) { return reject("class instanceof constructor has no proved completion"); }
        mlir::Value ancestor = original.getResult();
        bool matches = false;
        while (ancestor) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (ancestor == constructor.getResult()) {
                matches = true;
                break;
            }
            auto parent = heritage.lookup(ancestor);
            ancestor = parent ? sourceValue(parent.getArgs()[1]) : mlir::Value{};
        }
        instanceOfResults[test] = matches;
        return mlir::WalkResult::advance();
    });
    return !walked.wasInterrupted();
}

void classInitialization::decideSlot(mlir::Value holder, ctjs::GetPropertyOp read) {
    const auto key = ctjs::constantKey(read.getKey());
    if (!holder || key.empty()) { return; }
    for (mlir::Operation * user : holder.getUsers()) {
        auto slot = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
        if (slot && slot.getObject() == holder && ctjs::constantKey(slot.getKey()) == key) {
            if (auto fn = target(slot.getValue().getDefiningOp<ctjs::CreateClosureOp>())) {
                decidedHelpers.insert(fn);
            }
        }
    }
}

bool classInitialization::dropUnreadStatics() {
    // A static method runs only through a read of its key: `C.k`, `this.k` in
    // a static body, `this.constructor.k`, or a read on a subclass. One that no
    // read reaches - outside the bodies of other unread static methods - never
    // runs: drop its definition, closure and every function only its body
    // creates, before any proof or census sees them. Every other use of a
    // class value is refused by the class proof itself.
    // ponytail: any read of the key anywhere keeps it, and so does a dynamic
    // key on a receiver, constructor or closure; a per-class read proof keeps
    // fewer.
    llvm::MapVector<mlir::Operation *, llvm::StringRef> statics;
    for (ctjs::CallOp call : calls) {
        auto closure = call.getArgs().front().getDefiningOp<ctjs::CreateClosureOp>();
        if (!closure) { continue; }
        for (mlir::Operation * user : closure->getUsers()) {
            if (!step()) { return false; }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            const auto key = write ? ctjs::constantKey(write.getKey()) : llvm::StringRef{};
            // Importer hooks such as `__fields` are read by runtime helpers
            // through the home object, never by a property read: keep them.
            if (write && write.getObject() == closure.getResult() && ctjs::ordinaryKey(key) &&
                key != "prototype" && !key.starts_with("__") &&
                write.getValue().getDefiningOp<ctjs::CreateClosureOp>() &&
                write->getBlock() == call->getBlock() && write->isBeforeInBlock(call)) {
                statics[write] = key;
            }
        }
    }
    // Every function a set of static bodies creates, transitively.
    const auto bodies = [&](auto && selected, llvm::SetVector<mlir::Operation *> & result) {
        for (auto [write, key] : statics) {
            if (!selected(key)) { continue; }
            auto value = llvm::cast<ctjs::SetPropertyOp>(write).getValue();
            if (auto fn = target(value.getDefiningOp<ctjs::CreateClosureOp>())) {
                result.insert(fn);
            }
        }
        for (size_t i = 0; i < result.size(); ++i) {
            const auto walked = result[i]->walk([&](ctjs::CreateClosureOp nested) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                if (auto fn = target(nested)) { result.insert(fn); }
                return mlir::WalkResult::advance();
            });
            if (walked.wasInterrupted()) { return false; }
        }
        return true;
    };
    llvm::StringSet<> unread;
    for (auto [write, key] : statics) { unread.insert(key); }
    for (bool changed = true; changed && !unread.empty();) {
        changed = false;
        llvm::SetVector<mlir::Operation *> excluded;
        if (!bodies([&](llvm::StringRef key) { return unread.contains(key); }, excluded)) {
            return false;
        }
        llvm::StringSet<> read;
        bool dynamic = false;
        const auto walked = module.walk([&](ctjs::GetPropertyOp get) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (excluded.contains(get->getParentOfType<ctjs::FuncOp>())) {
                return mlir::WalkResult::advance();
            }
            const auto key = ctjs::constantKey(get.getKey());
            if (!key.empty()) {
                read.insert(key);
                return mlir::WalkResult::advance();
            }
            // Only a class value, a receiver or a constructor read can name a
            // static method through a computed key.
            const auto object = sourceValue(get.getObject());
            auto argument = llvm::dyn_cast<mlir::BlockArgument>(object);
            auto owner = object.getDefiningOp<ctjs::GetPropertyOp>();
            dynamic |= (argument && argument.getArgNumber() == ctjs::arg_receiver &&
                        argument.getOwner()->isEntryBlock() &&
                        llvm::isa<ctjs::FuncOp>(argument.getOwner()->getParentOp())) ||
                       object.getDefiningOp<ctjs::CreateClosureOp>() ||
                       (owner && ctjs::constantKey(owner.getKey()) == "constructor");
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
        if (dynamic) { return true; }
        for (auto [write, key] : statics) {
            if (unread.contains(key) && read.contains(key)) {
                unread.erase(key);
                changed = true;
            }
        }
    }
    llvm::SetVector<mlir::Operation *> dead;
    if (!bodies([&](llvm::StringRef key) { return unread.contains(key); }, dead)) { return false; }
    // Check every closure, body and symbol before changing anything.
    llvm::SmallVector<mlir::Operation *> homes;
    for (auto [write, key] : statics) {
        if (!unread.contains(key)) { continue; }
        auto definition = llvm::cast<ctjs::SetPropertyOp>(write);
        auto closure = definition.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        // The definition, its lexical home and roots are the closure's only uses.
        for (mlir::Operation * user : closure->getUsers()) {
            if (!step()) { return false; }
            auto home = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            if (user == definition || llvm::isa<ctjs::RootOp>(user)) { continue; }
            if (!home || home.getObject() != closure.getResult() ||
                ctjs::constantKey(home.getKey()) != "__home") {
                return refuse("unread static method identity escapes its definition");
            }
            homes.push_back(home);
        }
    }
    for (mlir::Operation * fn : dead) {
        const auto walked = fn->walk([&](mlir::Operation * op) {
            if (!step() || llvm::is_contained(calls, op) || llvm::isa<ctjs::StoreGlobalOp>(op)) {
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        });
        if (!reason.empty()) { return false; }
        if (walked.wasInterrupted() ||
            !mlir::SymbolTable::symbolKnownUseEmpty(fn, module.getOperation())) {
            return refuse("unread static method declares, publishes or is named");
        }
    }
    // Their calls were source calls: a captured helper or holder slot one
    // named may have lost its last call. No capture proof has run yet, so
    // follow each capture to its cell's initial value.
    llvm::DenseMap<mlir::Operation *, ctjs::CreateClosureOp> creators;
    module.walk([&](ctjs::CreateClosureOp closure) {
        if (auto fn = target(closure)) { creators[fn] = closure; }
    });
    const auto captured = [&](mlir::Value value) -> mlir::Value {
        auto load = value.getDefiningOp<ctjs::LoadUpvalueOp>();
        auto closure =
            load ? creators.lookup(load->getParentOfType<ctjs::FuncOp>()) : ctjs::CreateClosureOp{};
        if (!closure || load.getIndex() < 0 ||
            static_cast<size_t>(load.getIndex()) >= closure.getUpvalues().size()) {
            return {};
        }
        auto initial = cells.lookup(closure.getUpvalues()[static_cast<size_t>(load.getIndex())]);
        return initial ? sourceValue(initial) : mlir::Value{};
    };
    for (mlir::Operation * fn : dead) {
        const auto noted = fn->walk([&](ctjs::CallOp call) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (read) {
                decideSlot(captured(read.getObject()), read);
            } else if (auto callee = captured(call.getCallee())) {
                if (auto helper = target(callee.getDefiningOp<ctjs::CreateClosureOp>())) {
                    decidedHelpers.insert(helper);
                }
            }
            return mlir::WalkResult::advance();
        });
        if (noted.wasInterrupted()) { return false; }
    }
    for (mlir::Operation * home : homes) { home->erase(); }
    for (auto [write, key] : statics) {
        if (!unread.contains(key)) { continue; }
        auto definition = llvm::cast<ctjs::SetPropertyOp>(write);
        auto closure = definition.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        definition.erase();
        eraseRooted(closure);
    }
    for (mlir::Operation * fn : dead) {
        fn->walk([&](mlir::Operation * op) {
            errorOperations.erase(op);
            cellOperations.erase(op);
        });
        forgetRecords(llvm::cast<ctjs::FuncOp>(fn).getBody());
        functions.erase(*functionIndex(llvm::cast<ctjs::FuncOp>(fn)));
        fn->erase();
    }
    return true;
}

bool classInitialization::prove(const HostContract & contract, bool domEntry) {
    auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    if (!entry || !functionIndex(entry)) {
        return refuse("class initialization requires a source entry");
    }
    if (contract.provider == HostContract::Provider::ctbrowserDOMDataSession &&
        (entry.getBody().empty() ||
         entry.getBody().front().getNumArguments() !=
             ctjs::implicit_arguments + contract.elementParameters.size())) {
        return refuse("class DOM preparation requires every entry element input");
    }
    for (mlir::Operation & op : module.getBody()->getOperations()) {
        if (!step()) { return false; }
        auto fn = llvm::dyn_cast<ctjs::FuncOp>(op);
        auto index = fn ? functionIndex(fn) : std::nullopt;
        if (!fn || !index || fn.getBody().empty() || fn.getBody().front().getNumArguments() < 3 ||
            !functions.try_emplace(*index, fn).second) {
            return refuse("class initialization requires complete source functions");
        }
    }
    ctjs::FuncOp declaration;
    if (functionIndex(entry) != 0) {
        declaration = functions.lookup(0);
        if (!declaration ||
            !host_detail::isInertEntryDeclaration(declaration, entry, [&] { return step(); })) {
            return refuse("class initialization requires an inert entry declaration");
        }
        // No host value may reenter source code before the fixed class
        // helper. DOM preparation defers these uses to its final typed proof.
        for (mlir::BlockArgument argument :
             entry.getBody().front().getArguments().drop_front(ctjs::implicit_arguments)) {
            if (!step() || (!domEntry &&
                            contract.provider != HostContract::Provider::ctbrowserDOMDataSession &&
                            !argument.use_empty())) {
                return refuse("class initialization requires unused entry parameters");
            }
        }
        for (mlir::OpOperand & use :
             entry.getBody().front().getArgument(ctjs::arg_callee).getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (!llvm::isa<ctjs::CreateClosureOp>(use.getOwner()) || use.getOperandNumber() != 0) {
                return refuse("class initialization entry observes its callee identity");
            }
        }
    }
    llvm::DenseSet<int64_t> createdFunctions;
    auto walked = module.walk([&](mlir::Operation * op) -> mlir::WalkResult {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (op->hasAttr("ctjs.skipped")) {
            refuse("class initialization cannot omit source functions");
            return mlir::WalkResult::interrupt();
        }
        if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
            closure && !createdFunctions.insert(closure.getFunction()).second) {
            refuse("class initialization requires unique source closure creation sites");
            return mlir::WalkResult::interrupt();
        }
        if (op->getNumRegions() && op != module.getOperation() &&
            !(llvm::isa<ctjs::FuncOp>(op) && op->getParentOp() == module.getOperation()) &&
            !llvm::isa<mlir::scf::IfOp, mlir::scf::ForOp, mlir::scf::WhileOp,
                       mlir::scf::IndexSwitchOp>(op)) {
            refuse("class initialization contains an unchecked source region");
            return mlir::WalkResult::interrupt();
        }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
            if (domEntry && llvm::is_contained(contract.initialIntrinsics, store.getName())) {
                refuse("declared DOM intrinsic binding is replaced by source");
                return mlir::WalkResult::interrupt();
            }
            auto [at, fresh] = globals.try_emplace(store.getName(), store);
            if (!fresh) { at->second = {}; }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
            auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
            if (load && load.getName() == host_detail::classDefinedIntrinsic) {
                calls.push_back(call);
            }
        }
        return mlir::WalkResult::advance();
    });
    if (walked.wasInterrupted()) { return false; }
    if (calls.empty()) { return refuse("source has no class initialization calls"); }
    if (llvm::is_contained(contract.initialIntrinsics, "Error")) {
        walked = module.walk([&](ctjs::ConstructOp made) -> mlir::WalkResult {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            auto load = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
            if (!load || load.getName() != "Error" || made.getNewTarget() != load.getResult() ||
                made.getArgs().size() != 1) {
                return mlir::WalkResult::advance();
            }
            auto message = made.getArgs().front().getDefiningOp<ctjs::ConstantOp>();
            if (!message || !llvm::isa<ctjs::StringAttr>(message.getValue())) {
                return mlir::WalkResult::advance();
            }
            for (mlir::OpOperand & use : made.getResult().getUses()) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(use.getOwner());
                if (!thrown) {
                    refuse("declared Error payload escapes its throw");
                    return mlir::WalkResult::interrupt();
                }
                errorOperations.insert(thrown);
            }
            errorOperations.insert(load);
            errorOperations.insert(made);
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
    }
    for (ctjs::CallOp call : calls) {
        auto scope = call->getParentOfType<ctjs::FuncOp>();
        if (!proveCells(scope)) { return false; }
        if (domEntry || !llvm::is_contained(contract.initialIntrinsics, "Map")) { continue; }
        for (ctjs::ConstructOp made : scope.getBody().front().getOps<ctjs::ConstructOp>()) {
            if (!step()) { return false; }
            if (!plainMapConstruction(made)) { continue; }
            maps.insert(made.getResult());
            mapOperations.insert(made);
            mapOperations.insert(made.getCallee().getDefiningOp());
        }
    }
    if (!proveHeritage(contract)) { return false; }
    if (!dropUnreadStatics()) { return false; }
    if (!maps.empty()) {
        llvm::SmallVector<unsigned> requiredHelpers;
        for (ctjs::CallOp call : calls) {
            if (!sinkCapturedPublication(call, contract, requiredHelpers)) { return false; }
        }
        llvm::DenseSet<mlir::Operation *> scopes;
        for (ctjs::CallOp call : calls) {
            if (!step()) { return false; }
            auto scope = call->getParentOfType<ctjs::FuncOp>();
            if (scopes.insert(scope).second &&
                (!normalizeCapturedMapHelpers(scope) || !normalizeNestedMaps(scope, contract))) {
                return false;
            }
        }
        for (unsigned helper : requiredHelpers) {
            if (!step()) { return false; }
            if (functions.contains(helper)) {
                return refuse("constructor registration requires complete captured Map expansion");
            }
        }
    }
    if (!heritage.empty()) {
        if (auto problem =
                host_detail::initialBindingProblem(module, classBindingContract(contract));
            !problem.empty()) {
            return refuse(problem);
        }
    }
    for (ctjs::CallOp call : calls) {
        if (!examine(call, contract, domEntry)) { return false; }
    }
    // Every class has recorded its getter reads; the census below sees the folds.
    if (!domEntry && !foldEmptyEntries(contract)) { return false; }
    if (!pendingSnapshots.empty()) {
        return refuse("class own-key snapshot deferred to leaves lacks a folding leaf");
    }
    if (!earlyCaptures.empty()) {
        return refuse("class local cell is observed before initialization");
    }
    if (!proveInstanceOf(contract)) { return false; }
    const auto recordHelper = [&](mlir::Operation * op) {
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(op);
        auto method = llvm::dyn_cast<ctjs::CallOp>(op);
        if (!direct && !method) { return mlir::WalkResult::advance(); }
        if (!step()) { return mlir::WalkResult::interrupt(); }
        auto callee = direct ? direct.getCalleeValue() : method.getCallee();
        auto closure =
            sourceClosure(sourceValue(callee),
                          domEntry && op->getParentOfType<ctjs::FuncOp>() == entry, domEntry);
        auto fn = target(closure);
        if (!fn || constructors.contains(fn) || methods.contains(fn) || getters.contains(fn)) {
            return mlir::WalkResult::advance();
        }
        auto read = callee.getDefiningOp<ctjs::GetPropertyOp>();
        const bool localCall = method && sourceValue(callee) == closure.getResult() &&
                               undefined(method.getReceiver()) &&
                               (domEntry || capturedHelpers.contains(closure));
        if (direct ? direct.getTarget() != fn || !undefined(direct.getReceiver()) ||
                         !undefined(direct.getNewTarget())
                   : !localCall && (!read || method.getReceiver() != read.getObject())) {
            return mlir::WalkResult::advance();
        }
        auto & block = fn.getBody().front();
        if (localCall &&
            method.getArgs().size() + ctjs::implicit_arguments > block.getNumArguments()) {
            refuse("class helper call has excess arguments");
            return mlir::WalkResult::interrupt();
        }
        const bool entryLocal = domEntry && op->getParentOfType<ctjs::FuncOp>() == entry &&
                                closure->getParentOfType<ctjs::FuncOp>() == entry &&
                                callee == closure.getResult();
        // A local helper can save its receiver in an inert filter callback.
        // Prove the original callback before examining that receiver use.
        if (entryLocal && !helperCallbacks(fn)) { return mlir::WalkResult::interrupt(); }
        if (!unusedReceiver(fn) || !block.getArgument(ctjs::arg_new_target).use_empty()) {
            return mlir::WalkResult::advance();
        }
        helpers.insert(fn);
        helperCalls.insert(op);
        if (entryLocal) {
            // This original call survives class rewriting and receives the
            // complete typed DOM proof. Uncalled holder slots may disappear
            // during rewriting, so they retain the strict source census.
            domEntryHelpers.insert(fn);
            if (method &&
                method.getArgs().size() + ctjs::implicit_arguments > block.getNumArguments()) {
                refuse("DOM class helper call has excess arguments");
                return mlir::WalkResult::interrupt();
            }
            inertCalleeCalls.push_back(op);
            capturedHelpers.insert(closure);
        }
        return mlir::WalkResult::advance();
    };
    // Prove holder targets before checking their enclosing direct callers,
    // whose receiver may only be saved by one of these unused-this arrows.
    walked = module.walk([&](ctjs::CallOp call) { return recordHelper(call); });
    if (walked.wasInterrupted() || !reason.empty()) { return false; }
    walked = module.walk([&](ctjs::CallDirectOp call) { return recordHelper(call); });
    if (walked.wasInterrupted() || !reason.empty()) { return false; }
    if (domEntry && !domEntryHelpers.empty()) {
        // A helper shared with an instance method must not grant an uncalled
        // static body DOM authority. Check after all captures/calls are known,
        // independent of which class or helper was encountered first.
        llvm::SetVector<mlir::Operation *> pending;
        for (ctjs::CreateClosureOp closure : staticMethodClosures) {
            if (!step()) { return false; }
            pending.insert(target(closure));
        }
        for (size_t i = 0; i < pending.size(); ++i) {
            auto function = llvm::cast<ctjs::FuncOp>(pending[i]);
            if (!step()) { return false; }
            if (domEntryHelpers.contains(function)) {
                return refuse("static method reaches a helper requiring DOM body proof");
            }
            auto scanned = function.walk([&](mlir::Operation * op) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                if (!helperCalls.contains(op)) { return mlir::WalkResult::advance(); }
                auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(op);
                auto call = llvm::dyn_cast<ctjs::CallOp>(op);
                auto callee = direct ? direct.getCalleeValue() : call.getCallee();
                auto fn =
                    direct ? direct.getTarget() : callableCaptures.lookup(callee.getDefiningOp());
                if (!fn) { fn = target(sourceClosure(callee)); }
                if (!fn) {
                    refuse("static method helper lacks a closed target");
                    return mlir::WalkResult::interrupt();
                }
                pending.insert(fn);
                return mlir::WalkResult::advance();
            });
            if (scanned.wasInterrupted() || !reason.empty()) { return false; }
        }
    }
    for (auto [read, object] : holderCaptures) {
        (void)read;
        if (!step() || !localHolders.contains(object.getDefiningOp())) {
            return refuse("captured holder lacks a complete local callable proof");
        }
    }
    for (auto & [publication, holder] : globalHolders) {
        (void)publication;
        for (ctjs::SetPropertyOp store : holder.stores) {
            if (!step()) { return false; }
            domEntryHelpers.erase(target(store.getValue().getDefiningOp<ctjs::CreateClosureOp>()));
        }
    }
    if (!deferredPublications.empty()) {
        return refuse("base publication lacks a completed unique leaf proof");
    }
    if (!proveMaps()) { return false; }
    if (contract.provider == HostContract::Provider::ctbrowserDOMDataSession) {
        if (!proveDOMDataFamily(contract)) { return false; }
    }
    // Every invocation the census below admits is a recorded call. A helper
    // or holder slot called in the source whose calls all sit in bodies that
    // never run - dropped or erased with a decided arm, in a leafBuilt original
    // or in another such helper - never runs either. One never called keeps
    // its census (the -unused-ambient controls).
    llvm::SmallVector<std::pair<mlir::Operation *, mlir::Operation *>> edges;
    llvm::SetVector<mlir::Operation *> uncalled = decidedHelpers;
    walked = module.walk([&](mlir::Operation * op) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (!helperCalls.contains(op)) { return mlir::WalkResult::advance(); }
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(op);
        auto call = llvm::dyn_cast<ctjs::CallOp>(op);
        auto callee = direct ? direct.getCalleeValue() : call.getCallee();
        auto fn = direct ? direct.getTarget() : callableCaptures.lookup(callee.getDefiningOp());
        if (!fn) { fn = target(sourceClosure(callee)); }
        if (!fn) {
            uncalled.clear();
            return mlir::WalkResult::interrupt();
        }
        uncalled.insert(fn);
        edges.emplace_back(op->getParentOfType<ctjs::FuncOp>(), fn);
        return mlir::WalkResult::advance();
    });
    if (!reason.empty()) { return false; }
    // DOM entries admit calls the typed proof checks later, without records.
    if (domEntry || contract.provider == HostContract::Provider::ctbrowserDOMDataSession) {
        uncalled.clear();
    }
    uncalled.remove_if([&](mlir::Operation * fn) {
        return methods.contains(fn) || constructors.contains(fn) || getters.contains(fn);
    });
    for (bool changed = true; changed;) {
        changed = false;
        for (auto [caller, callee] : edges) {
            if (!step()) { return false; }
            if (uncalled.contains(callee) && !leafBuilt.contains(caller) &&
                !uncalled.contains(caller)) {
                uncalled.remove(callee);
                changed = true;
            }
        }
    }
    uncalledHelpers.insert(uncalled.begin(), uncalled.end());
    // No ambient object, unknown callee, accessor, dynamic key or reflective
    // instruction can replace the fixed helper between entry and any call.
    // Reject the whole module, including suffixes and uncalled bodies.
    walked = module.walk([&](mlir::Operation * op) -> mlir::WalkResult {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        // This entire declaration was checked above. Retain it for the
        // entry provider, which separately decides whether it can be erased.
        if (declaration &&
            (op == declaration || op->getParentOfType<ctjs::FuncOp>() == declaration)) {
            return mlir::WalkResult::advance();
        }
        if (uncalledHelpers.contains(op->getParentOfType<ctjs::FuncOp>())) {
            return mlir::WalkResult::advance();
        }
        if (domDataOperations.contains(op) || mapOperations.contains(op) || setup.contains(op) ||
            retainedSetup.contains(op) || instanceOfResults.contains(op) ||
            methodCalls.contains(op) || helperCalls.contains(op) || llvm::is_contained(calls, op) ||
            llvm::is_contained(constructorReads, op) || cellOperations.contains(op) ||
            llvm::is_contained(captureReads, op)) {
            return mlir::WalkResult::advance();
        }
        if (errorOperations.contains(op) &&
            throwingGetters.contains(op->getParentOfType<ctjs::FuncOp>())) {
            return mlir::WalkResult::advance();
        }
        bool accepted =
            llvm::isa<mlir::ModuleOp, ctjs::ConstantOp, ctjs::CreateObjectOp, ctjs::FrameEnterOp,
                      ctjs::FrameExitOp, ctjs::RootOp, ctjs::ReturnOp, ctjs::StoreGlobalOp,
                      ctjs::BinaryOp, ctjs::UnaryOp, ctjs::CompareOp, ctjs::TruthyOp,
                      ctjs::FromBoolOp>(op);
        // Proved ordinary methods and exact local helpers may contain control flow.
        // The recursive census still checks every arm/body, including ones
        // never called. Constructors additionally retain structured conditional
        // fields; setup and getter cloning stay linear.
        // The lift represents break/continue/return edges with integer
        // flags and switches. These exact transport ops cannot reenter or
        // change the class helper; switch arms still get the full census.
        // Static ++/-- conversions also cannot reenter; native admission
        // separately requires numeric operands before emitting arithmetic.
        if (llvm::isa<mlir::scf::IfOp, mlir::scf::ForOp, mlir::scf::WhileOp,
                      mlir::scf::IndexSwitchOp, mlir::scf::YieldOp, mlir::scf::ConditionOp,
                      mlir::arith::ConstantOp, mlir::arith::IndexCastUIOp, mlir::arith::TruncIOp,
                      mlir::ub::PoisonOp, ctjs::BinaryStaticOp, mlir::cf::BranchOp,
                      mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(op)) {
            auto fn = op->getParentOfType<ctjs::FuncOp>();
            // Entry short-circuit results keep their source branches for
            // the final typed DOM proof; class setup is still checked above.
            accepted =
                methods.contains(fn) || helpers.contains(fn) ||
                (constructors.contains(fn) &&
                 llvm::isa<mlir::scf::IfOp, mlir::scf::YieldOp, mlir::arith::ConstantOp,
                           mlir::ub::PoisonOp, ctjs::BinaryStaticOp>(op)) ||
                ((domEntry ||
                  contract.provider == HostContract::Provider::ctbrowserDOMDataSession) &&
                 fn == entry &&
                 llvm::isa<mlir::scf::IfOp, mlir::scf::YieldOp, mlir::arith::ConstantOp>(op));
            if (accepted && llvm::isa<mlir::scf::IndexSwitchOp>(op)) {
                dispatchMethods.insert(op->getParentOfType<ctjs::FuncOp>());
            }
        }
        if (auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(op)) {
            // An uncaught object throw can reenter through formatting.
            // Preserve literal primitive throws; downstream lowering still
            // has to prove their completion and payload representation.
            accepted = (methods.contains(op->getParentOfType<ctjs::FuncOp>()) ||
                        throwingGetters.contains(op->getParentOfType<ctjs::FuncOp>())) &&
                       thrown.getValue().getDefiningOp<ctjs::ConstantOp>();
        }
        if (llvm::isa<ctjs::PushHandlerOp, ctjs::PopHandlerOp, ctjs::CheckOp, ctjs::CatchLandOp,
                      ctjs::CreateCellOp, ctjs::CellGetOp, ctjs::CellSetOp>(op)) {
            // Keep the original exception CFG for the existing DOM URI/JSON
            // normalizer and local cells for the DOM capture proof. Neither
            // status edges nor local state disappear in this census.
            accepted = domEntryHelpers.contains(op->getParentOfType<ctjs::FuncOp>());
            needsDOMMethodProof |= accepted;
        }
        if (auto fn = llvm::dyn_cast<ctjs::FuncOp>(op)) {
            auto & block = fn.getBody().front();
            const bool receiverUnused = unusedReceiver(fn);
            if (!reason.empty()) { return mlir::WalkResult::interrupt(); }
            accepted = methods.contains(fn) || helpers.contains(fn) ||
                       uncalledHelpers.contains(fn) ||
                       (llvm::hasSingleElement(fn.getBody()) &&
                        (constructors.contains(fn) || getters.contains(fn) ||
                         (((declaration && fn == entry) || block.getNumArguments() == 3) &&
                          receiverUnused && block.getArgument(ctjs::arg_new_target).use_empty())));
            accepted &= fn.getUpvalueCount() == 0 || methods.contains(fn) ||
                        llvm::any_of(capturedClosures, [&](ctjs::CreateClosureOp closure) {
                            return target(closure) == fn;
                        });
        }
        if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
            auto fn = target(closure);
            accepted =
                fn && fn != entry &&
                (closure.getUpvalues().empty() || llvm::is_contained(capturedClosures, closure)) &&
                (undefined(closure.getEnclosingThis()) || helpers.contains(fn));
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            accepted = load.getName() == host_detail::classDefinedIntrinsic ||
                       globalHolderLoads.contains(load) ||
                       static_cast<bool>(sourceClosure(load.getResult()));
            auto fn = op->getParentOfType<ctjs::FuncOp>();
            if (domEntry && (fn == entry || methods.contains(fn) || domEntryHelpers.contains(fn)) &&
                load.getName() != "Error" &&
                llvm::is_contained(contract.initialIntrinsics, load.getName())) {
                // Only the complete typed DOM proof can authorize these
                // identities and their uses, including unused method bodies.
                accepted = true;
                needsDOMMethodProof |= methods.contains(fn) || domEntryHelpers.contains(fn);
            }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            accepted = helperCalls.contains(call);
        }
        if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
            accepted = constructors.contains(
                target(sourceValue(made.getCallee()).getDefiningOp<ctjs::CreateClosureOp>()));
        }
        if (auto key = ctjs::propertyKey(op)) {
            auto constant = key.getDefiningOp<ctjs::ConstantOp>();
            // Literal Number keys cannot name a prototype hook or invoke
            // user coercion. Native field/index representation is separate.
            accepted = ctjs::ordinaryKey(key) ||
                       (helpers.contains(op->getParentOfType<ctjs::FuncOp>()) && constant &&
                        llvm::isa<ctjs::NumberAttr>(constant.getValue()));
            auto fn = op->getParentOfType<ctjs::FuncOp>();
            if (domEntry && (fn == entry || methods.contains(fn) || domEntryHelpers.contains(fn)) &&
                (!constant ||
                 (llvm::isa<ctjs::GetPropertyOp>(op) && ctjs::constantKey(key) == "toString"))) {
                // Keep dynamic reads/writes for the complete typed DOM
                // proof of their actual receiver, key and mutation order.
                // Class/holder identity uses were checked separately above;
                // unused methods still require the same private probes.
                accepted = true;
                needsDOMMethodProof |= methods.contains(fn) || domEntryHelpers.contains(fn);
            }
        }
        // Calls and iterable materialization defer to complete private DOM
        // probes, including the existing snapshot/iterator identity proof.
        // Exact entry-called helpers
        // survive into the final proof; other helpers keep the strict census.
        if (domEntry && llvm::isa<ctjs::CallOp, ctjs::IterableOp>(op)) {
            auto fn = op->getParentOfType<ctjs::FuncOp>();
            if (fn == entry || methods.contains(fn) || domEntryHelpers.contains(fn)) {
                accepted = true;
                needsDOMMethodProof |= methods.contains(fn) || domEntryHelpers.contains(fn);
            }
        }
        if (accepted) { return mlir::WalkResult::advance(); }
        auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
        refuse("class initialization source contains an unknown call, binding or reflective "
               "effect" +
               (load ? " (global \"" + load.getName().str() + "\")"
                     : " (op " + op->getName().getStringRef().str() + ")"));
        return mlir::WalkResult::interrupt();
    });
    if (walked.wasInterrupted()) { return false; }
    for (auto & [publication, holder] : globalHolders) {
        (void)holder;
        const auto name = llvm::cast<ctjs::StoreGlobalOp>(publication).getName();
        for (const auto & root : contract.roots) {
            if (!step()) { return false; }
            if (root.binding == name) {
                return refuse("global callable holder is requested by the host");
            }
        }
        for (const auto & observation : contract.observations) {
            if (!step()) { return false; }
            if (observation == name) {
                return refuse("global callable holder is requested by the host");
            }
        }
    }
    // Closure references and calls are covered above. Symbol attributes
    // must not retain a getter definition after all its reads are expanded.
    if (auto stop = host_detail::forEachSymbolUse(
            module, true, [&] { return step(); },
            [&](const mlir::SymbolTable::SymbolUse &, ctjs::FuncOp target) -> std::optional<bool> {
                if (!target) {
                    return refuse("class initialization has an unresolved symbol reference");
                }
                if (getters.contains(target)) {
                    return refuse("static getter has a remaining symbol reference");
                }
                return std::nullopt;
            })) {
        return *stop ? refuse("static getter symbol uses could not be enumerated") : false;
    }
    // Reject before normalization can replace a method's body and
    // invalidate a nested construction or definition recorded above.
    if (domEntry && needsDOMMethodProof) {
        for (auto [made, definition] : methodProbes) {
            if (!step()) { return false; }
            auto fn = target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
            if (!fn || made->getParentOfType<ctjs::FuncOp>() != entry) {
                return refuse("DOM class methods require entry-local instances");
            }
        }
    }
    return transportHelperMaps();
}

bool classInitialization::proveDOMMethods(const HostContract & contract) {
    if (!needsDOMMethodProof) { return true; }
    llvm::SetVector<mlir::Operation *> unused;
    for (auto [made, definition] : methodProbes) {
        (void)made;
        if (!step()) { return false; }
        bool readKey = false;
        const auto key = ctjs::constantKey(definition.getKey());
        const auto scanned = module.walk([&](ctjs::GetPropertyOp read) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            const auto selected = ctjs::constantKey(read.getKey());
            readKey |= selected.empty() || selected == key;
            return mlir::WalkResult::advance();
        });
        if (scanned.wasInterrupted()) { return false; }
        if (!readKey) { unused.insert(definition); }
    }
    const auto omitUnused = [&](mlir::ModuleOp candidate, mlir::IRMapping * mapping,
                                ctjs::SetPropertyOp keep) {
        for (mlir::Operation * operation : unused) {
            if (!step()) { return false; }
            if (operation == keep) { continue; }
            auto definition =
                llvm::cast<ctjs::SetPropertyOp>(mapping ? mapping->lookup(operation) : operation);
            auto closure = definition.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            auto original = target(llvm::cast<ctjs::SetPropertyOp>(operation)
                                       .getValue()
                                       .getDefiningOp<ctjs::CreateClosureOp>());
            auto fn = candidate.lookupSymbol<ctjs::FuncOp>(original.getSymName());
            if (!mlir::SymbolTable::symbolKnownUseEmpty(fn, candidate.getOperation()) ||
                !mlir::SymbolTable::symbolKnownUseEmpty(fn, &candidate.getBodyRegion())) {
                return refuse("unused DOM method retains a symbol reference");
            }
            definition.erase();
            for (mlir::Operation * root : llvm::make_early_inc_range(closure->getUsers())) {
                if (!step() || !llvm::isa<ctjs::RootOp>(root)) {
                    return refuse("unused DOM method retains a callable use");
                }
                root->erase();
            }
            closure.erase();
            fn.erase();
        }
        return true;
    };
    // Reachability only establishes which original bodies the shared proof
    // must cover. It grants no authority to their parameters: actual calls
    // and field state stay in source order in the private DOM proof below.
    llvm::DenseMap<mlir::Operation *, llvm::SetVector<mlir::Operation *>> originalCalls;
    const auto calledFromEntry = [&](ctjs::ConstructOp made, ctjs::FuncOp method) {
        auto [found, fresh] = originalCalls.try_emplace(made);
        auto & reached = found->second;
        if (fresh) {
            llvm::StringMap<ctjs::FuncOp> definitions;
            for (auto [instance, definition] : methodProbes) {
                if (!step()) { return false; }
                if (instance == made) {
                    definitions[ctjs::constantKey(definition.getKey())] =
                        target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
                }
            }
            const auto record = [&](ctjs::CallOp call, mlir::Value receiver) {
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!read || call.getReceiver() != receiver || read.getObject() != receiver) {
                    return;
                }
                if (auto fn = definitions.lookup(ctjs::constantKey(read.getKey()))) {
                    reached.insert(fn);
                }
            };
            for (mlir::Operation * user : made.getResult().getUsers()) {
                if (!step()) { return false; }
                auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                if (call && call->getBlock() == made->getBlock() && made->isBeforeInBlock(call)) {
                    record(call, made.getResult());
                }
            }
            // Construction is an original call too: its same-this method
            // calls retain their actual arguments and initialization order.
            auto constructor =
                target(sourceValue(made.getCallee()).getDefiningOp<ctjs::CreateClosureOp>());
            if (!constructor) { return refuse("DOM class construction lost its source body"); }
            reached.insert(constructor);
            // ponytail: only exact same-this edges; aliases need their own
            // receiver proof. A visited set bounds recursive source graphs.
            for (size_t i = 0; i < reached.size(); ++i) {
                auto fn = llvm::cast<ctjs::FuncOp>(reached[i]);
                auto receiver = fn.getBody().front().getArgument(ctjs::arg_receiver);
                auto walked = fn.walk([&](mlir::Operation * op) {
                    if (!step()) { return mlir::WalkResult::interrupt(); }
                    if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) { record(call, receiver); }
                    return mlir::WalkResult::advance();
                });
                if (walked.wasInterrupted()) { return false; }
            }
        }
        return reached.contains(method);
    };
    bool provedOriginalCalls = false;
    // Probe every method, including callers that overwrite a field before
    // dispatching to a DOM method; direct DOM calls alone are not a census.
    for (auto [made, definition] : methodProbes) {
        if (!step()) { return false; }
        auto fn = target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
        const bool hasParameters =
            fn.getBody().front().getNumArguments() != ctjs::implicit_arguments;
        const bool hasOriginalCall = calledFromEntry(made, fn);
        if (hasParameters && !hasOriginalCall) {
            return refuse("DOM class method parameters require original entry-call "
                          "reachability for each instance");
        }
        if (hasOriginalCall && provedOriginalCalls) { continue; }
        // Reserve the clone's operation/operand walk before allocating it.
        if (!spendWalk(module,
                       [](mlir::Operation * op) { return uint64_t(1) + op->getNumOperands(); })) {
            return false;
        }
        mlir::IRMapping mapping;
        mlir::OwningOpRef<mlir::ModuleOp> probe(llvm::cast<mlir::ModuleOp>(module->clone(mapping)));
        // Other uncalled methods have their own independent probes. Omit
        // them only from this proof copy so the existing lift sees exactly
        // the method under test and all original reachable method calls.
        if (!omitUnused(*probe, &mapping, definition)) { return false; }
        // Existing calls prove their complete bodies together. Only an
        // uncalled zero-argument method needs a synthetic invocation.
        if (!hasOriginalCall) {
            auto instance = llvm::cast<ctjs::ConstructOp>(mapping.lookup(made.getOperation()));
            mlir::OpBuilder at(instance);
            at.setInsertionPointAfter(instance);
            auto read = ctjs::GetPropertyOp::create(at, instance.getLoc(), instance.getType(),
                                                    instance.getResult(),
                                                    mapping.lookup(definition.getKey()));
            ctjs::CallOp::create(at, instance.getLoc(), instance.getType(), read.getResult(),
                                 instance.getResult(), mlir::ValueRange{});
        }
        if (auto error = liftDOMClasses(*probe, remaining)) {
            return refuse(llvm::toString(std::move(error)));
        }
        HostContract checked = contract;
        checked.moduleSha256 = hostContractFingerprint(*probe);
        // No class intrinsic remains, so this reuses normal DOM preparation
        // without recursing into class normalization. These calls exist only
        // in discarded proof copies; the emitted candidate is checked again.
        if (auto error = prepareDOMEntry(*probe, checked, remaining)) {
            return refuse("DOM class method body: " + llvm::toString(std::move(error)));
        }
        provedOriginalCalls |= hasOriginalCall;
    }
    // Only now have all unused bodies passed the same typed source proof.
    // The original no-read census makes removing their definitions inert.
    return omitUnused(module, nullptr, {});
}

bool classInitialization::normalizeMethods() {
    for (mlir::Operation * operation : dispatchMethods) {
        auto function = llvm::cast<ctjs::FuncOp>(operation);
        // Charge the private copy before allocating it. All source effect
        // and receiver checks have finished; only structural transport is
        // normalized, with the existing bounded exception machinery.
        if (!spendWalk(function, cloneCost)) { return false; }
        mlir::IRMapping mapping;
        mlir::OwningOpRef<ctjs::FuncOp> copy(llvm::cast<ctjs::FuncOp>(function->clone(mapping)));
        if (auto failed = lowering_detail::normalizeStructuredExits(*copy, remaining)) {
            auto message = llvm::toString(std::move(failed));
            return refuse(message == "native exception recovery work budget exhausted"
                              ? "class initialization work budget exhausted"
                              : message);
        }
        // Exit normalization moves branch bodies without replacing their
        // property reads. Expansion must follow the private copy too.
        uint64_t cost = getterReads.size() + constructorReads.size() + staticMethodReads.size() +
                        cellReads.size() + cells.size() + inertReceiverClosures.size() +
                        captureReads.size();
        for (auto & item : globalHolders) {
            cost += item.second.reads.size() + item.second.loads.size();
        }
        for (auto & item : localHolders) { cost += item.second.reads.size(); }
        if (!spend(cost)) { return false; }
        const auto remap = [&](auto & op) {
            if (auto * mapped = mapping.lookupOrNull(op.getOperation())) {
                op = llvm::cast<std::remove_reference_t<decltype(op)>>(mapped);
            }
        };
        for (auto & item : getterReads) { remap(item.first); }
        for (auto & read : constructorReads) { remap(read); }
        for (auto & item : staticMethodReads) { remap(item.first); }
        for (auto & read : cellReads) { remap(read); }
        llvm::MapVector<mlir::Value, mlir::Value> mappedCells;
        for (auto [cell, value] : cells) {
            mappedCells[mapping.lookupOrDefault(cell)] = mapping.lookupOrDefault(value);
        }
        cells.swap(mappedCells);
        for (auto & closure : inertReceiverClosures) { remap(closure); }
        for (ctjs::LoadUpvalueOp & read : captureReads) {
            if (auto * mapped = mapping.lookupOrNull(read.getOperation())) {
                if (auto helper = callableCaptures.lookup(read)) {
                    callableCaptures.erase(read);
                    callableCaptures[mapped] = helper;
                }
                if (auto object = holderCaptures.lookup(read.getResult())) {
                    holderCaptures.erase(read.getResult());
                    holderCaptures[mapped->getResult(0)] = object;
                }
                read = llvm::cast<ctjs::LoadUpvalueOp>(mapped);
            }
        }
        for (auto & item : globalHolders) {
            for (auto & read : item.second.reads) { remap(read.first); }
            for (auto & load : item.second.loads) { remap(load); }
        }
        for (auto & item : localHolders) {
            for (auto & read : item.second.reads) { remap(read.first); }
        }
        normalizedMethods.emplace_back(function, std::move(copy));
    }
    return true;
}

} // namespace ctcompile::ctnative::class_detail
