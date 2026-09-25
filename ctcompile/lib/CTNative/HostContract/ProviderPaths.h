#pragma once

#include "Prefix.h"

namespace ctcompile::ctnative::host_detail {

// Provider paths discover normal results only. They have no branch-rewrite or
// ordinary heap-publication hook, and cannot specialize a reusable method body.
template <class Reader>
prefixAnalysis::completion providerRegion(Reader & reader, mlir::Region & region,
                                          prefixAnalysis::environment & values,
                                          mlir::Operation ** stopped = nullptr,
                                          unsigned depth = 0) {
    using completion = prefixAnalysis::completion;
    if (stopped) { *stopped = region.getParentOp(); }
    if (depth > 64) { return {}; }
    if (region.empty()) {
        return {completion::Kind::yielded, {}};
    }
    if (!llvm::hasSingleElement(region)) { return {}; }
    for (mlir::Operation & operation : region.front()) {
        if (stopped) { *stopped = &operation; }
        if (!reader.prefix.step()) { return {}; }
        if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
            return {completion::Kind::returned, {values.lookup(returned.getValue())}};
        }
        if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
            completion result{completion::Kind::yielded, {}};
            for (mlir::Value value : yield.getOperands()) {
                if (!reader.prefix.step()) { return {}; }
                result.values.push_back(values.lookup(value));
            }
            return result;
        }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            auto bit = prefixTruth(values.lookup(branch.getCondition()));
            if (!bit) { return {}; }
            auto selected = providerRegion(reader, branch->getRegion(*bit ? 0u : 1u), values,
                                           stopped, depth + 1);
            if (selected.kind == completion::Kind::returned) { return selected; }
            if (selected.kind != completion::Kind::yielded ||
                selected.values.size() != branch.getNumResults()) {
                return {};
            }
            for (auto [result, value] : llvm::zip(branch.getResults(), selected.values)) {
                if (!reader.prefix.step()) { return {}; }
                values[result] = value;
            }
            continue;
        }
        if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) { continue; }
        const auto value = reader.operation(&operation, values);
        if (value.kind == prefixValue::Kind::unknown || operation.getNumResults() > 1) {
            return {};
        }
        if (operation.getNumResults() == 1) { values[operation.getResult(0)] = value; }
    }
    return {};
}

} // namespace ctcompile::ctnative::host_detail
