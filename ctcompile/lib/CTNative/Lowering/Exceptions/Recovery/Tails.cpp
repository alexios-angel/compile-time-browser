// recovery - the normal and catch tails: collecting an acyclic, handler-balanced
// tail and cloning it into a try region with completion terminators.
//
// One of six files carved out of a 1,251-line Exceptions/Recovery.cpp on
// 2026-09-08. All are member functions of `recovery`, the disposable-clone
// transaction declared in Attempt.h beside this; Exceptions/Recovery.h, the
// header the rest of the lowering sees, did not change.

#include "Attempt.h"

namespace ctcompile::ctnative::lowering_detail {

bool recovery::collect(tail & result, mlir::Block * start, bool initiallyActive, bool isCatch) {
    llvm::SmallVector<std::pair<mlir::Block *, bool>> pending{{start, initiallyActive}};
    while (!pending.empty()) {
        if (!spend()) { return false; }
        auto [block, active] = pending.pop_back_val();
        if (block == &function.getBody().front() || block->getParent() != &function.getBody() ||
            block->getNumArguments() != width || (!isCatch && block == push.getHandler())) {
            return reject("native exception tail leaves its preserved register CFG");
        }
        auto [known, inserted] = result.active.try_emplace(block, active);
        if (!inserted) {
            if (known->second != active) {
                return reject("native exception handler balance differs at a CFG join");
            }
            continue;
        }
        result.blocks.push_back(block);
        for (mlir::Operation & operation : block->without_terminator()) {
            if (!spend()) { return false; }
            if (llvm::isa<ctjs::PopHandlerOp>(operation)) {
                if (!active) { return reject("native exception path pops an inactive handler"); }
                active = false;
            }
            if (auto found = llvm::dyn_cast<ctjs::CatchLandOp>(operation)) {
                if (!isCatch || block != start || found != landing) {
                    return reject("native exception tail contains another catch landing");
                }
            }
        }
        auto * term = block->getTerminator();
        if (llvm::isa<ctjs::ThrowOp>(term)) {
            if (!active || isCatch) {
                return reject(
                    "native exception recovery does not catch a throw outside the active try");
            }
            for (mlir::Operation & operation : block->without_terminator()) {
                if (!llvm::isa<ctjs::RootOp>(operation)) {
                    return reject("native throw needs an unchanged throw-site register block");
                }
            }
            ++throws;
            continue;
        }
        if (llvm::isa<ctjs::ReturnOp>(term)) {
            // A function return also leaves an active handler; the
            // importer emits frame_exit rather than a separate pop for
            // return inside try. Completion returns from the recovered
            // scope and never re-enters its catch.
            continue;
        }
        if (auto check = llvm::dyn_cast<ctjs::CheckOp>(term)) {
            // Completed computations can change registers before a later
            // status check, so its handler values need not equal block
            // entry arguments. Keep the normal edge's values verbatim.
            // Discarding its handler edge still requires admission to
            // prove every protected operation nonthrowing; explicit
            // throws still need an unchanged throw-site block above.
            if (!active || check.getHandler() != push.getHandler() ||
                check.getHandlerOperands().size() != width) {
                return reject(
                    "native exception check lacks a complete register vector for its handler");
            }
            if (mode != ExceptionRecoveryMode::ExplicitThrows && !inspectInvocation(check)) {
                return false;
            }
            result.edges[block].push_back(check.getCont());
        } else if (llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(
                       term)) {
            for (mlir::Block * successor : term->getSuccessors()) {
                result.edges[block].push_back(successor);
            }
        } else {
            return reject("native exception tail has an unsupported control-flow exit");
        }
        for (mlir::Block * successor : result.edges[block]) {
            if (!spend()) { return false; }
            pending.emplace_back(successor, active);
        }
    }
    // Kahn's algorithm rejects cycles without recursion or speculative
    // unrolling; shared normal/catch continuations remain ordinary DAGs.
    llvm::DenseMap<mlir::Block *, unsigned> incoming;
    for (mlir::Block * block : result.blocks) {
        for (mlir::Block * successor : result.edges[block]) {
            if (!spend()) { return false; }
            ++incoming[successor];
        }
    }
    llvm::SmallVector<mlir::Block *> ready;
    for (mlir::Block * block : result.blocks) {
        if (incoming.lookup(block) == 0) { ready.push_back(block); }
    }
    unsigned visited = 0;
    while (!ready.empty()) {
        if (!spend()) { return false; }
        auto * block = ready.pop_back_val();
        ++visited;
        for (mlir::Block * successor : result.edges[block]) {
            if (--incoming[successor] == 0) { ready.push_back(successor); }
        }
    }
    return visited == result.blocks.size() ||
           reject("native exception recovery does not support loops");
}

bool recovery::cloneTail(const tail & plan, mlir::Region & destination, bool isCatch) {
    mlir::IRMapping mapping;
    auto * start = plan.blocks.front();
    for (mlir::Block * block : plan.blocks) {
        if (!spend(uint64_t(1) + block->getNumArguments())) { return false; }
        auto * copy = new mlir::Block;
        destination.push_back(copy);
        mapping.map(block, copy);
        if (block == start && !isCatch) {
            for (auto [argument, value] :
                 llvm::zip(block->getArguments(), push.getBodyOperands())) {
                mapping.map(argument, value);
            }
            continue;
        }
        if (block == start) {
            copy->addArgument(ctjs::ValueType::get(function.getContext()), landing.getLoc());
        }
        for (mlir::BlockArgument argument : block->getArguments()) {
            mapping.map(argument, copy->addArgument(argument.getType(), argument.getLoc()));
        }
    }
    if (isCatch) { mapping.map(landing.getThrown(), destination.front().getArgument(0)); }
    struct operandsToMap {
        mlir::Operation * operation;
        llvm::SmallVector<mlir::Value> values;
    };
    llvm::SmallVector<operandsToMap> operands;
    for (mlir::Block * block : plan.blocks) {
        mlir::OpBuilder builder = mlir::OpBuilder::atBlockEnd(mapping.lookup(block));
        ctjs::InvokeOp invocation;
        for (mlir::Operation & operation : *block) {
            if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
            if (llvm::isa<ctjs::PopHandlerOp, ctjs::FrameExitOp, ctjs::CatchLandOp>(operation)) {
                continue;
            }
            mlir::Operation * copied = nullptr;
            llvm::SmallVector<mlir::Value> sources;
            if (!isCatch &&
                invocations.lookup(block->getTerminator()).getOperation() == &operation) {
                // The invocation returns a value-only completion tuple:
                // JS boolean, normal result, thrown payload, saved state.
                // Convert the boolean only after leaving the invocation;
                // try_exit's i1 never crosses a CTJS value region edge.
                if (!spend(uint64_t(20) + width * uint64_t(4))) { return false; }
                auto type = ctjs::ValueType::get(function.getContext());
                mlir::OperationState state(operation.getLoc(), "ctjs.invoke");
                llvm::SmallVector<mlir::Type> types(width + 3, type);
                state.addTypes(types);
                for (unsigned index = 0; index != 3; ++index) { state.addRegion(); }
                invocation = llvm::cast<ctjs::InvokeOp>(builder.create(state));
                auto & body = invocation.getBody().emplaceBlock();
                auto & normal = invocation.getNormalBody().emplaceBlock();
                auto & unwind = invocation.getUnwindBody().emplaceBlock();
                normal.addArgument(type, operation.getLoc());
                for (unsigned index = 0; index != width + 1; ++index) {
                    unwind.addArgument(type, operation.getLoc());
                }
                mlir::OpBuilder callBuilder = mlir::OpBuilder::atBlockEnd(&body);
                copied = callBuilder.clone(operation, mapping);
                llvm::append_range(sources, operation.getOperands());
                auto check = llvm::cast<ctjs::CheckOp>(block->getTerminator());
                llvm::SmallVector<mlir::Value> stateValues{copied->getResult(0)};
                llvm::append_range(stateValues, check.getHandlerOperands());
                mlir::OperationState dispatch(operation.getLoc(), "ctjs.invoke_exit");
                dispatch.addOperands(stateValues);
                operands.push_back({callBuilder.create(dispatch), std::move(stateValues)});
                mapping.map(operation.getResult(0), invocation.getResult(1));
                for (auto [continuation, failed] :
                     {std::pair{&normal, false}, std::pair{&unwind, true}}) {
                    mlir::OpBuilder at = mlir::OpBuilder::atBlockEnd(continuation);
                    auto flag = ctjs::ConstantOp::create(
                        at, operation.getLoc(), type,
                        ctjs::BooleanAttr::get(function.getContext(), failed));
                    auto poison = mlir::ub::PoisonOp::create(at, operation.getLoc(), type);
                    llvm::SmallVector<mlir::Value> completion{flag.getResult()};
                    if (failed) {
                        completion.push_back(poison.getResult());
                        llvm::append_range(completion, unwind.getArguments());
                    } else {
                        completion.push_back(normal.getArgument(0));
                        completion.append(width + 1, poison.getResult());
                    }
                    mlir::OperationState yield(operation.getLoc(), "ctjs.invoke_yield");
                    yield.addOperands(completion);
                    at.create(yield);
                }
            } else if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                mlir::OperationState state(operation.getLoc(),
                                           isCatch ? "ctjs.try_yield" : "ctjs.try_exit");
                if (!isCatch) {
                    sources.push_back(
                        mlir::arith::ConstantIntOp::create(builder, operation.getLoc(), 0, 1));
                }
                sources.push_back(returned.getValue());
                if (!isCatch) {
                    auto poison = mlir::ub::PoisonOp::create(
                        builder, operation.getLoc(), ctjs::ValueType::get(function.getContext()));
                    sources.append(width + 1, poison.getResult());
                }
                state.addOperands(sources);
                copied = builder.create(state);
            } else if (auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(operation)) {
                sources.push_back(
                    mlir::arith::ConstantIntOp::create(builder, operation.getLoc(), 1, 1));
                sources.push_back(
                    mlir::ub::PoisonOp::create(builder, operation.getLoc(),
                                               ctjs::ValueType::get(function.getContext()))
                        .getResult());
                sources.push_back(thrown.getValue());
                llvm::append_range(sources, block->getArguments());
                mlir::OperationState state(operation.getLoc(), "ctjs.try_exit");
                state.addOperands(sources);
                copied = builder.create(state);
            } else if (auto check = llvm::dyn_cast<ctjs::CheckOp>(operation)) {
                if (invocation) {
                    auto * thrown = new mlir::Block;
                    destination.push_back(thrown);
                    mlir::OpBuilder at = mlir::OpBuilder::atBlockEnd(thrown);
                    auto flag = mlir::arith::ConstantIntOp::create(at, operation.getLoc(), 1, 1);
                    auto poison = mlir::ub::PoisonOp::create(
                        at, operation.getLoc(), ctjs::ValueType::get(function.getContext()));
                    llvm::SmallVector<mlir::Value> completion{flag.getResult(), poison.getResult()};
                    llvm::append_range(completion, invocation.getResults().drop_front(2));
                    mlir::OperationState exit(operation.getLoc(), "ctjs.try_exit");
                    exit.addOperands(completion);
                    at.create(exit);
                    auto failed = ctjs::TruthyOp::create(
                        builder, operation.getLoc(), builder.getI1Type(), invocation.getResult(0));
                    llvm::append_range(sources, check.getContOperands());
                    copied = mlir::cf::CondBranchOp::create(
                        builder, operation.getLoc(), failed.getResult(), thrown, mlir::ValueRange{},
                        mapping.lookup(check.getCont()), sources);
                    sources.insert(sources.begin(), failed.getResult());
                } else {
                    llvm::append_range(sources, check.getContOperands());
                    copied = mlir::cf::BranchOp::create(builder, operation.getLoc(),
                                                        mapping.lookup(check.getCont()), sources);
                }
            } else {
                copied = builder.clone(operation, mapping);
                llvm::append_range(sources, operation.getOperands());
            }
            operands.push_back({copied, std::move(sources)});
        }
    }
    // Imported block order need not be dominance order. Remap operands
    // only after every defining operation has a clone.
    for (auto & pending : operands) {
        for (auto [index, source] : llvm::enumerate(pending.values)) {
            if (!spend()) { return false; }
            auto value = mapping.lookupOrDefault(source);
            if (value == source && source.getParentBlock()->getParent() == &function.getBody() &&
                source.getParentBlock() != &function.getBody().front()) {
                return reject("native exception tail captures an unmapped register definition");
            }
            pending.operation->setOperand(static_cast<unsigned>(index), value);
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
