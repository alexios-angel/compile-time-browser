//===- Expansion.cpp - Inline DOM helper calls ------------------*- C++ -*-===//
//
// DOMSource::inlineCall, which inlines one call of a helper (including a call
// protected by an invoke, whose exception edge must survive), and
// DOMSource::expand, the recursive driver that normalizes a helper and
// inlines everything it calls. "The DOM entry proof" below means the typed
// DOM entry analysis that callers of expandDOMHelpers run on the result; it
// rejects any throw, coercion, reentry or mutation the rewrites here assume
// away.
//
//===----------------------------------------------------------------------===//

#include "Proof.hpp"
#include "mlir/IR/Verifier.h"

namespace ctcompile::ctnative::dom_source_detail {

bool DOMSource::inlineCall(ctjs::FuncOp function, ctjs::FuncOp target, mlir::Operation * call,
                           mlir::ValueRange arguments, mlir::Value receiver, mlir::Value callee,
                           llvm::MutableArrayRef<Capture> captures, unsigned depth) {
    ctjs::InvokeOp protectedInvocation;
    ctjs::CallOp protectedLeaf, secondLeaf, selectorLeaf;
    llvm::DenseSet<mlir::Operation *> suffixLeaves;
    bool selectorFeedsWrite = false;
    ctjs::CreateObjectOp discardedResult;
    llvm::DenseSet<mlir::Operation *> discardedFields;
    // A missing JavaScript argument is undefined. checkBody, run by expand
    // before inlining, has already refused helpers that read `arguments` or
    // `new.target`, so this padding cannot be observed.
    const auto bindArguments = [&](mlir::OpBuilder & at, mlir::IRMapping & mapping) {
        auto & body = target.getBody().front();
        mapping.map(body.getArgument(ctjs::arg_callee), callee);
        mapping.map(body.getArgument(ctjs::arg_receiver), receiver);
        for (auto [index, formal] :
             llvm::enumerate(body.getArguments().drop_front(ctjs::implicit_arguments))) {
            if (!step()) { return false; }
            mlir::Value actual = index < arguments.size() ? arguments[index] : mlir::Value{};
            if (!actual) {
                actual = ctjs::ConstantOp::create(at, call->getLoc(),
                                                  ctjs::UndefinedAttr::get(call->getContext()));
                ++operationCount;
            }
            mapping.map(formal, actual);
        }
        return true;
    };
    // Bind captured values separately at each inlined call, including loads
    // inside branches, so a body shared by several callers is never bound to
    // the first caller's captures.
    const auto bindCapture = [&](ctjs::LoadUpvalueOp load, mlir::OpBuilder & at,
                                 mlir::IRMapping & mapping) {
        auto & capture = captures[static_cast<unsigned>(load.getIndex())];
        mlir::Value value;
        if (capture.enclosingIndex >= 0) {
            value = ctjs::LoadUpvalueOp::create(
                at, load.getLoc(), load.getType(),
                function.getBody().front().getArgument(ctjs::arg_callee), capture.enclosingIndex);
            ++operationCount;
        } else {
            value = capture.value();
        }
        mapping.map(load.getResult(), value);
    };
    auto rethrow = savedThrow(call->getNextNode());
    if (!rethrow || call->getNumResults() != 1 || !call->getResult(0).hasOneUse() ||
        rethrow.getValue() != call->getResult(0)) {
        rethrow = {};
    }
    if (auto invocation = llvm::dyn_cast_or_null<ctjs::InvokeOp>(call->getParentOp());
        invocation && call->getParentRegion() == &invocation.getBody()) {
        if (invocation.getNumResults()) {
            auto returned = llvm::dyn_cast<ctjs::ReturnOp>(target.getBody().front().back());
            auto calls = target.getBody().front().getOps<ctjs::CallOp>();
            // The caller's invoke protects every call in the helper, but the
            // helper's local results are not part of the caller's saved state.
            // So each helper call gets its own nested invoke: success continues
            // into the next call, and failure gets a copy of the caller's saved
            // state and unwind continuation.
            // ponytail: accepts only straight-line hasAttribute/setAttribute
            // chains and fresh data records; refuses other effects until they
            // have their own effect proof.
            unsigned callCount = 0;
            bool chain = static_cast<bool>(returned);
            for (auto & operation : target.getBody().front()) {
                const uint64_t cost =
                    2 * (uint64_t(1) + operation.getNumOperands() + operation.getNumResults());
                if (cost > remaining) { return refuse("DOM helper chain work budget exhausted"); }
                remaining -= static_cast<unsigned>(cost);
                if (auto leaf = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                    ++callCount;
                    auto method = leaf.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    chain &= method && leaf.getReceiver() == method.getObject() &&
                             (ctjs::constantKey(method.getKey()) == "hasAttribute" ||
                              ctjs::constantKey(method.getKey()) == "setAttribute");
                } else if (auto method = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                    chain &= ctjs::constantKey(method.getKey()) == "hasAttribute" ||
                             ctjs::constantKey(method.getKey()) == "setAttribute";
                } else if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
                    chain &= dataObject(object);
                } else if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                    chain &= static_cast<bool>(
                                 store.getObject().getDefiningOp<ctjs::CreateObjectOp>()) &&
                             ctjs::ordinaryKey(ctjs::constantKey(store.getKey()));
                } else {
                    chain &=
                        llvm::isa<ctjs::ConstantOp, ctjs::LoadUpvalueOp, ctjs::RootOp,
                                  ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp>(operation);
                }
            }
            if (chain && callCount > 1 && callCount < 64 - depth) {
                uint64_t copyCost = 0;
                unsigned copyOperations = 0;
                bool deferredCall = false;
                const auto counted = invocation.walk([&](mlir::Operation * operation) {
                    ++copyOperations;
                    // The caller has already collected the local calls to
                    // expand; copying a call in the unwind body would add one
                    // it does not know about.
                    deferredCall |=
                        invocation.getUnwindBody().isAncestor(operation->getParentRegion()) &&
                        llvm::isa<ctjs::CallOp, ctjs::CallDirectOp, ctjs::CreateClosureOp>(
                            operation);
                    const uint64_t cost = operationWork(operation, false);
                    if (cost > remaining) { return mlir::WalkResult::interrupt(); }
                    remaining -= static_cast<unsigned>(cost);
                    copyCost += cost;
                    return mlir::WalkResult::advance();
                });
                // Charge for verification and for one copy of the whole invoke
                // (unwind body included) per helper call, before changing IR.
                if (counted.wasInterrupted() || copyCost > remaining / (callCount + 1) ||
                    deferredCall || mlir::failed(mlir::verify(invocation))) {
                    return refuse("DOM helper chain lost its invocation correspondence or budget");
                }
                remaining -= static_cast<unsigned>(copyCost * (callCount + 1));
                operationCount += copyOperations * (callCount + 1);
                mlir::OpBuilder at(invocation);
                mlir::IRMapping mapping;
                auto & body = target.getBody().front();
                if (!bindArguments(at, mapping)) { return false; }
                auto exit = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
                const auto emit =
                    [&](auto && self, mlir::Block::iterator cursor,
                        mlir::OpBuilder & into) -> mlir::FailureOr<llvm::SmallVector<mlir::Value>> {
                    for (; cursor != body.end(); ++cursor) {
                        auto & operation = *cursor;
                        if (!step()) { return mlir::failure(); }
                        if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(
                                operation)) {
                            continue;
                        }
                        if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                            bindCapture(load, into, mapping);
                        } else if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                            auto & normal = invocation.getNormalBody().front();
                            normal.getArgument(0).replaceAllUsesWith(
                                mapping.lookup(result.getValue()));
                            // Move (not copy) the one success continuation, so
                            // calls the caller already collected stay valid.
                            for (auto & next :
                                 llvm::make_early_inc_range(normal.without_terminator())) {
                                next.moveBefore(into.getBlock(), into.getInsertionPoint());
                            }
                            return llvm::SmallVector<mlir::Value>(
                                llvm::cast<ctjs::InvokeYieldOp>(normal.back()).getValues());
                        } else if (llvm::isa<ctjs::CallOp>(operation)) {
                            mlir::OperationState state(operation.getLoc(),
                                                       ctjs::InvokeOp::getOperationName());
                            state.addTypes(invocation.getResultTypes());
                            for (unsigned index = 0; index != 3; ++index) { state.addRegion(); }
                            auto nested = llvm::cast<ctjs::InvokeOp>(into.create(state));
                            auto & called = nested.getBody().emplaceBlock();
                            auto & normal = nested.getNormalBody().emplaceBlock();
                            auto payload = normal.addArgument(
                                ctjs::ValueType::get(call->getContext()), operation.getLoc());
                            mlir::OpBuilder callAt = mlir::OpBuilder::atBlockEnd(&called);
                            auto * cloned = callAt.clone(operation, mapping);
                            ctjs::InvokeExitOp::create(callAt, operation.getLoc(),
                                                       cloned->getResult(0), exit.getState());
                            mapping.map(operation.getResult(0), payload);
                            mlir::IRMapping unwindMapping;
                            invocation.getUnwindBody().cloneInto(&nested.getUnwindBody(),
                                                                 unwindMapping);
                            mlir::OpBuilder normalAt = mlir::OpBuilder::atBlockEnd(&normal);
                            auto values = self(self, std::next(cursor), normalAt);
                            if (mlir::failed(values)) { return mlir::failure(); }
                            ctjs::InvokeYieldOp::create(normalAt, operation.getLoc(), *values);
                            operationCount += 4;
                            return llvm::SmallVector<mlir::Value>(nested.getResults());
                        } else {
                            into.clone(operation, mapping);
                            ++operationCount;
                        }
                    }
                    return mlir::failure();
                };
                auto values = emit(emit, body.begin(), at);
                if (mlir::failed(values)) {
                    return refuse("DOM helper chain lost its normal result");
                }
                invocation.replaceAllUsesWith(*values);
                // The caller erases its now-unused helper call after inlineCall.
                call->moveBefore(invocation);
                invocation.erase();
                return true;
            }
            auto leaf = llvm::hasSingleElement(calls) ? *calls.begin() : ctjs::CallOp{};
            auto method = leaf ? leaf.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                               : ctjs::GetPropertyOp{};
            bool forwards = returned && method &&
                            (returned.getValue() == leaf.getResult() ||
                             llvm::isa<mlir::BlockArgument>(returned.getValue()) ||
                             returned.getValue().getDefiningOp<ctjs::ConstantOp>() ||
                             returned.getValue().getDefiningOp<ctjs::LoadUpvalueOp>()) &&
                            leaf.getReceiver() == method.getObject() &&
                            (ctjs::constantKey(method.getKey()) == "hasAttribute" ||
                             ctjs::constantKey(method.getKey()) == "setAttribute");
            // Keep the call's exception edge as well as its result: only the
            // single hasAttribute/setAttribute call stays inside the invoke,
            // and the inert operations preparing it move out. The DOM entry
            // proof later checks the bound receiver and method.
            // ponytail: accepts one call and a result that is that call, a
            // parameter, a constant or a capture; refuses more effects or
            // computed results until they have their own completion proof.
            for (auto & operation : target.getBody().front()) {
                if (!step()) { return false; }
                forwards &=
                    &operation == leaf || &operation == method ||
                    llvm::isa<ctjs::ConstantOp, ctjs::LoadUpvalueOp, ctjs::RootOp,
                              ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp>(operation);
            }
            if (forwards) {
                const auto counted = invocation.walk([&](mlir::Operation * operation) {
                    const uint64_t cost = operationWork(operation, false);
                    if (cost > remaining / 2) { return mlir::WalkResult::interrupt(); }
                    remaining -= static_cast<unsigned>(cost * 2);
                    return mlir::WalkResult::advance();
                });
                if (counted.wasInterrupted() || mlir::failed(mlir::verify(invocation))) {
                    return refuse("DOM forwarded helper lost its invocation correspondence");
                }
                protectedInvocation = invocation;
                protectedLeaf = leaf;
            } else {
                // The invoke's result is used, so the call may leave the invoke
                // only if proveUnusedBody shows the helper cannot throw for any
                // input. Then the normal continuation is projected onto the
                // call's result (the unwind state is dropped) and the ordinary
                // inlining below binds that result.
                if (!proveUnusedBody(target)) { return false; }
                mlir::OpBuilder at(invocation);
                auto projected = host_detail::projectInvocationContinuation(
                    invocation, false, call->getResult(0), at, remaining);
                if (mlir::failed(projected)) {
                    return refuse("DOM inert helper invocation lost its completion tuple");
                }
                call->moveBefore(invocation);
                invocation.replaceAllUsesWith(*projected);
                invocation.erase();
            }
        }
    }
    if (auto invocation = llvm::dyn_cast_or_null<ctjs::InvokeOp>(call->getParentOp());
        invocation && call->getParentRegion() == &invocation.getBody() && !protectedInvocation) {
        if (!step()) { return false; }
        auto & called = invocation.getBody();
        auto & normal = invocation.getNormalBody();
        auto & unwind = invocation.getUnwindBody();
        auto exit = llvm::dyn_cast_or_null<ctjs::InvokeExitOp>(call->getNextNode());
        const auto emptyContinuation = [](mlir::Region & region) {
            if (!region.hasOneBlock()) { return false; }
            auto & block = region.front();
            auto yield = llvm::hasSingleElement(block)
                             ? llvm::dyn_cast<ctjs::InvokeYieldOp>(block.front())
                             : ctjs::InvokeYieldOp{};
            return yield && yield.getValues().empty() && block.getNumArguments() == 1 &&
                   block.getArgument(0).use_empty();
        };
        if (invocation.getNumResults() || !called.hasOneBlock() ||
            call->getParentRegion() != &called || called.front().getNumArguments() ||
            &called.front().front() != call || !exit || exit->getNextNode() ||
            exit.getNormalResult() != call->getResult(0) || !exit.getState().empty() ||
            !call->getResult(0).hasOneUse() || !emptyContinuation(normal) ||
            !emptyContinuation(unwind)) {
            return refuse("DOM protected helper requires exact unused-result suppression");
        }
        // Keep the setAttribute write protected and its reads in source
        // order, cloned under the original guard. Moving the preparation and
        // hasAttribute reads out is sound only because the DOM entry proof
        // shows the built-in Element methods, called with primitive arguments,
        // cannot throw or re-enter script.
        // ponytail: accept only helpers built from setAttribute/hasAttribute/
        // getAttribute/matches calls: up to two writes, each optionally fed by
        // a preceding read, at most one `matches` selector, then further
        // read/write pairs and unused reads between them. Any other effect is
        // refused until its exception edge is modelled.
        const auto attributeLeaf = [&] {
            ctjs::GetPropertyOp method, readMethod, trailingMethod, secondMethod;
            ctjs::GetPropertyOp selectorMethod, finalMethod, finalReadMethod;
            ctjs::CallOp readCall, trailingCall, finalReadCall;
            llvm::SmallVector<mlir::Value> suffixValues;
            llvm::DenseSet<mlir::OpOperand *> suffixUses;
            llvm::DenseSet<mlir::Value> suffixReads;
            llvm::SmallVector<ctjs::CallOp> consumedSelectors;
            auto result = llvm::dyn_cast<ctjs::ReturnOp>(target.getBody().front().back());
            // Whether `call` invokes `method` on its own receiver with `arity`
            // arguments.
            const auto calls = [](ctjs::CallOp call, ctjs::GetPropertyOp method, unsigned arity) {
                return call.getCallee() == method.getResult() &&
                       call.getReceiver() == method.getObject() && call.getArgs().size() == arity;
            };
            // Record a finished method/read pair in suffixValues/suffixUses, the
            // set of values and uses checked exhaustively at the end of this
            // lambda.
            const auto retainRead = [&](ctjs::GetPropertyOp pairMethod, ctjs::CallOp pairRead) {
                suffixValues.append({pairMethod.getResult(), pairRead.getResult()});
                suffixUses.insert(&pairRead->getOpOperand(0));
                suffixReads.insert(pairRead.getResult());
            };
            const auto selectFeedingRead = [&](ctjs::CallOp leaf,
                                               ctjs::GetPropertyOp & pendingMethod,
                                               ctjs::CallOp & pendingRead) {
                if (pendingMethod && !pendingRead) { return false; }
                if (pendingRead && leaf.getArgs()[1] == pendingRead.getResult()) {
                    suffixReads.insert(pendingRead.getResult());
                    return true;
                }
                if (!suffixReads.contains(leaf.getArgs()[1])) { return !pendingRead; }
                // An earlier standalone read may feed the write. Record any
                // pending argument read in suffixValues so its uses are checked.
                if (pendingRead) { retainRead(pendingMethod, pendingRead); }
                pendingRead = leaf.getArgs()[1].getDefiningOp<ctjs::CallOp>();
                pendingMethod = pendingRead.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                return true;
            };
            for (mlir::Operation & operation : target.getBody().front()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                              ctjs::ReturnOp>(operation)) {
                    continue;
                }
                if (llvm::isa<ctjs::LoadUpvalueOp>(operation)) { continue; }
                // These producers stay under the original guard. The DOM entry
                // proof must exclude coercion, throws and reentry.
                if (!protectedLeaf && llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp,
                                                ctjs::CompareOp, ctjs::TruthyOp>(operation)) {
                    continue;
                }
                if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation);
                    truth && protectedLeaf && !secondLeaf &&
                    suffixReads.contains(truth.getValue()) && truth.getResult().hasOneUse()) {
                    auto guard =
                        llvm::dyn_cast<mlir::scf::IfOp>(*truth.getResult().getUsers().begin());
                    if (!guard || guard->getBlock() != truth->getBlock()) { return false; }
                    suffixValues.push_back(truth.getResult());
                    suffixUses.insert(&truth->getOpOperand(0));
                    suffixUses.insert(&guard->getOpOperand(0));
                    continue;
                }
                if (auto guard = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    if (!protectedLeaf || secondLeaf || secondMethod || trailingMethod ||
                        finalReadMethod || selectorMethod) {
                        return false;
                    }
                    const auto retainTrailingRead = [&] {
                        if (!trailingMethod) { return true; }
                        if (!trailingCall) { return false; }
                        retainRead(trailingMethod, trailingCall);
                        trailingMethod = {};
                        trailingCall = {};
                        return true;
                    };
                    const auto scanGuard = [&](auto && self, mlir::scf::IfOp guard,
                                               unsigned guardDepth) -> bool {
                        auto truth = guard.getCondition().getDefiningOp<ctjs::TruthyOp>();
                        if (guardDepth == 64 || !truth || !truth.getResult().hasOneUse() ||
                            truth->getBlock() != guard->getBlock() ||
                            !suffixReads.contains(truth.getValue()) ||
                            !guard.getThenRegion().hasOneBlock() ||
                            guard.getThenRegion().front().getNumArguments()) {
                            return false;
                        }
                        // A completion join may yield an ignored close payload.
                        // Only forwarding through other checked guards is
                        // allowed; the final use check rejects any other use.
                        for (mlir::Value value : guard.getResults()) {
                            if (!step()) { return false; }
                            suffixValues.push_back(value);
                        }
                        // ponytail: accepts only cleanup guards nested at most
                        // 64 deep; refuses other control flow until its
                        // exception edges are modelled.
                        for (auto & arm : guard->getRegions()) {
                            if (arm.empty()) { continue; }
                            if (!arm.hasOneBlock() || arm.front().getNumArguments()) {
                                return false;
                            }
                            ctjs::GetPropertyOp guardedMethod;
                            for (mlir::Operation & nested : arm.front()) {
                                if (!step()) { return false; }
                                if (llvm::isa<ctjs::ConstantOp, ctjs::LoadUpvalueOp, ctjs::RootOp>(
                                        nested)) {
                                    continue;
                                }
                                if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(nested)) {
                                    if (guardedMethod || !retainTrailingRead() ||
                                        !suffixReads.contains(truth.getValue()) ||
                                        !truth.getResult().hasOneUse()) {
                                        return false;
                                    }
                                    auto branch = llvm::dyn_cast<mlir::scf::IfOp>(
                                        *truth.getResult().getUsers().begin());
                                    if (!branch || branch->getBlock() != truth->getBlock()) {
                                        return false;
                                    }
                                    suffixValues.push_back(truth.getResult());
                                    suffixUses.insert(&truth->getOpOperand(0));
                                    suffixUses.insert(&branch->getOpOperand(0));
                                    continue;
                                }
                                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(nested)) {
                                    if (guardedMethod || trailingMethod ||
                                        !self(self, branch, guardDepth + 1)) {
                                        return false;
                                    }
                                    continue;
                                }
                                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(nested);
                                    read && !guardedMethod &&
                                    ctjs::constantKey(read.getKey()) == "setAttribute") {
                                    guardedMethod = read;
                                    continue;
                                }
                                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(nested);
                                    read && (ctjs::constantKey(read.getKey()) == "hasAttribute" ||
                                             ctjs::constantKey(read.getKey()) == "getAttribute" ||
                                             ctjs::constantKey(read.getKey()) == "matches")) {
                                    if (!retainTrailingRead()) { return false; }
                                    trailingMethod = read;
                                    continue;
                                }
                                if (auto read = llvm::dyn_cast<ctjs::CallOp>(nested);
                                    read && trailingMethod && !trailingCall &&
                                    calls(read, trailingMethod, 1)) {
                                    trailingCall = read;
                                    // This read stays in the branch but leaves the
                                    // invoke; validSelector below and the DOM
                                    // entry proof must exclude throws.
                                    if (ctjs::constantKey(trailingMethod.getKey()) == "matches") {
                                        consumedSelectors.push_back(read);
                                    }
                                    continue;
                                }
                                if (auto leaf = llvm::dyn_cast<ctjs::CallOp>(nested);
                                    leaf && guardedMethod && calls(leaf, guardedMethod, 2) &&
                                    (secondLeaf || trailingCall ||
                                     suffixReads.contains(leaf.getArgs()[1]))) {
                                    if (!selectFeedingRead(leaf, trailingMethod, trailingCall)) {
                                        return false;
                                    }
                                    if (!secondLeaf) {
                                        secondMethod = guardedMethod;
                                        secondLeaf = leaf;
                                    } else {
                                        suffixValues.append(
                                            {guardedMethod.getResult(), leaf.getResult()});
                                        suffixUses.insert(&leaf->getOpOperand(0));
                                        suffixLeaves.insert(leaf);
                                    }
                                    guardedMethod = {};
                                    // Later reads in the branch may replace
                                    // trailingCall, so record this write's use
                                    // of the current one now.
                                    suffixUses.insert(&leaf->getOpOperand(3));
                                    if (!retainTrailingRead()) { return false; }
                                    continue;
                                }
                                if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(nested);
                                    yield && secondLeaf) {
                                    for (mlir::OpOperand & operand : yield->getOpOperands()) {
                                        if (!step()) { return false; }
                                        suffixUses.insert(&operand);
                                    }
                                    continue;
                                }
                                return false;
                            }
                            // A finished read stays recorded, but a pending
                            // method/read pair must not cross into the other arm.
                            if (guardedMethod || !retainTrailingRead()) { return false; }
                        }
                        return bool(secondLeaf);
                    };
                    if (!scanGuard(scanGuard, guard, 0)) { return false; }
                    continue;
                }
                auto & writeMethod = protectedLeaf ? secondMethod : method;
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                    read && !writeMethod && !secondLeaf &&
                    ctjs::constantKey(read.getKey()) == "setAttribute") {
                    writeMethod = read;
                    continue;
                }
                const bool suffixRead = !writeMethod || secondLeaf;
                auto & sourceReadMethod = suffixRead      ? finalReadMethod
                                          : protectedLeaf ? trailingMethod
                                                          : readMethod;
                auto & sourceReadCall = suffixRead      ? finalReadCall
                                        : protectedLeaf ? trailingCall
                                                        : readCall;
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                    read && (ctjs::constantKey(read.getKey()) == "hasAttribute" ||
                             ctjs::constantKey(read.getKey()) == "getAttribute" ||
                             ((selectorLeaf || !secondLeaf) &&
                              ctjs::constantKey(read.getKey()) == "matches"))) {
                    if (sourceReadMethod) {
                        if (suffixRead || !sourceReadCall) { return false; }
                        // Record the finished argument read; the write picks
                        // its feeding read below.
                        retainRead(sourceReadMethod, sourceReadCall);
                        sourceReadCall = {};
                    }
                    sourceReadMethod = read;
                    continue;
                }
                if (auto read = llvm::dyn_cast<ctjs::CallOp>(operation);
                    read && sourceReadMethod && !sourceReadCall &&
                    calls(read, sourceReadMethod, 1)) {
                    sourceReadCall = read;
                    const bool selector = ctjs::constantKey(sourceReadMethod.getKey()) == "matches";
                    // Reads before the first write move out of the invoke, so
                    // every `matches` selector there, used or not, is validated.
                    if (selector && !secondLeaf) { consumedSelectors.push_back(read); }
                    if (suffixRead && (selector || !finalMethod)) {
                        // Record standalone reads and argument selectors in
                        // source order. An unused selector after the second
                        // write is emitted with its own suppression; every read
                        // gets the final use check and the DOM entry proof. A
                        // read that feeds a pending write is handled by
                        // selectFeedingRead instead.
                        retainRead(sourceReadMethod, read);
                        if (selector && secondLeaf) { suffixLeaves.insert(read); }
                        sourceReadMethod = {};
                        sourceReadCall = {};
                    }
                    continue;
                }
                if (auto leaf = llvm::dyn_cast<ctjs::CallOp>(operation);
                    leaf && method && !protectedLeaf && calls(leaf, method, 2)) {
                    if (!selectFeedingRead(leaf, readMethod, readCall)) { return false; }
                    protectedLeaf = leaf;
                    continue;
                }
                if (auto leaf = llvm::dyn_cast<ctjs::CallOp>(operation);
                    leaf && secondMethod && !secondLeaf && calls(leaf, secondMethod, 2) &&
                    (trailingCall || suffixReads.contains(leaf.getArgs()[1]))) {
                    if (!selectFeedingRead(leaf, trailingMethod, trailingCall)) { return false; }
                    secondLeaf = leaf;
                    continue;
                }
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                    read && secondLeaf && !selectorMethod &&
                    ctjs::constantKey(read.getKey()) == "matches") {
                    selectorMethod = read;
                    continue;
                }
                if (auto leaf = llvm::dyn_cast<ctjs::CallOp>(operation);
                    leaf && selectorMethod && !selectorLeaf && calls(leaf, selectorMethod, 1)) {
                    selectorLeaf = leaf;
                    continue;
                }
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                    read && selectorLeaf && !finalMethod &&
                    ctjs::constantKey(read.getKey()) == "setAttribute") {
                    finalMethod = read;
                    continue;
                }
                if (auto leaf = llvm::dyn_cast<ctjs::CallOp>(operation);
                    leaf && finalMethod && calls(leaf, finalMethod, 2)) {
                    if (!selectFeedingRead(leaf, finalReadMethod, finalReadCall)) { return false; }
                    if (finalReadMethod &&
                        (!finalReadCall || leaf.getArgs()[1] != finalReadCall.getResult())) {
                        return false;
                    }
                    // A saved read may feed several writes. The final use
                    // check covers every use, including ones in later pairs.
                    suffixValues.append({finalMethod.getResult(), leaf.getResult()});
                    suffixUses.insert(&leaf->getOpOperand(0));
                    if (finalReadCall) {
                        suffixValues.append(
                            {finalReadMethod.getResult(), finalReadCall.getResult()});
                        suffixUses.insert(&finalReadCall->getOpOperand(0));
                        suffixUses.insert(&leaf->getOpOperand(3));
                        if (suffixLeaves.erase(finalReadCall)) {
                            consumedSelectors.push_back(finalReadCall);
                        }
                    }
                    if (suffixLeaves.empty() && leaf.getArgs()[1] == selectorLeaf.getResult()) {
                        selectorFeedsWrite = true;
                        suffixUses.insert(&leaf->getOpOperand(3));
                    }
                    suffixLeaves.insert(leaf);
                    finalMethod = {};
                    finalReadMethod = {};
                    finalReadCall = {};
                    continue;
                }
                if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation);
                    object && protectedLeaf && !discardedResult && result &&
                    result.getValue() == object.getResult()) {
                    discardedResult = object;
                    continue;
                }
                if (auto field = llvm::dyn_cast<ctjs::SetPropertyOp>(operation);
                    field && discardedResult && field.getObject() == discardedResult &&
                    ctjs::ordinaryKey(ctjs::constantKey(field.getKey()))) {
                    // The helper returns a fresh record that nobody reads, so
                    // its stores and allocation are dropped; the operations
                    // producing the stored values are kept.
                    discardedFields.insert(field);
                    continue;
                }
                return false;
            }
            // This invoke's result is unused. Every producer is kept, and the
            // return value is dropped only at this call; other callers of the
            // same helper still get its real result.
            if (!protectedLeaf || !result ||
                (readMethod && (!readCall || protectedLeaf.getArgs()[1] != readCall.getResult())) ||
                (trailingMethod && !trailingCall) || (secondMethod && !secondLeaf) ||
                (selectorMethod && !selectorLeaf) || finalMethod ||
                (finalReadMethod && !finalReadCall)) {
                return false;
            }
            if (finalReadCall) {
                // An ignored close payload may leave a final read. Keep it in
                // order and require that its result has no other use.
                suffixValues.append({finalReadMethod.getResult(), finalReadCall.getResult()});
                suffixUses.insert(&finalReadCall->getOpOperand(0));
            }
            if (selectorFeedsWrite) { consumedSelectors.push_back(selectorLeaf); }
            for (ctjs::CallOp consumed : consumedSelectors) {
                auto selector = ctjs::constantAttr<ctjs::StringAttr>(consumed.getArgs()[0]);
                if (!selector) { return false; }
                for (std::size_t i = 0; i < selector.getValue().size(); ++i) {
                    if (!step()) { return false; }
                }
                if (!validSelector(selector.getValue())) { return false; }
            }
            suffixValues.append({method.getResult(), protectedLeaf.getResult(),
                                 discardedResult ? discardedResult.getResult() : mlir::Value{},
                                 readMethod ? readMethod.getResult() : mlir::Value{},
                                 readCall ? readCall.getResult() : mlir::Value{},
                                 trailingMethod ? trailingMethod.getResult() : mlir::Value{},
                                 trailingCall ? trailingCall.getResult() : mlir::Value{},
                                 secondMethod ? secondMethod.getResult() : mlir::Value{},
                                 secondLeaf ? secondLeaf.getResult() : mlir::Value{},
                                 selectorMethod ? selectorMethod.getResult() : mlir::Value{},
                                 selectorLeaf ? selectorLeaf.getResult() : mlir::Value{}});
            const auto recordUse = [&](mlir::Value expected, mlir::Operation * consumer,
                                       unsigned index) {
                if (expected && consumer && index < consumer->getNumOperands() &&
                    consumer->getOperand(index) == expected) {
                    suffixUses.insert(&consumer->getOpOperand(index));
                }
            };
            recordUse(method.getResult(), protectedLeaf, 0);
            recordUse(readMethod ? readMethod.getResult() : mlir::Value{}, readCall, 0);
            recordUse(readCall ? readCall.getResult() : mlir::Value{}, protectedLeaf, 3);
            recordUse(trailingMethod ? trailingMethod.getResult() : mlir::Value{}, trailingCall, 0);
            recordUse(trailingCall ? trailingCall.getResult() : mlir::Value{}, secondLeaf, 3);
            recordUse(secondMethod ? secondMethod.getResult() : mlir::Value{}, secondLeaf, 0);
            recordUse(selectorMethod ? selectorMethod.getResult() : mlir::Value{}, selectorLeaf, 0);
            for (mlir::Value value : suffixValues) {
                if (!value) { continue; }
                for (mlir::OpOperand & use : value.getUses()) {
                    if (!step()) { return false; }
                    if (auto root = llvm::dyn_cast<ctjs::RootOp>(use.getOwner());
                        root && use.getOperandNumber() == 1) {
                        continue;
                    }
                    if (use.getOwner() == result) { continue; }
                    if (suffixUses.contains(&use)) { continue; }
                    if (discardedResult && value == discardedResult.getResult() &&
                        use.getOperandNumber() == 0 && discardedFields.contains(use.getOwner())) {
                        continue;
                    }
                    return false;
                }
            }
            return true;
        };
        if (attributeLeaf()) {
            protectedInvocation = invocation;
        } else {
            if (!reason.empty()) { return false; }
            protectedLeaf = {};
            secondLeaf = {};
            selectorLeaf = {};
            suffixLeaves.clear();
            selectorFeedsWrite = false;
            discardedResult = {};
            discardedFields.clear();
            // The call may leave the invoke only if proveUnusedBody shows the
            // helper cannot throw for any input.
            if (!proveUnusedBody(target)) {
                return refuse("DOM protected helper needs an independent inert-body proof");
            }
            if (!step() || !step()) { return false; }
            call->moveBefore(invocation);
            invocation.erase();
        }
    }
    auto & block = function.getBody().front();
    auto & body = target.getBody().front();
    mlir::OpBuilder at(protectedInvocation ? protectedInvocation.getOperation() : call);
    mlir::IRMapping mapping;
    if (!bindArguments(at, mapping)) { return false; }
    const auto cloneBody = [&](auto && self, mlir::Block & source, mlir::OpBuilder & at) -> bool {
        for (mlir::Operation & operation : source) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) {
                continue;
            }
            // The fresh record was only returned, and that result is
            // discarded, so its allocation is dropped. This says nothing
            // about whether its value producers can throw.
            if (discardedResult && &operation == discardedResult) { continue; }
            if (discardedFields.contains(&operation)) { continue; }
            if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                bindCapture(load, at, mapping);
            } else if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                if (protectedInvocation && protectedInvocation.getNumResults() &&
                    result.getValue() != protectedLeaf.getResult()) {
                    // The invoke still dispatches on the protected call. Only its
                    // normal continuation sees the helper's return value; the
                    // unwind continuation keeps the original payload and state.
                    auto payload = protectedInvocation.getNormalBody().front().getArgument(0);
                    if (!drain(payload.getNumUses())) { return false; }
                    payload.replaceAllUsesWith(mapping.lookup(result.getValue()));
                }
                call->getResult(0).replaceAllUsesWith(
                    mapping.lookup(protectedLeaf ? protectedLeaf.getResult() : result.getValue()));
            } else if (protectedInvocation && llvm::isa<mlir::scf::YieldOp>(operation)) {
                // attributeLeaf allowed guard results only when they are
                // ignored, so the yield drops its operands. The operations
                // producing them stay for the DOM entry proof.
                mlir::scf::YieldOp::create(at, operation.getLoc());
                ++operationCount;
            } else if (llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(operation)) {
                mlir::OperationState state(operation.getLoc(), operation.getName());
                for (mlir::Value operand : operation.getOperands()) {
                    if (!step()) { return false; }
                    state.addOperands(mapping.lookup(operand));
                }
                if (!protectedInvocation) { state.addTypes(operation.getResultTypes()); }
                state.addAttributes(operation.getAttrs());
                for (unsigned i = 0; i < operation.getNumRegions(); ++i) { state.addRegion(); }
                auto * cloned = at.create(state);
                ++operationCount;
                for (auto [from, to] : llvm::zip(operation.getRegions(), cloned->getRegions())) {
                    if (from.empty()) { continue; }
                    auto & destination = to.emplaceBlock();
                    for (mlir::BlockArgument argument : from.front().getArguments()) {
                        if (!step()) { return false; }
                        mapping.map(argument,
                                    destination.addArgument(argument.getType(), argument.getLoc()));
                    }
                    mlir::OpBuilder nested(&destination, destination.begin());
                    if (!self(self, from.front(), nested)) { return false; }
                }
                if (!protectedInvocation) {
                    mapping.map(operation.getResults(), cloned->getResults());
                }
            } else {
                if (&operation == protectedLeaf) { at.setInsertionPoint(call); }
                mlir::Operation * cloned;
                // A consumed selector stays an ordered read at this guard. Its
                // literal was checked by validSelector above; the DOM entry
                // proof must still exclude receiver failure, coercion and
                // reentry.
                if (&operation == secondLeaf ||
                    (&operation == selectorLeaf && !selectorFeedsWrite) ||
                    suffixLeaves.contains(&operation)) {
                    // Giving each call its own suppression is valid only because
                    // the DOM entry proof excludes exceptions from every one of
                    // them. Each write keeps its own valid-name check, and
                    // `matches` keeps its selector check before the final write.
                    if (!step() || !step() || !step() || !step()) { return false; }
                    cloned = emitSuppressedCall(at, operation, mapping);
                    operationCount += 4;
                } else {
                    cloned = at.clone(operation, mapping);
                }
                for (auto [from, to] : llvm::zip(operation.getResults(), cloned->getResults())) {
                    if (!step()) { return false; }
                    if (inactiveFillers.contains(from)) { inactiveFillers.insert(to); }
                }
                if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
                    closure && confinedCallback(closure, closureTarget(closure))) {
                    // confinedCallback checked the enclosure and unused implicit
                    // arguments before cloning. Keep the callback in its arm,
                    // re-parented to the caller's closure (and receiver).
                    auto callback = llvm::cast<ctjs::CreateClosureOp>(cloned);
                    callback.getEnclosingClosureMutable().assign(
                        block.getArgument(ctjs::arg_callee));
                    if (closure.getEnclosingThis() == body.getArgument(ctjs::arg_receiver)) {
                        callback.getEnclosingThisMutable().assign(
                            block.getArgument(ctjs::arg_receiver));
                    }
                }
                // An operation with regions (a normalized invoke) is cloned
                // with the same mapping; charge every nested operation.
                const auto counted = cloned->walk([&](mlir::Operation * inner) {
                    if (inner == cloned) { return mlir::WalkResult::advance(); }
                    if (!step()) { return mlir::WalkResult::interrupt(); }
                    ++operationCount;
                    return mlir::WalkResult::advance();
                });
                if (counted.wasInterrupted()) { return false; }
                if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(cloned)) {
                    callDepth[cloned] = 1 + callDepth.lookup(call) + callDepth.lookup(&operation);
                    if (depth + callDepth[cloned] >= 64) {
                        return refuse("DOM helper call tree is recursive or too deep");
                    }
                }
                ++operationCount;
                // Operations after the protected call are emitted after the
                // original invoke at the same guard. The DOM entry proof must
                // show the write and every moved read cannot throw or re-enter
                // script.
                if (&operation == protectedLeaf) {
                    if (protectedInvocation.getNumResults()) {
                        // Inert constants and captures after the call must
                        // dominate the normal continuation that uses the
                        // helper's result.
                        at.setInsertionPoint(protectedInvocation);
                    } else {
                        at.setInsertionPointAfter(protectedInvocation);
                    }
                }
            }
        }
        return true;
    };
    if (!cloneBody(cloneBody, body, at)) { return false; }
    if (rethrow) {
        const auto payload = rethrow.getValue();
        const auto where = rethrow.getLoc();
        if (!step()) { return false; }
        rethrow->getParentOp()->erase();
        // The call's result is only rethrown, so instead of joining it through
        // an scf.if result, push the throw into each arm that produces it,
        // keeping each payload and its producers in their own arm.
        // ponytail: accepts only one-result scf.if chains followed by unused
        // constants; anything else rethrows the joined value.
        const auto emitThrow = [&](auto && self, mlir::Value value, mlir::Operation * end,
                                   unsigned level) -> bool {
            if (!step() || level == 64) { return refuse("DOM throw projection is too deep"); }
            auto branch = value.getDefiningOp<mlir::scf::IfOp>();
            bool project = branch && branch.getNumResults() == 1 && value.use_empty() &&
                           branch->getBlock() == end->getBlock();
            llvm::SmallVector<mlir::Operation *> unused;
            if (project) {
                for (auto * tail = branch->getNextNode(); tail != end; tail = tail->getNextNode()) {
                    if (!step()) { return false; }
                    if (!tail || !llvm::isa<ctjs::ConstantOp>(tail) || !tail->use_empty()) {
                        project = false;
                        break;
                    }
                    unused.push_back(tail);
                }
            }
            mlir::OpBuilder at(end);
            if (!project) {
                if (!step() || !step()) { return false; }
                emitSavedThrow(at, where, value);
                operationCount += 2;
                return true;
            }
            if (!step()) { return false; }
            at.setInsertionPoint(branch);
            auto projected = mlir::scf::IfOp::create(at, branch.getLoc(), mlir::TypeRange{},
                                                     branch.getCondition());
            ++operationCount;
            for (auto [source, destination] :
                 llvm::zip(branch->getRegions(), projected->getRegions())) {
                if (!step()) { return false; }
                destination.takeBody(source);
                auto yield = llvm::cast<mlir::scf::YieldOp>(destination.front().getTerminator());
                const auto selected = yield.getOperand(0);
                yield.getResultsMutable().clear();
                if (!self(self, selected, yield, level + 1)) { return false; }
            }
            for (auto * constant : unused) {
                if (!step()) { return false; }
                inactiveFillers.erase(constant->getResult(0));
                constant->erase();
            }
            inactiveFillers.erase(value);
            branch.erase();
            return true;
        };
        if (!emitThrow(emitThrow, payload, call, 0)) { return false; }
    }
    // The caller erases `call` once this returns. For a protected call the
    // cloned leaf call was inserted right before `call`, so the invoke still
    // holds exactly one call followed by its exit.
    return true;
}

bool DOMSource::expand(ctjs::FuncOp function, unsigned depth, bool entry, bool directReceiver) {
    if (!step()) { return false; }
    if (expanded.contains(function)) { return true; }
    // ponytail: accepts only call trees up to 64 deep; refuses recursion,
    // which would need its own call and lifetime proof rather than expansion.
    if (depth == 64 || !active.insert(function).second) {
        return refuse("DOM helper call tree is recursive or too deep");
    }
    if (directReceiver) {
        // If the receiver is only rooted or captured as a closure's
        // enclosing `this`, treat the helper like an ordinary closure call.
        // Keep the stricter direct-receiver checks only when the body
        // actually uses its receiver.
        directReceiver = false;
        for (mlir::Operation * use :
             function.getBody().front().getArgument(ctjs::arg_receiver).getUsers()) {
            if (!step()) { return false; }
            directReceiver |= !llvm::isa<ctjs::RootOp, ctjs::CreateClosureOp>(use);
        }
    }
    if (!normalizeCompletion(function) || !checkBody(function, entry, directReceiver, true)) {
        return false;
    }
    if (!normalizeBooleanCell(function, depth)) { return false; }
    // A helper with nested functions may still have cells no closure
    // captures. Resolve their reads now, before foldConstantReplacements;
    // captured cells wait for the capture proof below.
    for (ctjs::CreateCellOp cell : function.getBody().front().getOps<ctjs::CreateCellOp>()) {
        bool captured = false;
        for (mlir::Operation * use : cell.getResult().getUsers()) {
            if (!step()) { return false; }
            captured |= llvm::isa<ctjs::CreateClosureOp>(use);
        }
        if (!captured && !resolveCell(cell)) { return false; }
    }
    if (!foldConstantReplacements(function)) { return false; }
    // Closure enclosures were checked on the original body above. Only
    // foldConstantReplacements may remove callbacks; check again that any
    // callback left is confined and capture-free.
    if (directReceiver && !checkBody(function, entry, true)) { return false; }
    llvm::SmallVector<ctjs::CallDirectOp> directCalls;
    const auto collected = function.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
            call && undefined(call.getCalleeValue())) {
            directCalls.push_back(call);
        }
        return mlir::WalkResult::advance();
    });
    if (collected.wasInterrupted()) { return false; }
    for (ctjs::CallDirectOp call : directCalls) {
        if (!step()) { return false; }
        auto target = call.getTarget();
        const auto index = target ? functionIndex(target) : std::nullopt;
        if (!index || functions.lookup(*index) != target || !target.isPrivate() ||
            creations.lookup(*index) != 0 || target.getUpvalueCount() != 0 ||
            !undefined(call.getNewTarget()) ||
            call.getArgs().size() + ctjs::implicit_arguments !=
                target.getBody().front().getNumArguments()) {
            return refuse("DOM direct helper requires an exact uncaptured local call");
        }
        if (depth + callDepth.lookup(call) >= 63) {
            return refuse("DOM helper call tree is recursive or too deep");
        }
        if (!bindConstantArguments({}, target)) { return false; }
    }
    // Bind constant arguments for every direct call (the loop above) before
    // expanding any callee. Otherwise a callee shared by sibling calls could
    // be specialised before a later sibling's string arguments were known.
    for (ctjs::CallDirectOp call : directCalls) {
        auto target = call.getTarget();
        // A direct call names its target by symbol, so there is no dispatch to
        // prove. Inlining binds the actual receiver and keeps every operation
        // for the DOM entry proof.
        if (!expand(target, depth + 1, false, true) ||
            !inlineCall(function, target, call, call.getArgs(), call.getReceiver(),
                        call.getCalleeValue(), {}, depth)) {
            return false;
        }
        callDepth.erase(call);
        call.erase();
    }
    if (!forwardFields(function)) { return false; }
    auto & block = function.getBody().front();
    llvm::SmallVector<ctjs::CreateClosureOp> closures;
    llvm::SmallVector<ctjs::CreateObjectOp> methodObjects;
    llvm::SmallVector<ctjs::CreateCellOp> localCells;
    for (mlir::Operation & operation : block) {
        if (!step()) { return false; }
        if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
            closures.push_back(closure);
        }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            const auto collected =
                branch.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * nested) {
                    if (!step()) { return mlir::WalkResult::interrupt(); }
                    if (llvm::isa<ctjs::InvokeOp>(nested)) { return mlir::WalkResult::skip(); }
                    if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(nested)) {
                        closures.push_back(closure);
                    }
                    return mlir::WalkResult::advance();
                });
            if (collected.wasInterrupted()) { return false; }
        }
        if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(operation)) {
            localCells.push_back(cell);
        }
        if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
            if (!dataObject(object)) {
                if (!reason.empty()) { return false; }
                methodObjects.push_back(object);
            }
        }
    }
    // Expand capturing closures first: captureTarget requires their targets to
    // be expanded already. Uncaptured helpers wait until the cells and
    // objects holding them are removed and every call is visible; only then
    // can bindConstantArguments specialise their bodies.
    for (ctjs::CreateClosureOp closure : closures) {
        auto target = closureTarget(closure);
        if (!target) { return refuse("DOM helper closure target is missing"); }
        if (!closure.getUpvalues().empty() && !expand(target, depth + 1)) { return false; }
    }
    for (ctjs::CreateCellOp cell : localCells) {
        if (!cells.contains(cell.getResult()) && !resolveCell(cell)) { return false; }
    }
    llvm::DenseSet<mlir::Operation *> methods, resolvedObjects;
    // Repeatedly remove cells, method objects and closures that nothing holds
    // any more, until all are gone; no progress means a cycle or an escape.
    // ponytail: rescans everything each round; index the worklist if large
    // helper graphs exhaust the work budget.
    while (!closures.empty() || !localCells.empty() ||
           resolvedObjects.size() != methodObjects.size()) {
        if (!step()) { return false; }
        bool progress = false;
        for (ctjs::CreateCellOp & cell : localCells) {
            if (!cell) { continue; }
            bool held = false;
            for (mlir::Operation * use : cell.getResult().getUsers()) {
                if (!step()) { return false; }
                held |= llvm::isa<ctjs::CreateClosureOp>(use);
            }
            if (held) { continue; }
            auto capture = cells.lookup(cell.getResult());
            if (capture.write) { capture.write.erase(); }
            if (!eraseRootedUses(cell.getResult(),
                                 "DOM helper capture cell has an unexpanded use")) {
                return false;
            }
            cells.erase(cell.getResult());
            cell.erase();
            cell = {};
            progress = true;
        }
        for (ctjs::CreateObjectOp object : methodObjects) {
            if (!step()) { return false; }
            if (resolvedObjects.contains(object)) { continue; }
            bool held = false;
            for (mlir::OpOperand & use : object.getResult().getUses()) {
                if (!step()) { return false; }
                held |= captureStorage(use);
            }
            if (held) { continue; }
            if (!resolveMethods(object, methods)) { return false; }
            resolvedObjects.insert(object);
            progress = true;
        }
        for (ctjs::CreateClosureOp & closure : closures) {
            if (!closure) { continue; }
            bool held = false;
            for (mlir::OpOperand & use : closure.getResult().getUses()) {
                if (!step()) { return false; }
                auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                auto object = store ? store.getObject().getDefiningOp<ctjs::CreateObjectOp>()
                                    : ctjs::CreateObjectOp{};
                held |= captureStorage(use) ||
                        (object && use.getOperandNumber() == 2 && object->getBlock() == &block);
            }
            if (held) { continue; }
            auto target = closureTarget(closure);
            if (!target) { return refuse("DOM helper closure target is missing"); }
            if (confinedCallback(closure, target)) {
                if (!expand(target, depth + 1)) { return false; }
                // Keep the closure and its function. The DOM entry proof must
                // establish the built-in method, the callback body and every
                // use.
                retainedCallbacks.insert(target);
                closure = {};
                progress = true;
                continue;
            }
            if (!reason.empty()) { return false; }
            if (closure.getUpvalues().size() != target.getUpvalueCount()) {
                return refuse("DOM helper capture count disagrees with its source target");
            }
            llvm::SmallVector<Capture> captures;
            if (!closure.getUpvalues().empty()) {
                if (!captureTarget(closure, target)) { return false; }
                const auto indices = closure.getEnclosingIndicesAttr();
                for (auto [slot, capture] : llvm::enumerate(closure.getUpvalues())) {
                    if (!step()) { return false; }
                    if (indices && indices[slot] >= 0) {
                        captures.push_back({{}, {}, indices[slot]});
                        continue;
                    }
                    const auto found = cells.find(capture);
                    if (found == cells.end()) {
                        return refuse("DOM helper capture lacks a proved local cell");
                    }
                    captures.push_back(found->second);
                }
            }
            struct Call {
                mlir::Operation * operation;
                mlir::ValueRange arguments;
            };
            llvm::SmallVector<Call> calls;
            llvm::SmallVector<ctjs::RootOp> roots;
            for (mlir::OpOperand & use : closure.getResult().getUses()) {
                if (!step()) { return false; }
                if (auto root = llvm::dyn_cast<ctjs::RootOp>(use.getOwner())) {
                    roots.push_back(root);
                } else {
                    auto * operation = use.getOwner();
                    mlir::ValueRange arguments;
                    if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                        call && use.getOperandNumber() == 0 &&
                        (undefined(call.getReceiver()) || methods.contains(operation))) {
                        arguments = call.getArgs();
                    } else if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
                               direct && use.getOperandNumber() == 2 &&
                               direct.getCallee() == target.getSymName() &&
                               (undefined(direct.getReceiver()) || methods.contains(operation)) &&
                               undefined(direct.getNewTarget())) {
                        arguments = direct.getArgs();
                    } else {
                        return refuse(
                            "DOM helper callable escapes or its call shape is unsupported");
                    }
                    if (!precedesInStructuredBody(closure, operation) ||
                        arguments.size() + ctjs::implicit_arguments >
                            target.getBody().front().getNumArguments()) {
                        return refuse("DOM helper call has unsupported arity or source order");
                    }
                    for (const Capture & capture : captures) {
                        if (!step()) { return false; }
                        if (capture.write && !precedesInStructuredBody(capture.write, operation)) {
                            return refuse(
                                "DOM helper capture assignment does not precede its call");
                        }
                    }
                    if (depth + callDepth.lookup(operation) >= 63) {
                        return refuse("DOM helper call tree is recursive or too deep");
                    }
                    calls.push_back({operation, arguments});
                }
            }
            if (calls.empty()) {
                if (!closure.getUpvalues().empty() || !uniqueCreation(closure) ||
                    !proveUnusedBody(target)) {
                    return refuse("DOM helper has no independently proved unused body");
                }
            } else {
                if (!bindConstantArguments(closure, target)) { return false; }
                if (!expand(target, depth + 1)) { return false; }
            }
            for (const Call & call : calls) {
                // A closure call binds the caller's own receiver and closure;
                // only direct calls that use their receiver bind the actual one.
                if (!inlineCall(function, target, call.operation, call.arguments,
                                block.getArgument(ctjs::arg_receiver),
                                block.getArgument(ctjs::arg_callee), captures, depth)) {
                    return false;
                }
                methods.erase(call.operation);
                callDepth.erase(call.operation);
                call.operation->erase();
            }
            for (ctjs::RootOp root : roots) { root.erase(); }
            closure.erase();
            closure = {};
            progress = true;
        }
        llvm::erase_if(closures, [](auto closure) { return !closure; });
        llvm::erase_if(localCells, [](auto cell) { return !cell; });
        if (!progress) { return refuse("DOM helper capture graph is cyclic or escapes"); }
    }
    for (ctjs::CreateObjectOp object : methodObjects) {
        if (!eraseRootedUses(object.getResult(), "DOM helper object has an unexpanded use")) {
            return false;
        }
        object.erase();
    }
    // Inlined calls may have returned fresh records whose fields are read in
    // a loop body. Forward those reads, keeping every value producer and
    // effect.
    if (!forwardFields(function) || !repairInactiveCompletion(function)) { return false; }
    active.erase(function);
    expanded.insert(function);
    return true;
}

} // namespace ctcompile::ctnative::dom_source_detail
