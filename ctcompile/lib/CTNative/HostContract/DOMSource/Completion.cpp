#include "Proof.hpp"
#include "mlir/IR/Verifier.h"

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
    struct ExitProjection {
        mlir::Operation * dispatch = nullptr;
        mlir::arith::IndexCastUIOp cast;
        mlir::arith::CmpIOp comparison;
        mlir::arith::ConstantOp literal;
        mlir::Region * fallback = nullptr;
        llvm::SmallVector<mlir::Region *> arms;
        llvm::SmallVector<int64_t> cases;
        unsigned selector = 0;
        llvm::SmallVector<unsigned> outputs;
        llvm::SmallVector<bool> consumed;
        llvm::DenseMap<mlir::Region *, llvm::SmallVector<ctjs::BooleanAttr>> booleans{};
        bool effectful = false;
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
        ExitProjection exit;
    };
    using Results = std::optional<llvm::SmallVector<mlir::Value>>;
    mlir::Region normalized;
    auto & destination = normalized.emplaceBlock();
    mlir::IRMapping mapping;
    mlir::DominanceInfo dominance(function);
    llvm::DenseSet<mlir::Operation *> visited;
    bool normalReturn = false;
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
                    const auto types = terminal.loop
                                           ? mlir::TypeRange(terminal.loop.getInits().getTypes())
                                           : mlir::TypeRange(terminal.types);
                    if (yield.getOperandTypes() != types) {
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
                llvm::SmallVector<mlir::Value> arguments;
                for (mlir::Value argument : condition.getArgs()) {
                    if (!step()) { return {}; }
                    arguments.push_back(values.lookup(argument));
                }
                mlir::Region * selectedExit = nullptr;
                if (terminal.exit.dispatch) {
                    if (!integer || !integer.getType().isInteger(1)) {
                        refuse("DOM helper loop exit predicate is not an exact constant");
                        return {};
                    }
                    if (integer.getValue().isZero()) {
                        auto selector = arguments[terminal.exit.selector];
                        auto tag = selector.getDefiningOp<mlir::arith::ConstantOp>();
                        auto key = tag ? llvm::dyn_cast<mlir::IntegerAttr>(tag.getValue())
                                       : mlir::IntegerAttr{};
                        if (!key || key.getValue().isNegative() ||
                            key.getValue().getActiveBits() > 63) {
                            refuse("DOM helper loop exit selector is not an exact constant");
                            return {};
                        }
                        auto * selected = terminal.exit.fallback;
                        unsigned choice = static_cast<unsigned>(terminal.exit.arms.size());
                        for (auto [index, value] : llvm::enumerate(terminal.exit.cases)) {
                            if (!step()) { return {}; }
                            if (value == key.getInt()) {
                                selected = terminal.exit.arms[index];
                                choice = static_cast<unsigned>(index);
                            }
                        }
                        selectedExit = selected;
                        if (terminal.exit.effectful) {
                            // Keep only Boolean facts shared by every path to
                            // this exit, before padding any inactive tuple slots.
                            auto [facts, first] = terminal.exit.booleans.try_emplace(selected);
                            for (auto [index, value] : llvm::enumerate(arguments)) {
                                if (!step()) { return {}; }
                                auto literal = value.getDefiningOp<ctjs::ConstantOp>();
                                auto boolean =
                                    literal ? llvm::dyn_cast<ctjs::BooleanAttr>(literal.getValue())
                                            : ctjs::BooleanAttr{};
                                if (first) {
                                    facts->second.push_back(boolean);
                                } else if (facts->second[index] != boolean) {
                                    facts->second[index] = {};
                                }
                            }
                            mlir::Attribute discriminator =
                                terminal.exit.cases.size() == 1
                                    ? mlir::Attribute(ctjs::BooleanAttr::get(function.getContext(),
                                                                             choice == 0))
                                    : mlir::Attribute(ctjs::NumberAttr::get(
                                          function.getContext(), static_cast<double>(choice)));
                            arguments[terminal.exit.selector] =
                                ctjs::ConstantOp::create(at, condition.getLoc(), discriminator);
                            ++operationCount;
                        } else {
                            // Read the entire selected tuple before replacing any
                            // slots: one output may select another output's slot.
                            llvm::SmallVector<mlir::Value> projected;
                            for (mlir::Value operand :
                                 selected->front().getTerminator()->getOperands()) {
                                if (!step()) { return {}; }
                                auto source = llvm::cast<mlir::OpResult>(operand);
                                projected.push_back(arguments[source.getResultNumber()]);
                            }
                            for (auto [output, value] :
                                 llvm::zip(terminal.exit.outputs, projected)) {
                                if (!step()) { return {}; }
                                arguments[output] = value;
                            }
                        }
                    } else if (terminal.exit.effectful) {
                        // The after region cannot observe the exit selector.
                        mlir::Attribute discriminator =
                            terminal.exit.cases.size() == 1
                                ? mlir::Attribute(
                                      ctjs::BooleanAttr::get(function.getContext(), false))
                                : mlir::Attribute(
                                      ctjs::NumberAttr::get(function.getContext(), 0.0));
                        arguments[terminal.exit.selector] =
                            ctjs::ConstantOp::create(at, condition.getLoc(), discriminator);
                        ++operationCount;
                    }
                }
                llvm::SmallVector<mlir::Value> result{predicate};
                for (auto [index, argument] : llvm::enumerate(arguments)) {
                    if (!step()) { return {}; }
                    auto value = argument;
                    if (value.getDefiningOp<mlir::ub::PoisonOp>()) {
                        bool inactiveExit =
                            terminal.loop.getResult(static_cast<unsigned>(index)).use_empty() ||
                            (terminal.exit.dispatch && terminal.exit.consumed[index] &&
                             !llvm::is_contained(terminal.exit.outputs, index));
                        if (selectedExit && terminal.exit.effectful) {
                            // A saved payload may be absent only on exits whose
                            // selected arm cannot observe it. Check every use,
                            // including nested arms and observers after dispatch.
                            inactiveExit = true;
                            for (mlir::OpOperand & use :
                                 terminal.loop.getResult(static_cast<unsigned>(index)).getUses()) {
                                if (!step()) { return {}; }
                                auto * owner = use.getOwner();
                                inactiveExit &= terminal.exit.dispatch->isProperAncestor(owner) &&
                                                !selectedExit->isAncestor(owner->getParentRegion());
                            }
                        }
                        const bool inactive = integer && (integer.getValue().isZero()
                                                              ? inactiveExit
                                                              : terminal.inactiveAfter[index]);
                        if (!inactive) {
                            refuse("DOM helper completion observes an inactive value");
                            return {};
                        }
                        mlir::Value replacement;
                        if (index < terminal.loop.getBeforeArguments().size()) {
                            replacement =
                                values.lookupOrNull(terminal.loop.getBeforeArguments()[index]);
                        }
                        if (terminal.exit.dispatch && !replacement) {
                            // The selected destination cannot observe this slot.
                            // A dropped exit-only before argument has no mapping;
                            // borrow a defined initial value of the same type.
                            for (mlir::Value initial : terminal.loop.getInits()) {
                                if (!step()) { return {}; }
                                auto mapped = values.lookupOrNull(initial);
                                if (mapped && mapped.getType() == value.getType() &&
                                    !mapped.getDefiningOp<mlir::ub::PoisonOp>()) {
                                    replacement = mapped;
                                    if (auto literal = mapped.getDefiningOp<ctjs::ConstantOp>()) {
                                        if (!step()) { return {}; }
                                        replacement = ctjs::ConstantOp::create(
                                            at, condition.getLoc(), literal.getValue());
                                        inactiveFillers.insert(replacement);
                                        ++operationCount;
                                    }
                                    break;
                                }
                            }
                        }
                        if (!replacement || replacement.getType() != value.getType() ||
                            replacement.getDefiningOp<mlir::ub::PoisonOp>()) {
                            refuse("DOM helper completion observes an inactive value");
                            return {};
                        }
                        // The predicate selects a destination with no use of
                        // this slot. Its existing state keeps the SCF tuple
                        // defined and preserves the carried representation.
                        value = replacement;
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
                // Pure projections join live exit values inside the loop.
                // Effectful two-arm dispatches instead carry a Boolean choice;
                // their reads, IteratorClose and frame exits stay after the loop.
                const auto exitProjection = [&]() -> ExitProjection {
                    ExitProjection exit;
                    auto * next = loop->getNextNode();
                    if (!next || !step()) { return {}; }
                    // The structured throw path spells the same two-way exit
                    // as an exact integer equality instead of index_switch.
                    // Share its selector and complete selected-arm use census.
                    mlir::Value selectorValue;
                    exit.literal = llvm::dyn_cast<mlir::arith::ConstantOp>(next);
                    if (exit.literal) { next = next->getNextNode(); }
                    if (!next || !step()) { return {}; }
                    exit.comparison = llvm::dyn_cast<mlir::arith::CmpIOp>(next);
                    if (exit.comparison) {
                        const auto predicate = exit.comparison.getPredicate();
                        if ((predicate != mlir::arith::CmpIPredicate::eq &&
                             predicate != mlir::arith::CmpIPredicate::ne) ||
                            !exit.comparison->hasOneUse()) {
                            return {};
                        }
                        next = next->getNextNode();
                        if (!next || !step()) { return {}; }
                        auto branch = llvm::dyn_cast<mlir::scf::IfOp>(next);
                        if (!branch || branch.getCondition() != exit.comparison.getResult()) {
                            return {};
                        }
                        selectorValue = exit.comparison.getLhs();
                        auto constant =
                            exit.comparison.getRhs().getDefiningOp<mlir::arith::ConstantOp>();
                        if (!constant) {
                            selectorValue = exit.comparison.getRhs();
                            constant =
                                exit.comparison.getLhs().getDefiningOp<mlir::arith::ConstantOp>();
                        }
                        auto key = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                            : mlir::IntegerAttr{};
                        if (!key || key.getValue().isNegative() ||
                            key.getValue().getActiveBits() > 63 ||
                            !dominance.dominates(constant.getResult(), exit.comparison) ||
                            (!exit.literal && !values.contains(constant.getResult())) ||
                            (exit.literal &&
                             (constant != exit.literal || !constant->hasOneUse()))) {
                            return {};
                        }
                        exit.dispatch = branch;
                        const bool equal = predicate == mlir::arith::CmpIPredicate::eq;
                        exit.arms.push_back(equal ? &branch.getThenRegion()
                                                  : &branch.getElseRegion());
                        exit.fallback = equal ? &branch.getElseRegion() : &branch.getThenRegion();
                        exit.cases.push_back(key.getInt());
                    } else {
                        if (exit.literal) { return {}; }
                        exit.cast = llvm::dyn_cast<mlir::arith::IndexCastUIOp>(next);
                        if (exit.cast) { next = next->getNextNode(); }
                        if (!next || !step()) { return {}; }
                        auto dispatch = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(next);
                        if (!dispatch || (exit.cast && (dispatch.getArg() != exit.cast.getOut() ||
                                                        !exit.cast->hasOneUse()))) {
                            return {};
                        }
                        exit.dispatch = dispatch;
                        exit.fallback = &dispatch.getDefaultRegion();
                        for (auto [index, key] : llvm::enumerate(dispatch.getCases())) {
                            if (!step()) { return {}; }
                            exit.cases.push_back(key);
                            exit.arms.push_back(&dispatch.getCaseRegions()[index]);
                        }
                        selectorValue = exit.cast ? exit.cast.getIn() : dispatch.getArg();
                    }
                    auto selector = llvm::dyn_cast<mlir::OpResult>(selectorValue);
                    if (!selector || selector.getOwner() != loop ||
                        !before.inactiveAfter[selector.getResultNumber()]) {
                        return {};
                    }
                    exit.selector = selector.getResultNumber();
                    exit.consumed.resize(loop.getNumResults(), false);
                    exit.consumed[exit.selector] = true;
                    for (auto & region : exit.dispatch->getRegions()) {
                        if (!step()) { return {}; }
                        if (!region.hasOneBlock() || region.front().empty() ||
                            region.front().getNumArguments()) {
                            return {};
                        }
                        auto yield =
                            llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                        if (!yield || yield.getOperandTypes() != exit.dispatch->getResultTypes()) {
                            return {};
                        }
                        exit.effectful |= !llvm::hasSingleElement(region.front());
                    }
                    // Each effectful case adds a branch to the bounded exit tree.
                    if (exit.effectful && (exit.cases.empty() || exit.cases.size() >= 64 - depth)) {
                        return {};
                    }
                    for (auto & region : exit.dispatch->getRegions()) {
                        if (!step()) { return {}; }
                        if (exit.effectful) { continue; }
                        auto yield = llvm::cast<mlir::scf::YieldOp>(region.front().front());
                        for (mlir::Value operand : yield.getOperands()) {
                            if (!step()) { return {}; }
                            auto value = llvm::dyn_cast<mlir::OpResult>(operand);
                            if (!value || value.getOwner() != loop ||
                                value.getResultNumber() == exit.selector ||
                                !before.inactiveAfter[value.getResultNumber()]) {
                                return {};
                            }
                            exit.consumed[value.getResultNumber()] = true;
                        }
                    }
                    for (auto [index, result] : llvm::enumerate(loop.getResults())) {
                        if (!step()) { return {}; }
                        if (!exit.consumed[index]) { continue; }
                        for (mlir::OpOperand & use : result.getUses()) {
                            if (!step()) { return {}; }
                            auto * owner = use.getOwner();
                            if (index == exit.selector) {
                                if (owner != (exit.cast         ? exit.cast.getOperation()
                                              : exit.comparison ? exit.comparison.getOperation()
                                                                : exit.dispatch)) {
                                    return {};
                                }
                            } else if (!llvm::isa<mlir::scf::YieldOp>(owner) ||
                                       owner->getParentOp() != exit.dispatch) {
                                return {};
                            }
                        }
                    }
                    if (exit.effectful) { return exit; }
                    // Reuse only slots whose complete use census belongs to
                    // this dispatch. Each output needs its own compatible slot.
                    for (mlir::Type type : exit.dispatch->getResultTypes()) {
                        if (!step()) { return {}; }
                        std::optional<unsigned> output;
                        for (auto [index, result] : llvm::enumerate(loop.getResults())) {
                            if (!step()) { return {}; }
                            if (exit.consumed[index] && index != exit.selector &&
                                result.getType() == type &&
                                !llvm::is_contained(exit.outputs, index)) {
                                output = static_cast<unsigned>(index);
                                break;
                            }
                        }
                        if (!output) { return {}; }
                        exit.outputs.push_back(*output);
                    }
                    return exit;
                };
                before.exit = exitProjection();
                if (!remaining) { return {}; }
                if (before.exit.effectful) {
                    before.types[before.exit.selector + 1] =
                        ctjs::ValueType::get(function.getContext());
                }
                auto copied = mlir::scf::WhileOp::create(
                    at, loop.getLoc(), mlir::TypeRange(before.types).drop_front(), initial);
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
                        for (auto [index, argument] :
                             llvm::enumerate(source.front().getArguments())) {
                            if (!step()) { return {}; }
                            path.map(argument, output.addArgument(copied.getResult(index).getType(),
                                                                  argument.getLoc()));
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
                if (before.exit.dispatch) {
                    auto dispatch = before.exit.dispatch;
                    const auto refineExit = [&](mlir::Region * source, mlir::IRMapping & path,
                                                mlir::OpBuilder & nested) {
                        auto facts = before.exit.booleans.find(source);
                        if (facts == before.exit.booleans.end()) { return true; }
                        for (auto [index, boolean] : llvm::enumerate(facts->second)) {
                            if (!step()) { return false; }
                            if (!boolean) { continue; }
                            auto value = ctjs::ConstantOp::create(nested, loop.getLoc(), boolean);
                            path.map(loop.getResult(index), value.getResult());
                            ++operationCount;
                        }
                        return true;
                    };
                    if (before.exit.effectful && before.exit.cases.size() > 1) {
                        // Keep each exit's exact tuple until its continuation has
                        // selected the live payload. Joining here would expose
                        // the sibling return/throw slot's poison or integer tag.
                        Continuation tail{&body, std::next(dispatch->getIterator()),
                                          dispatch->getResults(), continuation};
                        const auto emitExit = [&](auto && exitSelf, unsigned index,
                                                  mlir::OpBuilder & builder) -> Results {
                            if (!step() || depth + index >= 64) { return {}; }
                            auto * source = index < before.exit.arms.size()
                                                ? before.exit.arms[index]
                                                : before.exit.fallback;
                            const auto emitArm = [&](mlir::OpBuilder & nested) -> Results {
                                for (const auto & item : values.getValueMap()) {
                                    (void)item;
                                    if (!step()) { return {}; }
                                }
                                for (const auto & item : values.getOperationMap()) {
                                    (void)item;
                                    if (!step()) { return {}; }
                                }
                                mlir::IRMapping path(values);
                                if (!refineExit(source, path, nested)) { return {}; }
                                return self(self, source->front(), source->front().begin(), path,
                                            nested, &tail, terminal, depth + index + 1);
                            };
                            if (index == before.exit.arms.size()) { return emitArm(builder); }
                            auto tag = ctjs::ConstantOp::create(
                                builder, dispatch->getLoc(),
                                ctjs::NumberAttr::get(function.getContext(),
                                                      static_cast<double>(index)));
                            auto equal = ctjs::CompareOp::create(
                                builder, dispatch->getLoc(), tag.getType(),
                                ctjs::CompareKind::StrictEq, copied.getResult(before.exit.selector),
                                tag);
                            auto choice =
                                ctjs::TruthyOp::create(builder, dispatch->getLoc(), equal);
                            auto branch = mlir::scf::IfOp::create(
                                builder, dispatch->getLoc(), terminal.types, choice.getResult());
                            operationCount += 4;
                            for (auto [arm, region] : llvm::enumerate(branch->getRegions())) {
                                auto & output = region.emplaceBlock();
                                mlir::OpBuilder nested(&output, output.begin());
                                auto result = arm == 0 ? emitArm(nested)
                                                       : exitSelf(exitSelf, index + 1, nested);
                                if (!result) { return {}; }
                                mlir::scf::YieldOp::create(nested, dispatch->getLoc(), *result);
                                ++operationCount;
                            }
                            return llvm::SmallVector<mlir::Value>(branch.getResults());
                        };
                        auto result = emitExit(emitExit, 0, at);
                        if (before.exit.cast) { visited.insert(before.exit.cast); }
                        visited.insert(dispatch);
                        return result;
                    }
                    if (before.exit.effectful) {
                        auto choice = ctjs::TruthyOp::create(
                            at, dispatch->getLoc(), copied.getResult(before.exit.selector));
                        auto branch = mlir::scf::IfOp::create(
                            at, dispatch->getLoc(), dispatch->getResultTypes(), choice.getResult());
                        operationCount += 2;
                        Terminal joined;
                        joined.kind = Terminal::yielded;
                        for (auto [index, type] : llvm::enumerate(dispatch->getResultTypes())) {
                            if (!step()) { return {}; }
                            joined.types.push_back(type);
                            joined.carried.push_back(static_cast<unsigned>(index));
                        }
                        for (auto [source, target] :
                             llvm::zip(llvm::ArrayRef<mlir::Region *>{before.exit.arms[0],
                                                                      before.exit.fallback},
                                       branch->getRegions())) {
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
                            mlir::OpBuilder nested(&output, output.begin());
                            if (!refineExit(source, path, nested)) { return {}; }
                            auto result = self(self, source->front(), source->front().begin(), path,
                                               nested, nullptr, joined, depth + 1);
                            if (!result) { return {}; }
                            mlir::scf::YieldOp::create(nested, dispatch->getLoc(), *result);
                            ++operationCount;
                        }
                        values.map(dispatch->getResults(), branch.getResults());
                    } else {
                        for (auto [result, output] :
                             llvm::zip(dispatch->getResults(), before.exit.outputs)) {
                            if (!step()) { return {}; }
                            values.map(result, copied.getResult(output));
                        }
                        for (auto & region : dispatch->getRegions()) {
                            if (!step()) { return {}; }
                            visited.insert(region.front().getTerminator());
                        }
                    }
                    if (before.exit.cast) { visited.insert(before.exit.cast); }
                    if (before.exit.comparison) { visited.insert(before.exit.comparison); }
                    if (before.exit.literal) { visited.insert(before.exit.literal); }
                    visited.insert(dispatch);
                    cursor = dispatch->getIterator();
                }
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
                normalReturn = true;
                return llvm::SmallVector<mlir::Value>{values.lookupOrDefault(result.getValue())};
            }
            if (auto abrupt = llvm::dyn_cast<mlir::scf::ExecuteRegionOp>(operation)) {
                auto & region = abrupt.getRegion();
                auto thrown = region.hasOneBlock() && llvm::hasSingleElement(region.front())
                                  ? llvm::dyn_cast<ctjs::ThrowOp>(region.front().front())
                                  : ctjs::ThrowOp{};
                if (!depth || !abrupt.getNoInline() || abrupt->getNumOperands() ||
                    abrupt.getNumResults() || !thrown || region.front().getNumArguments() ||
                    !step() || !values.contains(thrown.getValue()) ||
                    !dominance.dominates(thrown.getValue(), thrown) ||
                    values.lookup(thrown.getValue()).getDefiningOp<mlir::ub::PoisonOp>()) {
                    refuse("DOM helper abrupt completion requires one exact saved throw");
                    return {};
                }
                visited.insert(thrown);
                auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(body.getTerminator());
                if (!yield || yield.getOperandTypes() != body.getParentOp()->getResultTypes()) {
                    refuse("DOM helper abrupt completion lacks its unreachable yield");
                    return {};
                }
                // Only structural padding may follow this non-returning region.
                // Do not visit the shared continuation on the throwing path.
                for (auto tail = std::next(cursor); tail != body.end(); ++tail) {
                    if (!step()) { return {}; }
                    if (&*tail != yield && !llvm::isa<mlir::ub::PoisonOp>(*tail)) {
                        refuse("DOM helper abrupt completion has an unproved continuation");
                        return {};
                    }
                    for (mlir::Value operand : tail->getOperands()) {
                        if (!step()) { return {}; }
                        if (!dominance.dominates(operand, &*tail)) {
                            refuse("DOM helper abrupt completion has an undefined yield");
                            return {};
                        }
                    }
                    visited.insert(&*tail);
                }
                llvm::SmallVector<mlir::Value> padding;
                for (mlir::Type type : terminal.types) {
                    if (!step()) { return {}; }
                    // ponytail: scalar source result slots only; abrupt loop
                    // conditions need a separate non-returning tuple proof.
                    if (!llvm::isa<ctjs::ValueType>(type)) {
                        refuse("DOM helper abrupt completion requires source result slots");
                        return {};
                    }
                    auto filler = ctjs::ConstantOp::create(
                        at, abrupt.getLoc(), ctjs::UndefinedAttr::get(function.getContext()));
                    inactiveFillers.insert(filler.getResult());
                    padding.push_back(filler.getResult());
                    ++operationCount;
                }
                at.clone(operation, values);
                operationCount += 2;
                return padding;
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
                // Defined JavaScript results can join before their common
                // continuation. Completion tags and inactive slots still need
                // path expansion so their selectors retain exact values.
                bool join = true;
                for (mlir::Type type : branch.getResultTypes()) {
                    if (!step()) { return {}; }
                    join &= llvm::isa<ctjs::ValueType>(type);
                }
                if (join) {
                    const auto scanned = branch.walk([&](mlir::Operation * inner) {
                        if (!step()) { return mlir::WalkResult::interrupt(); }
                        join &= !llvm::isa<mlir::ub::PoisonOp, mlir::scf::IndexSwitchOp>(inner);
                        for (mlir::Value operand : inner->getOperands()) {
                            if (!step()) { return mlir::WalkResult::interrupt(); }
                            join &= !values.lookupOrDefault(operand)
                                         .getDefiningOp<mlir::ub::PoisonOp>();
                        }
                        return mlir::WalkResult::advance();
                    });
                    if (scanned.wasInterrupted()) { return {}; }
                }
                Terminal joined;
                joined.kind = Terminal::yielded;
                if (join) {
                    for (auto [index, type] : llvm::enumerate(branch.getResultTypes())) {
                        if (!step()) { return {}; }
                        joined.types.push_back(type);
                        joined.carried.push_back(static_cast<unsigned>(index));
                    }
                }
                auto & armTerminal = join ? joined : terminal;
                // ponytail: other continuations are duplicated within the
                // existing work budget; general completion joins need proof.
                auto copied =
                    mlir::scf::IfOp::create(at, branch.getLoc(), armTerminal.types,
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
                        returned = join ? Results(llvm::SmallVector<mlir::Value>{})
                                        : self(self, body, tail.next, path, nested, continuation,
                                               terminal, depth + 1);
                    } else if (source.hasOneBlock() && source.front().getNumArguments() == 0) {
                        returned = self(self, source.front(), source.front().begin(), path, nested,
                                        join ? nullptr : &tail, armTerminal, depth + 1);
                    }
                    if (!returned) {
                        refuse("DOM helper completion has an incomplete branch");
                        return {};
                    }
                    mlir::scf::YieldOp::create(nested, branch.getLoc(), *returned);
                    ++operationCount;
                }
                if (join) {
                    values.map(branch.getResults(), copied.getResults());
                    continue;
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
            if (auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(operation)) {
                // Preserve suppression and both continuations verbatim. The
                // complete DOM admission still proves their host effects.
                const auto checked = invocation.walk([&](mlir::Operation * nested) {
                    // Charge verification and cloning before either traverses
                    // this opaque region without a work callback.
                    const uint64_t cost =
                        uint64_t(3) * (1 + nested->getNumOperands() + nested->getNumResults() +
                                       nested->getAttrs().size());
                    if (cost > remaining) {
                        remaining = 0;
                        refuse("DOM helper expansion work budget exhausted");
                        return mlir::WalkResult::interrupt();
                    }
                    remaining -= static_cast<unsigned>(cost);
                    for (auto & region : nested->getRegions()) {
                        for (auto & block : region) {
                            if (!step()) { return mlir::WalkResult::interrupt(); }
                            for (auto argument : block.getArguments()) {
                                (void)argument;
                                if (!step()) { return mlir::WalkResult::interrupt(); }
                            }
                        }
                    }
                    if (llvm::isa<mlir::ub::PoisonOp>(nested)) {
                        refuse("DOM helper invocation contains inactive source state");
                        return mlir::WalkResult::interrupt();
                    }
                    visited.insert(nested);
                    for (mlir::Value operand : nested->getOperands()) {
                        if (!step()) { return mlir::WalkResult::interrupt(); }
                        auto * owner = operand.getParentBlock()->getParentOp();
                        if (owner == invocation || invocation->isAncestor(owner)) { continue; }
                        if (!values.contains(operand) || !dominance.dominates(operand, nested) ||
                            values.lookup(operand).getDefiningOp<mlir::ub::PoisonOp>()) {
                            refuse("DOM helper invocation observes an inactive or unbound value");
                            return mlir::WalkResult::interrupt();
                        }
                    }
                    ++operationCount;
                    return mlir::WalkResult::advance();
                });
                if (checked.wasInterrupted()) { return {}; }
                if (mlir::failed(mlir::verify(invocation))) {
                    refuse("DOM helper completion has an invalid invocation");
                    return {};
                }
                at.clone(operation, values);
                continue;
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
            for (auto [from, to] : llvm::zip(operation.getResults(), copied->getResults())) {
                if (!step()) { return {}; }
                if (inactiveFillers.contains(from)) { inactiveFillers.insert(to); }
            }
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
    if (!normalReturn) { return refuse("DOM helper completion lacks a reachable source return"); }
    const auto complete = function.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (operation != function && !visited.contains(operation)) {
            refuse("DOM helper completion contains unvisited source operations");
            return mlir::WalkResult::interrupt();
        }
        for (mlir::Value value : operation->getResults()) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            inactiveFillers.erase(value);
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

bool DOMSource::repairInactiveCompletion(ctjs::FuncOp function) {
    if (inactiveFillers.empty()) { return true; }
    llvm::SmallVector<ctjs::ConstantOp> fillers;
    const auto collected = function.walk([&](ctjs::ConstantOp literal) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (inactiveFillers.contains(literal.getResult())) { fillers.push_back(literal); }
        return mlir::WalkResult::advance();
    });
    if (collected.wasInterrupted()) { return false; }
    mlir::DominanceInfo dominance(function);
    for (auto filler : fillers) {
        // Expansion can reveal an existing element behind an iterator call.
        // Change only our privately recorded, proved-unobserved padding. Follow
        // the same SCF slot to an available value; never hoist its producer.
        llvm::SmallVector<mlir::Value> pending{filler.getResult()};
        llvm::DenseSet<mlir::Value> seen;
        while (!pending.empty()) {
            if (!step()) { return false; }
            const auto value = pending.pop_back_val();
            if (!seen.insert(value).second) { continue; }
            if (!inactiveFillers.contains(value) && value.getType() == filler.getType() &&
                dominance.properlyDominates(value, filler.getOperation())) {
                filler.getResult().replaceAllUsesWith(value);
                inactiveFillers.erase(filler.getResult());
                filler.erase();
                break;
            }
            for (mlir::OpOperand & use : value.getUses()) {
                if (!step()) { return false; }
                auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(use.getOwner());
                auto branch = yield ? llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp())
                                    : mlir::scf::IfOp{};
                if (branch && use.getOperandNumber() < branch.getNumResults()) {
                    pending.push_back(branch.getResult(use.getOperandNumber()));
                }
            }
            auto result = llvm::dyn_cast<mlir::OpResult>(value);
            auto branch =
                result ? llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner()) : mlir::scf::IfOp{};
            if (!branch) { continue; }
            for (auto & region : branch->getRegions()) {
                if (!step()) { return false; }
                if (!region.hasOneBlock()) { continue; }
                auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                if (yield && result.getResultNumber() < yield.getNumOperands()) {
                    pending.push_back(yield.getOperand(result.getResultNumber()));
                }
            }
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::dom_source_detail
