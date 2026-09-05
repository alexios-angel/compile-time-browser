#include "Driver.h"

#include "../Specialization/Candidates.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative::supercompilation {

Driver::Driver(mlir::ModuleOp module, ctjs::FuncOp source, Limits limits)
    : module(module), source(source), limits(limits), budget{limits.steps},
      drafts(mlir::ModuleOp::create(module.getLoc())) {}

ctjs::FuncOp Driver::drive(Bindings bindings, llvm::SmallVector<unsigned> history) {
    if (!problem.empty() || !llvm::any_of(bindings, [](auto attr) { return bool(attr); })) {
        return {};
    }
    // Folding is semantic equality modulo dynamic argument names. The whistle
    // is a different relation and must never authorize a residual backedge.
    for (const auto & configuration : configurations) {
        if (configuration.bindings == bindings) {
            ++stats.folds;
            return configuration.residual;
        }
    }
    for (unsigned ancestor : history) {
        if (embeds(configurations[ancestor].bindings, bindings)) {
            ++stats.whistles;
            return {}; // Retain the generic recursive call, including arguments.
        }
    }
    if (configurations.size() >= limits.contexts) {
        problem = "configuration budget exhausted; retained identity alternative";
        return {};
    }
    auto residual = llvm::cast<ctjs::FuncOp>(source->clone());
    residual.walk([](mlir::Operation * op) {
        llvm::SmallVector<mlir::StringAttr> remove;
        for (auto attr : op->getAttrs()) {
            if (attr.getName().strref().starts_with("ctnative.")) {
                remove.push_back(attr.getName());
            }
        }
        for (auto name : remove) { op->removeAttr(name); }
    });
    std::string name;
    do {
        name = (source.getSymName() + "__supercompiled_" + std::to_string(nextName++)).str();
    } while (mlir::SymbolTable::lookupSymbolIn(module, name) ||
             mlir::SymbolTable::lookupSymbolIn(*drafts, name));
    residual.setSymName(name);
    // Isolate local driving from other promises. Return summaries from another
    // in-progress configuration must not rewrite a currently visited branch.
    mlir::OwningOpRef<mlir::ModuleOp> local(mlir::ModuleOp::create(module.getLoc()));
    local->getBody()->push_back(residual);
    const auto index = static_cast<unsigned>(configurations.size());
    configurations.push_back({bindings, residual});
    history.push_back(index);
    mlir::OpBuilder at(&residual.getBody().front(), residual.getBody().front().begin());
    for (auto [i, literal] : llvm::enumerate(bindings)) {
        if (!literal) { continue; }
        auto constant = ctjs::ConstantOp::create(at, residual.getLoc(), literal);
        residual.getBody()
            .front()
            .getArgument(static_cast<unsigned>(i))
            .replaceAllUsesWith(constant.getResult());
    }
    symbolic::Analysis facts(*local, budget);
    facts.run();
    const auto changes = symbolic::rewrite(*local, facts, budget);
    stats.expressions += changes.expressions;
    stats.branches += changes.branches;
    if (budget.exhausted) {
        problem = "driving budget exhausted; retained identity alternative";
        return {};
    }
    residual->moveBefore(drafts->getBody(), drafts->getBody()->end());
    llvm::SmallVector<ctjs::CallDirectOp> calls;
    residual.walk([&](ctjs::CallDirectOp call) { calls.push_back(call); });
    for (auto call : calls) {
        auto next = drive(specialization::staticArguments(call, source), history);
        if (next) { call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(next.getSymNameAttr())); }
        if (!problem.empty()) { return {}; }
    }
    return residual;
}

bool Driver::build(llvm::ArrayRef<ctjs::CallDirectOp> roots) {
    if (limits.steps == 0) {
        problem = "driving budget exhausted; retained identity alternative";
        return false;
    }
    // No IR is redirected before the entire alternative fits its budgets.
    for (auto call : roots) {
        auto residual = drive(specialization::staticArguments(call, source), {});
        stats.steps = budget.steps;
        stats.contexts = static_cast<unsigned>(configurations.size());
        if (!problem.empty()) { return false; }
        if (residual) { redirects.emplace_back(call, residual); }
    }
    stats.steps = budget.steps;
    stats.contexts = static_cast<unsigned>(configurations.size());
    if (redirects.empty() || (!stats.expressions && !stats.branches)) {
        problem = "identity alternative selected: no proved local simplification";
        return false;
    }
    uint64_t cost = 0;
    for (auto & configuration : configurations) {
        cost += specialization::bodySize(configuration.residual);
    }
    // The generic definition remains. Every residual operation is therefore
    // added code, not a replacement that can be discounted by source size.
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
