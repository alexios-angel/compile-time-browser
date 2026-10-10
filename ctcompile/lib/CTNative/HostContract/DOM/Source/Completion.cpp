//===- Completion.cpp - Path-by-path completion normalization ---*- C++ -*-===//
//
// DOMSource::normalizeCompletion, which rewrites a helper whose returns and
// throws are routed through completion tags (scf.index_switch dispatch,
// poison padding, conditions tested twice) into one body in which every path
// carries its exact values, and DOMSource::repairInactiveCompletion, which
// later swaps the undefined padding it inserted for real values.
//
//===----------------------------------------------------------------------===//

#include "Proof.hpp"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/Verifier.h"

namespace ctcompile::ctnative::dom_source_detail {

bool DOMSource::normalizeCompletion(ctjs::FuncOp function) {
    const auto repeatedCondition = [&](mlir::scf::IfOp branch) {
        if (!branch.getNumResults()) { return false; }
        for (mlir::Operation * user : branch.getCondition().getUsers()) {
            if (!step()) { return false; }
            auto later = llvm::dyn_cast<mlir::scf::IfOp>(user);
            if (later && later != branch && later->getBlock() == branch->getBlock() &&
                branch->isBeforeInBlock(later)) {
                return true;
            }
        }
        return false;
    };
    // Normalize only when the body has a completion dispatch: an
    // scf.index_switch, a poison value (inactive padding), or an scf.if whose
    // condition is tested again later. Otherwise there is nothing to do.
    bool dispatch = false;
    const auto walked = function.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        dispatch |= llvm::isa<mlir::scf::IndexSwitchOp, mlir::ub::PoisonOp>(operation);
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            dispatch |= repeatedCondition(branch);
        }
        return mlir::WalkResult::advance();
    });
    if (walked.wasInterrupted()) { return false; }
    if (!dispatch) { return true; }

    // Build the new body in `normalized` and keep the original until every
    // completion path has been copied. A yield supplies the exact values for
    // its continuation; inactive poison slots may be forwarded but must never
    // be observed.
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
            yielded,
            tryExit,
            tryYield
        } kind = returned;
        llvm::SmallVector<mlir::Type> types;
        mlir::scf::WhileOp loop;
        llvm::SmallVector<unsigned> carried;
        llvm::SmallVector<bool> inactiveAfter;
        ExitProjection exit;
        bool protectedTry() const { return kind == tryExit || kind == tryYield; }
    };
    using Results = std::optional<llvm::SmallVector<mlir::Value>>;
    mlir::Region normalized;
    auto & destination = normalized.emplaceBlock();
    mlir::IRMapping mapping;
    mlir::DominanceInfo dominance(function);
    llvm::DenseSet<mlir::Operation *> visited;
    auto terminalThrow = llvm::dyn_cast<ctjs::ThrowOp>(function.getBody().front().back());
    bool normalReturn = false;
    for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
        if (!step()) { return false; }
        mapping.map(argument, destination.addArgument(argument.getType(), argument.getLoc()));
    }
    // Charge each mapped value and operation (and block, unless told not to)
    // before copying; a charge that does not fit drains the budget.
    const auto copyMapping = [&](const mlir::IRMapping & from, mlir::IRMapping & to,
                                 bool blocks = true) {
        if (!drain(from.getValueMap().size() + from.getOperationMap().size() +
                   (blocks ? from.getBlockMap().size() : 0))) {
            return false;
        }
        to = from;
        return true;
    };
    // Carry every one of `types` through `joined`'s yield, charged per slot.
    const auto carryAll = [&](Terminal & joined, mlir::TypeRange types) {
        for (auto [index, type] : llvm::enumerate(types)) {
            if (!step()) { return false; }
            joined.types.push_back(type);
            joined.carried.push_back(static_cast<unsigned>(index));
        }
        return true;
    };
    // Creates undefined padding for a slot no selected path observes and
    // records it in inactiveFillers.
    const auto undefinedFiller = [&](mlir::OpBuilder & builder, mlir::Location where) {
        auto filler = ctjs::ConstantOp::create(builder, where,
                                               ctjs::UndefinedAttr::get(function.getContext()));
        inactiveFillers.insert(filler.getResult());
        ++operationCount;
        return filler.getResult();
    };
    const auto emit = [&](auto && self, mlir::Block & body, mlir::Block::iterator begin,
                          mlir::IRMapping & values, mlir::OpBuilder & at,
                          const Continuation * continuation, Terminal & terminal,
                          unsigned depth) -> Results {
        if (depth >= 64) { return fail("DOM helper completion nesting is too deep"); }
        for (auto cursor = begin; cursor != body.end(); ++cursor) {
            if (!step()) { return {}; }
            auto & operation = *cursor;
            visited.insert(&operation);
            for (mlir::Value operand : operation.getOperands()) {
                if (!step()) { return {}; }
                if (!values.contains(operand) || !dominance.dominates(operand, &operation)) {
                    return fail("DOM helper completion operand has no preceding local definition");
                }
            }
            if (llvm::isa<mlir::scf::YieldOp, ctjs::InvokeYieldOp>(operation)) {
                auto * yield = &operation;
                if (!continuation && terminal.kind == Terminal::yielded) {
                    const auto types = terminal.loop
                                           ? mlir::TypeRange(terminal.loop.getInits().getTypes())
                                           : mlir::TypeRange(terminal.types);
                    if (yield->getOperandTypes() != types) {
                        return fail("DOM helper loop lost its carried result correspondence");
                    }
                    llvm::SmallVector<mlir::Value> result;
                    for (unsigned index : terminal.carried) {
                        if (!step()) { return {}; }
                        auto value = values.lookup(yield->getOperand(index));
                        if (value.getDefiningOp<mlir::ub::PoisonOp>()) {
                            return fail("DOM helper completion observes an inactive value");
                        }
                        result.push_back(value);
                    }
                    return result;
                }
                if (!continuation || yield->getOperandTypes() != continuation->results.getTypes()) {
                    return fail("DOM helper completion lost its result correspondence");
                }
                for (auto [result, operand] :
                     llvm::zip(continuation->results, yield->getOperands())) {
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
            if (llvm::isa<ctjs::TryExitOp, ctjs::TryYieldOp>(operation)) {
                const bool exiting = llvm::isa<ctjs::TryExitOp>(operation);
                if (continuation ||
                    terminal.kind != (exiting ? Terminal::tryExit : Terminal::tryYield) ||
                    operation.getNumOperands() != terminal.types.size()) {
                    return fail("DOM helper completion lost its protected terminal");
                }
                llvm::SmallVector<mlir::Value> result;
                bool caught = false;
                for (auto [index, operand] : llvm::enumerate(operation.getOperands())) {
                    if (!step()) { return {}; }
                    auto value = values.lookup(operand);
                    if (exiting && index == 0) {
                        mlir::IntegerAttr flag;
                        if (!mlir::matchPattern(value, mlir::m_Constant(&flag)) ||
                            !flag.getType().isInteger(1)) {
                            return fail(
                                "DOM helper protected exit requires an exact completion flag");
                        }
                        caught = !flag.getValue().isZero();
                        value = ctjs::ConstantOp::create(
                            at, operation.getLoc(),
                            ctjs::BooleanAttr::get(function.getContext(), caught));
                        ++operationCount;
                    } else if (value.getDefiningOp<mlir::ub::PoisonOp>()) {
                        if (!exiting || (index == 1 ? !caught : caught)) {
                            return fail("DOM helper completion observes an inactive value");
                        }
                        value = undefinedFiller(at, operation.getLoc());
                    }
                    result.push_back(value);
                }
                return result;
            }
            if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(operation)) {
                if (continuation || terminal.kind != Terminal::condition ||
                    condition.getArgs().getTypes() != terminal.loop.getResultTypes()) {
                    return fail("DOM helper loop lost its condition result correspondence");
                }
                auto predicate = values.lookup(condition.getCondition());
                mlir::IntegerAttr integer;
                (void)mlir::matchPattern(predicate, mlir::m_Constant(&integer));
                if (predicate.getDefiningOp<mlir::ub::PoisonOp>()) {
                    return fail("DOM helper completion observes an inactive value");
                }
                llvm::SmallVector<mlir::Value> arguments;
                for (mlir::Value argument : condition.getArgs()) {
                    if (!step()) { return {}; }
                    arguments.push_back(values.lookup(argument));
                }
                mlir::Region * selectedExit = nullptr;
                if (terminal.exit.dispatch) {
                    if (!integer || !integer.getType().isInteger(1)) {
                        return fail("DOM helper loop exit predicate is not an exact constant");
                    }
                    if (integer.getValue().isZero()) {
                        auto key = exactTag(arguments[terminal.exit.selector]
                                                .getDefiningOp<mlir::arith::ConstantOp>(),
                                            63);
                        if (!key) {
                            return fail("DOM helper loop exit selector is not an exact constant");
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
                            // Keep only the Boolean constants shared by every
                            // path to this exit, before padding inactive slots.
                            auto [facts, first] = terminal.exit.booleans.try_emplace(selected);
                            for (auto [index, value] : llvm::enumerate(arguments)) {
                                if (!step()) { return {}; }
                                auto boolean = ctjs::constantAttr<ctjs::BooleanAttr>(value);
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
                            return fail("DOM helper completion observes an inactive value");
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
                            return fail("DOM helper completion observes an inactive value");
                        }
                        // The chosen destination never uses this slot, so any
                        // defined value of the right type keeps the scf tuple
                        // well-formed without changing what is observed.
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
                    return fail("DOM helper loop lacks complete source regions");
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
                        return fail("DOM helper completion observes an inactive value");
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
                // Recognize the exit dispatch right after the loop. If every
                // arm only yields loop results, the live values are selected
                // inside the loop. If an arm has effects, the loop instead
                // carries which arm to take (a Boolean, or a number when there
                // are more arms), and the arm's reads, IteratorClose and frame
                // exits stay after the loop.
                const auto exitProjection = [&]() -> ExitProjection {
                    ExitProjection exit;
                    auto * next = loop->getNextNode();
                    if (!next || !step()) { return {}; }
                    // The structured throw path writes the same two-way exit
                    // as an integer equality feeding scf.if instead of an
                    // index_switch; the selector and use checks below are
                    // shared by both forms.
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
                        mlir::arith::ConstantOp constant;
                        std::tie(selectorValue, constant) = constantComparison(exit.comparison);
                        auto key = exactTag(constant, 63);
                        if (!key || !dominance.dominates(constant.getResult(), exit.comparison) ||
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
                    // Each effectful case adds one nesting level to the exit
                    // tree, which must stay within the 64-level depth limit.
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
                    // Reuse only slots whose every use is in this dispatch.
                    // Each output needs its own slot of the same type.
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
                    mlir::IRMapping path;
                    if (!copyMapping(values, path, false)) { return {}; }
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
                        // chosen the live payload. Joining here would expose the
                        // other arm's return/throw slot, which is poison or an
                        // integer tag.
                        Continuation tail{&body, std::next(dispatch->getIterator()),
                                          dispatch->getResults(), continuation};
                        const auto emitExit = [&](auto && exitSelf, unsigned index,
                                                  mlir::OpBuilder & builder) -> Results {
                            if (!step() || depth + index >= 64) { return {}; }
                            auto * source = index < before.exit.arms.size()
                                                ? before.exit.arms[index]
                                                : before.exit.fallback;
                            const auto emitArm = [&](mlir::OpBuilder & nested) -> Results {
                                mlir::IRMapping path;
                                if (!copyMapping(values, path, false)) { return {}; }
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
                        if (!carryAll(joined, dispatch->getResultTypes())) { return {}; }
                        for (auto [source, target] :
                             llvm::zip(llvm::ArrayRef<mlir::Region *>{before.exit.arms[0],
                                                                      before.exit.fallback},
                                       branch->getRegions())) {
                            mlir::IRMapping path;
                            if (!copyMapping(values, path, false)) { return {}; }
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
                    return fail("DOM helper completion observes an inactive value");
                }
            }
            if (llvm::isa<ctjs::ReturnOp>(operation) || &operation == terminalThrow) {
                if (continuation || terminal.kind != Terminal::returned) {
                    return fail("DOM helper completion returns inside a source region");
                }
                if (terminalThrow && &operation != terminalThrow) {
                    return fail("DOM helper completion mixes return and throw terminals");
                }
                normalReturn = true;
                return llvm::SmallVector<mlir::Value>{
                    values.lookupOrDefault(operation.getOperand(0))};
            }
            if (auto abrupt = llvm::dyn_cast<mlir::scf::ExecuteRegionOp>(operation)) {
                auto thrown = savedThrow(abrupt);
                if (!depth || !thrown || !step() || !values.contains(thrown.getValue()) ||
                    !dominance.dominates(thrown.getValue(), thrown) ||
                    values.lookup(thrown.getValue()).getDefiningOp<mlir::ub::PoisonOp>()) {
                    return fail("DOM helper abrupt completion requires one exact saved throw");
                }
                visited.insert(thrown);
                auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(body.getTerminator());
                if (!yield || yield.getOperandTypes() != body.getParentOp()->getResultTypes()) {
                    return fail("DOM helper abrupt completion lacks its unreachable yield");
                }
                // Only poison padding and the yield may follow this throwing
                // region; the shared continuation is not visited on this path.
                for (auto tail = std::next(cursor); tail != body.end(); ++tail) {
                    if (!step()) { return {}; }
                    if (&*tail != yield && !llvm::isa<mlir::ub::PoisonOp>(*tail)) {
                        return fail("DOM helper abrupt completion has an unproved continuation");
                    }
                    for (mlir::Value operand : tail->getOperands()) {
                        if (!step()) { return {}; }
                        if (!dominance.dominates(operand, &*tail)) {
                            return fail("DOM helper abrupt completion has an undefined yield");
                        }
                    }
                    visited.insert(&*tail);
                }
                llvm::SmallVector<mlir::Value> padding;
                for (mlir::Type type : terminal.types) {
                    if (!step()) { return {}; }
                    // ponytail: accepts only JavaScript-value result slots;
                    // refuses throws inside a loop condition (i1 slot) until a
                    // proof for that tuple exists.
                    if (!llvm::isa<ctjs::ValueType>(type)) {
                        return fail("DOM helper abrupt completion requires source result slots");
                    }
                    padding.push_back(undefinedFiller(at, abrupt.getLoc()));
                }
                at.clone(operation, values);
                operationCount += 2;
                return padding;
            }
            Continuation tail{&body, std::next(cursor), operation.getResults(), continuation};
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                mlir::IntegerAttr integer;
                (void)mlir::matchPattern(values.lookupOrDefault(branch.getCondition()),
                                         mlir::m_Constant(&integer));
                bool protectedValues = terminal.protectedTry();
                for (auto type : branch.getResultTypes()) {
                    if (!step()) { return {}; }
                    protectedValues &= llvm::isa<ctjs::ValueType>(type);
                }
                // A constant condition selects one arm, and only that arm is
                // copied. Inside a try whose results are all JavaScript values
                // both arms are copied instead, since either can reach the
                // protected terminal.
                if (integer && integer.getType().isInteger(1) && !protectedValues) {
                    auto & selected = integer.getValue().isZero() ? branch.getElseRegion()
                                                                  : branch.getThenRegion();
                    if (selected.empty() && branch.getNumResults() == 0) {
                        return self(self, body, tail.next, values, at, continuation, terminal,
                                    depth + 1);
                    }
                    if (!selected.hasOneBlock() || selected.front().getNumArguments()) {
                        return fail("DOM helper completion has an incomplete selected arm");
                    }
                    // As with a switch, follow only the selected arm. The
                    // `visited` check after emit still requires every source
                    // operation to be reached on some path.
                    return self(self, selected.front(), selected.front().begin(), values, at, &tail,
                                terminal, depth + 1);
                }
                // Arms whose results are all defined JavaScript values are
                // joined by an scf.if before their common continuation.
                // Completion tags and poison slots are not joined: the
                // continuation is copied into each arm so tags stay exact.
                // Nor is a condition tested again later, which may close an
                // iterator and throw before the saved result is observed; its
                // later test is folded to a constant in each copy.
                const bool repeated = repeatedCondition(branch);
                bool join = !repeated;
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
                if (join && !carryAll(joined, branch.getResultTypes())) { return {}; }
                auto & armTerminal = join ? joined : terminal;
                // ponytail: any other continuation is copied into each arm,
                // which the work budget bounds. Merging arms at a common
                // continuation is left for when a proof of the general join
                // exists.
                auto copied =
                    mlir::scf::IfOp::create(at, branch.getLoc(), armTerminal.types,
                                            values.lookupOrDefault(branch.getCondition()));
                ++operationCount;
                for (auto [source, target] :
                     llvm::zip(branch->getRegions(), copied->getRegions())) {
                    mlir::IRMapping path;
                    if (!copyMapping(values, path, false)) { return {}; }
                    target.emplaceBlock();
                    mlir::OpBuilder nested(&target.front(), target.front().begin());
                    if (repeated) {
                        if (!step()) { return {}; }
                        auto selected = mlir::arith::ConstantIntOp::create(
                            nested, branch.getLoc(), &source == &branch.getThenRegion(), 1);
                        path.map(branch.getCondition(), selected.getResult());
                        ++operationCount;
                    }
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
                        return fail("DOM helper completion has an incomplete branch");
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
                auto integer = exactTag(selector.getDefiningOp<mlir::arith::ConstantOp>(), 63);
                if (!integer) {
                    return fail("DOM helper completion selector is not an exact constant");
                }
                auto * selected = &switcher.getDefaultRegion();
                for (auto [index, key] : llvm::enumerate(switcher.getCases())) {
                    if (!step()) { return {}; }
                    if (key == integer.getInt()) { selected = &switcher.getCaseRegions()[index]; }
                }
                if (!selected->hasOneBlock() || selected->front().getNumArguments()) {
                    return fail("DOM helper completion has an incomplete switch arm");
                }
                return self(self, selected->front(), selected->front().begin(), values, at, &tail,
                            terminal, depth + 1);
            }
            if (llvm::isa<ctjs::InvokeOp, ctjs::TryOp>(operation)) {
                auto * invocation = &operation;
                bool recursive = terminal.protectedTry();
                const auto inputs = invocation->walk([&](mlir::Operation * nested) {
                    if (!step()) { return mlir::WalkResult::interrupt(); }
                    for (mlir::Value operand : nested->getOperands()) {
                        if (!step()) { return mlir::WalkResult::interrupt(); }
                        auto mapped = values.lookupOrNull(operand);
                        recursive |=
                            mapped && static_cast<bool>(mapped.getDefiningOp<mlir::ub::PoisonOp>());
                    }
                    return mlir::WalkResult::advance();
                });
                if (inputs.wasInterrupted()) { return {}; }
                // Copy the invocation, its error suppression and both
                // continuations verbatim. The DOM entry proof checks their
                // host effects later.
                const auto checked = invocation->walk([&](mlir::Operation * nested) {
                    // Charge verification and cloning before either traverses
                    // this opaque region without a work callback.
                    if (!drain(uint64_t(3) *
                               (1 + nested->getNumOperands() + nested->getNumResults() +
                                nested->getAttrs().size()))) {
                        return mlir::WalkResult::interrupt();
                    }
                    for (auto & region : nested->getRegions()) {
                        for (auto & block : region) {
                            if (!step()) { return mlir::WalkResult::interrupt(); }
                            for (auto argument : block.getArguments()) {
                                (void)argument;
                                if (!step()) { return mlir::WalkResult::interrupt(); }
                            }
                        }
                    }
                    if (!recursive && llvm::isa<mlir::ub::PoisonOp>(nested) &&
                        !llvm::isa<ctjs::TryOp>(invocation)) {
                        return halt("DOM helper invocation contains inactive source state");
                    }
                    if (!recursive) { visited.insert(nested); }
                    for (mlir::Value operand : nested->getOperands()) {
                        if (!step()) { return mlir::WalkResult::interrupt(); }
                        auto * owner = operand.getParentBlock()->getParentOp();
                        if (owner == invocation || invocation->isAncestor(owner)) { continue; }
                        if (!values.contains(operand) || !dominance.dominates(operand, nested) ||
                            (!recursive &&
                             values.lookup(operand).getDefiningOp<mlir::ub::PoisonOp>())) {
                            return halt(
                                "DOM helper invocation observes an inactive or unbound value");
                        }
                    }
                    ++operationCount;
                    return mlir::WalkResult::advance();
                });
                if (checked.wasInterrupted()) { return {}; }
                if (mlir::failed(mlir::verify(invocation))) {
                    return fail("DOM helper completion has an invalid invocation");
                }
                if (recursive) {
                    auto attempt = llvm::dyn_cast<ctjs::TryOp>(operation);
                    auto invoke = llvm::dyn_cast<ctjs::InvokeOp>(operation);
                    const auto types = attempt ? mlir::TypeRange(operation.getResultTypes())
                                               : mlir::TypeRange(terminal.types);
                    // ponytail: accepts only JavaScript-value results; refuses a
                    // protected call ending in a loop condition until that
                    // tuple has its own proof.
                    for (auto type : types) {
                        if (!step()) { return {}; }
                        if (!llvm::isa<ctjs::ValueType>(type)) {
                            return fail("DOM helper invocation requires source result slots");
                        }
                    }
                    mlir::OperationState built(operation.getLoc(), operation.getName());
                    built.addTypes(types);
                    built.addAttributes(operation.getAttrs());
                    for (auto & region : operation.getRegions()) {
                        (void)region;
                        if (!step()) { return {}; }
                        built.addRegion();
                    }
                    // A poison saved-state value is padding only if the matching
                    // unwind argument is never used in the continuation. Keep
                    // every live saved value and the original slot count.
                    llvm::SmallVector<mlir::Value> saved;
                    if (invoke) {
                        auto exit = llvm::cast<ctjs::InvokeExitOp>(invoke.getBody().front().back());
                        for (auto operand : exit.getState()) {
                            if (!step()) { return {}; }
                            auto value = values.lookupOrNull(operand);
                            if (!value) {
                                return fail("DOM helper invocation has unbound saved state");
                            }
                            if (value.getDefiningOp<mlir::ub::PoisonOp>()) {
                                value = undefinedFiller(at, operation.getLoc());
                            }
                            saved.push_back(value);
                        }
                    }
                    auto * copied = at.create(built);
                    ++operationCount;
                    for (auto [source, target] :
                         llvm::zip(operation.getRegions(), copied->getRegions())) {
                        mlir::IRMapping path;
                        if (!copyMapping(values, path)) { return {}; }
                        auto & output = target.emplaceBlock();
                        mlir::OpBuilder nested(&output, output.end());
                        for (auto [index, argument] :
                             llvm::enumerate(source.front().getArguments())) {
                            if (!step()) { return {}; }
                            mlir::Value replacement =
                                output.addArgument(argument.getType(), argument.getLoc());
                            if (invoke && &source == &invoke.getUnwindBody() && index) {
                                auto exit =
                                    llvm::cast<ctjs::InvokeExitOp>(invoke.getBody().front().back());
                                auto original = values.lookup(exit.getState()[index - 1]);
                                if (original.getDefiningOp<mlir::ub::PoisonOp>()) {
                                    replacement = original;
                                }
                            }
                            path.map(argument, replacement);
                        }
                        if (invoke && &source == &invoke.getBody()) {
                            auto & call = source.front().front();
                            for (auto operand : call.getOperands()) {
                                if (!step()) { return {}; }
                                auto value = path.lookupOrNull(operand);
                                if (!value || value.getDefiningOp<mlir::ub::PoisonOp>()) {
                                    return fail(
                                        "DOM helper invocation observes an inactive call operand");
                                }
                            }
                            auto * called = nested.clone(call, path);
                            ctjs::InvokeExitOp::create(nested, operation.getLoc(),
                                                       called->getResult(0), saved);
                            visited.insert(&call);
                            visited.insert(source.front().getTerminator());
                            operationCount += 2;
                            continue;
                        }
                        Terminal protectedTerminal;
                        if (attempt) {
                            const bool body = &source == &attempt.getBody();
                            protectedTerminal.kind = body ? Terminal::tryExit : Terminal::tryYield;
                            for (auto operand : source.front().getTerminator()->getOperands()) {
                                (void)operand;
                                if (!step()) { return {}; }
                                protectedTerminal.types.push_back(
                                    ctjs::ValueType::get(function.getContext()));
                            }
                        }
                        auto result = self(self, source.front(), source.front().begin(), path,
                                           nested, attempt ? nullptr : &tail,
                                           attempt ? protectedTerminal : terminal, depth + 1);
                        if (!result) { return {}; }
                        if (attempt && &source == &attempt.getBody()) {
                            auto flag =
                                ctjs::TruthyOp::create(nested, operation.getLoc(), result->front());
                            ctjs::TryExitOp::create(nested, operation.getLoc(), flag, (*result)[1],
                                                    mlir::ValueRange(*result).drop_front(2));
                            operationCount += 2;
                        } else if (attempt) {
                            ctjs::TryYieldOp::create(nested, operation.getLoc(), result->front());
                            ++operationCount;
                        } else {
                            ctjs::InvokeYieldOp::create(nested, operation.getLoc(), *result);
                            ++operationCount;
                        }
                    }
                    if (invoke) { return llvm::SmallVector<mlir::Value>(copied->getResults()); }
                    values.map(operation.getResults(), copied->getResults());
                    continue;
                }
                at.clone(operation, values);
                continue;
            }
            if (operation.getNumRegions() || operation.getNumSuccessors()) {
                return fail("DOM helper completion requires acyclic structured source");
            }
            if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation);
                truth && terminal.protectedTry()) {
                if (auto flag =
                        ctjs::constantAttr<ctjs::BooleanAttr>(values.lookup(truth.getValue()))) {
                    auto bit = mlir::arith::ConstantIntOp::create(at, operation.getLoc(),
                                                                  flag.getValue(), 1);
                    values.map(truth.getResult(), bit.getResult());
                    ++operationCount;
                    continue;
                }
            }
            if (auto truncate = llvm::dyn_cast<mlir::arith::TruncIOp>(operation)) {
                mlir::IntegerAttr integer;
                if (mlir::matchPattern(values.lookup(truncate.getIn()),
                                       mlir::m_Constant(&integer)) &&
                    truncate.getType().isInteger(1)) {
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
                // Two-way completion switches are lowered to index casts and
                // integer comparisons; fold them with MLIR's own folders.
                llvm::SmallVector<mlir::Value> folded;
                if (mlir::succeeded(at.tryFold(copied, folded)) && !folded.empty()) {
                    values.map(operation.getResults(), folded);
                    copied->erase();
                }
            }
            ++operationCount;
        }
        return fail("DOM helper completion has no return or yield");
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
            return halt("DOM helper completion contains unvisited source operations");
        }
        for (mlir::Value value : operation->getResults()) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            inactiveFillers.erase(value);
        }
        return mlir::WalkResult::advance();
    });
    if (complete.wasInterrupted()) { return false; }
    if (terminalThrow) {
        // Rethrow the saved payload where the original threw, keeping each
        // path's effects. Only the iterator-close rewrite (DOM/Iteration)
        // may suppress it.
        ctjs::ThrowOp::create(at, function.getLoc(), result->front());
    } else {
        ctjs::ReturnOp::create(at, function.getLoc(), result->front());
    }
    // Remove only completion arithmetic left unused. Every source effect
    // stays for checkBody, which the caller runs next.
    if (!eraseUnusedArithmetic(normalized)) { return false; }
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
        // Inlining can make a real value available for a slot that was padded
        // (for example an element behind an iterator call). Only fillers this
        // DOMSource recorded are changed. Follow the same scf.if result slot to
        // a value that already dominates the filler; never move its producer.
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
