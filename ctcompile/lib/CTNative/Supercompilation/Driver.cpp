//===- Driver.cpp - Supercompilation driver -------------------------------===//
//
// Implements supercompilation::Driver (see Driver.h).
//
//===----------------------------------------------------------------------===//

#include "Driver.h"

#include "../Specialization/Candidates.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative::supercompilation {

constexpr const char * kDrivingExhausted =
    "driving budget exhausted; retained identity alternative";

Driver::Driver(mlir::ModuleOp module, ctjs::FuncOp source, Limits limits)
    : module(module), source(source), limits(limits), budget{limits.steps},
      drafts(mlir::ModuleOp::create(module.getLoc())) {}

bool Driver::spend() {
    if (budget.spend()) { return true; }
    problem = kDrivingExhausted;
    return false;
}

/// Returns the residual function for a call with `bindings`, driving a new
/// one if needed. `history` holds the indices of the configurations on the
/// current path, for the whistle. Returns null to keep the generic call.
ctjs::FuncOp Driver::drive(Bindings bindings, llvm::SmallVector<unsigned> history) {
    if (!problem.empty() || !llvm::any_of(bindings, [](auto attr) { return bool(attr); })) {
        return {};
    }
    if (!spend()) { return {}; }
    // Folding: a configuration whose bindings equal an earlier one reuses
    // that residual function. The whistle below is a weaker relation, so it
    // must never by itself make a call point back at an earlier residual.
    for (const auto & configuration : configurations) {
        if (!spend()) { return {}; }
        if (configuration.bindings == bindings) {
            ++stats.folds;
            return configuration.residual;
        }
    }
    for (unsigned ancestor : history) {
        if (!spend()) { return {}; }
        if (embeds(configurations[ancestor].bindings, bindings)) {
            ++stats.whistles;
            auto generalized = commonBindings(configurations[ancestor].bindings, bindings);
            for (auto ignored : generalized) {
                (void)ignored;
                if (!spend()) { return {}; }
            }
            if (!llvm::any_of(generalized, [](auto attr) { return bool(attr); })) {
                // No useful constant binding remains; keep the generic call.
                return {};
            }
            // Generalize this child; never rewrite an ancestor's already-driven
            // body. The call's operands still pass the forgotten values, in
            // their original positions, to a freshly driven configuration.
            auto residual = drive(std::move(generalized), std::move(history));
            if (residual) { ++stats.generalizations; }
            return residual;
        }
    }
    if (configurations.size() >= limits.contexts) {
        problem = "configuration budget exhausted; retained identity alternative";
        return {};
    }
    auto residual = specialization::cloneWithBindings(source, bindings);
    std::string name;
    do {
        name = (source.getSymName() + "__supercompiled_" + std::to_string(nextName++)).str();
    } while (mlir::SymbolTable::lookupSymbolIn(module, name) ||
             mlir::SymbolTable::lookupSymbolIn(*drafts, name));
    residual.setSymName(name);
    // Simplify the new residual in a module of its own, so return-value facts
    // derived from other configurations still being driven cannot rewrite it.
    mlir::OwningOpRef<mlir::ModuleOp> local(mlir::ModuleOp::create(module.getLoc()));
    local->getBody()->push_back(residual);
    const auto index = static_cast<unsigned>(configurations.size());
    configurations.push_back({bindings, residual});
    history.push_back(index);
    symbolic::Analysis facts(*local, budget);
    facts.run();
    const auto changes = symbolic::rewrite(*local, facts, budget);
    stats.expressions += changes.expressions;
    stats.branches += changes.branches;
    if (budget.exhausted) {
        problem = kDrivingExhausted;
        return {};
    }
    residual->moveBefore(drafts->getBody(), drafts->getBody()->end());
    llvm::SmallVector<ctjs::CallDirectOp> calls;
    residual.walk([&](ctjs::CallDirectOp call) { calls.push_back(call); });
    for (auto call : calls) {
        auto recursive = specialization::staticArguments(call, source);
        for (auto [i, binding] : llvm::enumerate(bindings)) {
            if (!spend()) { return {}; }
            // Once an argument position is dynamic it stays dynamic down this
            // path, even if a recursive call passes a literal there again;
            // otherwise a literal could restart unbounded specialisation.
            if (!binding) { recursive[i] = {}; }
        }
        auto next = drive(std::move(recursive), history);
        if (next) { call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(next.getSymNameAttr())); }
        if (!problem.empty()) { return {}; }
    }
    return residual;
}

bool Driver::build(llvm::ArrayRef<ctjs::CallDirectOp> roots) {
    if (limits.steps == 0) {
        problem = kDrivingExhausted;
        return false;
    }
    const auto record = [&] {
        stats.steps = static_cast<unsigned>(limits.steps - budget.remaining);
        stats.contexts = static_cast<unsigned>(configurations.size());
    };
    // No call is redirected here; commit() does that once everything fits the
    // budgets.
    for (auto call : roots) {
        auto residual = drive(specialization::staticArguments(call, source), {});
        record();
        if (!problem.empty()) { return false; }
        if (residual) { redirects.emplace_back(call, residual); }
    }
    record();
    if (redirects.empty() || (!stats.expressions && !stats.branches)) {
        problem = "identity alternative selected: no proved local simplification";
        return false;
    }
    uint64_t cost = 0;
    for (auto & configuration : configurations) {
        cost += specialization::bodySize(configuration.residual);
    }
    // The generic definition remains, so every residual operation is added
    // code; the source's size is not subtracted from the cost.
    if (cost > limits.residualOps || cost > limits.growth) {
        problem = "residual size budget exhausted; retained identity alternative";
        return false;
    }
    stats.residualOps = static_cast<unsigned>(cost);
    return true;
}

void Driver::commit() {
    for (auto & configuration : configurations) {
        configuration.residual->moveBefore(module.getBody(), module.getBody()->end());
    }
    for (auto [call, residual] : redirects) {
        call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(residual.getSymNameAttr()));
    }
}

} // namespace ctcompile::ctnative::supercompilation
