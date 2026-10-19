//===- Analysis.cpp - Host entry prefix interpreter core --------*- C++ -*-===//
//
// The prefixAnalysis constructor (a census of the module), its budget
// helpers, the interpreter's function/region walk, uniqueContext, and the
// public HostEntryPrefixAnalysis constructor that runs it from the entry.
//
//===----------------------------------------------------------------------===//

#include "Analysis.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/IR/SymbolTable.h"

#include <limits>

namespace ctcompile::ctnative::host_detail {

prefixAnalysis::prefixAnalysis(mlir::ModuleOp module, const HostContract & contract,
                               unsigned maxSteps, HostPrefixFollow follow)
    : module(module), contract(contract), remaining(maxSteps),
      followPublication(follow.publication), followProviderReads(follow.providerReads),
      followProviderMutations(follow.providerMutations),
      followProviderDiagnostics(follow.providerDiagnostics),
      followProviderCallbacks(follow.providerCallbacks),
      followProviderObjects(follow.providerObjects), dominance(module) {
    refusal = initialBindingProblem(module, contract);
    if (!refusal.empty()) { return; }
    if (auto declarations = module->getAttrOfType<mlir::ArrayAttr>("ctjs.hoisted_vars")) {
        for (mlir::Attribute declaration : declarations) {
            if (!step()) { return; }
            declaredGlobals.insert(llvm::cast<mlir::StringAttr>(declaration).getValue());
        }
    }
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
    // Census of the module: count operations, closure creations and direct
    // callers, and set `reflective` on anything that could observe or re-bind
    // a function outside its known call sites.
    module.walk([&](mlir::Operation * operation) {
        if (!step()) { return; }
        ++operationCount;
        if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
            if (made.getFunction() >= 0) {
                ++creations[functions.lookup(static_cast<unsigned>(made.getFunction()))];
            }
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
            auto target = call.getTarget();
            if (target) { callers[target].push_back(call); }
        }
        if (operation->getName().getStringRef() == "ctjs.pass_new_target") {
            pendingNewTarget.insert(operation->getParentOfType<ctjs::FuncOp>());
        }
        if (llvm::isa<ctjs::MakeArgumentsOp, ctjs::DynamicImportOp>(operation)) {
            reflective = true;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            static const llvm::StringSet<> reflectiveGlobals{"eval", "Function", "Reflect",
                                                             "Object"};
            // The length test skips the set lookup for names longer than any
            // entry ("Function" is 8 characters).
            reflective |= load.getName().size() <= 8 && reflectiveGlobals.contains(load.getName());
        }
        if (auto key = ctjs::propertyKey(operation)) {
            auto text = ctjs::constantAttr<ctjs::StringAttr>(key);
            const bool number = static_cast<bool>(ctjs::constantAttr<ctjs::NumberAttr>(key));
            static const llvm::StringSet<> reflectiveProperties{"caller",
                                                                "callee",
                                                                "arguments",
                                                                "eval",
                                                                "Function",
                                                                "Reflect",
                                                                "Object",
                                                                "getOwnPropertyDescriptor",
                                                                "getOwnPropertyDescriptors",
                                                                "getPrototypeOf",
                                                                "setPrototypeOf",
                                                                "ownKeys"};
            // 25 is the longest entry ("getOwnPropertyDescriptors").
            reflective |=
                !number &&
                (!text || !ctjs::ordinaryKey(text.getValue()) ||
                 (text.getValue().size() <= 25 && reflectiveProperties.contains(text.getValue())));
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
        globals[name] = prefixValue::of(prefixValue::Kind::absent, 0);
    }
    for (const auto & name : contract.undefinedBindings) {
        globals[name] = prefixValue::constant(ctjs::UndefinedAttr::get(module.getContext()));
    }
}

bool prefixAnalysis::step() {
    return spend(1);
}

bool prefixAnalysis::spend(uint64_t count) {
    return chargeBudget(remaining, exhausted, count);
}

bool prefixAnalysis::spendText(llvm::StringRef text) {
    return text.size() <= std::numeric_limits<unsigned>::max() &&
           spend(static_cast<unsigned>(text.size()));
}

const HostPrefixCapture * prefixAnalysis::capture(const HostPrefixFactory & factory,
                                                  prefixValue closure, ctjs::FuncOp function,
                                                  ctjs::LoadUpvalueOp load) {
    if (load.getClosure() != function.getBody().front().getArgument(2) || load.getIndex() < 0) {
        return nullptr;
    }
    for (const auto & capture : factory.captures) {
        if (!step()) { return nullptr; }
        if (capture.closure == closure.made &&
            capture.index == static_cast<unsigned>(load.getIndex())) {
            return &capture;
        }
    }
    return nullptr;
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
    // The function's body will be specialised in place, so it is not enough
    // that this call is the one executed: every creation and use must rule out
    // a second invocation or a later escape. Each caller already passed the
    // same check on the way down.
    auto & sites = callers[function];
    if (!step() || sites.size() != 1 || sites.front() != call || creations.lookup(function) != 1 ||
        !producer || reflective || function.getBody().empty() ||
        function.getBody().front().getNumArguments() < 3 ||
        pendingNewTarget.contains(call->getParentOfType<ctjs::FuncOp>())) {
        return false;
    }
    // Storing the closure anywhere, even in a global, publishes it: a later
    // this.f, globalThis.f or unknown effect could call it without a
    // load_global. Accept only a closure literal used solely as this call's
    // callee.
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
    // Call depth is capped at 64 to bound the walk; `visited` analyses each
    // function at most once, which also stops recursion.
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
            // In functions marked discoveryOnly (after a provider summary), the
            // branch is followed only to find the next call; it is not recorded
            // as a proof and stays a runtime branch.
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

HostEntryPrefixAnalysis::HostEntryPrefixAnalysis(
    mlir::ModuleOp module, const HostContract & contract, unsigned maxSteps, bool followPublication,
    bool followProviderReads, bool followProviderMutations, bool followProviderDiagnostics,
    bool followProviderCallbacks, bool followProviderObjects) {
    if (followProviderObjects && !followProviderMutations) {
        refusal = "provider objects require follow-provider-mutations";
        return;
    }
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
    if (contract.provider == HostContract::Provider::ctbrowserDOMDataSession) {
        refusal = "host prefix does not support DOM Data input contracts";
        return;
    }
    host_detail::prefixAnalysis analysis(module, contract, maxSteps,
                                         {followPublication, followProviderReads,
                                          followProviderMutations, followProviderDiagnostics,
                                          followProviderCallbacks, followProviderObjects});
    auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    if (!entry || entry.getBody().empty() ||
        entry.getBody().front().getNumArguments() != ctjs::implicit_arguments ||
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
            // Resolving a call in the entry (which nothing else references)
            // keeps the call and does not specialise a reusable callee body,
            // so with followPublication the entry is exempt. Other callers
            // still need identitySafeFunction.
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
