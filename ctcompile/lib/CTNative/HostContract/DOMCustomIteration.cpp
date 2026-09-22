#include "DOMSource/Proof.hpp"
#include "mlir/IR/Verifier.h"

#include <bit>

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
    const unsigned sourceCost = maxSteps - work.remaining;
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
    // The bounded census above covers this entry. Check region correspondence
    // before replacing private methods or threading state through those edges.
    if (mlir::failed(mlir::verify(entry))) {
        return error("DOM custom iterator requires a well-formed entry");
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
    llvm::DenseSet<mlir::Operation *> siblingHelpers;
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
    mlir::DominanceInfo entryDominance(entry);
    for (auto cell : entry.getBody().front().getOps<ctjs::CreateCellOp>()) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        auto found = capturedState.find(cell.getResult());
        if (found == capturedState.end()) { continue; }
        auto original = cell.getInitial().getDefiningOp<ctjs::ConstantOp>();
        ctjs::CellSetOp initializer;
        if (original && llvm::isa<ctjs::UndefinedAttr>(original.getValue())) {
            for (auto write : cell->getBlock()->getOps<ctjs::CellSetOp>()) {
                if (!spend()) { return error("DOM custom iterator budget exhausted"); }
                if (write.getCell() == cell.getResult()) {
                    initializer = write;
                    break;
                }
            }
        }
        auto initial = (initializer ? initializer.getValue() : cell.getInitial())
                           .getDefiningOp<ctjs::ConstantOp>();
        if (!initial || !llvm::isa<ctjs::NumberAttr>(initial.getValue()) || !original ||
            !llvm::isa<ctjs::NumberAttr, ctjs::UndefinedAttr>(original.getValue()) ||
            (initializer && !initializer->isBeforeInBlock(open))) {
            return error("DOM iterator capture requires one literal Number initialization");
        }
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            auto * user = use.getOwner();
            if (!entryDominance.properlyDominates(cell.getOperation(), user)) {
                return error("DOM iterator capture cell escapes its local initialization");
            }
            if (llvm::isa<ctjs::RootOp>(user) && use.getOperandNumber() == 1) { continue; }
            if (initializer && user != initializer &&
                !entryDominance.properlyDominates(initializer.getOperation(), user)) {
                return error("DOM iterator capture initialization must precede its observers");
            }
            if (llvm::isa<ctjs::CellGetOp, ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) {
                continue;
            }
            auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
            bool method = false;
            for (auto & [name, slot] : slots) {
                (void)name;
                if (!spend()) { return error("DOM custom iterator budget exhausted"); }
                method |= closure && slot.getValue() == closure.getResult();
            }
            if (!method && closure && use.getOperandNumber() >= 2 &&
                closure->getBlock() == &entry.getBody().front()) {
                siblingHelpers.insert(closure);
                continue;
            }
            if (!method || use.getOperandNumber() < 2) {
                return error("DOM iterator capture cell has an external reader or writer");
            }
        }
        found->second = static_cast<unsigned>(stateInitials.size());
        stateInitials.push_back(initial.getResult());
        stateStorage.insert(cell);
        if (initializer) { stateStorage.insert(initializer); }
    }
    // Follow single-initialized callable cells in both directions: a helper
    // may capture another helper without directly capturing iterator state.
    llvm::DenseMap<mlir::Value, ctjs::CreateClosureOp> callableCells;
    llvm::SmallVector<mlir::Operation *> callableStorage;
    const auto callableCell = [&](ctjs::CreateCellOp cell) -> bool {
        if (!cell || cell->getBlock() != &entry.getBody().front()) { return false; }
        if (callableCells.contains(cell)) { return true; }
        ctjs::CellSetOp write;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!spend()) { return false; }
            auto store = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
            if (!store) { continue; }
            if (write || use.getOperandNumber() != 0) { return false; }
            write = store;
        }
        auto value = write ? write.getValue() : cell.getInitial();
        auto closure = value.getDefiningOp<ctjs::CreateClosureOp>();
        auto * initialized = write ? write.getOperation() : cell.getOperation();
        if (!closure || closure->getBlock() != cell->getBlock() ||
            (write && (!undefined(cell.getInitial()) || write->getBlock() != cell->getBlock() ||
                       !cell->isBeforeInBlock(write))) ||
            !closure->isBeforeInBlock(initialized)) {
            return false;
        }
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!spend()) { return false; }
            auto * user = use.getOwner();
            if (user == write) { continue; }
            if (llvm::isa<ctjs::RootOp>(user) && use.getOperandNumber() == 1) {
                callableStorage.push_back(user);
                continue;
            }
            auto observer = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
            if (!observer || use.getOperandNumber() < 2 ||
                observer->getBlock() != cell->getBlock() ||
                !initialized->isBeforeInBlock(observer)) {
                return false;
            }
            siblingHelpers.insert(observer);
        }
        siblingHelpers.insert(closure);
        callableCells[cell] = closure;
        if (write) { callableStorage.push_back(write); }
        return true;
    };
    llvm::DenseSet<mlir::Operation *> discovered;
    while (discovered.size() != siblingHelpers.size()) {
        for (auto closure : entry.getBody().front().getOps<ctjs::CreateClosureOp>()) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            if (!siblingHelpers.contains(closure) || !discovered.insert(closure).second) {
                continue;
            }
            for (auto capture : closure.getUpvalues()) {
                if (!spend()) { return error("DOM custom iterator budget exhausted"); }
                if (!capturedState.contains(capture) &&
                    !callableCell(capture.getDefiningOp<ctjs::CreateCellOp>())) {
                    return error(
                        "DOM iterator sibling helper requires proved state or callable captures");
                }
            }
            for (mlir::OpOperand & use : closure.getResult().getUses()) {
                if (!spend()) { return error("DOM custom iterator budget exhausted"); }
                auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner());
                auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
                if (write && use.getOperandNumber() == 1) {
                    cell = write.getCell().getDefiningOp<ctjs::CreateCellOp>();
                }
                if ((cell || write) && !callableCell(cell)) {
                    return error("DOM iterator sibling callable storage is mutable or escapes");
                }
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                if (call || direct) {
                    auto callee = call ? call.getCallee() : direct.getCalleeValue();
                    auto target = callee.getDefiningOp<ctjs::CreateClosureOp>();
                    if (target && target->getBlock() == &entry.getBody().front()) {
                        siblingHelpers.insert(target);
                    }
                }
            }
        }
    }
    // Prove the complete family before changing bodies. Inline its cell
    // accesses at each call, then let the entry rewrite below carry their state
    // through source branches and loops alongside the ordinary return value.
    using Targets = llvm::SmallVector<unsigned, 2>;
    struct ReturnDependencies {
        Targets arguments;
        Targets callables;
    };
    struct Helper {
        ctjs::CreateClosureOp closure;
        ctjs::FuncOp body;
        llvm::SmallVector<mlir::Operation *> calls;
        std::optional<ReturnDependencies> returned;
    };
    llvm::SmallVector<Helper> helpers;
    for (auto closure : entry.getBody().front().getOps<ctjs::CreateClosureOp>()) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        if (!siblingHelpers.contains(closure)) { continue; }
        auto body = targetOf(closure);
        auto indices = closure.getEnclosingIndicesAttr();
        if (!body || body == entry || body->hasAttr("ctjs.skipped") ||
            !body.getBody().hasOneBlock() || body.getBody().front().empty() ||
            body.getBody().front().getNumArguments() < ctjs::implicit_arguments ||
            body.getUpvalueCount() != closure.getUpvalues().size() ||
            (!undefined(closure.getEnclosingThis()) &&
             closure.getEnclosingThis() !=
                 entry.getBody().front().getArgument(ctjs::arg_receiver)) ||
            closure.getEnclosingClosure() !=
                entry.getBody().front().getArgument(ctjs::arg_callee) ||
            (indices && (indices.size() != closure.getUpvalues().size() ||
                         llvm::any_of(indices.asArrayRef(), [](int32_t i) { return i != -1; })))) {
            return error("DOM iterator sibling helper requires an exact local leaf");
        }
        unsigned count = 0;
        const auto unique = candidate.walk([&](ctjs::CreateClosureOp other) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            count += other.getFunction() == closure.getFunction();
            return mlir::WalkResult::advance();
        });
        if (unique.wasInterrupted() || count != 1 || mlir::failed(mlir::verify(body))) {
            return error("DOM iterator sibling helper is shared or malformed");
        }
        // Prove loop-break continuations before the scalar leaf census, as for
        // iterator methods. Argument snapshots and ordered state writes survive.
        if (!work.normalizeCompletion(body)) { return error(work.reason); }
        const auto leaf = body.walk([&](mlir::Operation * operation) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            if (operation == body.getOperation() ||
                llvm::isa<ctjs::ConstantOp, ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp,
                          ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp, ctjs::CompareOp,
                          ctjs::TruthyOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                          ctjs::ReturnOp, ctjs::CallOp, ctjs::CallDirectOp, mlir::arith::ConstantOp,
                          mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::ConditionOp,
                          mlir::scf::YieldOp>(operation)) {
                return mlir::WalkResult::advance();
            }
            return mlir::WalkResult::interrupt();
        });
        if (leaf.wasInterrupted()) {
            return error("DOM iterator sibling helper must be a scalar leaf");
        }
        if (!work.checkBody(body, false, false, true)) { return error(work.reason); }
        helpers.push_back({closure, body, {}, std::nullopt});
    }
    llvm::DenseMap<mlir::Value, unsigned> helperIndices;
    llvm::DenseMap<mlir::Operation *, unsigned> helperBodies;
    for (auto [index, helper] : llvm::enumerate(helpers)) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        helperIndices[helper.closure] = static_cast<unsigned>(index);
        helperBodies[helper.body] = static_cast<unsigned>(index);
    }
    auto fixedCallables = helperIndices;
    llvm::SmallVector<mlir::Operation *> familyCalls;
    llvm::SmallVector<mlir::Operation *> familyFlow;
    const auto collectCalls = [&](ctjs::FuncOp body) {
        return body.walk([&](mlir::Operation * operation) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(operation)) {
                familyCalls.push_back(operation);
            }
            if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp, mlir::scf::IfOp, mlir::scf::WhileOp>(
                    operation)) {
                familyFlow.push_back(operation);
            }
            return mlir::WalkResult::advance();
        });
    };
    if (collectCalls(entry).wasInterrupted()) {
        return error("DOM custom iterator budget exhausted");
    }
    for (auto & helper : helpers) {
        const auto captures = helper.body.walk([&](mlir::Operation * operation) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation);
            auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(operation);
            if (read || write) {
                auto capture = helper.closure.getUpvalues()[static_cast<unsigned>(
                    read ? read.getIndex() : write.getIndex())];
                if (auto target = callableCells.lookup(capture)) {
                    if (write) { return mlir::WalkResult::interrupt(); }
                    fixedCallables[read.getResult()] = helperIndices.lookup(target);
                }
            }
            return mlir::WalkResult::advance();
        });
        if (captures.wasInterrupted() || collectCalls(helper.body).wasInterrupted()) {
            return error("DOM iterator sibling callable capture is mutable or unproved");
        }
    }
    const auto mergeTargets = [&](Targets & into, llvm::ArrayRef<unsigned> from) {
        for (auto index : from) {
            if (!spend()) { return false; }
            if (!llvm::is_contained(into, index)) { into.push_back(index); }
        }
        return true;
    };
    // Follow every incoming edge, including the zero-trip initializer and the
    // backedge. A visited set closes transport cycles; only known leaves supply
    // identities. Never cache a partial traversal of a cycle.
    const auto transportInputs = [](mlir::Value value,
                                    llvm::SmallVectorImpl<mlir::Value> & inputs) {
        if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value)) {
            auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(argument.getOwner()->getParentOp());
            if (!loop) { return false; }
            auto index = argument.getArgNumber();
            if (argument.getOwner() == &loop.getBefore().front()) {
                inputs.push_back(loop.getInits()[index]);
                inputs.push_back(loop.getAfter().front().getTerminator()->getOperand(index));
            } else {
                inputs.push_back(
                    llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().getTerminator())
                        .getArgs()[index]);
            }
            return true;
        }
        auto result = llvm::dyn_cast<mlir::OpResult>(value);
        if (!result) { return false; }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner())) {
            for (auto & region : branch->getRegions()) {
                inputs.push_back(
                    region.front().getTerminator()->getOperand(result.getResultNumber()));
            }
            return true;
        }
        if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(result.getOwner())) {
            inputs.push_back(
                llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().getTerminator())
                    .getArgs()[result.getResultNumber()]);
            return true;
        }
        return false;
    };
    // Summarize every return leaf as an immutable formal or fixed helper.
    // Complete transport edges include branch arms and loop initializers;
    // only grounded summaries compose, so cycles cannot invent a target.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto & helper : helpers) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            if (helper.returned) { continue; }
            auto & block = helper.body.getBody().front();
            llvm::SmallVector<mlir::Value> pending{
                llvm::cast<ctjs::ReturnOp>(block.getTerminator()).getValue()};
            llvm::DenseSet<mlir::Value> visited;
            ReturnDependencies dependencies;
            bool complete = true;
            while (!pending.empty()) {
                if (!spend()) { return error("DOM custom iterator budget exhausted"); }
                auto value = pending.pop_back_val();
                if (!visited.insert(value).second) { continue; }
                if (auto found = fixedCallables.find(value); found != fixedCallables.end()) {
                    if (!mergeTargets(dependencies.callables, {found->second})) {
                        return error("DOM custom iterator budget exhausted");
                    }
                    continue;
                }
                if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
                    argument && argument.getOwner() == &block &&
                    argument.getArgNumber() >= ctjs::implicit_arguments) {
                    if (!mergeTargets(dependencies.arguments,
                                      {argument.getArgNumber() - ctjs::implicit_arguments})) {
                        return error("DOM custom iterator budget exhausted");
                    }
                    continue;
                }
                if (transportInputs(value, pending)) { continue; }
                auto call = value.getDefiningOp<ctjs::CallOp>();
                auto direct = value.getDefiningOp<ctjs::CallDirectOp>();
                if (!call && !direct) {
                    complete = false;
                    continue;
                }
                auto found = fixedCallables.find(call ? call.getCallee() : direct.getCalleeValue());
                if (found == fixedCallables.end() || !helpers[found->second].returned) {
                    complete = false;
                    continue;
                }
                const auto & returned = *helpers[found->second].returned;
                if (!mergeTargets(dependencies.callables, returned.callables)) {
                    return error("DOM custom iterator budget exhausted");
                }
                auto args = call ? call.getArgs() : direct.getArgs();
                for (auto argument : returned.arguments) {
                    if (!spend()) { return error("DOM custom iterator budget exhausted"); }
                    if (argument < args.size()) {
                        pending.push_back(args[argument]);
                    } else {
                        complete = false;
                    }
                }
            }
            if (complete && (!dependencies.arguments.empty() || !dependencies.callables.empty())) {
                helper.returned = std::move(dependencies);
                changed = true;
            }
        }
    }
    // Bind each invocation separately before changing any body. The aggregate
    // identities below only enumerate observers; they never select a callee.
    using CallableValues = llvm::DenseMap<mlir::Value, Targets>;
    CallableValues callableValues, initialCallables;
    for (auto [value, index] : fixedCallables) { initialCallables[value] = {index}; }
    const auto resolveTargets = [&](mlir::Value value, const CallableValues & known,
                                    Targets & targets) {
        llvm::SmallVector<mlir::Value> pending{value};
        llvm::DenseSet<mlir::Value> visited;
        bool complete = true;
        while (!pending.empty()) {
            if (!spend()) { return false; }
            auto current = pending.pop_back_val();
            if (!visited.insert(current).second) { continue; }
            if (auto found = known.find(current); found != known.end()) {
                if (!mergeTargets(targets, found->second)) { return false; }
            } else if (!transportInputs(current, pending)) {
                auto call = current.getDefiningOp<ctjs::CallOp>();
                auto direct = current.getDefiningOp<ctjs::CallDirectOp>();
                if (!call && !direct) {
                    complete = false;
                    continue;
                }
                auto callee = call ? call.getCallee() : direct.getCalleeValue();
                auto found = known.find(callee);
                if (found == known.end()) {
                    complete = false;
                    continue;
                }
                // Arguments remain snapshots and fixed helpers retain identity.
                // The summary only enumerates possibilities; source branches
                // and effects still execute once when the proved call expands.
                auto args = call ? call.getArgs() : direct.getArgs();
                for (auto index : found->second) {
                    if (!spend()) { return false; }
                    const auto & returned = helpers[index].returned;
                    if (!returned) {
                        complete = false;
                        continue;
                    }
                    if (!mergeTargets(targets, returned->callables)) { return false; }
                    for (auto argument : returned->arguments) {
                        if (!spend()) { return false; }
                        if (argument < args.size()) {
                            pending.push_back(args[argument]);
                        } else {
                            complete = false;
                        }
                    }
                }
            }
        }
        return complete && !targets.empty();
    };
    llvm::DenseSet<mlir::Value> unknownArguments;
    const auto proveCalls = [&](auto && self, ctjs::FuncOp caller, CallableValues values,
                                unsigned depth, Targets & result) -> bool {
        if (!spend()) { return false; }
        if (depth == 64 || !work.active.insert(caller).second) {
            return work.refuse("DOM iterator sibling call tree is recursive or too deep");
        }
        const auto bindTransport = [&](mlir::Value value) {
            Targets targets;
            if (resolveTargets(value, values, targets)) {
                values[value] = std::move(targets);
            } else if (!targets.empty()) {
                return work.refuse("DOM iterator callable branch has an unproved arm");
            }
            return work.remaining != 0;
        };
        // Calls retain source order. Transport-only cycles can be resolved
        // before their enclosing loop is visited, from all of their leaves.
        for (auto * operation : familyFlow) {
            if (!spend()) { return false; }
            if (operation->getParentOfType<ctjs::FuncOp>() != caller) { continue; }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                for (auto value : branch.getResults()) {
                    if (!bindTransport(value)) { return false; }
                }
                continue;
            }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
                for (auto value : loop.getResults()) {
                    if (!bindTransport(value)) { return false; }
                }
                for (auto & region : loop->getRegions()) {
                    for (auto argument : region.front().getArguments()) {
                        if (!bindTransport(argument)) { return false; }
                    }
                }
                continue;
            }
            auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
            auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
            auto callee = call ? call.getCallee() : direct.getCalleeValue();
            if (!bindTransport(callee)) { return false; }
            auto found = values.find(callee);
            if (found == values.end()) {
                if (caller != entry) {
                    return work.refuse(
                        "DOM iterator sibling call requires an immutable local helper");
                }
                auto producer = callee.getDefiningOp();
                auto ordinary = llvm::dyn_cast_or_null<ctjs::CallOp>(producer);
                auto exact = llvm::dyn_cast_or_null<ctjs::CallDirectOp>(producer);
                if ((ordinary || exact) &&
                    values.contains(ordinary ? ordinary.getCallee() : exact.getCalleeValue())) {
                    return work.refuse(
                        "DOM iterator sibling call requires a proved returned callable");
                }
                continue;
            }
            Targets results;
            bool unknownResult = false;
            const auto callees = found->second;
            for (auto index : callees) {
                if (!spend()) { return false; }
                auto & target = helpers[index];
                auto & body = target.body.getBody().front();
                auto args = call ? call.getArgs() : direct.getArgs();
                if (args.size() + ctjs::implicit_arguments != body.getNumArguments()) {
                    return work.refuse("DOM iterator sibling helper requires exact argument arity");
                }
                if (!undefined(call ? call.getReceiver() : direct.getReceiver()) ||
                    (direct &&
                     (direct.getTarget() != target.body || !undefined(direct.getNewTarget())))) {
                    return work.refuse(
                        "DOM iterator sibling helper has an unsupported call target");
                }
                auto arguments = initialCallables;
                for (auto [index, actual] : llvm::enumerate(args)) {
                    if (!spend()) { return false; }
                    if (!bindTransport(actual)) { return false; }
                    auto formal =
                        body.getArgument(ctjs::implicit_arguments + static_cast<unsigned>(index));
                    if (auto bound = values.find(actual); bound != values.end()) {
                        arguments[formal] = bound->second;
                    } else {
                        unknownArguments.insert(formal);
                    }
                }
                Targets returned;
                if (!self(self, target.body, std::move(arguments), depth + 1, returned)) {
                    return false;
                }
                unknownResult |= returned.empty();
                if (!mergeTargets(results, returned)) { return false; }
            }
            if (!results.empty()) {
                if (unknownResult) {
                    return work.refuse("DOM iterator callable return has an unproved target");
                }
                values[operation->getResult(0)] = std::move(results);
            }
        }
        // checkBody proved one root return; every possible identity is confined.
        auto returned = llvm::cast<ctjs::ReturnOp>(caller.getBody().front().getTerminator());
        if (!bindTransport(returned.getValue())) { return false; }
        if (auto bound = values.find(returned.getValue()); bound != values.end()) {
            result = bound->second;
        }
        for (auto & [value, indices] : values) {
            if (!spend()) { return false; }
            auto & targets = callableValues[value];
            if (!mergeTargets(targets, indices)) { return false; }
        }
        work.active.erase(caller);
        return true;
    };
    Targets entryResult;
    if (!proveCalls(proveCalls, entry, initialCallables, 0, entryResult)) {
        return error(work.reason);
    }
    for (auto formal : unknownArguments) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        if (callableValues.contains(formal)) {
            return error("DOM iterator callable argument has an unproved actual");
        }
    }
    llvm::DenseSet<mlir::Operation *> callableRoots;
    const auto callsOf = [&](mlir::Value value, Helper & helper, ctjs::FuncOp caller) -> bool {
        for (mlir::OpOperand & use : value.getUses()) {
            if (!spend()) { return false; }
            auto * user = use.getOwner();
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(user);
                root && use.getOperandNumber() == 1) {
                if (caller == entry) { callableRoots.insert(root); }
                continue;
            }
            if (caller == entry && llvm::is_contained(callableStorage, user)) { continue; }
            if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(user);
                caller == entry && cell && use.getOperandNumber() == 0 &&
                callableCells.lookup(cell) == helper.closure) {
                continue;
            }
            if (llvm::isa<ctjs::ReturnOp>(user) && caller != entry &&
                user == caller.getBody().front().getTerminator()) {
                continue; // Every result observer is checked through callableValues.
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(user)) {
                auto branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
                if (branch && callableValues.contains(branch.getResult(use.getOperandNumber()))) {
                    continue; // Both arms and every joined observer were proved above.
                }
                auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(yield->getParentOp());
                if (loop && callableValues.contains(
                                loop.getBefore().front().getArgument(use.getOperandNumber()))) {
                    continue;
                }
            }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(user);
                loop && callableValues.contains(
                            loop.getBefore().front().getArgument(use.getOperandNumber()))) {
                continue;
            }
            if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(user);
                condition && use.getOperandNumber() > 0) {
                auto loop = llvm::cast<mlir::scf::WhileOp>(condition->getParentOp());
                auto index = use.getOperandNumber() - 1;
                if (callableValues.contains(loop.getResult(index)) &&
                    callableValues.contains(loop.getAfter().front().getArgument(index))) {
                    continue;
                }
            }
            auto ordinary = llvm::dyn_cast<ctjs::CallOp>(user);
            auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user);
            if (ordinary || direct) {
                auto args = ordinary ? ordinary.getArgs() : direct.getArgs();
                const unsigned first = user->getNumOperands() - args.size();
                if (use.getOperandNumber() >= first) {
                    auto target = callableValues.find(ordinary ? ordinary.getCallee()
                                                               : direct.getCalleeValue());
                    if (target != callableValues.end() &&
                        user->getParentOfType<ctjs::FuncOp>() == caller) {
                        continue; // The complete actual/formal census above proved this edge.
                    }
                }
            }
            bool callable = false;
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
                callable = use.getOperandNumber() == 0 &&
                           call.getArgs().size() + ctjs::implicit_arguments ==
                               helper.body.getBody().front().getNumArguments() &&
                           undefined(call.getReceiver());
            } else if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
                callable = use.getOperandNumber() == 2 && call.getTarget() == helper.body &&
                           call.getArgs().size() + ctjs::implicit_arguments ==
                               helper.body.getBody().front().getNumArguments() &&
                           undefined(call.getReceiver()) && undefined(call.getNewTarget());
            }
            if (!callable || user->getParentOfType<ctjs::FuncOp>() != caller ||
                (caller == entry &&
                 !entryDominance.properlyDominates(helper.closure.getOperation(), user))) {
                return false;
            }
            helper.calls.push_back(user);
        }
        return true;
    };
    for (auto & [value, indices] : callableValues) {
        auto * parent = value.getParentBlock()->getParentOp();
        auto caller = llvm::dyn_cast<ctjs::FuncOp>(parent);
        if (!caller) { caller = parent->getParentOfType<ctjs::FuncOp>(); }
        for (auto index : indices) {
            if (!callsOf(value, helpers[index], caller)) {
                return error("DOM iterator sibling helper escapes or has an unsupported call");
            }
        }
    }
    for (auto & helper : helpers) {
        if (helper.calls.empty()) {
            return error("DOM iterator sibling helper has no proved invocation");
        }
    }
    if (!helpers.empty()) {
        // Enumerate both symbol scopes once for the complete family. Charge
        // the source walk before allocating either list, then each visited use.
        if (work.remaining / 2 < sourceCost) {
            return error("DOM custom iterator budget exhausted");
        }
        work.remaining -= 2 * sourceCost;
        for (const auto & uses : {mlir::SymbolTable::getSymbolUses(candidate.getOperation()),
                                  mlir::SymbolTable::getSymbolUses(&candidate.getBodyRegion())}) {
            if (!uses) { return error("DOM iterator sibling helper has unknown symbol uses"); }
            for (const auto & use : *uses) {
                if (!spend()) { return error("DOM custom iterator budget exhausted"); }
                auto body = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                    use.getUser(), use.getSymbolRef());
                auto found = helperBodies.find(body);
                if (found != helperBodies.end() &&
                    !llvm::is_contained(helpers[found->second].calls, use.getUser())) {
                    return error("DOM iterator sibling helper has an unproved symbolic observer");
                }
            }
        }
    }
    for (auto & helper : helpers) {
        auto & block = helper.body.getBody().front();
        llvm::SmallVector<mlir::Value> arguments;
        for (auto cell : helper.closure.getUpvalues()) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            arguments.push_back(block.addArgument(cell.getType(), cell.getLoc()));
        }
        const auto replaced = helper.body.walk([&](mlir::Operation * operation) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            mlir::OpBuilder at(operation);
            if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                auto index = static_cast<unsigned>(read.getIndex());
                auto cell = arguments[index];
                auto value = callableCells.contains(helper.closure.getUpvalues()[index])
                                 ? cell
                                 : ctjs::CellGetOp::create(at, read.getLoc(), read.getType(), cell)
                                       .getResult();
                read.getResult().replaceAllUsesWith(value);
                read.erase();
            } else if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(operation)) {
                auto cell = arguments[static_cast<unsigned>(write.getIndex())];
                ctjs::CellSetOp::create(at, write.getLoc(), cell, write.getValue());
                write.erase();
            }
            return mlir::WalkResult::advance();
        });
        if (replaced.wasInterrupted()) { return error("DOM custom iterator budget exhausted"); }
        helper.body.setFunctionTypeAttr(mlir::TypeAttr::get(
            mlir::FunctionType::get(candidate.getContext(), block.getArgumentTypes(),
                                    helper.body.getFunctionType().getResults())));
        helper.body.setUpvalueCount(0);
    }
    llvm::SmallVector<mlir::Operation *> pending;
    // Source order keeps dependency-scan costs independent of SSA hash order.
    for (auto * operation : familyCalls) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        auto ordinary = llvm::dyn_cast<ctjs::CallOp>(operation);
        auto callee = ordinary ? ordinary.getCallee()
                               : llvm::cast<ctjs::CallDirectOp>(operation).getCalleeValue();
        if (operation->getParentOfType<ctjs::FuncOp>() == entry &&
            callableValues.contains(callee)) {
            pending.push_back(operation);
        }
    }
    // All observers are confined calls or transport edges. A closed scalar
    // discriminator preserves branch selection until its ordinary calls expand;
    // no callable object, lookup table or runtime dispatch survives.
    llvm::SmallVector<mlir::Value> tags;
    CallableValues tagCallables;
    for (auto [index, helper] : llvm::enumerate(helpers)) {
        if (!spend()) { return error("DOM custom iterator budget exhausted"); }
        mlir::OpBuilder at(helper.closure);
        auto tag = ctjs::ConstantOp::create(
            at, helper.closure.getLoc(),
            ctjs::NumberAttr::get(candidate.getContext(),
                                  std::bit_cast<uint64_t>(static_cast<double>(index))));
        helper.closure.getResult().replaceAllUsesWith(tag);
        helperIndices[tag] = static_cast<unsigned>(index);
        tagCallables[tag] = {static_cast<unsigned>(index)};
        tags.push_back(tag);
    }
    while (!pending.empty()) {
        // ponytail: bounded linear scan; use a dependency worklist if large
        // helper families exhaust the shared budget. Expand a returned callable's
        // producer first unless its formal-return dependency proves the target.
        Targets targets;
        auto ready = llvm::find_if(pending, [&](mlir::Operation * operation) {
            if (!spend()) { return false; }
            targets.clear();
            auto ordinary = llvm::dyn_cast<ctjs::CallOp>(operation);
            auto callee = ordinary ? ordinary.getCallee()
                                   : llvm::cast<ctjs::CallDirectOp>(operation).getCalleeValue();
            return resolveTargets(callee, tagCallables, targets);
        });
        if (ready == pending.end() || !work.remaining) {
            return error("DOM iterator sibling call has no proved target");
        }
        auto * call = *ready;
        pending.erase(ready);
        auto ordinary = llvm::dyn_cast<ctjs::CallOp>(call);
        auto callee =
            ordinary ? ordinary.getCallee() : llvm::cast<ctjs::CallDirectOp>(call).getCalleeValue();
        auto receiver =
            ordinary ? ordinary.getReceiver() : llvm::cast<ctjs::CallDirectOp>(call).getReceiver();
        // Keep each already-evaluated argument's snapshot before appending
        // the cell identities. Later state writes cannot change that value.
        llvm::SmallVector<mlir::Value> actuals(
            ordinary ? ordinary.getArgs() : llvm::cast<ctjs::CallDirectOp>(call).getArgs());
        if (targets.size() > 1) {
            // Evaluate the original producer and arguments once, then select
            // exactly one body. Inlining inside these arms preserves state order.
            const auto dispatch = [&](auto && self, mlir::OpBuilder & at,
                                      unsigned position) -> mlir::Value {
                if (position == 64) {
                    work.refuse("DOM iterator callable selection is too deep");
                    return {};
                }
                for (unsigned operation = 0; operation < 6; ++operation) {
                    if (!spend()) { return {}; }
                }
                auto tag = tags[targets[position]];
                if (position + 1 == targets.size()) {
                    auto selected = ctjs::CallOp::create(
                        at, call->getLoc(), call->getResult(0).getType(), tag, receiver, actuals);
                    pending.push_back(selected);
                    return selected.getResult();
                }
                auto equal = ctjs::CompareOp::create(at, call->getLoc(), callee.getType(),
                                                     ctjs::CompareKind::StrictEq, callee, tag);
                auto condition = ctjs::TruthyOp::create(at, call->getLoc(), at.getI1Type(), equal);
                auto branch = mlir::scf::IfOp::create(at, call->getLoc(), call->getResultTypes(),
                                                      condition.getResult());
                for (auto [index, region] : llvm::enumerate(branch->getRegions())) {
                    auto & arm = region.emplaceBlock();
                    mlir::OpBuilder nested(&arm, arm.begin());
                    mlir::Value value;
                    if (index == 0) {
                        auto selected = ctjs::CallOp::create(
                            nested, call->getLoc(), callee.getType(), tag, receiver, actuals);
                        pending.push_back(selected);
                        value = selected;
                    } else {
                        value = self(self, nested, position + 1);
                    }
                    if (!value) { return {}; }
                    mlir::scf::YieldOp::create(nested, call->getLoc(), value);
                }
                return branch.getResult(0);
            };
            mlir::OpBuilder at(call);
            auto result = dispatch(dispatch, at, 0);
            if (!result) { return error(work.reason); }
            call->getResult(0).replaceAllUsesWith(result);
            work.callDepth.erase(call);
            call->erase();
            continue;
        }
        auto & helper = helpers[targets.front()];
        for (auto cell : helper.closure.getUpvalues()) {
            if (!spend()) { return error("DOM custom iterator budget exhausted"); }
            auto callable = callableCells.lookup(cell);
            actuals.push_back(callable ? tags[helperIndices.lookup(callable)] : cell);
        }
        auto * previous = call->getPrevNode();
        if (!work.inlineCall(entry, helper.body, call, actuals, receiver,
                             entry.getBody().front().getArgument(ctjs::arg_callee), {}, 0)) {
            return error(work.reason);
        }
        auto * first = previous ? previous->getNextNode() : &call->getBlock()->front();
        for (auto * added = first; added != call; added = added->getNextNode()) {
            const auto nested = added->walk([&](mlir::Operation * operation) {
                if (!spend()) { return mlir::WalkResult::interrupt(); }
                if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(operation)) {
                    pending.push_back(operation);
                }
                return mlir::WalkResult::advance();
            });
            if (nested.wasInterrupted()) { return error("DOM custom iterator budget exhausted"); }
        }
        work.callDepth.erase(call);
        call->erase();
    }
    for (auto * storage : callableStorage) { storage->erase(); }
    for (auto * root : callableRoots) { root->erase(); }
    for (auto & helper : helpers) { helper.closure->dropAllReferences(); }
    for (auto [cell, closure] : callableCells) {
        (void)closure;
        if (!cell.use_empty()) {
            return error("DOM iterator callable cell has a remaining observer");
        }
        cell.getDefiningOp()->erase();
    }
    for (auto & helper : helpers) {
        helper.closure.erase();
        helper.body.erase();
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
            auto read = llvm::dyn_cast<ctjs::CellGetOp>(operation);
            auto write = llvm::dyn_cast<ctjs::CellSetOp>(operation);
            if (read || write) {
                auto found = capturedState.find(read ? read.getCell() : write.getCell());
                if (found != capturedState.end()) {
                    auto & current = state[found->second + 1];
                    if (read) {
                        values.map(read.getResult(), current);
                    } else {
                        current = values.lookup(write.getValue());
                    }
                    continue;
                }
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
                    auto branch = mlir::scf::IfOp::create(
                        at, where, mlir::TypeRange(mlir::ValueRange(state).drop_front()), test,
                        true);
                    for (auto & region : branch->getRegions()) {
                        auto & block = region.front();
                        mlir::OpBuilder inside(&block, block.begin());
                        for (auto value : state) {
                            (void)value;
                            if (!spend()) { return false; }
                        }
                        auto closingState = state;
                        if (&region == &branch.getElseRegion()) {
                            if (!methodCall(inside, "return", values, closingState)) {
                                return false;
                            }
                        }
                        if (state.size() > 1) {
                            mlir::scf::YieldOp::create(inside, where,
                                                       mlir::ValueRange(closingState).drop_front());
                        }
                    }
                    llvm::copy(branch.getResults(), state.begin() + 1);
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
                    // Each source SSA definition is cloned once. Share its
                    // mapping across regions; only mutable state needs a copy.
                    for (auto argument : from.front().getArguments()) {
                        if (!spend()) { return false; }
                        values.map(argument,
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
                    if (!self(self, from.front(), inside, values, innerState, depth + 1, true)) {
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
