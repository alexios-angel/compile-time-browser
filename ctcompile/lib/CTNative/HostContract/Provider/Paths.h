//===- Paths.h - Shared region walker for provider methods ------*- C++ -*-===//
//
// Declares providerRegion, the structured-control-flow walker that the
// provider-method readers (reads, mutations, callbacks) share.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Prefix/Analysis.h"

namespace ctcompile::ctnative::host_detail {

/// Runs `region` with `reader`, following the one known arm of each scf.if,
/// and returns how it completed (returned or yielded, with values). Unknown on
/// anything the reader cannot model. Provider paths only find normal results:
/// they have no hook to rewrite branches or publish heap facts, so they never
/// specialize a shared method body. A reader with `primitive(prefixValue)` (a
/// callback reader) is primitive-only: each region entry costs a step,
/// returned, yielded and branch values pass through `primitive`, and a yielded
/// non-primitive stops the path. `stopped`, when given, receives the
/// operation the path stopped at. `depth` caps scf.if nesting at 64.
template <class Reader>
prefixAnalysis::completion providerRegion(Reader & reader, mlir::Region & region,
                                          prefixAnalysis::environment & values,
                                          mlir::Operation ** stopped = nullptr,
                                          unsigned depth = 0) {
    using completion = prefixAnalysis::completion;
    constexpr bool primitiveOnly = requires(Reader & r) { r.primitive(prefixValue{}); };
    const auto carry = [&](mlir::Value value) {
        if constexpr (primitiveOnly) {
            return reader.primitive(values.lookup(value));
        } else {
            return values.lookup(value);
        }
    };
    if (stopped) { *stopped = region.getParentOp(); }
    if (depth > 64) { return {}; }
    if constexpr (primitiveOnly) {
        if (!reader.prefix.step()) { return {}; }
    }
    if (region.empty()) {
        return {completion::Kind::yielded, {}};
    }
    if (!llvm::hasSingleElement(region)) { return {}; }
    for (mlir::Operation & operation : region.front()) {
        if (stopped) { *stopped = &operation; }
        if (!reader.prefix.step()) { return {}; }
        if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
            return {completion::Kind::returned, {carry(returned.getValue())}};
        }
        if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
            completion result{completion::Kind::yielded, {}};
            for (mlir::Value value : yield.getOperands()) {
                if (!reader.prefix.step()) { return {}; }
                auto known = carry(value);
                if (primitiveOnly && known.kind != prefixValue::Kind::primitive) { return {}; }
                result.values.push_back(known);
            }
            return result;
        }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            auto bit = prefixTruth(carry(branch.getCondition()));
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
