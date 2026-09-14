#include "Analysis.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/ScopeExit.h"

namespace ctcompile::ctnative::host_detail {

std::optional<HostObjectGlobalRead> analyzer::objectGlobalRead(ctjs::LoadGlobalOp read) {
    // ponytail: repeated chain walks are quadratic; cache origins if the shared budget limits them.
    ctjs::StoreGlobalOp initialization;
    llvm::DenseSet<mlir::Operation *> seen;
    auto current = read;
    while (true) {
        if (!step() || current->getParentOp() != entry ||
            !llvm::hasSingleElement(entry.getBody())) {
            return std::nullopt;
        }
        // The initial census includes every store in the module, including
        // inactive arms, later writes and stores outside the script entry.
        const auto & stores = globals[current.getName()];
        if (stores.size() != 1) { return std::nullopt; }
        auto store = stores.front();
        if (!seen.insert(store).second || store->getParentOp() != entry ||
            !dominance.properlyDominates(store.getOperation(), current.getOperation())) {
            return std::nullopt;
        }
        if (!initialization) { initialization = store; }
        if (auto made = store.getValue().getDefiningOp<ctjs::CreateObjectOp>()) {
            if (made->getParentOp() != entry ||
                !dominance.properlyDominates(made.getOperation(), store.getOperation())) {
                return std::nullopt;
            }
            // Structural evidence only: the complete family-use and environment
            // proofs must succeed before any public HostObjectGlobalRead exists.
            return HostObjectGlobalRead{initialization, read, made};
        }
        auto source = store.getValue().getDefiningOp<ctjs::LoadGlobalOp>();
        if (!source || !dominance.properlyDominates(source.getOperation(), store.getOperation())) {
            return std::nullopt;
        }
        current = source;
    }
}

std::optional<std::vector<HostObjectGlobalRead>> analyzer::objectGlobalReads(
    ctjs::CreateObjectOp made) {
    if (!step()) { return std::nullopt; }
    ctjs::StoreGlobalOp initialization;
    for (mlir::OpOperand & use : made.getResult().getUses()) {
        if (!step()) { return std::nullopt; }
        auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(use.getOwner());
        if (!store) { continue; }
        if (initialization || use.getOperandNumber() != 0) { return std::nullopt; }
        initialization = store;
    }
    if (!initialization) { return std::nullopt; }
    llvm::StringMap<ctjs::StoreGlobalOp> bindings;
    bindings.try_emplace(initialization.getName(), initialization);
    // Census all successor stores before following the reachable bindings.
    // Even an early/nonentry edge to an intermediate alias must be checked,
    // regardless of where it appears in the module traversal.
    llvm::StringMap<llvm::SmallVector<ctjs::StoreGlobalOp>> successors;
    const auto stores = module.walk([&](ctjs::StoreGlobalOp store) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (auto source = store.getValue().getDefiningOp<ctjs::LoadGlobalOp>()) {
            successors[source.getName()].push_back(store);
        }
        return mlir::WalkResult::advance();
    });
    if (stores.wasInterrupted()) { return std::nullopt; }
    llvm::SmallVector<ctjs::StoreGlobalOp> pending{initialization};
    for (size_t index = 0; index < pending.size(); ++index) {
        if (!step()) { return std::nullopt; }
        for (ctjs::StoreGlobalOp store : successors[pending[index].getName()]) {
            if (!step()) { return std::nullopt; }
            auto source = store.getValue().getDefiningOp<ctjs::LoadGlobalOp>();
            const auto predecessor = objectGlobalRead(source);
            const auto & writes = globals[store.getName()];
            if (!predecessor || predecessor->object != made || store->getParentOp() != entry ||
                writes.size() != 1 || writes.front() != store ||
                !dominance.properlyDominates(source.getOperation(), store.getOperation()) ||
                !bindings.try_emplace(store.getName(), store).second) {
                return std::nullopt;
            }
            pending.push_back(store);
        }
    }
    std::vector<HostObjectGlobalRead> reads;
    llvm::DenseSet<mlir::Operation *> readInitializations;
    const auto census = module.walk([&](ctjs::LoadGlobalOp read) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        const auto found = bindings.find(read.getName());
        if (found == bindings.end()) { return mlir::WalkResult::advance(); }
        const auto edge = objectGlobalRead(read);
        if (!edge || edge->object != made || edge->initialization != found->second) {
            return mlir::WalkResult::interrupt();
        }
        reads.push_back(*edge);
        readInitializations.insert(edge->initialization);
        return mlir::WalkResult::advance();
    });
    if (census.wasInterrupted() || readInitializations.size() != bindings.size()) {
        return std::nullopt;
    }
    return reads;
}

bool analyzer::capturedMapParameters(
    ctjs::FuncOp function, bool prepared, llvm::ArrayRef<mlir::Operation *> calls,
    const llvm::DenseSet<mlir::Operation *> & familyCalls,
    const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & results,
    HostMethodParameters & result, HostCapturedMap * capture) {
    const unsigned count = function.getBody().front().getNumArguments() - (prepared ? 4u : 3u);
    std::optional<std::vector<PrimitiveAlternatives>> found;
    std::vector<mlir::BlockArgument> objectKeys;
    for (mlir::Operation * operation : calls) {
        if (!step()) { return false; }
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
        const auto args = direct ? direct.getArgs() : llvm::cast<ctjs::CallOp>(operation).getArgs();
        std::vector<PrimitiveAlternatives> tags;
        for (mlir::Value actual : args.drop_front(prepared ? 1u : 0u)) {
            if (!step()) { return false; }
            const auto categories = entryCategories(actual, results, operation);
            if (!categories.known || !(categories.truthy | categories.falsy)) {
                auto made = actual.getDefiningOp<ctjs::CreateObjectOp>();
                std::optional<HostObjectGlobalRead> global;
                if (auto read = actual.getDefiningOp<ctjs::LoadGlobalOp>()) {
                    global = objectGlobalRead(read);
                    if (!global) { return false; }
                    made = global->object;
                }
                if (!made || made->getParentOp() != entry || operation->getParentOp() != entry ||
                    !dominance.properlyDominates(made.getOperation(), operation) ||
                    !dominance.dominates(actual, operation)) {
                    return false;
                }
                llvm::SmallVector<mlir::Value> aliases{made.getResult()};
                llvm::DenseSet<mlir::Operation *> initializations;
                if (global) {
                    const auto reads = objectGlobalReads(made);
                    if (!reads) { return false; }
                    for (const HostObjectGlobalRead & edge : *reads) {
                        if (!step()) { return false; }
                        auto read = edge.read;
                        aliases.push_back(read.getResult());
                        initializations.insert(edge.initialization);
                    }
                }
                // Caller leaves may hold only scalar own fields. Census every
                // alias before accepting reads; the method bodies independently
                // limit these formals to Map keys/payloads, never outgoing edges.
                llvm::SmallVector<ctjs::SetPropertyOp> writes;
                for (mlir::Value alias : aliases) {
                    for (mlir::OpOperand & use : alias.getUses()) {
                        if (!step()) { return false; }
                        auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                        if (!write) { continue; }
                        const auto payload = entryCategories(write.getValue(), results, write);
                        if (use.getOperandNumber() != 0 || write->getParentOp() != entry ||
                            !dominance.dominates(alias, write) ||
                            !dominance.dominates(write.getKey(), write) ||
                            !ctjs::ordinaryKey(ctjs::constantKey(write.getKey())) ||
                            !payload.tag()) {
                            return false;
                        }
                        writes.push_back(write);
                    }
                }
                for (mlir::Value alias : aliases) {
                    for (mlir::OpOperand & use : alias.getUses()) {
                        if (!step() || !dominance.dominates(alias, use.getOwner())) {
                            return false;
                        }
                        if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                            (initializations.contains(use.getOwner()) &&
                             use.getOperandNumber() == 0 &&
                             llvm::cast<ctjs::StoreGlobalOp>(use.getOwner()).getValue() == alias)) {
                            continue;
                        }
                        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner())) {
                            if (!llvm::is_contained(writes, write)) { return false; }
                            if (capture && !llvm::is_contained(capture->leafWrites, write)) {
                                capture->leafWrites.push_back(write);
                            }
                            continue;
                        }
                        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner())) {
                            if (use.getOperandNumber() != 0 || read->getParentOp() != entry ||
                                !dominance.dominates(read.getKey(), read) ||
                                !ctjs::ordinaryKey(ctjs::constantKey(read.getKey()))) {
                                return false;
                            }
                            bool initialized = false;
                            for (ctjs::SetPropertyOp write : writes) {
                                if (!step()) { return false; }
                                if (ctjs::constantKey(write.getKey()) ==
                                        ctjs::constantKey(read.getKey()) &&
                                    dominance.properlyDominates(write.getOperation(), read)) {
                                    initialized = true;
                                }
                            }
                            if (!initialized) { return false; }
                            if (capture && !llvm::is_contained(capture->leafReads, read)) {
                                capture->leafReads.push_back(read);
                            }
                            continue;
                        }
                        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(use.getOwner());
                            compare && compare.getKind() == ctjs::CompareKind::StrictEq &&
                            compare->getParentOp() == entry) {
                            if (!dominance.dominates(compare.getLhs(), compare) ||
                                !dominance.dominates(compare.getRhs(), compare)) {
                                return false;
                            }
                            continue;
                        }
                        auto directUse = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                        auto callUse = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                        if ((!directUse && !callUse) ||
                            use.getOperandNumber() < (directUse ? 3u : 2u) + (prepared ? 1u : 0u) ||
                            !familyCalls.contains(use.getOwner())) {
                            return false;
                        }
                    }
                }
                const auto parameter = function.getBody().front().getArgument(
                    (prepared ? 4u : 3u) + static_cast<unsigned>(tags.size()));
                if (!llvm::is_contained(objectKeys, parameter)) { objectKeys.push_back(parameter); }
            }
            tags.push_back(categories);
        }
        if (found) {
            if (found->size() != tags.size()) { return false; }
            for (unsigned index = 0; index < tags.size(); ++index) {
                if (!step()) { return false; }
                tags[index] = tags[index].joined((*found)[index]);
            }
        }
        found = std::move(tags);
    }
    // An uncalled zero-argument sibling can still have its effects checked;
    // the owning plan separately requires current calls for the full family.
    if (exhausted || (!found && count != 0)) { return false; }
    if (found) {
        for (auto [index, alternatives] : llvm::enumerate(*found)) {
            if (!step()) { return false; }
            if (llvm::is_contained(objectKeys,
                                   function.getBody().front().getArgument(
                                       (prepared ? 4u : 3u) + static_cast<unsigned>(index)))) {
                continue;
            }
            const auto mask = alternatives.truthy | alternatives.falsy;
            // Extend the established exact primitive boundary only to the
            // existing nullable String family. Unknown/empty sets and other
            // heterogeneous parameters still lack a supported proof here.
            constexpr unsigned nullableString = PrimitiveAlternatives::String |
                                                PrimitiveAlternatives::Null |
                                                PrimitiveAlternatives::Undefined;
            if (!alternatives.known || !mask ||
                (!alternatives.tag() &&
                 (!(mask & PrimitiveAlternatives::String) || (mask & ~nullableString)))) {
                return false;
            }
        }
    }
    result.alternatives = found ? std::move(*found) : std::vector<PrimitiveAlternatives>{};
    // Canonical ordering makes every call expose the same complete family.
    llvm::sort(objectKeys,
               [](auto lhs, auto rhs) { return lhs.getArgNumber() < rhs.getArgNumber(); });
    result.objectKeys = std::move(objectKeys);
    return true;
}

bool analyzer::capturedMapOuterKeys(
    bool prepared, llvm::ArrayRef<llvm::SmallVector<mlir::Operation *>> familyCalls,
    HostCapturedMap & result) {
    llvm::DenseSet<mlir::Operation *> checked(result.calls.begin(), result.calls.end());
    llvm::DenseSet<mlir::Value> outerValues;
    if (prepared) {
        for (auto parameters : result.parameters) {
            if (!step()) { return false; }
            outerValues.insert(parameters.function.getBody().front().getArgument(3));
        }
    } else {
        for (ctjs::LoadUpvalueOp load : result.upvalues) {
            if (!step()) { return false; }
            outerValues.insert(load.getResult());
        }
    }
    const auto outer = [&](mlir::Value value) {
        while (step()) {
            if (outerValues.contains(value)) { return true; }
            auto call = value.getDefiningOp<ctjs::CallOp>();
            auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                             : ctjs::GetPropertyOp{};
            if (!read || !checked.contains(call) || ctjs::constantKey(read.getKey()) != "set") {
                return false;
            }
            value = call.getReceiver();
        }
        return false;
    };
    llvm::DenseSet<mlir::Operation *> keyCalls;
    for (ctjs::CallOp call : result.calls) {
        if (!step()) { return false; }
        if (!outer(call.getReceiver())) { continue; }
        auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        const auto action = read ? ctjs::constantKey(read.getKey()) : llvm::StringRef{};
        // Even an unused outer snapshot can carry these identities away from
        // the direct formal. Child snapshots do not contain outer keys.
        if (action == "keys") { return !exhausted; }
        if (action == "set" || action == "get" || action == "has" || action == "delete") {
            keyCalls.insert(call);
        }
    }
    llvm::DenseSet<mlir::OpOperand *> keyUses;
    llvm::DenseSet<mlir::Operation *> candidates;
    for (auto [index, parameters] : llvm::enumerate(result.parameters)) {
        for (mlir::BlockArgument parameter : parameters.objectKeys) {
            if (!step()) { return false; }
            bool onlyKeys = true, usedKey = false;
            for (mlir::OpOperand & use : parameter.getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                const bool key = keyCalls.contains(use.getOwner()) && use.getOperandNumber() == 2;
                onlyKeys &= key;
                usedKey |= key;
            }
            for (mlir::Operation * operation : familyCalls[index]) {
                if (!step()) { return false; }
                const unsigned position =
                    parameter.getArgNumber() - (llvm::isa<ctjs::CallDirectOp>(operation) ? 0u : 1u);
                mlir::OpOperand & use = operation->getOpOperand(position);
                auto made = use.get().getDefiningOp<ctjs::CreateObjectOp>();
                if (auto read = use.get().getDefiningOp<ctjs::LoadGlobalOp>()) {
                    const auto edge = objectGlobalRead(read);
                    if (edge) { made = edge->object; }
                }
                if (!made) { continue; }
                candidates.insert(made);
                if (onlyKeys && usedKey) { keyUses.insert(&use); }
            }
        }
    }
    // Source order gives every family invocation the same evidence. All uses
    // of each allocation and every named alias must satisfy the narrower role;
    // one sibling payload/child-key use removes it without rejecting ownership.
    const auto census = entry.walk([&](ctjs::CreateObjectOp made) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (!candidates.contains(made)) { return mlir::WalkResult::advance(); }
        llvm::SmallVector<mlir::Value> aliases{made.getResult()};
        llvm::DenseSet<mlir::Operation *> initializations;
        bool named = false;
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            named |= llvm::isa<ctjs::StoreGlobalOp>(use.getOwner());
        }
        std::vector<HostObjectGlobalRead> reads;
        if (named) {
            const auto found = objectGlobalReads(made);
            if (!found) { return mlir::WalkResult::advance(); }
            reads = *found;
        }
        for (const auto & edge : reads) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            auto read = edge.read;
            aliases.push_back(read.getResult());
            initializations.insert(edge.initialization);
        }
        for (mlir::Value alias : aliases) {
            for (mlir::OpOperand & use : alias.getUses()) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                if (llvm::isa<ctjs::RootOp>(use.getOwner()) || keyUses.contains(&use) ||
                    (initializations.contains(use.getOwner()) && use.getOperandNumber() == 0)) {
                    continue;
                }
                return mlir::WalkResult::advance();
            }
        }
        result.outerKeyObjects.push_back(made);
        return mlir::WalkResult::advance();
    });
    return !census.wasInterrupted() && !exhausted;
}

std::string analyzer::environmentProblem() {
    std::string reason;
    const auto reject = [&](llvm::StringRef why) {
        if (reason.empty()) { reason = why.str(); }
    };
    if (ambiguousFunctions) { reject("ambiguous numeric source function identities"); }
    for (const std::string & name : contract.absentBindings) {
        if (!globals[name].empty()) { reject("an absent host binding has a source write"); }
    }
    for (const std::string & name : contract.undefinedBindings) {
        if (!globals[name].empty()) { reject("a fixed undefined host binding has a source write"); }
    }
    if (llvm::is_contained(contract.initialIntrinsics, "Map")) {
        // The constructor and cell precede their invocation in source order.
        // Discover complete capture edges first, then admit only those exact
        // operations during the environment census below.
        module.walk([&](mlir::Operation * operation) {
            if (!step()) { return; }
            if (!llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(operation)) { return; }
            auto edge = propertyCall(operation);
            if (!edge || !edge->capturedMap) { return; }
            capturedCalls.try_emplace(operation, *edge);
            auto capture = *edge->capturedMap;
            for (mlir::Operation * allowed :
                 {capture.intrinsic.getOperation(), capture.allocation.getOperation(),
                  capture.cell.getOperation(), capture.initialization.getOperation(),
                  capture.argument.getOperation()}) {
                if (allowed) { capturedOperations.insert(allowed); }
            }
            for (ctjs::LoadUpvalueOp load : capture.upvalues) {
                if (step()) { capturedOperations.insert(load); }
            }
            for (ctjs::GetPropertyOp read : capture.reads) {
                if (step()) { capturedOperations.insert(read); }
            }
            for (ctjs::CallOp call : capture.calls) {
                if (step()) { capturedOperations.insert(call); }
            }
            for (ctjs::SetPropertyOp write : capture.leafWrites) {
                if (step()) { capturedOperations.insert(write); }
            }
            for (ctjs::GetPropertyOp read : capture.leafReads) {
                if (step()) { capturedOperations.insert(read); }
            }
            for (mlir::Operation * snapshot : capture.snapshotOperations) {
                if (step()) { capturedOperations.insert(snapshot); }
            }
            for (const auto & callback : capture.scalarCallbacks) {
                for (mlir::Operation * allowed : callback.operations) {
                    if (step()) { capturedOperations.insert(allowed); }
                }
                for (ctjs::LoadGlobalOp load : callback.loads) {
                    if (step()) { capturedOperations.insert(load); }
                }
                for (ctjs::GetPropertyOp read : callback.reads) {
                    if (step()) { capturedOperations.insert(read); }
                }
                for (mlir::Operation * call : callback.calls) {
                    if (step()) { capturedOperations.insert(call); }
                }
                for (const auto & global : callback.globals) {
                    for (ctjs::LoadGlobalOp read : global.reads) {
                        if (step()) { mutableScalarReads.insert(read); }
                    }
                }
            }
            for (ctjs::ConstructOp child : capture.childMaps) {
                if (step()) { capturedOperations.insert(child); }
                if (step()) { capturedOperations.insert(child.getCallee().getDefiningOp()); }
            }
        });
    }
    module.walk([&](mlir::Operation * operation) {
        if (!step()) { return; }
        if (llvm::isa<ctjs::BinaryOp, ctjs::CompareOp, ctjs::UnaryOp, ctjs::TruthyOp,
                      mlir::scf::IfOp, mlir::scf::YieldOp>(operation)) {
            // Even an inactive source arm must have real SSA operands. A
            // selected-arm effect anchor cannot export its local definitions.
            for (mlir::Value operand : operation->getOperands()) {
                if (!step() || !dominance.dominates(operand, operation)) {
                    reject("provider operand is outside its source scope");
                    return;
                }
            }
        }
        if (!active(operation)) { return; }
        if (capturedOperations.contains(operation)) { return; }
        if (auto function = llvm::dyn_cast<ctjs::FuncOp>(operation)) {
            if (function.getBody().empty()) { reject("external function provider is unsupported"); }
            return;
        }
        if (operation->hasAttr("ctjs.skipped")) { reject("source contains unimported functions"); }
        if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
            if (!callable(made.getResult())) {
                reject("closure lacks an exact source function identity");
            }
            return;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            const auto & definitions = globals[load.getName()];
            if (definitions.empty() &&
                !llvm::is_contained(contract.absentBindings, load.getName()) &&
                !llvm::is_contained(contract.undefinedBindings, load.getName())) {
                reject(("unproved host binding `" + load.getName() + "`").str());
            }
            if (llvm::is_contained(contract.absentBindings, load.getName())) {
                if (!load.getTypeofLookup()) {
                    reject("absent binding lacks source typeof lookup mode");
                }
            } else if (!definitions.empty() &&
                       !llvm::any_of(definitions, [&](ctjs::StoreGlobalOp store) {
                           return before(store, load);
                       })) {
                reject("global read lacks definite source initialization");
            }
            return;
        }
        if (indirectFactories.contains(operation)) {
            const auto factory = target(operation);
            const bool captured = llvm::any_of(capturedCalls, [&](const auto & item) {
                return step() &&
                       item.second.closure->template getParentOfType<ctjs::FuncOp>() == factory;
            });
            if (!captured || !exactCall(operation)) {
                reject("indirect factory lacks a complete current captured Map proof");
            }
            return;
        }
        if (llvm::isa<ctjs::CallOp>(operation) || (llvm::isa<ctjs::CallDirectOp>(operation) &&
                                                   llvm::cast<ctjs::CallDirectOp>(operation)
                                                       .getCalleeValue()
                                                       .getDefiningOp<ctjs::GetPropertyOp>())) {
            if (auto captured = capturedCalls.find(operation); captured != capturedCalls.end()) {
                checkedCalls.push_back(captured->second);
            } else if (auto edge = propertyCall(operation)) {
                checkedCalls.push_back(*edge);
            } else {
                reject("property call lacks a current source getter proof");
            }
            return;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
            if (!exactCall(call)) { reject("direct call lacks exact source callable identity"); }
            return;
        }
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            if (!ctjs::ordinaryKey(ctjs::constantKey(get.getKey()))) {
                reject("dynamic/prototype property read is unsupported");
            }
            if (!object(get.getObject())) {
                reject("property receiver lacks a fresh own-data object proof");
            }
            return;
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            if (!ctjs::ordinaryKey(ctjs::constantKey(set.getKey()))) {
                reject("dynamic/prototype property write is unsupported");
            }
            if (!object(set.getObject())) {
                reject("property receiver lacks a fresh own-data object proof");
            }
            return;
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
            if (unary.getKind() != ctjs::UnaryKind::TypeOf &&
                unary.getKind() != ctjs::UnaryKind::Not &&
                unary.getKind() != ctjs::UnaryKind::Void) {
                reject("unproved conversion behavior is unsupported");
            }
            return;
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
            if (compare.getKind() != ctjs::CompareKind::StrictEq &&
                !primitive(compare.getResult())) {
                reject("unproved comparison behavior is unsupported");
            }
            return;
        }
        if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation)) {
            if (entryCategories(binary.getResult(), capturedResults).tag() !=
                mlir::TypeID::get<ctjs::NumberAttr>()) {
                reject("unsupported provider behavior through `ctjs.binary`");
            }
            return;
        }
        if (llvm::isa<mlir::ModuleOp, ctjs::ConstantOp, ctjs::CreateObjectOp, ctjs::CreateClosureOp,
                      ctjs::StoreGlobalOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp,
                      ctjs::RootOp, ctjs::TruthyOp, mlir::scf::IfOp, mlir::scf::YieldOp>(
                operation) ||
            operation->getName().getDialectNamespace() == "arith" ||
            operation->getName().getStringRef() == "ub.poison") {
            return;
        }
        reject(
            ("unsupported provider behavior through `" + operation->getName().getStringRef() + "`")
                .str());
    });
    return reason;
}

} // namespace ctcompile::ctnative::host_detail

namespace ctcompile::ctnative {

HostContractAnalysis::HostContractAnalysis(mlir::ModuleOp module, const HostContract & contract,
                                           unsigned maxSteps) {
    // A fresh instance ignores all old/forged reports, including a forged
    // success flag. Its supplied contract is bound to the actual current IR.
    if (hostContractFingerprint(module) != contract.moduleSha256) {
        refusal = "host contract module fingerprint mismatch";
        return;
    }
    host_detail::analyzer analysis(module, contract, maxSteps);
    const llvm::scope_exit recordWork([&] {
        workSteps = maxSteps - analysis.remaining;
        budgetExhausted = analysis.exhausted;
    });
    if (auto problem = host_detail::initialBindingProblem(module, contract); !problem.empty()) {
        refusal = problem;
        return;
    }
    if (!analysis.entry || analysis.entry.getBody().empty()) {
        refusal = "host contract script entry is missing or external";
        return;
    }
    if (analysis.entry.getBody().front().getNumArguments() != 3 ||
        analysis.entry.getUpvalueCount() != 0) {
        refusal = "closed-source script entry cannot take host arguments or captures";
        return;
    }
    for (const auto & root : contract.roots) {
        for (const std::string & key : root.properties) {
            reports.push_back(analysis.slot(root, key));
        }
    }
    refusal = analysis.environmentProblem();
    for (const std::string & name : contract.observations) {
        const auto & stores = analysis.globals[name];
        if (stores.empty() && refusal.empty()) {
            refusal = "declared observation has no source store: " + name;
        }
        observed.insert(observed.end(), stores.begin(), stores.end());
    }
    if (analysis.exhausted) { refusal = "host contract analysis work budget exhausted"; }
    if (refusal.empty()) {
        for (const HostSlotReport & report : reports) {
            if (!report.reason.empty()) {
                refusal = report.binding + "." + report.property + ": " + report.reason;
                break;
            }
        }
    }
    std::vector<HostScalarGlobalRead> scalarReads;
    std::vector<HostObjectGlobalRead> objectReads;
    llvm::SmallVector<ctjs::CreateObjectOp> objectOrigins;
    llvm::DenseSet<mlir::Operation *> seenOrigins;
    if (refusal.empty()) {
        // Reconstruct named-key origins from completed callable edges only.
        // Failed/provisional family attempts cannot publish global evidence.
        for (const HostCallableEdge & call : analysis.checkedCalls) {
            for (const HostMethodArgument & argument : call.arguments) {
                if (!analysis.step()) { break; }
                auto read = argument.actual.getDefiningOp<ctjs::LoadGlobalOp>();
                if (!argument.object || !read) { continue; }
                const auto edge = analysis.objectGlobalRead(read);
                if (!edge || edge->object != argument.object) {
                    refusal = "named object key lacks its completed source initialization";
                    break;
                }
                if (seenOrigins.insert(argument.object).second) {
                    objectOrigins.push_back(argument.object);
                }
            }
            if (analysis.exhausted || !refusal.empty()) { break; }
        }
        if (analysis.exhausted) { refusal = "host contract analysis work budget exhausted"; }
    }
    if (refusal.empty()) {
        // Publish predecessor loads too, even when only an alias is passed
        // to a method. They still execute to initialize its owning binding.
        for (ctjs::CreateObjectOp object : objectOrigins) {
            if (!analysis.step()) { break; }
            const auto edges = analysis.objectGlobalReads(object);
            if (!edges) {
                refusal = "named object key reads lack their completed source initializations";
                break;
            }
            for (const HostObjectGlobalRead & edge : *edges) {
                if (!analysis.step()) { break; }
                objectReads.push_back(edge);
            }
        }
        if (analysis.exhausted) { refusal = "host contract analysis work budget exhausted"; }
    }
    if (refusal.empty()) {
        module.walk([&](ctjs::LoadGlobalOp read) {
            if (analysis.exhausted) { return; }
            if (auto edge = analysis.scalarGlobalRead(read)) {
                scalarReads.push_back(std::move(*edge));
            }
        });
        if (analysis.exhausted) { refusal = "host contract analysis work budget exhausted"; }
    }
    if (refusal.empty()) {
        checkedCalls = std::move(analysis.checkedCalls);
        checkedScalarReads = std::move(scalarReads);
        checkedObjectReads = std::move(objectReads);
    }
}

const HostSlotEdge * HostContractAnalysis::property(ctjs::GetPropertyOp read) const {
    if (!proved()) { return nullptr; }
    for (const HostSlotReport & report : reports) {
        for (const HostSlotEdge & edge : report.edges) {
            if (edge.read == read) { return &edge; }
        }
    }
    return nullptr;
}

const HostCallableEdge * HostContractAnalysis::callable(mlir::Operation * call) const {
    for (const HostCallableEdge & edge : checkedCalls) {
        if (edge.call == call) { return &edge; }
    }
    return nullptr;
}

const HostScalarGlobalRead * HostContractAnalysis::scalarRead(ctjs::LoadGlobalOp read) const {
    for (const HostScalarGlobalRead & edge : checkedScalarReads) {
        if (edge.read == read) { return &edge; }
    }
    return nullptr;
}

const HostObjectGlobalRead * HostContractAnalysis::objectRead(ctjs::LoadGlobalOp read) const {
    for (const HostObjectGlobalRead & edge : checkedObjectReads) {
        if (edge.read == read) { return &edge; }
    }
    return nullptr;
}

} // namespace ctcompile::ctnative
