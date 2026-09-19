#include "Proof.hpp"

namespace ctcompile::ctnative::dom_source_detail {

bool DOMSource::normalizeCompletion(ctjs::FuncOp function) {
    bool dispatch = false;
    const auto walked = function.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        dispatch |= llvm::isa<mlir::scf::IndexSwitchOp, mlir::ub::PoisonOp>(operation);
        return mlir::WalkResult::advance();
    });
    if (walked.wasInterrupted()) { return false; }
    if (!dispatch) { return true; }

    // Inactive poison slots need completion proof even without a switch.
    // Use the same exact continuation proof before helper expansion.

    // Keep the original body until every completion path has been copied.
    // A yield supplies the exact values for its continuation; inactive
    // poison slots may be forwarded but must never be observed.
    struct Continuation {
        mlir::Block * block;
        mlir::Block::iterator next;
        mlir::ValueRange results;
        const Continuation * outer;
    };
    struct Terminal {
        enum Kind {
            returned,
            condition,
            yielded
        } kind = returned;
        llvm::SmallVector<mlir::Type> types;
        mlir::scf::WhileOp loop;
        llvm::SmallVector<unsigned> carried;
        llvm::SmallVector<bool> inactiveAfter;
    };
    using Results = std::optional<llvm::SmallVector<mlir::Value>>;
    mlir::Region normalized;
    auto & destination = normalized.emplaceBlock();
    mlir::IRMapping mapping;
    mlir::DominanceInfo dominance(function);
    llvm::DenseSet<mlir::Operation *> visited;
    for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
        if (!step()) { return false; }
        mapping.map(argument, destination.addArgument(argument.getType(), argument.getLoc()));
    }
    const auto emit = [&](auto && self, mlir::Block & body, mlir::Block::iterator begin,
                          mlir::IRMapping & values, mlir::OpBuilder & at,
                          const Continuation * continuation, Terminal & terminal,
                          unsigned depth) -> Results {
        if (depth >= 64) {
            refuse("DOM helper completion nesting is too deep");
            return {};
        }
        for (auto cursor = begin; cursor != body.end(); ++cursor) {
            if (!step()) { return {}; }
            auto & operation = *cursor;
            visited.insert(&operation);
            for (mlir::Value operand : operation.getOperands()) {
                if (!step()) { return {}; }
                if (!values.contains(operand) || !dominance.dominates(operand, &operation)) {
                    refuse("DOM helper completion operand has no preceding local definition");
                    return {};
                }
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
                if (!continuation && terminal.kind == Terminal::yielded) {
                    if (yield.getOperandTypes() != terminal.loop.getInits().getTypes()) {
                        refuse("DOM helper loop lost its carried result correspondence");
                        return {};
                    }
                    llvm::SmallVector<mlir::Value> result;
                    for (unsigned index : terminal.carried) {
                        if (!step()) { return {}; }
                        auto value = values.lookup(yield.getOperand(index));
                        if (value.getDefiningOp<mlir::ub::PoisonOp>()) {
                            refuse("DOM helper completion observes an inactive value");
                            return {};
                        }
                        result.push_back(value);
                    }
                    return result;
                }
                if (!continuation || yield.getOperandTypes() != continuation->results.getTypes()) {
                    refuse("DOM helper completion lost its result correspondence");
                    return {};
                }
                for (auto [result, operand] :
                     llvm::zip(continuation->results, yield.getOperands())) {
                    if (!step()) { return {}; }
                    values.map(result, values.lookupOrDefault(operand));
                }
                return self(self, *continuation->block, continuation->next, values, at,
                            continuation->outer, terminal, depth);
            }
            if (auto poison = llvm::dyn_cast<mlir::ub::PoisonOp>(operation)) {
                values.map(poison.getResult(), poison.getResult());
                continue;
            }
            if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(operation)) {
                if (continuation || terminal.kind != Terminal::condition ||
                    condition.getArgs().getTypes() != terminal.loop.getResultTypes()) {
                    refuse("DOM helper loop lost its condition result correspondence");
                    return {};
                }
                auto predicate = values.lookup(condition.getCondition());
                auto constant = predicate.getDefiningOp<mlir::arith::ConstantOp>();
                auto integer = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                        : mlir::IntegerAttr{};
                if (predicate.getDefiningOp<mlir::ub::PoisonOp>()) {
                    refuse("DOM helper completion observes an inactive value");
                    return {};
                }
                llvm::SmallVector<mlir::Value> result{predicate};
                for (auto [index, argument] : llvm::enumerate(condition.getArgs())) {
                    if (!step()) { return {}; }
                    auto value = values.lookup(argument);
                    if (value.getDefiningOp<mlir::ub::PoisonOp>()) {
                        const bool inactive =
                            integer &&
                            (integer.getValue().isZero()
                                 ? terminal.loop.getResult(static_cast<unsigned>(index)).use_empty()
                                 : terminal.inactiveAfter[index]);
                        if (!inactive || index >= terminal.loop.getBeforeArguments().size()) {
                            refuse("DOM helper completion observes an inactive value");
                            return {};
                        }
                        auto state = terminal.loop.getBeforeArguments()[index];
                        if (!values.contains(state) || state.getType() != value.getType()) {
                            refuse("DOM helper completion observes an inactive value");
                            return {};
                        }
                        // The predicate selects a destination with no use of
                        // this slot. Its existing state keeps the SCF tuple
                        // defined and preserves the carried representation.
                        value = values.lookup(state);
                    }
                    result.push_back(value);
                }
                return result;
            }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
                if (!loop.getBefore().hasOneBlock() || !loop.getAfter().hasOneBlock() ||
                    loop.getBefore().front().getArgumentTypes() != loop.getInits().getTypes() ||
                    loop.getAfter().front().getArgumentTypes() != loop.getResultTypes()) {
                    refuse("DOM helper loop lacks complete source regions");
                    return {};
                }
                Terminal before;
                before.kind = Terminal::condition;
                before.loop = loop;
                before.types.push_back(at.getI1Type());
                before.types.append(loop.getResultTypes().begin(), loop.getResultTypes().end());
                Terminal after;
                after.kind = Terminal::yielded;
                after.loop = loop;
                llvm::SmallVector<mlir::Value> initial;
                for (auto [index, argument] : llvm::enumerate(loop.getBeforeArguments())) {
                    if (!step()) { return {}; }
                    if (argument.use_empty()) { continue; }
                    auto value = values.lookup(loop.getInits()[index]);
                    if (value.getDefiningOp<mlir::ub::PoisonOp>()) {
                        refuse("DOM helper completion observes an inactive value");
                        return {};
                    }
                    after.carried.push_back(static_cast<unsigned>(index));
                    after.types.push_back(argument.getType());
                    initial.push_back(value);
                }
                for (mlir::BlockArgument argument : loop.getAfterArguments()) {
                    if (!step()) { return {}; }
                    bool inactive = true;
                    for (mlir::OpOperand & use : argument.getUses()) {
                        if (!step()) { return {}; }
                        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(use.getOwner());
                        inactive &= yield && yield->getBlock() == &loop.getAfter().front() &&
                                    !llvm::is_contained(after.carried, use.getOperandNumber());
                    }
                    before.inactiveAfter.push_back(inactive);
                }
                auto copied =
                    mlir::scf::WhileOp::create(at, loop.getLoc(), loop.getResultTypes(), initial);
                ++operationCount;
                for (auto [source, target] : llvm::zip(loop->getRegions(), copied->getRegions())) {
                    for (const auto & item : values.getValueMap()) {
                        (void)item;
                        if (!step()) { return {}; }
                    }
                    for (const auto & item : values.getOperationMap()) {
                        (void)item;
                        if (!step()) { return {}; }
                    }
                    mlir::IRMapping path(values);
                    auto & output = target.emplaceBlock();
                    const bool isBefore = &source == &loop.getBefore();
                    if (isBefore) {
                        for (unsigned index : after.carried) {
                            if (!step()) { return {}; }
                            auto argument = source.front().getArgument(index);
                            path.map(argument,
                                     output.addArgument(argument.getType(), argument.getLoc()));
                        }
                    } else {
                        for (mlir::BlockArgument argument : source.front().getArguments()) {
                            if (!step()) { return {}; }
                            path.map(argument,
                                     output.addArgument(argument.getType(), argument.getLoc()));
                        }
                    }
                    mlir::OpBuilder nested(&output, output.begin());
                    auto result = self(self, source.front(), source.front().begin(), path, nested,
                                       nullptr, isBefore ? before : after, depth + 1);
                    if (!result) { return {}; }
                    if (isBefore) {
                        mlir::scf::ConditionOp::create(nested, loop.getLoc(), result->front(),
                                                       mlir::ValueRange(*result).drop_front());
                    } else {
                        mlir::scf::YieldOp::create(nested, loop.getLoc(), *result);
                    }
                    ++operationCount;
                }
                values.map(loop.getResults(), copied.getResults());
                continue;
            }
            for (mlir::Value operand : operation.getOperands()) {
                if (!step()) { return {}; }
                if (values.lookupOrDefault(operand).getDefiningOp<mlir::ub::PoisonOp>()) {
                    refuse("DOM helper completion observes an inactive value");
                    return {};
                }
            }
            if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                if (continuation || terminal.kind != Terminal::returned) {
                    refuse("DOM helper completion returns inside a source region");
                    return {};
                }
                return llvm::SmallVector<mlir::Value>{values.lookupOrDefault(result.getValue())};
            }
            Continuation tail{&body, std::next(cursor), operation.getResults(), continuation};
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                auto condition = values.lookupOrDefault(branch.getCondition());
                auto constant = condition.getDefiningOp<mlir::arith::ConstantOp>();
                auto integer = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                        : mlir::IntegerAttr{};
                if (integer && integer.getType().isInteger(1)) {
                    auto & selected = integer.getValue().isZero() ? branch.getElseRegion()
                                                                  : branch.getThenRegion();
                    if (selected.empty() && branch.getNumResults() == 0) {
                        return self(self, body, tail.next, values, at, continuation, terminal,
                                    depth + 1);
                    }
                    if (!selected.hasOneBlock() || selected.front().getNumArguments()) {
                        refuse("DOM helper completion has an incomplete selected arm");
                        return {};
                    }
                    // Like a switch, select only a proved completion arm.
                    // The original-body census still requires every operation
                    // to be visited on some source path before publication.
                    return self(self, selected.front(), selected.front().begin(), values, at, &tail,
                                terminal, depth + 1);
                }
                // ponytail: duplicate bounded continuations, preserving their
                // effects in each arm. Large trees stop at the work budget.
                auto copied =
                    mlir::scf::IfOp::create(at, branch.getLoc(), terminal.types,
                                            values.lookupOrDefault(branch.getCondition()));
                ++operationCount;
                for (auto [source, target] :
                     llvm::zip(branch->getRegions(), copied->getRegions())) {
                    for (const auto & item : values.getValueMap()) {
                        (void)item;
                        if (!step()) { return {}; }
                    }
                    for (const auto & item : values.getOperationMap()) {
                        (void)item;
                        if (!step()) { return {}; }
                    }
                    mlir::IRMapping path(values);
                    target.emplaceBlock();
                    mlir::OpBuilder nested(&target.front(), target.front().begin());
                    Results returned;
                    if (source.empty() && branch.getNumResults() == 0) {
                        returned = self(self, body, tail.next, path, nested, continuation, terminal,
                                        depth + 1);
                    } else if (source.hasOneBlock() && source.front().getNumArguments() == 0) {
                        returned = self(self, source.front(), source.front().begin(), path, nested,
                                        &tail, terminal, depth + 1);
                    }
                    if (!returned) {
                        refuse("DOM helper completion has an incomplete branch");
                        return {};
                    }
                    mlir::scf::YieldOp::create(nested, branch.getLoc(), *returned);
                    ++operationCount;
                }
                return llvm::SmallVector<mlir::Value>(copied.getResults());
            }
            if (auto switcher = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(operation)) {
                auto selector = values.lookupOrDefault(switcher.getArg());
                if (auto cast = selector.getDefiningOp<mlir::arith::IndexCastUIOp>()) {
                    selector = cast.getIn();
                }
                auto constant = selector.getDefiningOp<mlir::arith::ConstantOp>();
                auto integer = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                        : mlir::IntegerAttr{};
                if (!integer || integer.getValue().isNegative() ||
                    integer.getValue().getActiveBits() > 63) {
                    refuse("DOM helper completion selector is not an exact constant");
                    return {};
                }
                auto * selected = &switcher.getDefaultRegion();
                for (auto [index, key] : llvm::enumerate(switcher.getCases())) {
                    if (!step()) { return {}; }
                    if (key == integer.getInt()) { selected = &switcher.getCaseRegions()[index]; }
                }
                if (!selected->hasOneBlock() || selected->front().getNumArguments()) {
                    refuse("DOM helper completion has an incomplete switch arm");
                    return {};
                }
                return self(self, selected->front(), selected->front().begin(), values, at, &tail,
                            terminal, depth + 1);
            }
            if (operation.getNumRegions() || operation.getNumSuccessors()) {
                refuse("DOM helper completion requires acyclic structured source");
                return {};
            }
            if (auto truncate = llvm::dyn_cast<mlir::arith::TruncIOp>(operation)) {
                auto operand = values.lookup(truncate.getIn());
                auto constant = operand.getDefiningOp<mlir::arith::ConstantOp>();
                auto integer = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                        : mlir::IntegerAttr{};
                if (integer && truncate.getType().isInteger(1)) {
                    auto value = mlir::arith::ConstantIntOp::create(
                        at, truncate.getLoc(), integer.getValue()[0] ? 1 : 0, 1);
                    values.map(truncate.getResult(), value.getResult());
                    ++operationCount;
                    continue;
                }
            }
            auto * copied = at.clone(operation, values);
            if (llvm::isa<mlir::arith::IndexCastUIOp, mlir::arith::CmpIOp>(operation)) {
                // Two-way completion switches lower to index casts and
                // integer comparisons. Reuse MLIR's exact arithmetic folds.
                llvm::SmallVector<mlir::Value> folded;
                if (mlir::succeeded(at.tryFold(copied, folded)) && !folded.empty()) {
                    values.map(operation.getResults(), folded);
                    copied->erase();
                }
            }
            ++operationCount;
        }
        refuse("DOM helper completion has no return or yield");
        return {};
    };
    mlir::OpBuilder at(function.getContext());
    at.setInsertionPointToStart(&destination);
    auto & body = function.getBody().front();
    Terminal terminal;
    terminal.types.append(function.getResultTypes().begin(), function.getResultTypes().end());
    auto result = emit(emit, body, body.begin(), mapping, at, nullptr, terminal, 0);
    if (!result) { return false; }
    const auto complete = function.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (operation != function && !visited.contains(operation)) {
            refuse("DOM helper completion contains unvisited source operations");
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    if (complete.wasInterrupted()) { return false; }
    ctjs::ReturnOp::create(at, function.getLoc(), result->front());
    // Only inert completion arithmetic is removed. Source effects remain
    // for the unchanged complete DOM/frame proof below.
    llvm::SmallVector<mlir::Operation *> arithmetic;
    const auto cleanup = normalized.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (llvm::isa<mlir::arith::ConstantOp, mlir::arith::IndexCastUIOp>(operation)) {
            arithmetic.push_back(operation);
        }
        return mlir::WalkResult::advance();
    });
    if (cleanup.wasInterrupted()) { return false; }
    for (mlir::Operation * operation : llvm::reverse(arithmetic)) {
        if (!step()) { return false; }
        if (operation->use_empty()) { operation->erase(); }
    }
    for (mlir::BlockArgument argument : body.getArguments()) {
        if (!step()) { return false; }
        auto found = stringInputs.find(argument);
        if (found == stringInputs.end()) { continue; }
        auto inputs = std::move(found->second);
        stringInputs.erase(found);
        stringInputs[mapping.lookup(argument)] = std::move(inputs);
    }
    function.getBody().takeBody(normalized);
    return true;
}

} // namespace ctcompile::ctnative::dom_source_detail
