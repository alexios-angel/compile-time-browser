#include "DOMSource/Proof.hpp"

namespace ctcompile::ctnative {

llvm::Error normalizeDOMCustomIteration(mlir::ModuleOp candidate, const HostContract & contract,
                                        unsigned maxSteps) {
    dom_source_detail::DOMSource work(maxSteps);
    const auto spend = [&] { return work.step(); };
    const auto error = [&](llvm::StringRef reason) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       work.remaining ? reason
                                                      : "DOM custom iterator budget exhausted");
    };
    const auto helper = [](ctjs::CallOp call) -> llvm::StringRef {
        auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
        return load ? load.getName() : llvm::StringRef{};
    };
    const auto undefined = dom_source_detail::DOMSource::undefined;
    bool custom = false;
    const auto census = candidate.walk([&](mlir::Operation * operation) {
        for (unsigned i = 0; i <= operation->getNumOperands(); ++i) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
            call && helper(call) == "__ctbrowser_for_of_open" && call.getArgs().size() == 1) {
            custom |= static_cast<bool>(call.getArgs()[0].getDefiningOp<ctjs::CreateObjectOp>());
        }
        return mlir::WalkResult::advance();
    });
    if (census.wasInterrupted()) { return error("DOM custom iterator budget exhausted"); }
    if (!custom) { return llvm::Error::success(); }
    if (candidate->hasAttr("ctjs.skipped") ||
        hostContractFingerprint(candidate) != contract.moduleSha256) {
        return error("DOM custom iterator fingerprint mismatch or incomplete source");
    }
    for (llvm::StringRef name : {"Object", "Symbol", "__ctbrowser_for_of_open",
                                 "__ctbrowser_iter_next", "__ctbrowser_iter_close"}) {
        if (!spend() || !llvm::is_contained(contract.initialIntrinsics, name)) {
            return error(
                "DOM custom iterator requires original Object, Symbol and helper identities");
        }
    }
    auto entry = candidate.lookupSymbol<ctjs::FuncOp>(contract.entry);
    if (!entry || !entry.getBody().hasOneBlock()) {
        return error("DOM custom iterator requires one complete entry");
    }
    // Source returns/throws need an independent IteratorClose handler proof.
    unsigned returns = 0;
    const auto completion = entry.walk([&](mlir::Operation * operation) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        if (llvm::isa<ctjs::ReturnOp>(operation)) {
            ++returns;
            if (operation->getBlock() != &entry.getBody().front()) {
                return mlir::WalkResult::interrupt();
            }
        }
        if (llvm::isa<ctjs::ThrowOp, ctjs::PushHandlerOp, ctjs::InvokeOp>(operation)) {
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    if (completion.wasInterrupted() || returns != 1) {
        return error("DOM custom iterator abrupt completion needs a handler proof");
    }
    ctjs::CallOp open;
    const auto opens = entry.walk([&](ctjs::CallOp call) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        if (helper(call) != "__ctbrowser_for_of_open" || call.getArgs().size() != 1 ||
            !call.getArgs()[0].getDefiningOp<ctjs::CreateObjectOp>()) {
            return mlir::WalkResult::advance();
        }
        if (open) { return mlir::WalkResult::interrupt(); }
        open = call;
        return mlir::WalkResult::advance();
    });
    // ponytail: one root-local custom iterator; use a state worklist when a
    // second independent protocol must be admitted in the same entry.
    if (opens.wasInterrupted() || !open || !undefined(open.getReceiver()) ||
        open->getBlock() != &entry.getBody().front()) {
        return error("DOM custom iterator requires one root-local open");
    }
    auto object = open.getArgs()[0].getDefiningOp<ctjs::CreateObjectOp>();
    if (object->getBlock() != open->getBlock() || !object->isBeforeInBlock(open)) {
        return error("DOM custom iterator holder lacks preceding local allocation");
    }
    llvm::StringMap<ctjs::SetPropertyOp> slots;
    llvm::SmallVector<ctjs::SetPropertyOp> stateSlots;
    llvm::StringMap<unsigned> stateIndices;
    ctjs::SetPropertyOp iteratorSlot;
    for (mlir::Operation * user : object->getUsers()) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
        if (!store) { continue; }
        auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        if (store.getObject() != object.getResult() || store->getBlock() != object->getBlock() ||
            !object->isBeforeInBlock(store) || !store->isBeforeInBlock(open)) {
            return error("DOM custom iterator slots must be unique unconditional callables");
        }
        if (!closure) {
            auto constant = store.getValue().getDefiningOp<ctjs::ConstantOp>();
            auto name = ctjs::constantKey(store.getKey());
            if (!constant || !llvm::isa<ctjs::NumberAttr>(constant.getValue()) ||
                !ctjs::ordinaryKey(name) || name == "next" || name == "return" ||
                !stateIndices.try_emplace(name, 0).second) {
                return error("DOM custom iterator state requires unique own Number initializers");
            }
            continue;
        }
        if (closure->getBlock() != object->getBlock() || !closure->isBeforeInBlock(store)) {
            return error("DOM custom iterator callable lacks source order");
        }
        if (auto name = ctjs::constantKey(store.getKey()); !name.empty()) {
            if ((name != "next" && name != "return") || !slots.try_emplace(name, store).second) {
                return error("DOM custom iterator has replaced or unsupported own slots");
            }
        } else {
            auto read = store.getKey().getDefiningOp<ctjs::GetPropertyOp>();
            auto symbol =
                read ? read.getObject().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
            if (iteratorSlot || !symbol || symbol.getName() != "Symbol" ||
                ctjs::constantKey(read.getKey()) != "iterator") {
                return error("DOM custom iterator requires its original Symbol.iterator slot");
            }
            iteratorSlot = store;
        }
    }
    // State positions follow source order, independent of SSA use-list ordering.
    for (auto store : object->getBlock()->getOps<ctjs::SetPropertyOp>()) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        if (store.getObject() != object.getResult()) { continue; }
        auto found = stateIndices.find(ctjs::constantKey(store.getKey()));
        if (found == stateIndices.end()) { continue; }
        found->second = static_cast<unsigned>(stateSlots.size());
        stateSlots.push_back(store);
    }
    if (!iteratorSlot || !slots.contains("next")) {
        return error("DOM custom iterator lacks its own iterator and next methods");
    }
    const auto targetOf = [&](ctjs::CreateClosureOp closure) {
        ctjs::FuncOp found;
        for (auto function : candidate.getOps<ctjs::FuncOp>()) {
            if (!spend()) { return ctjs::FuncOp{}; }
            if (functionIndex(function) == static_cast<unsigned>(closure.getFunction())) {
                if (found) { return ctjs::FuncOp{}; }
                found = function;
            }
        }
        return found;
    };
    auto identity = iteratorSlot.getValue().getDefiningOp<ctjs::CreateClosureOp>();
    auto identityBody = targetOf(identity);
    if (!identityBody || identityBody->hasAttr("ctjs.skipped") ||
        !undefined(identity.getEnclosingThis()) ||
        identity.getEnclosingClosure() != entry.getBody().front().getArgument(ctjs::arg_callee) ||
        !identity.getUpvalues().empty() || identityBody.getUpvalueCount() ||
        !identityBody.getBody().hasOneBlock() ||
        identityBody.getBody().front().getNumArguments() != ctjs::implicit_arguments) {
        return error("DOM Symbol.iterator must be a closed identity method");
    }
    if (!work.checkBody(identityBody, false, true)) { return error(work.reason); }
    unsigned identityReturns = 0, creations = 0;
    for (mlir::Operation & operation : identityBody.getBody().front()) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
            ++identityReturns;
            if (returned.getValue() !=
                identityBody.getBody().front().getArgument(ctjs::arg_receiver)) {
                return error("DOM Symbol.iterator must return its receiver");
            }
        } else if (!llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                              ctjs::ConstantOp>(operation)) {
            return error("DOM Symbol.iterator identity method has additional effects");
        }
    }
    const auto identities = candidate.walk([&](ctjs::CreateClosureOp closure) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        creations += closure.getFunction() == identity.getFunction();
        return mlir::WalkResult::advance();
    });
    if (identities.wasInterrupted() || identityReturns != 1 || creations != 1 ||
        !mlir::SymbolTable::symbolKnownUseEmpty(identityBody, candidate.getOperation()) ||
        !mlir::SymbolTable::symbolKnownUseEmpty(identityBody, &candidate.getBodyRegion())) {
        return error("DOM iterator identity method is shared or symbolically observed");
    }
    for (mlir::Operation * user : identity->getUsers()) {
        if (!spend() || (user != iteratorSlot && !llvm::isa<ctjs::RootOp>(user))) {
            return error("DOM iterator identity method escapes its own slot");
        }
    }
    // Mutable captures join the existing receiver-state tuple only after a
    // complete local-cell census. Generic helper captures remain immutable.
    llvm::DenseMap<mlir::Value, unsigned> capturedState;
    llvm::DenseSet<mlir::Operation *> stateStorage;
    llvm::SmallVector<mlir::Value> stateInitials;
    for (auto field : stateSlots) { stateInitials.push_back(field.getValue()); }
    const auto structuredStateAccess = [&](mlir::Operation * operation, ctjs::FuncOp body) {
        unsigned depth = 0;
        while (operation->getBlock() != &body.getBody().front()) {
            if (!spend() || ++depth == 64) { return false; }
            operation = operation->getParentOp();
            if (!llvm::isa_and_nonnull<mlir::scf::IfOp, mlir::scf::WhileOp>(operation)) {
                return false;
            }
        }
        return true;
    };
    for (auto & [name, slot] : slots) {
        (void)name;
        auto closure = slot.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        auto body = targetOf(closure);
        auto indices = closure.getEnclosingIndicesAttr();
        if (body == entry) { return error("DOM iterator method must not alias the entry"); }
        if (!body || body->hasAttr("ctjs.skipped") || !body.getBody().hasOneBlock() ||
            body.getBody().front().getNumArguments() != ctjs::implicit_arguments ||
            closure.getUpvalues().size() != body.getUpvalueCount() ||
            closure.getEnclosingClosure() !=
                entry.getBody().front().getArgument(ctjs::arg_callee) ||
            (indices && (indices.size() != closure.getUpvalues().size() ||
                         llvm::any_of(indices.asArrayRef(), [](int32_t i) { return i != -1; })))) {
            return error("DOM iterator capture requires exact local slots");
        }
        // Resolve proved break dispatch before checking state access ancestry.
        // The complete continuation proof retains every source effect.
        if (!work.normalizeCompletion(body)) { return error(work.reason); }
        const auto stores = body.walk([&](ctjs::StoreUpvalueOp store) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            if (store.getClosure() != body.getBody().front().getArgument(ctjs::arg_callee) ||
                store.getIndex() < 0 || store.getIndex() >= body.getUpvalueCount() ||
                !structuredStateAccess(store, body)) {
                return mlir::WalkResult::interrupt();
            }
            auto cell = closure.getUpvalues()[static_cast<unsigned>(store.getIndex())]
                            .getDefiningOp<ctjs::CreateCellOp>();
            if (!cell || cell->getBlock() != open->getBlock()) {
                return mlir::WalkResult::interrupt();
            }
            capturedState.try_emplace(cell.getResult(), 0);
            return mlir::WalkResult::advance();
        });
        if (stores.wasInterrupted()) {
            return error("DOM iterator mutation requires direct local capture stores");
        }
    }
    for (auto cell : entry.getBody().front().getOps<ctjs::CreateCellOp>()) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        auto found = capturedState.find(cell.getResult());
        if (found == capturedState.end()) { continue; }
        ctjs::CellSetOp initializer;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            auto * user = use.getOwner();
            if (user->getBlock() != cell->getBlock() || !cell->isBeforeInBlock(user)) {
                return error("DOM iterator capture cell escapes its local initialization");
            }
            if (llvm::isa<ctjs::RootOp>(user) && use.getOperandNumber() == 1) { continue; }
            if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(user);
                write && use.getOperandNumber() == 0 && !initializer) {
                initializer = write;
                continue;
            }
            auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
            bool method = false;
            for (auto & [name, slot] : slots) {
                (void)name;
                if (!spend()) { return error("DOM custom iterator budget exhausted"); }
                method |= closure && slot.getValue() == closure.getResult();
            }
            if (!method || use.getOperandNumber() < 2) {
                return error("DOM iterator capture cell has an external reader or writer");
            }
        }
        auto initial = (initializer ? initializer.getValue() : cell.getInitial())
                           .getDefiningOp<ctjs::ConstantOp>();
        auto original = cell.getInitial().getDefiningOp<ctjs::ConstantOp>();
        if (!initial || !llvm::isa<ctjs::NumberAttr>(initial.getValue()) || !original ||
            !llvm::isa<ctjs::NumberAttr, ctjs::UndefinedAttr>(original.getValue()) ||
            (initializer && !initializer->isBeforeInBlock(open))) {
            return error("DOM iterator capture requires one literal Number initialization");
        }
        for (auto * user : cell->getUsers()) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            if (initializer && llvm::isa<ctjs::CreateClosureOp>(user) &&
                !initializer->isBeforeInBlock(user)) {
                return error("DOM iterator capture initialization must precede its methods");
            }
        }
        found->second = static_cast<unsigned>(stateInitials.size());
        stateInitials.push_back(initial.getResult());
        stateStorage.insert(cell);
        if (initializer) { stateStorage.insert(initializer); }
    }
    // Projecting these records cannot invoke getters, consult a prototype or
    // lose evaluation of a field producer. Complete DOM proof checks values.
    for (auto & [name, store] : slots) {
        auto body = targetOf(store.getValue().getDefiningOp<ctjs::CreateClosureOp>());
        if (!body || !body.getBody().hasOneBlock() ||
            body.getBody().front().getNumArguments() != ctjs::implicit_arguments) {
            return error("DOM iterator methods require complete zero-argument bodies");
        }
        auto & block = body.getBody().front();
        for (auto argument : block.getArguments().take_front(ctjs::arg_callee)) {
            for (mlir::Operation * user : argument.getUsers()) {
                if (!spend()) { return error("DOM custom iterator budget exhausted"); }
                if (llvm::isa<ctjs::RootOp>(user)) { continue; }
                auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                auto key = get   ? ctjs::constantKey(get.getKey())
                           : set ? ctjs::constantKey(set.getKey())
                                 : llvm::StringRef{};
                if (argument.getArgNumber() != ctjs::arg_receiver ||
                    !structuredStateAccess(user, body) || !stateIndices.contains(key) ||
                    (get ? get.getObject() != argument
                         : !set || set.getObject() != argument || set.getValue() == argument)) {
                    return error("DOM iterator receiver requires direct own state access");
                }
            }
        }
        auto returned = llvm::dyn_cast<ctjs::ReturnOp>(block.back());
        auto record = returned ? returned.getValue().getDefiningOp<ctjs::CreateObjectOp>()
                               : ctjs::CreateObjectOp{};
        if (!record || record->getBlock() != &block) {
            return error("DOM iterator method must return one fresh own-field record");
        }
        llvm::StringSet<> fields;
        for (mlir::Operation * user : record->getUsers()) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            if (user == returned || llvm::isa<ctjs::RootOp>(user)) { continue; }
            auto field = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            auto key = field ? ctjs::constantKey(field.getKey()) : llvm::StringRef{};
            if (!field || field.getObject() != record.getResult() || field->getBlock() != &block ||
                !record->isBeforeInBlock(field) || !field->isBeforeInBlock(returned) ||
                name != "next" || (key != "done" && key != "value") || !fields.insert(key).second) {
                return error("DOM iterator result fields are incomplete, replaced or escaped");
            }
        }
        if (name == "next" && (!fields.contains("done") || !fields.contains("value"))) {
            return error("DOM next result requires own done and value fields");
        }
        if (!stateInitials.empty()) {
            auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            unsigned count = 0;
            const auto unique = candidate.walk([&](ctjs::CreateClosureOp other) {
                if (!spend()) { return mlir::WalkResult::interrupt(); }
                count += other.getFunction() == closure.getFunction();
                return mlir::WalkResult::advance();
            });
            if (unique.wasInterrupted() || count != 1 || !undefined(closure.getEnclosingThis()) ||
                !mlir::SymbolTable::symbolKnownUseEmpty(body, candidate.getOperation()) ||
                !mlir::SymbolTable::symbolKnownUseEmpty(body, &candidate.getBodyRegion())) {
                return error("DOM iterator state method is shared or symbolically observed");
            }
            for (mlir::Operation * user : closure->getUsers()) {
                if (!spend() || (user != store && !llvm::isa<ctjs::RootOp>(user))) {
                    return error("DOM iterator state method escapes its own slot");
                }
            }
            if (!work.checkBody(body, false, true, true)) { return error(work.reason); }
            if (!capturedState.empty()) {
                const auto nested = body.walk([&](ctjs::CreateClosureOp) {
                    spend();
                    return mlir::WalkResult::interrupt();
                });
                if (nested.wasInterrupted()) {
                    return error("DOM iterator mutable capture requires leaf methods");
                }
            }
            const auto reads = body.walk([&](ctjs::LoadUpvalueOp read) {
                if (!spend()) { return mlir::WalkResult::interrupt(); }
                auto cell = closure.getUpvalues()[static_cast<unsigned>(read.getIndex())];
                if (capturedState.contains(cell) && !structuredStateAccess(read, body)) {
                    return mlir::WalkResult::interrupt();
                }
                return mlir::WalkResult::advance();
            });
            if (reads.wasInterrupted()) {
                return error("DOM iterator capture requires direct state reads");
            }
        }
    }

    ctjs::CallOp next, close;
    for (mlir::Operation * user : open->getUsers()) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        if (llvm::isa<ctjs::RootOp, mlir::scf::YieldOp>(user)) { continue; }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(user);
            compare && compare.getKind() == ctjs::CompareKind::StrictEq &&
            ((compare.getLhs() == open.getResult() && undefined(compare.getRhs())) ||
             (compare.getRhs() == open.getResult() && undefined(compare.getLhs())))) {
            continue;
        }
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
            get && get.getObject() == open.getResult() &&
            ctjs::constantKey(get.getKey()) == "done") {
            // The runtime coerces result.done to Boolean. Only truth tests may
            // observe our projected field without materializing that Boolean.
            for (mlir::Operation * observer : get->getUsers()) {
                if (!spend() || !llvm::isa<ctjs::RootOp, ctjs::TruthyOp>(observer)) {
                    return error("DOM iterator done field requires truth-only observations");
                }
            }
            continue;
        }
        auto call = llvm::dyn_cast<ctjs::CallOp>(user);
        if (!call || !undefined(call.getReceiver()) || call.getArgs().empty() ||
            call.getArgs()[0] != open.getResult()) {
            return error("DOM iterator record has an unsupported observer");
        }
        if (helper(call) == "__ctbrowser_iter_next" && call.getArgs().size() == 1 && !next) {
            next = call;
        } else if (helper(call) == "__ctbrowser_iter_close" && call.getArgs().size() == 2 &&
                   !close) {
            auto flag = call.getArgs()[1].getDefiningOp<ctjs::ConstantOp>();
            auto boolean =
                flag ? llvm::dyn_cast<ctjs::BooleanAttr>(flag.getValue()) : ctjs::BooleanAttr{};
            if (!boolean || boolean.getValue()) {
                return error("DOM iterator close requires normal completion");
            }
            close = call;
        } else {
            return error("DOM iterator protocol requires one next and close site");
        }
    }
    if (!next || !close || close->getBlock() != open->getBlock() || !open->isBeforeInBlock(close)) {
        return error("DOM iterator close must follow its complete traversal");
    }
    auto sourceLoop = next->getParentOfType<mlir::scf::WhileOp>();
    if (!sourceLoop || sourceLoop->getBlock() != open->getBlock() ||
        !sourceLoop.getBefore().isAncestor(next->getParentRegion()) ||
        !open->isBeforeInBlock(sourceLoop) || !sourceLoop->isBeforeInBlock(close)) {
        return error("DOM custom next requires one direct loop test");
    }
    for (mlir::Operation * user : next->getUsers()) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        if (llvm::isa<ctjs::RootOp>(user)) { continue; }
        bool guarded = false;
        for (auto * parent = user->getParentOp(); parent; parent = parent->getParentOp()) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(parent);
            auto truth =
                branch ? branch.getCondition().getDefiningOp<ctjs::TruthyOp>() : ctjs::TruthyOp{};
            auto read = truth ? truth.getValue().getDefiningOp<ctjs::GetPropertyOp>()
                              : ctjs::GetPropertyOp{};
            guarded |= read && read.getObject() == open.getResult() &&
                       ctjs::constantKey(read.getKey()) == "done" &&
                       read->getBlock() == next->getBlock() && next->isBeforeInBlock(read) &&
                       branch.getElseRegion().isAncestor(user->getParentRegion());
        }
        if (!guarded) { return error("DOM iterator item requires its not-done continuation"); }
    }

    if (!stateInitials.empty()) {
        for (mlir::Operation * user : object->getUsers()) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            if (user != open &&
                !llvm::isa<ctjs::RootOp, ctjs::SetPropertyOp, ctjs::IterableOp, mlir::scf::YieldOp>(
                    user)) {
                return error("DOM iterator state holder escapes its protocol");
            }
        }
    }
    // These bodies are private to their own method slots. Bind fields and cells
    // as scalar arguments/results; ordinary helper expansion later removes the
    // fresh result records. All source value producers remain for DOM reproof.
    for (auto & [name, store] : slots) {
        (void)name;
        if (stateInitials.empty()) { break; }
        auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        auto body = targetOf(closure);
        auto & block = body.getBody().front();
        llvm::SmallVector<mlir::Value> current;
        for (auto initial : stateInitials) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            current.push_back(block.addArgument(initial.getType(), initial.getLoc()));
        }
        body.setFunctionTypeAttr(mlir::TypeAttr::get(
            mlir::FunctionType::get(candidate.getContext(), block.getArgumentTypes(),
                                    body.getFunctionType().getResults())));
        const auto scalarize = [&](auto && self, mlir::Block & source,
                                   llvm::SmallVector<mlir::Value> & current,
                                   unsigned depth) -> bool {
            if (depth == 64) { return false; }
            for (mlir::Operation & operation : llvm::make_early_inc_range(source)) {
                if (!spend()) { return false; }
                if (llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(operation)) {
                    const bool loop = llvm::isa<mlir::scf::WhileOp>(operation);
                    mlir::OperationState built(operation.getLoc(), operation.getName());
                    built.addOperands(operation.getOperands());
                    if (loop) { built.addOperands(current); }
                    built.addTypes(operation.getResultTypes());
                    for (auto value : current) {
                        if (!spend()) { return false; }
                        built.addTypes(value.getType());
                    }
                    built.addAttributes(operation.getAttrs());
                    for (auto & region : operation.getRegions()) {
                        built.addRegion()->takeBody(region);
                    }
                    mlir::OpBuilder at(&operation);
                    auto * joined = at.create(built);
                    for (auto & region : joined->getRegions()) {
                        for (auto value : current) {
                            (void)value;
                            if (!spend()) { return false; }
                        }
                        auto armState = current;
                        if (region.empty()) {
                            auto & arm = region.emplaceBlock();
                            mlir::OpBuilder inside(&arm, arm.end());
                            mlir::scf::YieldOp::create(inside, operation.getLoc(), armState);
                        } else {
                            // Both loop regions receive the preceding edge's state.
                            // The condition forwards its latest values on exit too.
                            if (loop) {
                                for (auto & value : armState) {
                                    value = region.front().addArgument(value.getType(),
                                                                       operation.getLoc());
                                }
                            }
                            if (!self(self, region.front(), armState, depth + 1)) { return false; }
                            auto * yield = region.front().getTerminator();
                            yield->insertOperands(yield->getNumOperands(), armState);
                        }
                    }
                    operation.replaceAllUsesWith(
                        joined->getResults().take_front(operation.getNumResults()));
                    llvm::copy(joined->getResults().take_back(current.size()), current.begin());
                    operation.erase();
                    continue;
                }
                auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(operation);
                auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation);
                auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(operation);
                auto receiver = block.getArgument(ctjs::arg_receiver);
                if (get && get.getObject() == receiver) {
                    get.getResult().replaceAllUsesWith(
                        current[stateIndices.lookup(ctjs::constantKey(get.getKey()))]);
                    get.erase();
                } else if (set && set.getObject() == receiver) {
                    current[stateIndices.lookup(ctjs::constantKey(set.getKey()))] = set.getValue();
                    set.erase();
                } else if (load || write) {
                    auto index = static_cast<unsigned>(load ? load.getIndex() : write.getIndex());
                    auto found = capturedState.find(closure.getUpvalues()[index]);
                    if (found == capturedState.end()) { continue; }
                    if (load) {
                        load.getResult().replaceAllUsesWith(current[found->second]);
                        load.erase();
                    } else {
                        current[found->second] = write.getValue();
                        write.erase();
                    }
                }
            }
            return true;
        };
        if (!scalarize(scalarize, block, current, 0)) {
            return error("DOM custom iterator state region budget or depth exhausted");
        }
        llvm::SmallVector<mlir::Value> retained;
        llvm::SmallVector<unsigned> indices;
        for (auto cell : closure.getUpvalues()) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            indices.push_back(static_cast<unsigned>(retained.size()));
            if (!capturedState.contains(cell)) { retained.push_back(cell); }
        }
        const auto reindexed = body.walk([&](ctjs::LoadUpvalueOp read) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            read.setIndex(indices[static_cast<unsigned>(read.getIndex())]);
            return mlir::WalkResult::advance();
        });
        if (reindexed.wasInterrupted()) { return error("DOM custom iterator budget exhausted"); }
        closure.getUpvaluesMutable().assign(retained);
        closure.removeEnclosingIndicesAttr();
        body.setUpvalueCount(static_cast<uint32_t>(retained.size()));
        auto returned = llvm::cast<ctjs::ReturnOp>(block.back());
        mlir::OpBuilder at(returned);
        for (auto [index, value] : llvm::enumerate(current)) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            auto key = ctjs::ConstantOp::create(
                at, returned.getLoc(),
                ctjs::StringAttr::get(candidate.getContext(),
                                      "__ctcompile_state_" + std::to_string(index)));
            ctjs::SetPropertyOp::create(at, returned.getLoc(), returned.getValue(), key, value);
        }
    }

    mlir::Region rewritten;
    auto & destination = rewritten.emplaceBlock();
    mlir::IRMapping mapping;
    for (auto argument : entry.getBody().front().getArguments()) {
        mapping.map(argument, destination.addArgument(argument.getType(), argument.getLoc()));
    }
    mlir::OpBuilder start(candidate.getContext());
    start.setInsertionPointToEnd(&destination);
    const auto type = open.getType();
    const auto where = open.getLoc();
    auto initial = ctjs::ConstantOp::create(start, where,
                                            ctjs::BooleanAttr::get(candidate.getContext(), false));
    auto marker = mlir::ub::PoisonOp::create(start, where, type);
    llvm::SmallVector<mlir::Value> state{initial};
    for (auto value : stateInitials) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        state.push_back(ctjs::ConstantOp::create(start, where, constant.getValue()));
    }
    ctjs::CallOp emittedNext;
    mlir::Value emittedDone;
    const auto methodCall = [&](mlir::OpBuilder & at, llvm::StringRef name,
                                mlir::IRMapping & values, llvm::SmallVector<mlir::Value> & state) {
        auto key = ctjs::ConstantOp::create(at, where,
                                            ctjs::StringAttr::get(candidate.getContext(), name));
        auto holder = values.lookup(object.getResult());
        auto method = ctjs::GetPropertyOp::create(at, where, type, holder, key);
        auto called = ctjs::CallOp::create(at, where, type, method, holder,
                                           mlir::ValueRange(state).drop_front());
        for (unsigned index = 0; index < stateInitials.size(); ++index) {
            if (!spend()) { return ctjs::CallOp{}; }
            auto field = ctjs::ConstantOp::create(
                at, where,
                ctjs::StringAttr::get(candidate.getContext(),
                                      "__ctcompile_state_" + std::to_string(index)));
            state[index + 1] = ctjs::GetPropertyOp::create(at, where, type, called, field);
        }
        return called;
    };
    const auto clone = [&](auto && self, mlir::Block & source, mlir::OpBuilder & at,
                           mlir::IRMapping & values, llvm::SmallVector<mlir::Value> & state,
                           unsigned depth, bool appendState) -> bool {
        if (depth == 64) { return false; }
        for (mlir::Operation & operation : source) {
            for (unsigned i = 0; i <= operation.getNumOperands() + state.size(); ++i) {
                if (!spend()) { return false; }
            }
            if (&operation == iteratorSlot || &operation == identity ||
                stateStorage.contains(&operation) || llvm::is_contained(stateSlots, &operation)) {
                continue;
            }
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation);
                root &&
                (root.getValue() == identity.getResult() || root.getValue() == open.getResult() ||
                 capturedState.contains(root.getValue()))) {
                continue;
            }
            if (&operation == open) {
                values.map(open.getResult(), marker.getResult());
                state.front() = initial;
                continue;
            }
            if (&operation == next) {
                emittedNext = methodCall(at, "next", values, state);
                if (!emittedNext) { return false; }
                auto key = ctjs::ConstantOp::create(
                    at, where, ctjs::StringAttr::get(candidate.getContext(), "done"));
                state.front() = ctjs::GetPropertyOp::create(at, where, type, emittedNext, key);
                emittedDone = state.front();
                key = ctjs::ConstantOp::create(
                    at, where, ctjs::StringAttr::get(candidate.getContext(), "value"));
                auto item = ctjs::GetPropertyOp::create(at, where, type, emittedNext, key);
                values.map(next.getResult(), item.getResult());
                continue;
            }
            if (&operation == close) {
                auto test = ctjs::TruthyOp::create(at, where, at.getI1Type(), state.front());
                state.front() = ctjs::ConstantOp::create(
                    at, where, ctjs::BooleanAttr::get(candidate.getContext(), true));
                if (slots.contains("return")) {
                    auto branch = mlir::scf::IfOp::create(at, where, mlir::TypeRange{}, test, true);
                    for (auto & region : branch->getRegions()) {
                        auto & block = region.front();
                        mlir::OpBuilder inside(&block, block.begin());
                        if (&region == &branch.getElseRegion()) {
                            for (auto value : state) {
                                (void)value;
                                if (!spend()) { return false; }
                            }
                            auto closingState = state;
                            if (!methodCall(inside, "return", values, closingState)) {
                                return false;
                            }
                        }
                    }
                }
                auto empty = ctjs::ConstantOp::create(
                    at, where, ctjs::UndefinedAttr::get(candidate.getContext()));
                values.map(close.getResult(), empty.getResult());
                continue;
            }
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                get && get.getObject() == open.getResult()) {
                values.map(get.getResult(), state.front());
                continue;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
                compare &&
                (compare.getLhs() == open.getResult() || compare.getRhs() == open.getResult())) {
                auto flag = ctjs::ConstantOp::create(
                    at, where, ctjs::BooleanAttr::get(candidate.getContext(), false));
                values.map(compare.getResult(), flag.getResult());
                continue;
            }
            if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
                auto constant =
                    values.lookup(truth.getValue()).template getDefiningOp<ctjs::ConstantOp>();
                auto flag = constant ? llvm::dyn_cast<ctjs::BooleanAttr>(constant.getValue())
                                     : ctjs::BooleanAttr{};
                if (flag) {
                    auto bit = mlir::arith::ConstantIntOp::create(at, where, flag.getValue(), 1);
                    values.map(truth.getResult(), bit.getResult());
                    continue;
                }
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                auto constant = values.lookup(branch.getCondition())
                                    .template getDefiningOp<mlir::arith::ConstantOp>();
                auto bit = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                    : mlir::IntegerAttr{};
                if (bit && bit.getType().isInteger(1)) {
                    auto & region = bit.getInt() ? branch.getThenRegion() : branch.getElseRegion();
                    if (!region.hasOneBlock() ||
                        !self(self, region.front(), at, values, state, depth + 1, false)) {
                        return false;
                    }
                    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().back());
                    if (!yield || yield.getNumOperands() != branch.getNumResults()) {
                        return false;
                    }
                    for (auto [result, operand] :
                         llvm::zip(branch.getResults(), yield.getOperands())) {
                        values.map(result, values.lookup(operand));
                    }
                    continue;
                }
            }
            if (auto dispatch = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(operation)) {
                bool projection = true;
                for (auto & region : dispatch->getRegions()) {
                    if (!spend()) { return false; }
                    if (!region.hasOneBlock() || region.front().getNumArguments() ||
                        !llvm::hasSingleElement(region.front())) {
                        projection = false;
                        continue;
                    }
                    for (mlir::Operation & nested : region.front()) {
                        if (!spend()) { return false; }
                        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(nested);
                        projection &= yield && yield.getOperandTypes() == dispatch.getResultTypes();
                        for (mlir::Value operand : nested.getOperands()) {
                            if (!spend()) { return false; }
                            projection &= values.contains(operand);
                        }
                    }
                }
                if (projection) {
                    // A pure selection cannot change the current done state.
                    // Keep its source tuple for the loop-exit continuation proof.
                    if (dispatch.getNumResults()) { at.clone(operation, values); }
                    continue;
                }
            }
            if (llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::IndexSwitchOp>(
                    operation)) {
                const bool loop = llvm::isa<mlir::scf::WhileOp>(operation);
                mlir::OperationState built(operation.getLoc(), operation.getName());
                for (auto operand : operation.getOperands()) {
                    built.addOperands(values.lookup(operand));
                }
                if (loop) { built.addOperands(state); }
                built.addTypes(operation.getResultTypes());
                for (auto value : state) {
                    if (!spend()) { return false; }
                    built.addTypes(value.getType());
                }
                built.addAttributes(operation.getAttrs());
                for (unsigned i = 0; i < operation.getNumRegions(); ++i) { built.addRegion(); }
                auto * copied = at.create(built);
                for (auto [from, to] : llvm::zip(operation.getRegions(), copied->getRegions())) {
                    if (from.empty()) {
                        if (loop) { return false; }
                        auto & block = to.emplaceBlock();
                        mlir::OpBuilder inside(&block, block.end());
                        mlir::scf::YieldOp::create(inside, where, state);
                        continue;
                    }
                    if (!from.hasOneBlock()) { return false; }
                    auto & block = to.emplaceBlock();
                    for (unsigned i = 0;
                         i < values.getValueMap().size() + values.getOperationMap().size(); ++i) {
                        if (!spend()) { return false; }
                    }
                    mlir::IRMapping nested(values);
                    for (auto argument : from.front().getArguments()) {
                        nested.map(argument,
                                   block.addArgument(argument.getType(), argument.getLoc()));
                    }
                    for (auto value : state) {
                        (void)value;
                        if (!spend()) { return false; }
                    }
                    auto innerState = state;
                    if (loop) {
                        for (auto & value : innerState) { value = block.addArgument(type, where); }
                    }
                    mlir::OpBuilder inside(&block, block.end());
                    if (!self(self, from.front(), inside, nested, innerState, depth + 1, true)) {
                        return false;
                    }
                }
                values.map(operation.getResults(), copied->getResults().drop_back(state.size()));
                llvm::copy(copied->getResults().take_back(state.size()), state.begin());
                continue;
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
                if (appendState) {
                    llvm::SmallVector<mlir::Value> results;
                    for (auto value : yield.getOperands()) {
                        results.push_back(values.lookup(value));
                    }
                    llvm::append_range(results, state);
                    mlir::scf::YieldOp::create(at, where, results);
                }
                continue;
            }
            if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(operation)) {
                llvm::SmallVector<mlir::Value> results;
                for (auto value : condition.getArgs()) { results.push_back(values.lookup(value)); }
                llvm::append_range(results, state);
                mlir::scf::ConditionOp::create(at, where, values.lookup(condition.getCondition()),
                                               results);
                continue;
            }
            if (operation.getNumRegions()) { return false; }
            at.clone(operation, values);
        }
        return true;
    };
    if (!clone(clone, entry.getBody().front(), start, mapping, state, 0, false) || !emittedNext) {
        return error("DOM custom iterator has unconfined record state or source regions");
    }
    if (marker->use_empty()) { marker.erase(); }
    llvm::SmallVector<ctjs::FuncOp> completed{entry};
    if (!stateInitials.empty()) {
        for (auto & [name, store] : slots) {
            (void)name;
            completed.push_back(targetOf(store.getValue().getDefiningOp<ctjs::CreateClosureOp>()));
            if (!completed.back()) { return error("DOM custom iterator budget exhausted"); }
        }
    }
    entry.getBody().takeBody(rewritten);
    // Only after selecting the custom arm can completion normalization discard
    // inactive eager-array state. A live record marker is poison and refuses.
    if (!work.normalizeCompletion(entry)) { return error(work.reason); }
    // Drop unused completion tuple positions without erasing their producers.
    // Parent-first order exposes dead nested branch results in the same pass.
    llvm::SmallVector<mlir::Operation *> tuples;
    for (auto function : completed) {
        const auto tupleScan =
            function.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * operation) {
                if (!spend()) { return mlir::WalkResult::interrupt(); }
                if (llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(operation)) {
                    tuples.push_back(operation);
                }
                return mlir::WalkResult::advance();
            });
        if (tupleScan.wasInterrupted()) { return error("DOM custom iterator budget exhausted"); }
    }
    for (mlir::Operation * operation : tuples) {
        auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation);
        llvm::SmallVector<unsigned> dropped;
        llvm::SmallVector<mlir::Type> kept;
        for (auto [index, result] : llvm::enumerate(operation->getResults())) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            if (result.use_empty() && (!loop || loop.getAfterArguments()[index].use_empty())) {
                dropped.push_back(static_cast<unsigned>(index));
            } else {
                kept.push_back(result.getType());
            }
        }
        if (dropped.empty()) { continue; }
        for (unsigned i = 0; i < operation->getNumOperands() + operation->getNumResults() +
                                     operation->getNumRegions() + dropped.size();
             ++i) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        }
        mlir::OperationState built(operation->getLoc(), operation->getName());
        built.addOperands(operation->getOperands());
        built.addTypes(kept);
        built.addAttributes(operation->getAttrs());
        for (auto & region : operation->getRegions()) {
            auto * target = built.addRegion();
            target->takeBody(region);
        }
        mlir::OpBuilder at(operation);
        auto * copied = at.create(built);
        for (unsigned index : llvm::reverse(dropped)) {
            if (loop) {
                copied->getRegion(0).front().getTerminator()->eraseOperand(index + 1);
                copied->getRegion(1).front().eraseArgument(index);
            } else {
                for (auto & region : copied->getRegions()) {
                    region.front().getTerminator()->eraseOperand(index);
                }
            }
        }
        unsigned target = 0, removed = 0;
        for (auto [index, result] : llvm::enumerate(operation->getResults())) {
            if (removed < dropped.size() && dropped[removed] == index) {
                ++removed;
            } else {
                result.replaceAllUsesWith(copied->getResult(target++));
            }
        }
        operation->erase();
    }
    llvm::SmallVector<mlir::Operation *> arithmetic;
    for (auto function : completed) {
        const auto arithmeticScan = function.walk([&](mlir::Operation * operation) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            if (llvm::isa<mlir::arith::ConstantOp, mlir::arith::IndexCastUIOp>(operation)) {
                arithmetic.push_back(operation);
            }
            return mlir::WalkResult::advance();
        });
        if (arithmeticScan.wasInterrupted()) {
            return error("DOM custom iterator budget exhausted");
        }
    }
    for (mlir::Operation * operation : llvm::reverse(arithmetic)) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        if (operation->use_empty()) { operation->erase(); }
    }
    emittedNext = {};
    emittedDone = {};
    const auto methods = entry.walk([&](mlir::Operation * operation) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
        auto get =
            call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
        if (get && ctjs::constantKey(get.getKey()) == "next" &&
            get.getObject() == call.getReceiver() &&
            get.getObject().getDefiningOp<ctjs::CreateObjectOp>()) {
            if (emittedNext) { return mlir::WalkResult::interrupt(); }
            emittedNext = call;
        }
        return mlir::WalkResult::advance();
    });
    if (methods.wasInterrupted() || !emittedNext) {
        return error("DOM custom iterator next call is ambiguous after completion");
    }
    for (mlir::Operation * user : emittedNext->getUsers()) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
        if (get && ctjs::constantKey(get.getKey()) == "done") {
            if (emittedDone) { return error("DOM custom iterator done field is ambiguous"); }
            emittedDone = get;
        }
    }
    auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(emittedNext->getParentOp());
    if (!loop || emittedNext->getParentRegion() != &loop.getBefore() ||
        loop->getBlock() != &entry.getBody().front() || !emittedDone) {
        return error("DOM custom next requires one direct loop test");
    }
    auto condition = llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().back());
    const auto stops = [&](auto && self, mlir::Value value, unsigned depth) -> bool {
        if (!spend() || depth == 64) { return false; }
        if (auto constant = value.getDefiningOp<mlir::arith::ConstantOp>()) {
            auto bit = llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue());
            return bit && bit.getType().isInteger(1) && bit.getInt() == 0;
        }
        auto result = llvm::dyn_cast<mlir::OpResult>(value);
        auto branch =
            result ? llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner()) : mlir::scf::IfOp{};
        if (!branch) { return false; }
        auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
        for (auto & region : branch->getRegions()) {
            if (!spend() || !region.hasOneBlock()) { return false; }
            if (truth && truth.getValue() == emittedDone && &region == &branch.getElseRegion()) {
                continue;
            }
            auto yield = llvm::cast<mlir::scf::YieldOp>(region.front().back());
            if (!self(self, yield.getOperand(result.getResultNumber()), depth + 1)) {
                return false;
            }
        }
        return true;
    };
    if (!stops(stops, condition.getCondition(), 0)) {
        return error("DOM custom iterator must stop before next can run after done");
    }
    llvm::SmallVector<ctjs::LoadGlobalOp> unusedHelpers;
    const auto cleanup = entry.walk([&](ctjs::LoadGlobalOp load) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        if (load->use_empty() && host_detail::iteratorIntrinsicArity(load.getName())) {
            unusedHelpers.push_back(load);
        }
        return mlir::WalkResult::advance();
    });
    if (cleanup.wasInterrupted()) { return error("DOM custom iterator budget exhausted"); }
    for (auto load : unusedHelpers) { load.erase(); }
    identityBody.erase();
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative
