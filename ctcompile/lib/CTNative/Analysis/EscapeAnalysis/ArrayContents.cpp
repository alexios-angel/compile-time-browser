#include "LoopProof.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"

namespace ctcompile::ctnative {
using namespace escape_detail;

ArrayContentsEvidence computeArrayContents(ctjs::FuncOp function, std::size_t workLimit) {
    ArrayContentsEvidence out;
    const auto refuse = [&](ArrayContentsFailure reason, mlir::Operation * by) {
        ArrayContentsEvidence failed;
        failed.failure = reason;
        failed.refusedBy = by;
        failed.work = out.work;
        return failed;
    };
    const auto spend = [&](std::size_t count = 1) {
        if (count > workLimit - out.work) {
            out.work = workLimit;
            return false;
        }
        out.work += count;
        return true;
    };
    if (function.getBody().empty() || function.getBody().front().empty()) {
        return refuse(ArrayContentsFailure::UnsupportedControlFlow, function);
    }

    // Enumerate structural paths, never solver flags or annotations. A join
    // keeps separate exact states: unioning array aliases before a strong
    // overwrite would incorrectly erase an element of an unmodified array.
    // Every ordinary branch edge is visited, including a constant flag's untaken
    // edge and a switch's default edge. Only a certified finite loop selects its
    // guard edge. Any unsupported path refuses everything.
    State state;
    for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, function); }
        if (llvm::isa<ctjs::ValueType>(argument.getType())) {
            state.values[argument] = {argument, ContentsKind::Opaque};
        }
    }
    state.visited.insert(&function.getBody().front());
    state.current = &function.getBody().front().front();
    llvm::SmallVector<State, 2> alternatives;
    llvm::SmallPtrSet<mlir::Operation *, 8> arraySites;
    llvm::SmallPtrSet<mlir::Operation *, 8> objectSites;
    const auto held = [&](mlir::Value value) { return state.values.lookup(value); };
    const auto origin = [&](mlir::Value value) { return held(value).origin(); };
    const auto transport = [&](State & path, mlir::ValueRange arguments,
                               mlir::ValueRange operands) {
        if (arguments.size() != operands.size()) {
            return ArrayContentsFailure::UnsupportedControlFlow;
        }
        // Read every source before assigning any destination: successor operands
        // are simultaneous, including swaps. One kind replaces exact/opaque state.
        if (!spend(operands.size())) { return ArrayContentsFailure::WorkLimit; }
        llvm::SmallVector<ContentsValue, 8> incoming;
        for (mlir::Value value : operands) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            const ContentsValue fact = path.values.lookup(value);
            if (!fact.original) { return ArrayContentsFailure::UnknownValue; }
            incoming.push_back(fact);
        }
        for (auto [argument, fact] : llvm::zip(arguments, incoming)) {
            path.values[argument] = fact;
        }
        return ArrayContentsFailure::None;
    };
    const auto forward = [&](State & path, mlir::Block * next, mlir::ValueRange operands) {
        if (next->getParent() != &function.getBody() || next->empty() ||
            (!path.visited.insert(next).second &&
             (!path.loop || (next != path.loop->header && next != path.loop->body)))) {
            return ArrayContentsFailure::UnsupportedControlFlow;
        }
        const auto failure = transport(path, next->getArguments(), operands);
        if (failure != ArrayContentsFailure::None) { return failure; }
        path.current = &next->front();
        return ArrayContentsFailure::None;
    };
    const auto snapshot = [&]() -> std::optional<State> {
        // Path enumeration can be exponential. Charge every copied value,
        // visited block, container and element before allocating the snapshot.
        if (!spend(state.values.size()) || !spend(state.visited.size())) { return std::nullopt; }
        for (const auto & [array, elements] : state.arrays) {
            (void)array;
            if (!spend() || !spend(elements.size())) { return std::nullopt; }
        }
        for (const auto & [object, properties] : state.objects) {
            (void)object;
            if (!spend() || !spend(properties.size())) { return std::nullopt; }
        }
        return state;
    };
    const auto alternative = [&](mlir::Block * next, mlir::ValueRange operands) {
        auto copy = snapshot();
        if (!copy) { return ArrayContentsFailure::WorkLimit; }
        const auto failure = forward(*copy, next, operands);
        if (failure == ArrayContentsFailure::None) { alternatives.push_back(std::move(*copy)); }
        return failure;
    };
    LoopProof loopProof{function, state, spend};
    while (true) {
        bool returned = false;
        while (state.current != nullptr) {
            mlir::Operation & op = *state.current;
            state.current = op.getNextNode();
            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
            if ((op.getNumRegions() != 0 && !llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(&op)) ||
                (op.getNumSuccessors() != 0 &&
                 !llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(&op))) {
                return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
            }
            if (auto entered = llvm::dyn_cast<ctjs::FrameEnterOp>(&op)) {
                // The importer enters before seeding registers or allocating any
                // tracked object. Its depth failure is real, but on that path no
                // object from these sites exists. This is NOT an effect proof that
                // permits erasing entry or treating it as pure/nonthrowing.
                if (&op != &function.getBody().front().front() || state.frame ||
                    entered.getRegCountAttr().getInt() < 0) {
                    return refuse(ArrayContentsFailure::InvalidFrame, &op);
                }
                state.frame = entered;
                state.values[state.frame.getResult()] = {state.frame.getResult()};
                continue;
            }
            if (auto exited = llvm::dyn_cast<ctjs::FrameExitOp>(&op)) {
                if (!state.frame || state.frameExited ||
                    origin(exited->getOperand(0)) != state.frame.getResult() ||
                    !llvm::isa_and_nonnull<ctjs::ReturnOp>(op.getNextNode())) {
                    return refuse(ArrayContentsFailure::InvalidFrame, &op);
                }
                state.frameExited = true;
                continue;
            }
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(&op)) {
                // RootOp parks only in THIS frame's window (Frames.td). The exact
                // matching exit kills that window; no call, suspension or unknown
                // effect in this query can keep it or expose its contents.
                if (!state.frame || state.frameExited ||
                    origin(root->getOperand(0)) != state.frame.getResult()) {
                    return refuse(ArrayContentsFailure::InvalidFrame, &op);
                }
                if (!origin(root.getValue())) {
                    return refuse(ArrayContentsFailure::UnknownValue, &op);
                }
                continue;
            }
            if (state.frameExited && !llvm::isa<ctjs::ReturnOp>(&op)) {
                return refuse(ArrayContentsFailure::InvalidFrame, &op);
            }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(&op)) {
                if (!llvm::hasSingleElement(loop.getBefore()) ||
                    !llvm::hasSingleElement(loop.getAfter()) || loop.getBefore().front().empty() ||
                    loop.getAfter().front().empty()) {
                    return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                }
                auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(
                    loop.getBefore().front().getTerminator());
                auto yield =
                    llvm::dyn_cast<mlir::scf::YieldOp>(loop.getAfter().front().getTerminator());
                if (!condition || !yield || condition.getArgs().size() != loop.getNumResults()) {
                    return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                }
                auto failure = transport(state, loop.getBeforeArguments(), loop.getInits());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                failure = loopProof.countedLoop(&loop.getBefore().front(), &loop.getAfter().front(),
                                                loop.getInits(), condition.getCondition(),
                                                condition.getArgs(), yield.getOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                state.current = &loop.getBefore().front().front();
                continue;
            }
            if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(&op)) {
                auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(condition->getParentOp());
                if (!loop || !state.loop || state.loop->header != op.getBlock()) {
                    return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                }
                const auto continued = loopProof.loopContinues();
                if (!continued) {
                    return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                }
                const auto failure =
                    transport(state,
                              *continued ? mlir::ValueRange(loop.getAfterArguments())
                                         : mlir::ValueRange(loop.getResults()),
                              condition.getArgs());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                state.current = *continued ? &loop.getAfter().front().front() : loop->getNextNode();
                if (!*continued) { state.loop.reset(); }
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(&op)) {
                if (!origin(branch.getCondition())) {
                    return refuse(ArrayContentsFailure::UnknownValue, &op);
                }
                // ponytail: single-block structured arms only; nested CFG
                // needs its own lifetime/transport proof. Both arms run in
                // separate exact states, including an implicit empty else.
                for (mlir::Region & region : branch->getRegions()) {
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    if (region.empty() && &region == &branch.getElseRegion() &&
                        branch.getNumResults() == 0) {
                        continue;
                    }
                    if (!llvm::hasSingleElement(region) || region.front().empty() ||
                        region.front().getNumArguments() != 0 ||
                        !llvm::isa<mlir::scf::YieldOp>(region.front().getTerminator())) {
                        return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                    }
                }
                auto copy = snapshot();
                if (!copy) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                if (!branch.getElseRegion().empty()) {
                    copy->current = &branch.getElseRegion().front().front();
                }
                alternatives.push_back(std::move(*copy));
                state.current = &branch.getThenRegion().front().front();
                continue;
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(&op)) {
                if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(yield->getParentOp())) {
                    if (!state.loop || state.loop->body != op.getBlock()) {
                        return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                    }
                    const auto failure =
                        transport(state, loop.getBeforeArguments(), yield.getOperands());
                    if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                    state.current = &loop.getBefore().front().front();
                    continue;
                }
                auto branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
                if (!branch) { return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op); }
                const auto failure = transport(state, branch.getResults(), yield.getOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                state.current = branch->getNextNode();
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(&op)) {
                const auto failure = forward(state, branch.getDest(), branch.getDestOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::CondBranchOp>(&op)) {
                if (!origin(branch.getCondition())) {
                    return refuse(ArrayContentsFailure::UnknownValue, &op);
                }
                if (!state.loop && !branch.getTrueDest()->empty()) {
                    auto latch =
                        llvm::dyn_cast<mlir::cf::BranchOp>(branch.getTrueDest()->getTerminator());
                    if (latch && latch.getDest() == op.getBlock()) {
                        const auto failure = loopProof.cfgCountedLoop(branch, latch);
                        if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                    }
                }
                if (state.loop && state.loop->header == op.getBlock()) {
                    const auto continued = loopProof.loopContinues();
                    if (!continued) {
                        return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                    }
                    // Only this independently checked finite guard selects an
                    // edge. Exact replay records every actual element alternative;
                    // all unrelated conditions retain both structural paths.
                    if (!*continued) { state.loop.reset(); }
                    const auto failure = forward(
                        state, *continued ? branch.getTrueDest() : branch.getFalseDest(),
                        *continued ? branch.getTrueDestOperands() : branch.getFalseDestOperands());
                    if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                    continue;
                }
                auto failure = alternative(branch.getFalseDest(), branch.getFalseDestOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                failure = forward(state, branch.getTrueDest(), branch.getTrueDestOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::SwitchOp>(&op)) {
                if (!origin(branch.getFlag())) {
                    return refuse(ArrayContentsFailure::UnknownValue, &op);
                }
                // The flag selects an edge without JS coercion. Prove every
                // structural edge, so neither case values nor exhaustiveness
                // supply a liveness fact. Repeated destinations still carry
                // their own operands. Push in reverse for default/case order.
                for (unsigned i = branch->getNumSuccessors() - 1; i != 0; --i) {
                    const auto failure = alternative(branch.getCaseDestinations()[i - 1],
                                                     branch.getCaseOperands(i - 1));
                    if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                }
                const auto failure =
                    forward(state, branch.getDefaultDestination(), branch.getDefaultOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                continue;
            }
            // Branch folding can leave an arith predicate constant behind.
            // It has no heap effects; preserve its identity for structural
            // branch traversal without using its value to prune either arm.
            if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(&op)) {
                state.values[constant.getResult()] = {constant.getResult()};
                continue;
            }
            // Truthy is total, noncapturing and nonthrowing (Operators.td). An
            // external input remains unknown for every other use; only its i1
            // result can be carried as a predicate. Neither branch is pruned.
            if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(&op)) {
                ContentsKind kind = ContentsKind::Identity;
                if (llvm::isa<ctjs::StringAttr>(constant.getValue())) {
                    kind = ContentsKind::String;
                } else if (llvm::isa<ctjs::BigIntAttr>(constant.getValue())) {
                    kind = ContentsKind::BigInt;
                } else if (llvm::isa<ctjs::UndefinedAttr, ctjs::NullAttr, ctjs::BooleanAttr,
                                     ctjs::NumberAttr>(constant.getValue())) {
                    kind = ContentsKind::NonBigInt;
                }
                state.values[constant.getResult()] = {constant.getResult(), kind};
                continue;
            }
            if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(&op)) {
                state.values[truthy.getResult()] = {truthy.getResult()};
                continue;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(&op)) {
                // Strict equality reads primitive contents or object identity;
                // it cannot coerce, call, throw or retain either operand
                // (Operators.td, value::strict_equals). The independent Boolean
                // result is known even when an operand is an opaque entry.
                // Every coercing kind needs BOTH original primitive origins.
                // Mixed BigInt comparisons have separate VM value-semantics
                // gaps, but no primitive conversion calls an object hook or
                // retains an input identity. Every structural arm is checked;
                // this query never infers a value, key or liveness fact.
                switch (compare.getKind()) {
                case ctjs::CompareKind::StrictEq: break;
                case ctjs::CompareKind::Eq:
                case ctjs::CompareKind::Lt:
                case ctjs::CompareKind::Le:
                case ctjs::CompareKind::Gt:
                case ctjs::CompareKind::Ge: {
                    const ContentsValue left = held(compare.getLhs());
                    const ContentsValue right = held(compare.getRhs());
                    const mlir::Value lhs = left.origin();
                    const mlir::Value rhs = right.origin();
                    if (!lhs || !rhs) {
                        return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                    }
                    if ((!left.nonBigInt() && !left.bigInt()) ||
                        (!right.nonBigInt() && !right.bigInt())) {
                        return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                    }
                    // loose_equals uses digits, String parsing and static
                    // numeric conversions. Its BigInt/Boolean arm additionally
                    // enters to_primitive's guard; all relational kinds do so.
                    // Primitive inputs return before any lookup or user call.
                    // The guard's independent Error cannot expose this whole
                    // frame's unpublished fresh locals. Calls, handlers and
                    // publication still refuse. Retention supplies no normal
                    // completion, allocation-success or no-throw contract.
                    break;
                }
                default: return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                state.values[compare.getResult()] = {compare.getResult(), ContentsKind::NonBigInt};
                continue;
            }
            if (auto convert = llvm::dyn_cast<ctjs::ConvertOp>(&op)) {
                // ToBoolean, like Truthy, is total and noncapturing, but returns
                // a !ctjs.value Boolean. Its primitive result carries no heap
                // alias; this does not turn its input into a known origin.
                if (convert.getKind() != ctjs::ConvertKind::ToBoolean) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                state.values[convert.getResult()] = {convert.getResult(), ContentsKind::NonBigInt};
                continue;
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(&op)) {
                // Not/TypeOf/Void never invoke user code or retain their
                // already-evaluated operand (Operators.td, VM coerce.cpp).
                // Not yields a Boolean; TypeOf yields a String; Void yields
                // Undefined. TypeOf's VM String allocation may hit the fatal
                // allocation ceiling, but carries no operand object identity.
                // This is a contents proof, not a no-allocation/effect claim.
                // Keep an independent primitive origin without a value, key,
                // operand alias or structural-edge liveness fact.
                ContentsKind kind = ContentsKind::NonBigInt;
                std::optional<std::size_t> integerNumber;
                std::optional<std::size_t> negativeIntegerNumber;
                std::optional<std::uint32_t> convertedBits;
                unsigned unaryDepth = 0;
                switch (unary.getKind()) {
                case ctjs::UnaryKind::Not:
                case ctjs::UnaryKind::Void: break;
                case ctjs::UnaryKind::TypeOf: kind = ContentsKind::String; break;
                case ctjs::UnaryKind::Neg:
                case ctjs::UnaryKind::Plus:
                case ctjs::UnaryKind::BitNot: {
                    const ContentsValue input = held(unary.getOperand());
                    if (input.bigInt()) {
                        if (unary.getKind() == ctjs::UnaryKind::Plus) {
                            // to_number_value rejects BigInt before any lookup
                            // or user conversion. Its TypeError (or depth-guard
                            // Error) has no input/local object edge. The entire
                            // frame still excludes calls, handlers and publication.
                            // Keep the independent Number carrier used by the VM
                            // and inspect EVERY structural continuation; neither
                            // successful completion nor dead code follows here.
                            break;
                        }
                        // negate_value/bit_not_value allocate independent BigInt
                        // digits before any conversion or user callback. Record
                        // the actual category, never a Number or concrete value.
                        // No allocation-success/native effect claim follows.
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        kind = ContentsKind::BigInt;
                        break;
                    }
                    if (!input.nonBigInt()) {
                        return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                    }
                    // Known primitive non-BigInt inputs cannot invoke object
                    // conversion or reach Plus's catchable BigInt TypeError.
                    // All three return an independent Number. BitNot uses the
                    // VM's static conversion, but objects stay outside this
                    // common source-compatible proof. String parsing may
                    // allocate C++ temporaries; allocation success is unproved.
                    // Neg/Plus still enter a recursion guard that may throw an
                    // unrelated RangeError. This whole-frame retention query
                    // rejects publication, calls and handlers, so that early
                    // exit cannot expose its fresh locals. This is NOT proof
                    // of normal completion or an effect/no-throw contract.
                    if (unary.getKind() != ctjs::UnaryKind::BitNot) {
                        // Preserve held signed Numbers through Plus/Neg, keeping
                        // negative magnitudes separate from own-index facts.
                        // Canonical original Strings share the exact decimal
                        // conversion used by Sub; zero stays nonnegative.
                        integerNumber = boundedConvertedNumber(input);
                        negativeIntegerNumber =
                            integerNumber ? std::nullopt : boundedConvertedNumber(input, true);
                        if (unary.getKind() == ctjs::UnaryKind::Neg && integerNumber != 0) {
                            std::swap(integerNumber, negativeIntegerNumber);
                        }
                        if ((integerNumber || negativeIntegerNumber) && !spend()) {
                            return refuse(ArrayContentsFailure::WorkLimit, &op);
                        }
                        unaryDepth = std::min(input.unaryDepth + 1, 65U);
                        if (!integerNumber && !negativeIntegerNumber && unaryDepth <= 64) {
                            // ToUint32(-x) is -ToUint32(x) modulo 2^32. Keep
                            // the read-time conversion without inventing an exact
                            // Number for subsequent arithmetic or property access.
                            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                            convertedBits = boundedConvertedBits(input);
                            if (convertedBits && unary.getKind() == ctjs::UnaryKind::Neg) {
                                *convertedBits = 0U - *convertedBits;
                            }
                        }
                    } else {
                        ContentsValue result;
                        boundedNumberComplement(input, result);
                        integerNumber = result.integerNumber;
                        negativeIntegerNumber = result.negativeIntegerNumber;
                        if ((integerNumber || negativeIntegerNumber) && !spend()) {
                            return refuse(ArrayContentsFailure::WorkLimit, &op);
                        }
                    }
                    break;
                }
                default: return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                state.values[unary.getResult()] = {
                    unary.getResult(), kind,          integerNumber, negativeIntegerNumber,
                    std::nullopt,      convertedBits, unaryDepth};
                if (unary.getKind() == ctjs::UnaryKind::Plus ||
                    unary.getKind() == ctjs::UnaryKind::Neg) {
                    auto & result = state.values[unary.getResult()];
                    boundedNumberUnary(held(unary.getOperand()),
                                       unary.getKind() == ctjs::UnaryKind::Neg, result);
                    if (result.arithmeticNumber && !spend()) {
                        return refuse(ArrayContentsFailure::WorkLimit, &op);
                    }
                }
                continue;
            }
            if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(&op)) {
                switch (binary.getKind()) {
                case ctjs::BinaryKind::Sub:
                case ctjs::BinaryKind::Mul:
                case ctjs::BinaryKind::Div:
                case ctjs::BinaryKind::Mod:
                case ctjs::BinaryKind::Pow:
                case ctjs::BinaryKind::Add:
                case ctjs::BinaryKind::Concat: break;
                default: return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                const ContentsValue left = held(binary.getLhs());
                const ContentsValue right = held(binary.getRhs());
                const mlir::Value lhs = left.origin();
                const mlir::Value rhs = right.origin();
                if (lhs && rhs && (left.nonBigInt() || left.bigInt()) &&
                    (right.nonBigInt() || right.bigInt()) &&
                    (binary.getKind() == ctjs::BinaryKind::Concat ||
                     (binary.getKind() == ctjs::BinaryKind::Add &&
                      (left.string() || right.string())))) {
                    // Concat converts both primitives before bigint_binary;
                    // Add selects its String arm before mixed-BigInt errors.
                    // BigInt conversion copies digits, never an input object
                    // or a user callback. Only an independently proved String
                    // permits Add: a generic non-BigInt result can be Number.
                    // Add's guarded ToPrimitive still has an unrelated Error
                    // exit. The entire frame's call/publication/handler refusals
                    // apply; this proves neither completion nor native effects.
                    if (binary.getKind() == ctjs::BinaryKind::Add) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    }
                    state.values[binary.getResult()] = {binary.getResult(), ContentsKind::String};
                    continue;
                }
                if (lhs && rhs && left.bigInt() && right.bigInt() &&
                    (binary.getKind() == ctjs::BinaryKind::Add ||
                     binary.getKind() == ctjs::BinaryKind::Sub ||
                     binary.getKind() == ctjs::BinaryKind::Mul ||
                     binary.getKind() == ctjs::BinaryKind::Div ||
                     binary.getKind() == ctjs::BinaryKind::Mod ||
                     binary.getKind() == ctjs::BinaryKind::Pow)) {
                    // bigint_binary combines digits into a fresh independent
                    // BigInt. Add first enters to_primitive's depth guard;
                    // Div/Mod raise an independent RangeError for zero divisors,
                    // and Pow for negative or VM-capped oversized exponents.
                    // bigint_pow checks those bounds before computing digits,
                    // including the VM's unconditional cap for small bases.
                    // No user conversion or local object edge enters those
                    // errors. The whole-frame exclusion of calls,
                    // handlers and publication keeps either early exit from
                    // exposing unpublished fresh objects. The normal result's
                    // category proves neither allocation success nor normal
                    // completion or no-throw/native effects. Mixed inputs remain
                    // a separate boundary even on observed success.
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    state.values[binary.getResult()] = {binary.getResult(), ContentsKind::BigInt};
                    continue;
                }
                if ((binary.getKind() == ctjs::BinaryKind::Add ||
                     binary.getKind() == ctjs::BinaryKind::Sub ||
                     binary.getKind() == ctjs::BinaryKind::Mul ||
                     binary.getKind() == ctjs::BinaryKind::Div ||
                     binary.getKind() == ctjs::BinaryKind::Mod ||
                     binary.getKind() == ctjs::BinaryKind::Pow) &&
                    lhs && rhs &&
                    ((left.bigInt() && right.nonBigInt()) ||
                     (left.nonBigInt() && right.bigInt()))) {
                    // Mixed original primitives cannot retain local objects:
                    // bigint_binary returns an independent TypeError/Undefined.
                    // Add first makes both operands primitive and may concatenate
                    // Strings instead; neither that result nor its depth-guard
                    // Error aliases an input. Only the separate String proof
                    // above supplies that category; this result is never BigInt.
                    // Calls, handlers and publication remain excluded across
                    // the whole frame. Check EVERY structural continuation:
                    // retention proves no successful completion or native effect.
                    state.values[binary.getResult()] = {binary.getResult(),
                                                        ContentsKind::NonBigInt};
                    continue;
                }
                if (!lhs || !rhs || !left.nonBigInt() || !right.nonBigInt()) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                // With primitive non-BigInt originals, binary_op cannot call
                // object conversions or return an operand object. Sub/Mul/Div/
                // Mod/Pow return Number via to_number_value. Add uses guarded
                // to_primitive followed by static Number/String operations;
                // Concat uses primitive to_string. Their result is independent,
                // without a Number/String tag, value, index or key inference
                // except for bounded exact Number arithmetic below.
                // Add and numeric conversions have a depth guard that may
                // throw an unrelated RangeError. This whole-frame query refuses
                // calls, handlers and publication, so it cannot retain fresh
                // locals on that exit. This is retention-only evidence, never
                // a normal-completion or no-throw/effect contract. String
                // results allocate in the VM and static conversions can allocate
                // C++ temporaries; absence/success of allocation is unproved.
                ContentsValue result{binary.getResult(), ContentsKind::NonBigInt};
                if (binary.getKind() == ctjs::BinaryKind::Add) {
                    boundedNumberSum(left, right, result);
                    if (result.convertedBits && !spend()) {
                        return refuse(ArrayContentsFailure::WorkLimit, &op);
                    }
                }
                if (binary.getKind() == ctjs::BinaryKind::Sub) {
                    boundedNumberDifference(left, right, result);
                    if (!result.integerNumber && !result.negativeIntegerNumber &&
                        boundedConvertedNumber(right) == 0 && left.unaryDepth <= 64) {
                        // ToNumber(x) - 0 preserves ToUint32(x), including signed
                        // zero and nonfinite inputs. This is no exact Number fact.
                        result.convertedBits = boundedConvertedBits(left);
                        result.unaryDepth = left.unaryDepth;
                    }
                    if (result.integerNumber || result.negativeIntegerNumber ||
                        result.convertedBits) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    }
                }
                if (binary.getKind() == ctjs::BinaryKind::Mul) {
                    boundedNumberProduct(left, right, result);
                    if (result.integerNumber || result.negativeIntegerNumber ||
                        result.convertedBits) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    }
                }
                if (binary.getKind() == ctjs::BinaryKind::Div ||
                    binary.getKind() == ctjs::BinaryKind::Mod) {
                    boundedNumberDivision(left, right, binary.getKind() == ctjs::BinaryKind::Mod,
                                          result);
                    if (result.integerNumber || result.negativeIntegerNumber ||
                        result.convertedBits) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    }
                }
                if (binary.getKind() == ctjs::BinaryKind::Pow) {
                    boundedNumberPower(left, right, result);
                    if (result.integerNumber || result.negativeIntegerNumber ||
                        result.convertedBits) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    }
                }
                state.values[binary.getResult()] = result;
                continue;
            }
            if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(&op)) {
                switch (binary.getKind()) {
                case ctjs::BinaryKind::Add:
                case ctjs::BinaryKind::BitAnd:
                case ctjs::BinaryKind::BitOr:
                case ctjs::BinaryKind::BitXor:
                case ctjs::BinaryKind::Shl:
                case ctjs::BinaryKind::Shr:
                case ctjs::BinaryKind::UShr: break;
                default: return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                const ContentsValue left = held(binary.getLhs());
                const ContentsValue right = held(binary.getRhs());
                const mlir::Value lhs = left.origin();
                const mlir::Value rhs = right.origin();
                if (!lhs || !rhs) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                if ((left.bigInt() && right.nonBigInt()) || (left.nonBigInt() && right.bigInt()) ||
                    (binary.getKind() == ctjs::BinaryKind::UShr && left.bigInt() &&
                     right.bigInt())) {
                    // bigint_binary rejects mixed original primitives, or two
                    // BigInts for UShr, before conversion. Its independent
                    // TypeError/Undefined carrier has no operand/local edge or
                    // BigInt category. Keep all continuations and whole-frame
                    // exclusions; normal completion/native effects are unproved.
                    state.values[binary.getResult()] = {binary.getResult(),
                                                        ContentsKind::NonBigInt};
                    continue;
                }
                if (left.bigInt() && right.bigInt() &&
                    (binary.getKind() == ctjs::BinaryKind::Add ||
                     binary.getKind() == ctjs::BinaryKind::BitAnd ||
                     binary.getKind() == ctjs::BinaryKind::BitOr ||
                     binary.getKind() == ctjs::BinaryKind::BitXor ||
                     binary.getKind() == ctjs::BinaryKind::Shl ||
                     binary.getKind() == ctjs::BinaryKind::Shr)) {
                    // These exact bigint_binary arms allocate independent digits
                    // before static Number conversion, with no input alias or
                    // user conversion. Signed shifts may instead raise an
                    // independent RangeError for an oversized left shift,
                    // including a negative right-shift count. The whole-frame
                    // exclusion of calls, handlers and publication prevents
                    // that early exit from retaining unpublished local objects.
                    // Keep the normal result's separate per-path category; this
                    // proves neither allocation success nor normal completion
                    // or no-throw/native effects.
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    state.values[binary.getResult()] = {binary.getResult(), ContentsKind::BigInt};
                    continue;
                }
                if (!left.nonBigInt() || !right.nonBigInt()) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                // Independent primitive origins exclude source user conversion.
                // Fresh objects/arrays are insufficient: inherited valueOf or
                // toString can retain them or their contents even though today's
                // VM converts them statically. Only bounded exact primitive
                // conversions or saved Number facts supply an index.
                ContentsValue result{binary.getResult(), ContentsKind::NonBigInt};
                if (binary.getKind() == ctjs::BinaryKind::Add) {
                    boundedNumberSum(left, right, result);
                    if (result.convertedBits && !spend()) {
                        return refuse(ArrayContentsFailure::WorkLimit, &op);
                    }
                } else {
                    boundedNumberBitwise(left, right, binary.getKind(), result);
                    if (result.integerNumber || result.negativeIntegerNumber) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    }
                }
                state.values[binary.getResult()] = result;
                continue;
            }
            if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(&op)) {
                if (objectSites.insert(&op).second) { out.objects.push_back(&op); }
                state.objects.try_emplace(&op);
                state.values[object.getResult()] = {object.getResult()};
                continue;
            }
            if (auto array = llvm::dyn_cast<ctjs::CreateArrayOp>(&op)) {
                if (arraySites.insert(&op).second) { out.arrays.push_back(&op); }
                auto & elements = state.arrays[&op];
                for (unsigned position = 0; position < array.getElements().size(); ++position) {
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    const ContentsValue value = held(array.getElements()[position]);
                    if (!value.origin()) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                    elements.push_back(value);
                    out.writes.push_back({&op, position, &op, position, value.original});
                }
                state.values[array.getResult()] = {array.getResult()};
                continue;
            }
            if (auto copy = llvm::dyn_cast<ctjs::CopyPropsOp>(&op)) {
                const mlir::Value source = origin(copy.getSource());
                const mlir::Value target = origin(copy.getTarget());
                mlir::Operation * from = source ? source.getDefiningOp() : nullptr;
                mlir::Operation * into = target ? target.getDefiningOp() : nullptr;
                auto sourceObject = state.objects.find(from);
                auto targetObject = state.objects.find(into);
                if (sourceObject == state.objects.end() || targetObject == state.objects.end()) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                // The live runtime copies enumerable own entries through property
                // lookup, which can invoke getters in general (objects/chain.cpp).
                // This complete query admits only fresh own data with attr_default:
                // no accessors, prototypes, descriptors or unknown effects occur.
                // Snapshot before writes, including when both exact aliases name
                // one object. Charge the allocation and each subsequent copy edge.
                if (!spend(sourceObject->second.size())) {
                    return refuse(ArrayContentsFailure::WorkLimit, &op);
                }
                const HeldProperties snapshot = sourceObject->second;
                for (const auto & [key, value] : snapshot) {
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    targetObject->second[key] = value;
                    out.propertyCopies.push_back({&op, from, into, key, value.original});
                }
                continue;
            }
            if (llvm::isa<ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(&op)) {
                const mlir::Value base = origin(op.getOperand(0));
                mlir::Operation * container = base ? base.getDefiningOp() : nullptr;
                auto object = state.objects.find(container);
                if (object == state.objects.end()) {
                    // Array deletion has different hole behavior in JS and the VM.
                    // This proof covers only fresh ordinary own data properties.
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                mlir::StringAttr key;
                if (auto named = llvm::dyn_cast<ctjs::DeleteNamedOp>(&op)) {
                    key = ownObjectKey(named.getNameAttr());
                } else {
                    const mlir::Value keyValue = origin(op.getOperand(1));
                    if (keyValue) { key = ownObjectKey(keyValue); }
                }
                if (!key) { return refuse(ArrayContentsFailure::UnknownPropertyKey, &op); }
                auto & properties = object->second;
                auto found = properties.find(key);
                const mlir::Value removed =
                    found == properties.end() ? mlir::Value{} : found->second.original;
                if (removed) {
                    // Fresh set_property fields are configurable (value.hpp's
                    // attr_default). delete_own_property erases exactly that own
                    // field; no prototype walk or accessor can occur in this subset.
                    // MapVector erase shifts fields and repairs its index. Charge
                    // the complete field set before mutating even a single entry.
                    if (!spend(properties.size())) {
                        return refuse(ArrayContentsFailure::WorkLimit, &op);
                    }
                    properties.erase(key);
                }
                out.propertyDeletions.push_back({&op, container, key, removed});
                continue;
            }
            if (llvm::isa<ctjs::AppendOp, ctjs::SetPropertyOp, ctjs::GetPropertyOp>(&op)) {
                if (auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(&op)) {
                    if (const auto string = boundedStringRead(
                            held(load.getObject()), held(op.getOperand(1)), load.getResult())) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        state.values[load.getResult()] = *string;
                        continue;
                    }
                }
                const mlir::Value base = origin(op.getOperand(0));
                mlir::Operation * container = base ? base.getDefiningOp() : nullptr;
                auto object = state.objects.find(container);
                if (object != state.objects.end() && !llvm::isa<ctjs::AppendOp>(&op)) {
                    // The implicit prototype can intercept even a first write
                    // and retain its value. Until an own-data/prototype proof
                    // exists, an assignment cannot establish object contents.
                    if (llvm::isa<ctjs::SetPropertyOp>(&op)) {
                        return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                    }
                    const mlir::Value keyValue = origin(op.getOperand(1));
                    const auto key = keyValue ? ownObjectKey(keyValue) : mlir::StringAttr{};
                    if (!key) { return refuse(ArrayContentsFailure::UnknownPropertyKey, &op); }
                    auto & properties = object->second;
                    auto found = properties.find(key);
                    if (found == properties.end()) {
                        return refuse(ArrayContentsFailure::MissingProperty, &op);
                    }
                    state.values[op.getResult(0)] = found->second;
                    out.propertyReads.push_back({&op, container, key, found->second.original});
                    continue;
                }
                auto found = state.arrays.find(container);
                if (found == state.arrays.end()) {
                    return refuse(ArrayContentsFailure::UnknownArray, &op);
                }
                auto & elements = found->second;
                if (auto append = llvm::dyn_cast<ctjs::AppendOp>(&op)) {
                    const ContentsValue value = held(append.getElement());
                    if (!value.origin()) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                    if (elements.size() >= 4294967295ULL) {
                        return refuse(ArrayContentsFailure::MissingElement, &op);
                    }
                    out.writes.push_back({&op, 1, container, elements.size(), value.original});
                    elements.push_back(value);
                    continue;
                }
                const ContentsValue keyValue = held(op.getOperand(1));
                const mlir::Value key = keyValue.origin();
                if (llvm::isa<ctjs::GetPropertyOp>(&op)) {
                    const auto name = key ? ownObjectKey(key) : mlir::StringAttr{};
                    if (name && name.getValue() == "length") {
                        // lookup_property returns js_length as Number before any
                        // prototype lookup. This exact tracked array admits no
                        // sparse writes/deletion/accessors. Snapshot this read;
                        // a saved origin must survive later array appends unchanged.
                        if (elements.size() > 4294967295ULL) {
                            return refuse(ArrayContentsFailure::MissingElement, &op);
                        }
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        state.values[op.getResult(0)] = {op.getResult(0), ContentsKind::NonBigInt,
                                                         elements.size()};
                        continue;
                    }
                } else if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(&op)) {
                    const auto name = key ? ownObjectKey(key) : mlir::StringAttr{};
                    if (name && name.getValue() == "length") {
                        const ContentsValue target = held(store.getValue());
                        const mlir::Value value = target.origin();
                        auto literal =
                            value ? value.getDefiningOp<ctjs::ConstantOp>() : ctjs::ConstantOp{};
                        auto wanted = target.integerNumber;
                        if (!wanted && literal && llvm::isa<ctjs::NumberAttr>(literal.getValue())) {
                            wanted = ownArrayIndex(value);
                        }
                        if (!wanted) { return refuse(ArrayContentsFailure::UnknownIndex, &op); }
                        if (*wanted > elements.size()) {
                            return refuse(ArrayContentsFailure::MissingElement, &op);
                        }
                        // Fresh dense arrays own writable length and configurable
                        // elements; an exact Number runs no coercion hook. Keep
                        // saved origins/lengths and every historical cycle edge.
                        // ponytail: non-growing lengths only; growth needs hole evidence.
                        if (!spend(elements.size() - *wanted)) {
                            return refuse(ArrayContentsFailure::WorkLimit, &op);
                        }
                        elements.resize(*wanted);
                        continue;
                    }
                }
                auto index = ownArrayIndex(keyValue);
                // The loop guard independently bounded every key before replay.
                // Retain its exact Number snapshot without generalizing scalar
                // contents, length writes or conversion facts.
                if (!index && state.loop) { index = arithmeticArrayIndex(keyValue); }
                if (!index) { return refuse(ArrayContentsFailure::UnknownIndex, &op); }
                // Overwrite only. Extending with set_property can leave holes or
                // consult a prototype setter; literal append has neither behavior.
                if (*index >= elements.size()) {
                    return refuse(ArrayContentsFailure::MissingElement, &op);
                }
                if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(&op)) {
                    const ContentsValue value = held(store.getValue());
                    if (!value.origin()) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                    elements[*index] = value;
                    out.writes.push_back({&op, 2, container, *index, value.original});
                } else {
                    state.values[op.getResult(0)] = elements[*index];
                    out.reads.push_back({&op, container, *index, elements[*index].original});
                }
                continue;
            }
            if (llvm::isa<ctjs::ReturnOp>(&op)) {
                if (state.frame && !state.frameExited) {
                    return refuse(ArrayContentsFailure::InvalidFrame, &op);
                }
                const mlir::Value value = origin(op.getOperand(0));
                if (!value) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                ArrayContentsExit exit;
                exit.by = &op;
                exit.value = value;
                llvm::SmallVector<mlir::Value, 8> pending{value};
                llvm::SmallPtrSet<mlir::Operation *, 8> visited;
                while (!pending.empty()) {
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    mlir::Operation * site = pending.pop_back_val().getDefiningOp();
                    if (!isTrackedSite(site) || !visited.insert(site).second) { continue; }
                    exit.reachableSites.push_back(site);
                    auto array = state.arrays.find(site);
                    if (array != state.arrays.end()) {
                        for (const ContentsValue & element : array->second) {
                            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                            pending.push_back(element.original);
                        }
                    }
                    auto object = state.objects.find(site);
                    if (object != state.objects.end()) {
                        for (const auto & [key, element] : object->second) {
                            (void)key;
                            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                            pending.push_back(element.original);
                        }
                    }
                }
                // Public evidence keeps original producers, never private scalar facts.
                // Charge the projection before copying each container and its values.
                for (const auto & [array, elements] : state.arrays) {
                    if (!spend() || !spend(elements.size())) {
                        return refuse(ArrayContentsFailure::WorkLimit, &op);
                    }
                    auto & originals = exit.arrays[array];
                    for (const ContentsValue & element : elements) {
                        originals.push_back(element.original);
                    }
                }
                for (const auto & [object, properties] : state.objects) {
                    if (!spend() || !spend(properties.size())) {
                        return refuse(ArrayContentsFailure::WorkLimit, &op);
                    }
                    auto & originals = exit.objects[object];
                    for (const auto & [key, element] : properties) {
                        originals[key] = element.original;
                    }
                }
                out.exits.push_back(std::move(exit));
                returned = true;
                continue;
            }
            // Throw is intentionally outside the subset: uncaught diagnostic
            // formatting may reenter JavaScript through toString/prototype hooks.
            return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
        }
        if (!returned) { return refuse(ArrayContentsFailure::UnsupportedControlFlow, function); }
        if (alternatives.empty()) { break; }
        state = alternatives.pop_back_val();
    }
    out.complete = true;
    return out;
}

} // namespace ctcompile::ctnative
