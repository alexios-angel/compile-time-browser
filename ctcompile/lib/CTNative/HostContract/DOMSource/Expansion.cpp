#include "Proof.hpp"
#include "ctbrowser/style/css/parser.hpp"

namespace ctcompile::ctnative::dom_source_detail {

bool DOMSource::inlineCall(ctjs::FuncOp function, ctjs::FuncOp target, mlir::Operation * call,
                           mlir::ValueRange arguments, mlir::Value receiver, mlir::Value callee,
                           llvm::MutableArrayRef<Capture> captures, unsigned depth) {
    ctjs::InvokeOp protectedInvocation;
    ctjs::CallOp protectedLeaf, secondLeaf, selectorLeaf, finalLeaf;
    bool selectorFeedsWrite = false;
    ctjs::CreateObjectOp discardedResult;
    llvm::DenseSet<mlir::Operation *> discardedFields;
    if (auto invocation = llvm::dyn_cast_or_null<ctjs::InvokeOp>(call->getParentOp())) {
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
        // Keep the write protected and its reads in source order. Moving
        // preparation and hasAttribute reads requires complete typed DOM reproof:
        // the initial Element methods and primitive arguments exclude source exceptions
        // and reentry. Clone them in order, under the original guard.
        // ponytail: two writes with feeding reads, matches, and a final read/write;
        // larger bodies need all their exceptional edges represented.
        const auto attributeLeaf = [&] {
            ctjs::GetPropertyOp method, readMethod, trailingMethod, secondMethod;
            ctjs::GetPropertyOp selectorMethod, finalMethod, finalReadMethod;
            ctjs::CallOp readCall, trailingCall, finalReadCall;
            auto result = llvm::dyn_cast<ctjs::ReturnOp>(target.getBody().front().back());
            for (mlir::Operation & operation : target.getBody().front()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                              ctjs::ReturnOp>(operation)) {
                    continue;
                }
                if (llvm::isa<ctjs::LoadUpvalueOp>(operation)) { continue; }
                // These producers remain under the source guard. Complete
                // typed DOM reproof must exclude coercion, throws and reentry.
                if (!protectedLeaf && llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp,
                                                ctjs::CompareOp, ctjs::TruthyOp>(operation)) {
                    continue;
                }
                auto & writeMethod = protectedLeaf ? secondMethod : method;
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                    read && !writeMethod && !secondLeaf &&
                    ctjs::constantKey(read.getKey()) == "setAttribute") {
                    writeMethod = read;
                    continue;
                }
                auto & sourceReadMethod = selectorLeaf    ? finalReadMethod
                                          : protectedLeaf ? trailingMethod
                                                          : readMethod;
                auto & sourceReadCall = selectorLeaf    ? finalReadCall
                                        : protectedLeaf ? trailingCall
                                                        : readCall;
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                    read && !sourceReadMethod && !finalLeaf &&
                    ctjs::constantKey(read.getKey()) == "hasAttribute") {
                    sourceReadMethod = read;
                    continue;
                }
                if (auto read = llvm::dyn_cast<ctjs::CallOp>(operation);
                    read && sourceReadMethod && !sourceReadCall &&
                    read.getCallee() == sourceReadMethod.getResult() &&
                    read.getReceiver() == sourceReadMethod.getObject() &&
                    read.getArgs().size() == 1) {
                    sourceReadCall = read;
                    continue;
                }
                if (auto leaf = llvm::dyn_cast<ctjs::CallOp>(operation);
                    leaf && method && !protectedLeaf && leaf.getCallee() == method.getResult() &&
                    leaf.getReceiver() == method.getObject() && leaf.getArgs().size() == 2) {
                    protectedLeaf = leaf;
                    continue;
                }
                if (auto leaf = llvm::dyn_cast<ctjs::CallOp>(operation);
                    leaf && secondMethod && !secondLeaf &&
                    leaf.getCallee() == secondMethod.getResult() &&
                    leaf.getReceiver() == secondMethod.getObject() && leaf.getArgs().size() == 2 &&
                    trailingCall && leaf.getArgs()[1] == trailingCall.getResult()) {
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
                    leaf && selectorMethod && !selectorLeaf &&
                    leaf.getCallee() == selectorMethod.getResult() &&
                    leaf.getReceiver() == selectorMethod.getObject() &&
                    leaf.getArgs().size() == 1) {
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
                    leaf && finalMethod && !finalLeaf &&
                    leaf.getCallee() == finalMethod.getResult() &&
                    leaf.getReceiver() == finalMethod.getObject() && leaf.getArgs().size() == 2) {
                    finalLeaf = leaf;
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
                    // A fresh unused data record has no observer. Retain its
                    // producers even though its stores and allocation vanish.
                    discardedFields.insert(field);
                    continue;
                }
                return false;
            }
            auto literal =
                result ? result.getValue().getDefiningOp<ctjs::ConstantOp>() : ctjs::ConstantOp{};
            const bool primitiveResult =
                literal && llvm::isa<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::StringAttr,
                                     ctjs::NullAttr, ctjs::UndefinedAttr>(literal.getValue());
            if (!protectedLeaf || (!discardedResult && !primitiveResult) ||
                (readMethod && (!readCall || protectedLeaf.getArgs()[1] != readCall.getResult())) ||
                (trailingMethod && !trailingCall) || (secondMethod && !secondLeaf) ||
                (selectorMethod && !selectorLeaf) || (finalMethod && !finalLeaf) ||
                (finalReadMethod && (!finalReadCall || !finalLeaf ||
                                     finalLeaf.getArgs()[1] != finalReadCall.getResult()))) {
                return false;
            }
            selectorFeedsWrite =
                selectorLeaf && finalLeaf && finalLeaf.getArgs()[1] == selectorLeaf.getResult();
            if (selectorFeedsWrite) {
                auto literal = selectorLeaf.getArgs()[0].getDefiningOp<ctjs::ConstantOp>();
                auto selector = literal ? llvm::dyn_cast<ctjs::StringAttr>(literal.getValue())
                                        : ctjs::StringAttr{};
                if (!selector) { return false; }
                for (std::size_t i = 0; i < selector.getValue().size(); ++i) {
                    if (!step()) { return false; }
                }
                ctbrowser::atom_table atoms;
                bool invalid = false;
                const std::string_view text{selector.getValue().data(), selector.getValue().size()};
                (void)ctbrowser::style::css::parse_selector_text(text, atoms, invalid);
                if (invalid) { return false; }
            }
            for (mlir::Value value : llvm::SmallVector<mlir::Value>{
                     method.getResult(), protectedLeaf.getResult(),
                     discardedResult ? discardedResult.getResult() : mlir::Value{},
                     readMethod ? readMethod.getResult() : mlir::Value{},
                     readCall ? readCall.getResult() : mlir::Value{},
                     trailingMethod ? trailingMethod.getResult() : mlir::Value{},
                     trailingCall ? trailingCall.getResult() : mlir::Value{},
                     secondMethod ? secondMethod.getResult() : mlir::Value{},
                     secondLeaf ? secondLeaf.getResult() : mlir::Value{},
                     selectorMethod ? selectorMethod.getResult() : mlir::Value{},
                     selectorLeaf ? selectorLeaf.getResult() : mlir::Value{},
                     finalReadMethod ? finalReadMethod.getResult() : mlir::Value{},
                     finalReadCall ? finalReadCall.getResult() : mlir::Value{},
                     finalMethod ? finalMethod.getResult() : mlir::Value{},
                     finalLeaf ? finalLeaf.getResult() : mlir::Value{}}) {
                if (!value) { continue; }
                for (mlir::OpOperand & use : value.getUses()) {
                    if (!step()) { return false; }
                    if (auto root = llvm::dyn_cast<ctjs::RootOp>(use.getOwner());
                        root && use.getOperandNumber() == 1) {
                        continue;
                    }
                    if ((value == method.getResult() && use.getOwner() == protectedLeaf &&
                         use.getOperandNumber() == 0) ||
                        (readMethod && value == readMethod.getResult() &&
                         use.getOwner() == readCall && use.getOperandNumber() == 0) ||
                        (readCall && value == readCall.getResult() &&
                         use.getOwner() == protectedLeaf && use.getOperandNumber() == 3) ||
                        (trailingMethod && value == trailingMethod.getResult() &&
                         use.getOwner() == trailingCall && use.getOperandNumber() == 0) ||
                        (secondLeaf && trailingCall && value == trailingCall.getResult() &&
                         use.getOwner() == secondLeaf && use.getOperandNumber() == 3) ||
                        (secondMethod && value == secondMethod.getResult() &&
                         use.getOwner() == secondLeaf && use.getOperandNumber() == 0) ||
                        (selectorMethod && value == selectorMethod.getResult() &&
                         use.getOwner() == selectorLeaf && use.getOperandNumber() == 0) ||
                        (selectorFeedsWrite && value == selectorLeaf.getResult() &&
                         use.getOwner() == finalLeaf && use.getOperandNumber() == 3) ||
                        (finalReadMethod && value == finalReadMethod.getResult() &&
                         use.getOwner() == finalReadCall && use.getOperandNumber() == 0) ||
                        (finalReadCall && value == finalReadCall.getResult() &&
                         use.getOwner() == finalLeaf && use.getOperandNumber() == 3) ||
                        (finalMethod && value == finalMethod.getResult() &&
                         use.getOwner() == finalLeaf && use.getOperandNumber() == 0) ||
                        (discardedResult && value == discardedResult.getResult() &&
                         use.getOwner() == result)) {
                        continue;
                    }
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
            finalLeaf = {};
            selectorFeedsWrite = false;
            discardedResult = {};
            discardedFields.clear();
            // Only an independently inert body may discharge suppression.
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
    mapping.map(body.getArgument(ctjs::arg_callee), callee);
    mapping.map(body.getArgument(ctjs::arg_receiver), receiver);
    for (auto [index, formal] :
         llvm::enumerate(body.getArguments().drop_front(ctjs::implicit_arguments))) {
        if (!step()) { return false; }
        mlir::Value actual;
        if (index < arguments.size()) {
            actual = arguments[index];
        } else {
            // Missing JavaScript arguments are undefined. The complete helper
            // proof excludes arguments/new.target observers before inlining.
            actual = ctjs::ConstantOp::create(at, call->getLoc(),
                                              ctjs::UndefinedAttr::get(call->getContext()));
            ++operationCount;
        }
        mapping.map(formal, actual);
    }
    const auto cloneBody = [&](auto && self, mlir::Block & source, mlir::OpBuilder & at) -> bool {
        for (mlir::Operation & operation : source) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) {
                continue;
            }
            // The fresh record's only observation was the discarded
            // helper result. This is allocation elision, not a no-throw fact.
            if (discardedResult && &operation == discardedResult) { continue; }
            if (discardedFields.contains(&operation)) { continue; }
            if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                // Substitute at each invocation, including branch-local
                // loads, never bind a shared body to its first caller.
                auto & capture = captures[static_cast<unsigned>(load.getIndex())];
                mlir::Value value;
                if (capture.enclosingIndex >= 0) {
                    value = ctjs::LoadUpvalueOp::create(at, load.getLoc(), load.getType(),
                                                        block.getArgument(ctjs::arg_callee),
                                                        capture.enclosingIndex);
                    ++operationCount;
                } else {
                    value = capture.value();
                }
                mapping.map(load.getResult(), value);
            } else if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                call->getResult(0).replaceAllUsesWith(
                    mapping.lookup(protectedLeaf ? protectedLeaf.getResult() : result.getValue()));
            } else if (llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(operation)) {
                mlir::OperationState state(operation.getLoc(), operation.getName());
                for (mlir::Value operand : operation.getOperands()) {
                    if (!step()) { return false; }
                    state.addOperands(mapping.lookup(operand));
                }
                state.addTypes(operation.getResultTypes());
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
                mapping.map(operation.getResults(), cloned->getResults());
            } else {
                if (&operation == protectedLeaf) { at.setInsertionPoint(call); }
                ctjs::InvokeOp trailingInvocation;
                // A consumed selector remains an ordered read at this guard.
                // Its literal syntax was checked above; complete typed reproof
                // must still exclude receiver failure, coercion and reentry.
                if (&operation == secondLeaf ||
                    (&operation == selectorLeaf && !selectorFeedsWrite) ||
                    &operation == finalLeaf) {
                    // Splitting suppression is valid only after complete DOM
                    // reproof excludes source exceptions from EVERY call.
                    // Each write retains its own valid-name check; matches
                    // retains literal-selector validation before the final write.
                    if (!step() || !step() || !step() || !step()) { return false; }
                    mlir::OperationState state(operation.getLoc(),
                                               ctjs::InvokeOp::getOperationName());
                    for (unsigned i = 0; i != 3; ++i) { state.addRegion(); }
                    trailingInvocation = llvm::cast<ctjs::InvokeOp>(at.create(state));
                    auto & called = trailingInvocation.getBody().emplaceBlock();
                    at.setInsertionPointToEnd(&called);
                    operationCount += 4;
                }
                auto * cloned = at.clone(operation, mapping);
                if (trailingInvocation) {
                    ctjs::InvokeExitOp::create(at, operation.getLoc(), cloned->getResult(0),
                                               mlir::ValueRange{});
                    for (auto * region : {&trailingInvocation.getNormalBody(),
                                          &trailingInvocation.getUnwindBody()}) {
                        auto & continuation = region->emplaceBlock();
                        continuation.addArgument(ctjs::ValueType::get(call->getContext()),
                                                 operation.getLoc());
                        mlir::OpBuilder yield = mlir::OpBuilder::atBlockEnd(&continuation);
                        ctjs::InvokeYieldOp::create(yield, operation.getLoc(), mlir::ValueRange{});
                    }
                    at.setInsertionPointAfter(trailingInvocation);
                }
                for (auto [from, to] : llvm::zip(operation.getResults(), cloned->getResults())) {
                    if (!step()) { return false; }
                    if (inactiveFillers.contains(from)) { inactiveFillers.insert(to); }
                }
                if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
                    closure &&
                    (confinedFilterCallback(
                         closure, functions.lookup(static_cast<unsigned>(closure.getFunction()))) ||
                     confinedReplacementCallback(closure, functions.lookup(static_cast<unsigned>(
                                                              closure.getFunction()))))) {
                    // The original enclosure and unused implicit arguments
                    // were proved before cloning. Preserve this callback in
                    // its source arm with the caller's inert enclosure.
                    auto callback = llvm::cast<ctjs::CreateClosureOp>(cloned);
                    callback.getEnclosingClosureMutable().assign(
                        block.getArgument(ctjs::arg_callee));
                    if (closure.getEnclosingThis() == body.getArgument(ctjs::arg_receiver)) {
                        callback.getEnclosingThisMutable().assign(
                            block.getArgument(ctjs::arg_receiver));
                    }
                }
                // Regions (a normalized invoke) clone with the
                // same mapping; charge every nested operation.
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
                // Suffix producers follow the original suppression at the
                // same guard. Complete DOM proof must show the write and
                // every moved read cannot throw or reenter source code.
                if (&operation == protectedLeaf) { at.setInsertionPointAfter(protectedInvocation); }
            }
        }
        return true;
    };
    if (!cloneBody(cloneBody, body, at)) { return false; }
    // The caller erases the old call after the charged clone succeeds,
    // restoring the exact call+exit inside the original suppression.
    return true;
}

bool DOMSource::expand(ctjs::FuncOp function, unsigned depth, bool entry, bool directReceiver) {
    if (!step()) { return false; }
    if (expanded.contains(function)) { return true; }
    // ponytail: bounded local call trees; recursive source needs a separate
    // call/lifetime proof, not recursive compiler expansion.
    if (depth == 64 || !active.insert(function).second) {
        return refuse("DOM helper call tree is recursive or too deep");
    }
    if (directReceiver) {
        // An inert receiver enclosure needs the complete local closure proof,
        // just like a source closure call. Keep the restricted direct path
        // when the original body observes its actual receiver.
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
    // Parameters with nested source functions may have an otherwise local
    // cell. Resolve those reads before specializing their intrinsic calls;
    // captured cells still wait for the unchanged child/capture proof.
    for (ctjs::CreateCellOp cell : function.getBody().front().getOps<ctjs::CreateCellOp>()) {
        bool captured = false;
        for (mlir::Operation * use : cell.getResult().getUsers()) {
            if (!step()) { return false; }
            captured |= llvm::isa<ctjs::CreateClosureOp>(use);
        }
        if (!captured && !resolveCell(cell)) { return false; }
    }
    if (!foldConstantReplacements(function)) { return false; }
    // Enclosing identities were checked on the original body above. Only
    // the complete replacement proof may remove callbacks. Only confined,
    // capture-free callbacks may survive direct helper expansion.
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
    // Bind every sibling's complete call census before expanding a shared
    // callee. Otherwise an unvisited sibling's formal hides its actual
    // String inputs from that callee's existing all-use proof.
    for (ctjs::CallDirectOp call : directCalls) {
        auto target = call.getTarget();
        // The symbol is already the direct-call contract. This normalization
        // proves no new source dispatch: it binds each actual receiver and
        // retains every operation for the complete DOM entry reproof.
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
    // Capturing children must be leaves before the shared capture query.
    // Uncaptured helpers wait until their holders retire and expose every
    // invocation; only then can argument facts specialize their bodies.
    for (ctjs::CreateClosureOp closure : closures) {
        auto target = functions.lookup(static_cast<unsigned>(closure.getFunction()));
        if (!target) { return refuse("DOM helper closure target is missing"); }
        if (!closure.getUpvalues().empty() && !expand(target, depth + 1)) { return false; }
    }
    for (ctjs::CreateCellOp cell : localCells) {
        if (!cells.contains(cell.getResult()) && !resolveCell(cell)) { return false; }
    }
    llvm::DenseSet<mlir::Operation *> methods, resolvedObjects;
    // ponytail: bounded rescans of local capture dependencies; index the
    // worklist if large helper graphs exhaust the existing work budget.
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
            while (!cell.getResult().use_empty()) {
                if (!step()) { return false; }
                auto root = llvm::dyn_cast<ctjs::RootOp>(*cell.getResult().getUsers().begin());
                if (!root) { return refuse("DOM helper capture cell has an unexpanded use"); }
                root.erase();
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
            auto target = functions.lookup(static_cast<unsigned>(closure.getFunction()));
            if (!target) { return refuse("DOM helper closure target is missing"); }
            if (confinedFilterCallback(closure, target) ||
                confinedReplacementCallback(closure, target)) {
                if (!expand(target, depth + 1)) { return false; }
                // Preserve both source identities. Complete DOM reproof must
                // establish the intrinsic method, callback body and every use.
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
                if (!closure.getUpvalues().empty() ||
                    creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1 ||
                    !proveUnusedBody(target)) {
                    return refuse("DOM helper has no independently proved unused body");
                }
            } else {
                if (!bindConstantArguments(closure, target)) { return false; }
                if (!expand(target, depth + 1)) { return false; }
            }
            for (const Call & call : calls) {
                // Live closure metadata retains its enclosing identities;
                // receiver-observing direct functions bind the actual instead.
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
        while (!object.getResult().use_empty()) {
            if (!step()) { return false; }
            auto root = llvm::dyn_cast<ctjs::RootOp>(*object.getResult().getUsers().begin());
            if (!root) { return refuse("DOM helper object has an unexpanded use"); }
            root.erase();
        }
        object.erase();
    }
    // Calls may return fresh own-field records in a loop body. Project only
    // their confined reads, retaining every source value producer and effect.
    if (!forwardFields(function) || !repairInactiveCompletion(function)) { return false; }
    active.erase(function);
    expanded.insert(function);
    return true;
}

} // namespace ctcompile::ctnative::dom_source_detail
