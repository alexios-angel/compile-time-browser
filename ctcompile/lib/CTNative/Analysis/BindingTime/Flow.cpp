#include "Analysis.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {
namespace {
using binding_time_detail::fact;
using binding_time_detail::flow;
flow merge(flow left, const flow & right, bool control) {
    for (const auto & item : right.heaps) {
        auto found = left.heaps.find(item.first);
        if (found == left.heaps.end()) {
            left.heaps[item.first] = item.second;
            continue;
        }
        auto & target = found->second;
        target.dynamic |= item.second.dynamic || !control;
        target.contents = binding_time_detail::join(target.contents, item.second.contents, control);
        for (const auto & edge : item.second.retained) {
            const bool present = llvm::any_of(target.retained, [&](const fact & other) {
                return edge.domain == other.domain && edge.time == other.time &&
                       edge.literal == other.literal && edge.nodes == other.nodes;
            });
            if (!present) { target.retained.push_back(edge); }
        }
        llvm::SmallVector<std::string> remove;
        for (auto & field : target.fields) {
            const auto other = item.second.fields.find(field.getKey());
            if (other == item.second.fields.end()) {
                remove.push_back(field.getKey().str());
            } else {
                field.second = binding_time_detail::join(field.second, other->second, control);
            }
        }
        for (const auto & field : remove) { target.fields.erase(field); }
    }
    return left;
}
llvm::SmallVector<fact> yielded(mlir::Region & region, const flow & state) {
    llvm::SmallVector<fact> result;
    if (region.empty()) { return result; }
    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.back().getTerminator());
    if (yield) {
        for (mlir::Value input : yield.getOperands()) {
            result.push_back(state.values.lookup(input));
        }
    }
    return result;
}
} // namespace

bool BindingTimeAnalysis::Impl::region(mlir::Region & body, flow & state, bool control,
                                       llvm::ArrayRef<fact> inputs) {
    bool all = true;
    for (mlir::Block & block : body) {
        const bool entry = &block == &body.front();
        const bool blockControl = control && entry && block.hasNoPredecessors();
        if (!blockControl) { binding_time_detail::invalidate(state); }
        for (auto [i, arg] : llvm::enumerate(block.getArguments())) {
            fact input = entry && i < inputs.size() ? inputs[i] : fact{};
            state.values[arg] = input;
            facts[arg] = input;
        }
        for (mlir::Operation & op : block) {
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                const bool known =
                    blockControl &&
                    state.values.lookup(branch.getCondition()).time == BindingTime::Static;
                flow yes = state, no = state;
                const bool yesStatic = region(branch.getThenRegion(), yes, known);
                const bool noStatic = region(branch.getElseRegion(), no, known);
                const bool eligible = known && yesStatic && noStatic;
                auto yesValues = yielded(branch.getThenRegion(), yes);
                auto noValues = yielded(branch.getElseRegion(), no);
                state = merge(std::move(yes), no, known);
                for (auto [i, output] : llvm::enumerate(op.getResults())) {
                    fact result = i < yesValues.size() && i < noValues.size()
                                      ? binding_time_detail::join(yesValues[i], noValues[i], known)
                                      : fact{};
                    state.values[output] = result;
                    facts[output] = result;
                }
                decisions[&op] = {eligible, eligible
                                                ? "static structured branch"
                                                : "branch requires runtime control or effects"};
                all &= eligible;
                continue;
            }
            if (op.getNumRegions() != 0) {
                // Loops and unknown region-bearing operations need an invariant
                // across iterations. Until proved, both their control and memory
                // effects stay dynamic; constants inside retain known SSA values.
                for (mlir::Region & nested : op.getRegions()) {
                    (void)region(nested, state, false);
                }
                binding_time_detail::invalidate(state);
                for (mlir::Value output : op.getResults()) {
                    state.values[output] = {};
                    facts[output] = {};
                }
                decisions[&op] = {false, "region needs a runtime control or loop invariant"};
                all = false;
                continue;
            }
            all &= operation(&op, state, blockControl);
        }
    }
    return all;
}
} // namespace ctcompile::ctnative
