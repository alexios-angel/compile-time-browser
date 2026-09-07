#include "Prefix.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative::host_detail {

prefixAnalysis::prefixAnalysis(mlir::ModuleOp module, const HostContract & contract,
                               unsigned maxSteps, bool followPublication, bool followProviderReads,
                               bool followProviderMutations, bool followProviderDiagnostics,
                               bool followProviderCallbacks)
    : module(module), contract(contract), remaining(maxSteps), followPublication(followPublication),
      followProviderReads(followProviderReads), followProviderMutations(followProviderMutations),
      followProviderDiagnostics(followProviderDiagnostics),
      followProviderCallbacks(followProviderCallbacks), dominance(module) {
    refusal = initialBindingProblem(module, contract);
    module.walk([&](ctjs::StoreGlobalOp store) {
        if (step()) { initializers[store.getName()].push_back(store); }
    });
    module.walk([&](ctjs::FuncOp function) {
        if (!step()) { return; }
        auto index = functionIndex(function);
        if (!index || !functions.try_emplace(*index, function).second) {
            refusal = "ambiguous or missing numeric source function identity";
        }
    });
    module.walk([&](mlir::Operation * operation) {
        if (!step()) { return; }
        ++operationCount;
        if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
            if (made.getFunction() >= 0) {
                ++creations[functions.lookup(static_cast<unsigned>(made.getFunction()))];
            }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
            auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                call, call.getCalleeAttr());
            if (target) { callers[target].push_back(call); }
        }
        if (operation->getName().getStringRef() == "ctjs.pass_new_target") {
            pendingNewTarget.insert(operation->getParentOfType<ctjs::FuncOp>());
        }
        if (llvm::isa<ctjs::MakeArgumentsOp, ctjs::DynamicImportOp>(operation)) {
            reflective = true;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            reflective |= load.getName() == "eval" || load.getName() == "Function" ||
                          load.getName() == "Reflect" || load.getName() == "Object";
        }
        mlir::Value propertyKey;
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            propertyKey = read.getKey();
        }
        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            propertyKey = write.getKey();
        }
        if (propertyKey) {
            auto literal = propertyKey.getDefiningOp<ctjs::ConstantOp>();
            auto text =
                literal ? llvm::dyn_cast<ctjs::StringAttr>(literal.getValue()) : ctjs::StringAttr{};
            const bool number = literal && llvm::isa<ctjs::NumberAttr>(literal.getValue());
            reflective |=
                !number && (!text || !ordinaryKey(text.getValue()) || text.getValue() == "caller" ||
                            text.getValue() == "callee" || text.getValue() == "arguments" ||
                            text.getValue() == "eval" || text.getValue() == "Function" ||
                            text.getValue() == "Reflect" || text.getValue() == "Object" ||
                            text.getValue() == "getOwnPropertyDescriptor" ||
                            text.getValue() == "getOwnPropertyDescriptors" ||
                            text.getValue() == "getPrototypeOf" ||
                            text.getValue() == "setPrototypeOf" || text.getValue() == "ownKeys");
        }
        reflective |= operation->hasAttr("ctjs.skipped");
        if (auto function = llvm::dyn_cast<ctjs::FuncOp>(operation)) {
            reflective |= function.getBody().empty();
        }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
            if (llvm::is_contained(contract.absentBindings, store.getName()) ||
                llvm::is_contained(contract.undefinedBindings, store.getName())) {
                refusal = "source writes a fixed absent/undefined host binding";
            }
        }
    });
    for (const auto & name : contract.absentBindings) {
        globals[name] = {prefixValue::Kind::absent, {}, {}, 0};
    }
    for (const auto & name : contract.undefinedBindings) {
        globals[name] = prefixValue::constant(ctjs::UndefinedAttr::get(module.getContext()));
    }
}

bool prefixAnalysis::step() {
    return spend(1);
}

bool prefixAnalysis::spend(unsigned count) {
    if (count > remaining) {
        exhausted = true;
        remaining = 0;
        return false;
    }
    remaining -= count;
    return true;
}

prefixValue prefixAnalysis::stop(mlir::Operation * operation, llvm::StringRef reason) {
    if (boundary.empty()) {
        boundary = (reason + " at `" + operation->getName().getStringRef() + "`").str();
    }
    return {};
}

ctjs::FuncOp prefixAnalysis::target(prefixValue value) {
    if (value.kind != prefixValue::Kind::closure || !value.made || value.made.getFunction() < 0) {
        return {};
    }
    return functions.lookup(static_cast<unsigned>(value.made.getFunction()));
}

bool prefixAnalysis::uniqueContext(ctjs::FuncOp function, ctjs::CallDirectOp call,
                                   ctjs::CreateClosureOp producer) {
    // The original body will be rewritten, so the actually executed call is
    // insufficient: all source producers/uses must exclude a second invocation
    // or later escape. Each caller already passed the same check on descent.
    auto & sites = callers[function];
    if (!step() || sites.size() != 1 || sites.front() != call || creations.lookup(function) != 1 ||
        !producer || reflective || function.getBody().empty() ||
        function.getBody().front().getNumArguments() < 3 ||
        pendingNewTarget.contains(call->getParentOfType<ctjs::FuncOp>())) {
        return false;
    }
    // A global declaration is publication too: a later this.f/globalThis.f or
    // unknown effect can invoke it without a load_global use. This in-place
    // consumer accepts only an unexposed literal closure at its sole call.
    for (mlir::OpOperand & use : producer.getResult().getUses()) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
        if (use.getOwner() != call || use.getOperandNumber() != 2) { return false; }
    }
    // A named IIFE can retrieve itself through its implicit callee argument
    // even when the outer create_closure value has no other use.
    for (mlir::Operation * user : function.getBody().front().getArgument(2).getUsers()) {
        if (!step() || !llvm::isa<ctjs::RootOp>(user)) { return false; }
    }
    bool initialized = true;
    module.walk([&](ctjs::LoadGlobalOp load) {
        if (!step() || !initializedGlobal(load)) { initialized = false; }
    });
    if (!initialized) { return false; }
    return true;
}

prefixAnalysis::completion prefixAnalysis::function(ctjs::FuncOp function,
                                                    llvm::ArrayRef<prefixValue> arguments,
                                                    unsigned depth) {
    if (!step()) { return {}; }
    if (depth > 64 || !visited.insert(function).second) {
        stop(function, "repeated or recursive function context");
        return {};
    }
    if (!llvm::hasSingleElement(function.getBody()) || function.getUpvalueCount() != 0 ||
        function->hasAttr("ctjs.skipped") ||
        function.getBody().front().getNumArguments() != arguments.size()) {
        stop(function, "function needs one imported capture-free block and exact arguments");
        return {};
    }
    environment values;
    for (auto [argument, value] : llvm::zip(function.getBody().front().getArguments(), arguments)) {
        values[argument] = value;
        observedValues[argument] = value;
    }
    return region(function.getBody(), values, depth);
}

prefixAnalysis::completion prefixAnalysis::region(mlir::Region & region, environment & values,
                                                  unsigned depth) {
    if (region.empty()) {
        return {completion::Kind::yielded, {}};
    }
    if (!llvm::hasSingleElement(region)) {
        stop(region.getParentOp(), "unstructured prefix control");
        return {};
    }
    for (mlir::Operation & operation : region.front()) {
        if (!step()) { return {}; }
        if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
            return {completion::Kind::returned, {values.lookup(returned.getValue())}};
        }
        if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
            completion result{completion::Kind::yielded, {}};
            for (mlir::Value value : yield.getOperands()) {
                result.values.push_back(values.lookup(value));
            }
            return result;
        }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            const auto selected = prefixTruth(values.lookup(branch.getCondition()));
            if (!selected) {
                stop(branch, "unknown branch condition");
                return {};
            }
            // After a provider summary, source observer branches are used
            // only to discover the next actual call. They remain runtime.
            if (!discoveryOnly.contains(branch->getParentOfType<ctjs::FuncOp>())) {
                branches.push_back({branch, *selected});
            }
            auto yielded = this->region(branch->getRegion(*selected ? 0u : 1u), values, depth);
            if (yielded.kind != completion::Kind::yielded ||
                yielded.values.size() != branch.getNumResults()) {
                return {};
            }
            for (auto [result, value] : llvm::zip(branch.getResults(), yielded.values)) {
                values[result] = value;
                observedValues[result] = value;
            }
            continue;
        }
        auto result = this->operation(&operation, values, depth);
        if (!boundary.empty() || exhausted) { return {}; }
        if (operation.getNumResults() == 1) {
            values[operation.getResult(0)] = result;
            observedValues[operation.getResult(0)] = result;
        }
    }
    stop(region.getParentOp(), "prefix region has no supported terminator");
    return {};
}

} // namespace ctcompile::ctnative::host_detail

namespace ctcompile::ctnative {

HostEntryPrefixAnalysis::HostEntryPrefixAnalysis(mlir::ModuleOp module,
                                                 const HostContract & contract, unsigned maxSteps,
                                                 bool followPublication, bool followProviderReads,
                                                 bool followProviderMutations,
                                                 bool followProviderDiagnostics,
                                                 bool followProviderCallbacks) {
    if (followProviderCallbacks && !followProviderDiagnostics) {
        refusal = "provider callbacks require follow-provider-diagnostics";
        return;
    }
    if (followProviderDiagnostics && !followProviderMutations) {
        refusal = "provider diagnostics require follow-provider-mutations";
        return;
    }
    if (followProviderMutations && (!followProviderReads || !followPublication)) {
        refusal = "provider mutations require follow-provider-reads and follow-publication";
        return;
    }
    if (followProviderReads && !followPublication) {
        refusal = "provider reads require follow-publication";
        return;
    }
    if (hostContractFingerprint(module) != contract.moduleSha256) {
        refusal = "host prefix module fingerprint mismatch";
        return;
    }
    host_detail::prefixAnalysis analysis(module, contract, maxSteps, followPublication,
                                         followProviderReads, followProviderMutations,
                                         followProviderDiagnostics, followProviderCallbacks);
    auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    if (!entry || entry.getBody().empty() || entry.getBody().front().getNumArguments() != 3 ||
        entry.getUpvalueCount() != 0 || !analysis.callers[entry].empty() ||
        analysis.creations.lookup(entry) != 0) {
        refusal = "host prefix requires an unreferenced closed script entry";
        return;
    }
    if (analysis.refusal.empty() && !analysis.exhausted) {
        const host_detail::prefixValue unknown;
        const host_detail::prefixValue receiver =
            contract.classicScriptRealm
                ? host_detail::prefixValue{host_detail::prefixValue::Kind::realm, {}, {}, 0}
                : unknown;
        analysis.function(entry, {receiver, unknown, unknown}, 0);
        llvm::DenseSet<mlir::Operation *> changed, stack;
        for (auto proof : analysis.branches) {
            changed.insert(proof.operation->getParentOfType<ctjs::FuncOp>());
        }
        for (auto proof : analysis.calls) {
            auto caller = proof.operation->getParentOfType<ctjs::FuncOp>();
            // Naming a callee in the unreferenced script entry preserves the
            // actual call and does not specialize a reusable callee body.
            // Helper bodies still need the full continuation identity check.
            if (!followPublication || caller != entry) { changed.insert(caller); }
        }
        for (auto * operation : changed) {
            if (!analysis.identitySafeFunction(llvm::cast<ctjs::FuncOp>(operation), stack)) {
                analysis.branches.clear();
                analysis.calls.clear();
                analysis.factories.clear();
                analysis.publications.clear();
                analysis.reads.clear();
                analysis.providerCalls.clear();
                analysis.boundary = "continuation may expose the active callable identity";
                break;
            }
        }
    }
    refusal = analysis.exhausted ? "host prefix analysis work budget exhausted" : analysis.refusal;
    stopped = analysis.boundary;
    providerStopped = analysis.providerBoundary;
    if (refusal.empty()) {
        branchProofs = std::move(analysis.branches);
        callProofs = std::move(analysis.calls);
        factoryProofs = std::move(analysis.factories);
        publicationProofs = std::move(analysis.publications);
        readProofs = std::move(analysis.reads);
        providerProofs = std::move(analysis.providerCalls);
    }
}

} // namespace ctcompile::ctnative
