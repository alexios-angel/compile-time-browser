//===- ArrayContents.cpp - Exact array/object contents query ----*- C++ -*-===//
//
// computeArrayContents: a path-by-path symbolic execution of one ctjs.func that
// tracks the exact elements of every fresh array and the own properties of
// every fresh object, and records each write, read, copy and deletion. Escape
// analysis uses it to confine sites that are stored only into containers which
// never escape.
//
// Every structural path is enumerated separately ("replay"); a counted loop
// proved by LoopProof is stepped one iteration at a time. Anything outside the
// supported subset refuses the whole function with an ArrayContentsFailure.
//
// Several operator arms below may exit early with an independent Error (a
// recursion-depth guard, a BigInt RangeError or TypeError). The function is
// refused if it calls, installs handlers or publishes values, so such an exit
// cannot retain fresh locals. The result is retention evidence only: it proves
// neither normal completion, no-throw behaviour nor allocation success.
//
//===----------------------------------------------------------------------===//

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
    // Whether a binary result records a Number fact that must be charged:
    // for Add only converted bits count, for every other kind any exact Number.
    const auto numberFact = [](ctjs::BinaryKind kind, const ContentsValue & result) {
        return result.convertedBits || (kind != ctjs::BinaryKind::Add && result.exactNumber());
    };
    if (function.getBody().empty() || function.getBody().front().empty()) {
        return refuse(ArrayContentsFailure::UnsupportedControlFlow, function);
    }

    // Enumerate structural paths, never solver flags or annotations. A join
    // keeps separate exact states: unioning array aliases before a strong
    // overwrite would wrongly erase an element of an unmodified array. Every
    // ordinary branch edge is visited, including a constant flag's untaken
    // edge and a switch's default edge. A loop certified by LoopProof follows
    // its guard and reuses exact i1 choices within that path. Unsupported
    // paths refuse.
    State state;
    for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, function); }
        if (llvm::isa<ctjs::ValueType>(argument.getType())) {
            state.values[argument] = {argument, ContentsKind::Opaque};
        }
    }
    state.predecessors.try_emplace(&function.getBody().front(), nullptr);
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
        // are assigned simultaneously, including swaps. The incoming fact
        // replaces the destination's previous exact or opaque state.
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
    const auto forward = [&](State & path, mlir::Block * previous, mlir::Block * next,
                             mlir::ValueRange operands) {
        if (next->getParent() != &function.getBody() || next->empty() ||
            (!path.predecessors.try_emplace(next, previous).second &&
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
        // incoming edge, container and element before allocating the snapshot.
        if (!spend(state.values.size()) || !spend(state.predecessors.size())) {
            return std::nullopt;
        }
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
    const auto alternative = [&](mlir::Block * previous, mlir::Block * next,
                                 mlir::ValueRange operands) {
        auto copy = snapshot();
        if (!copy) { return ArrayContentsFailure::WorkLimit; }
        const auto failure = forward(*copy, previous, next, operands);
        if (failure == ArrayContentsFailure::None) { alternatives.push_back(std::move(*copy)); }
        return failure;
    };
    LoopProof loopProof{function, state, spend};
    bool returned = false;
    const auto step = [&](mlir::Operation & op) -> ArrayContentsFailure {
        if ((op.getNumRegions() != 0 && !llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(&op)) ||
            (op.getNumSuccessors() != 0 &&
             !llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(&op))) {
            return ArrayContentsFailure::UnsupportedControlFlow;
        }
        if (auto entered = llvm::dyn_cast<ctjs::FrameEnterOp>(&op)) {
            // The importer enters before seeding registers or allocating any
            // tracked object. Its depth failure is real, but on that path no
            // object from these sites exists. This is not an effect proof that
            // permits erasing the entry or treating it as pure or nonthrowing.
            if (&op != &function.getBody().front().front() || state.frame ||
                entered.getRegCountAttr().getInt() < 0) {
                return ArrayContentsFailure::InvalidFrame;
            }
            state.frame = entered;
            state.values[state.frame.getResult()] = {state.frame.getResult()};
            return ArrayContentsFailure::None;
        }
        if (auto exited = llvm::dyn_cast<ctjs::FrameExitOp>(&op)) {
            if (!state.frame || state.frameExited ||
                origin(exited->getOperand(0)) != state.frame.getResult() ||
                !llvm::isa_and_nonnull<ctjs::ReturnOp>(op.getNextNode())) {
                return ArrayContentsFailure::InvalidFrame;
            }
            state.frameExited = true;
            return ArrayContentsFailure::None;
        }
        if (auto root = llvm::dyn_cast<ctjs::RootOp>(&op)) {
            // RootOp parks a value only in this frame's window (Frames.td). The exact
            // matching exit kills that window; no call, suspension or unknown
            // effect in this query can keep it or expose its contents.
            if (!state.frame || state.frameExited ||
                origin(root->getOperand(0)) != state.frame.getResult()) {
                return ArrayContentsFailure::InvalidFrame;
            }
            if (!origin(root.getValue())) { return ArrayContentsFailure::UnknownValue; }
            return ArrayContentsFailure::None;
        }
        if (state.frameExited && !llvm::isa<ctjs::ReturnOp>(&op)) {
            return ArrayContentsFailure::InvalidFrame;
        }
        if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(&op)) {
            auto condition = loop.getConditionOp();
            auto failure = transport(state, loop.getBeforeArguments(), loop.getInits());
            if (failure != ArrayContentsFailure::None) { return failure; }
            failure = loopProof.countedLoop(&loop.getBefore().front(), &loop.getAfter().front(),
                                            loop.getInits(), condition.getCondition(),
                                            condition.getArgs(), loop.getYieldOp().getOperands());
            if (failure != ArrayContentsFailure::None) { return failure; }
            state.current = &loop.getBefore().front().front();
            return ArrayContentsFailure::None;
        }
        if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(&op)) {
            auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(condition->getParentOp());
            if (!loop || !state.loop || state.loop->header != op.getBlock()) {
                return ArrayContentsFailure::UnsupportedControlFlow;
            }
            const auto continued = loopProof.loopContinues();
            if (!continued) { return ArrayContentsFailure::UnsupportedControlFlow; }
            const auto failure = transport(state,
                                           *continued ? mlir::ValueRange(loop.getAfterArguments())
                                                      : mlir::ValueRange(loop.getResults()),
                                           condition.getArgs());
            if (failure != ArrayContentsFailure::None) { return failure; }
            state.current = *continued ? &loop.getAfter().front().front() : loop->getNextNode();
            if (!*continued) { state.loop.reset(); }
            return ArrayContentsFailure::None;
        }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(&op)) {
            if (!origin(branch.getCondition())) { return ArrayContentsFailure::UnknownValue; }
            // ponytail: single-block structured arms only; nested CFG would need
            // its own lifetime and successor-operand proof. Without a recorded
            // choice, both arms run in separate states, including an implicit
            // empty else. One charge per arm.
            if (!spend(2)) { return ArrayContentsFailure::WorkLimit; }
            // Inside a certified loop, LoopProof admits a nested scf.if only when
            // it runs once per iteration. A result-producing If reuses the arm
            // already chosen for its condition on this path; an If with no
            // results always replays both arms.
            const bool correlated = state.loop && branch.getNumResults() != 0;
            const auto [choiceKey, negated] = conditionChoiceKey(branch.getCondition(), spend);
            if (!choiceKey) { return ArrayContentsFailure::WorkLimit; }
            const auto choice = held(choiceKey).conditionChoice;
            if (correlated && choice) {
                auto & region =
                    *choice != negated ? branch.getThenRegion() : branch.getElseRegion();
                if (!region.empty()) { state.current = &region.front().front(); }
                return ArrayContentsFailure::None;
            }
            auto copy = snapshot();
            if (!copy) { return ArrayContentsFailure::WorkLimit; }
            if (correlated) {
                state.values[choiceKey].conditionChoice = !negated;
                copy->values[choiceKey].conditionChoice = negated;
            }
            if (!branch.getElseRegion().empty()) {
                copy->current = &branch.getElseRegion().front().front();
            }
            alternatives.push_back(std::move(*copy));
            state.current = &branch.getThenRegion().front().front();
            return ArrayContentsFailure::None;
        }
        if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(&op)) {
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(yield->getParentOp())) {
                if (!state.loop || state.loop->body != op.getBlock()) {
                    return ArrayContentsFailure::UnsupportedControlFlow;
                }
                const auto failure =
                    transport(state, loop.getBeforeArguments(), yield.getOperands());
                if (failure != ArrayContentsFailure::None) { return failure; }
                state.current = &loop.getBefore().front().front();
                return ArrayContentsFailure::None;
            }
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
            if (!branch) { return ArrayContentsFailure::UnsupportedControlFlow; }
            const auto failure = transport(state, branch.getResults(), yield.getOperands());
            if (failure != ArrayContentsFailure::None) { return failure; }
            state.current = branch->getNextNode();
            return ArrayContentsFailure::None;
        }
        if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(&op)) {
            return forward(state, op.getBlock(), branch.getDest(), branch.getDestOperands());
        }
        if (auto branch = llvm::dyn_cast<mlir::cf::CondBranchOp>(&op)) {
            if (!origin(branch.getCondition())) { return ArrayContentsFailure::UnknownValue; }
            if (!state.loop && !branch.getTrueDest()->empty()) {
                auto latch =
                    llvm::dyn_cast<mlir::cf::BranchOp>(branch.getTrueDest()->getTerminator());
                if (latch && latch.getDest() == op.getBlock()) {
                    const auto failure = loopProof.cfgCountedLoop(branch, latch);
                    if (failure != ArrayContentsFailure::None) { return failure; }
                }
            }
            if (state.loop && state.loop->header == op.getBlock()) {
                const auto continued = loopProof.loopContinues();
                if (!continued) { return ArrayContentsFailure::UnsupportedControlFlow; }
                // Only the certified loop guard selects an edge. Replay records
                // every actual element alternative; every other condition keeps
                // both structural paths.
                if (!*continued) { state.loop.reset(); }
                return forward(
                    state, op.getBlock(), *continued ? branch.getTrueDest() : branch.getFalseDest(),
                    *continued ? branch.getTrueDestOperands() : branch.getFalseDestOperands());
            }
            const auto failure =
                alternative(op.getBlock(), branch.getFalseDest(), branch.getFalseDestOperands());
            if (failure != ArrayContentsFailure::None) { return failure; }
            return forward(state, op.getBlock(), branch.getTrueDest(),
                           branch.getTrueDestOperands());
        }
        if (auto branch = llvm::dyn_cast<mlir::cf::SwitchOp>(&op)) {
            if (!origin(branch.getFlag())) { return ArrayContentsFailure::UnknownValue; }
            // The flag selects an edge without JS coercion. Every structural
            // edge is explored, so neither case values nor exhaustiveness are
            // used to prune. Repeated destinations still carry their own
            // operands. Cases are pushed in reverse so they run in order.
            for (unsigned i = branch->getNumSuccessors() - 1; i != 0; --i) {
                const auto failure = alternative(op.getBlock(), branch.getCaseDestinations()[i - 1],
                                                 branch.getCaseOperands(i - 1));
                if (failure != ArrayContentsFailure::None) { return failure; }
            }
            return forward(state, op.getBlock(), branch.getDefaultDestination(),
                           branch.getDefaultOperands());
        }
        // Branch folding can leave an arith predicate constant behind.
        // It has no heap effects; preserve its identity for structural
        // branch traversal without using its value to prune either arm.
        if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(&op)) {
            state.values[constant.getResult()] = {constant.getResult()};
            return ArrayContentsFailure::None;
        }
        // A constant has no heap effect. Record its primitive category so later
        // arithmetic and comparisons can tell String, BigInt and other
        // primitives apart.
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
            return ArrayContentsFailure::None;
        }
        // Truthy is total, noncapturing and nonthrowing (Operators.td). An
        // external input stays unknown for every other use; only its i1 result
        // is carried as a predicate, and neither branch is pruned.
        if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(&op)) {
            state.values[truthy.getResult()] = {truthy.getResult()};
            return ArrayContentsFailure::None;
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(&op)) {
            // Strict equality reads primitive contents or object identity; it
            // cannot coerce, call, throw or retain either operand (Operators.td,
            // value::strict_equals), so its Boolean result is known even for an
            // opaque operand. Every coercing kind needs both operands to be
            // known primitives: converting a primitive calls no object hook and
            // retains no input. The result is never used to prune a branch.
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
                if (!lhs || !rhs) { return ArrayContentsFailure::UnsupportedOperation; }
                if ((!left.nonBigInt() && !left.bigInt()) ||
                    (!right.nonBigInt() && !right.bigInt())) {
                    return ArrayContentsFailure::UnsupportedOperation;
                }
                // loose_equals uses digits, String parsing and static numeric
                // conversions; its BigInt/Boolean arm and every relational kind
                // also enter to_primitive's depth guard. Primitive inputs return
                // before any lookup or user call (see the file header for the
                // guard's early exit).
                break;
            }
            default: return ArrayContentsFailure::UnsupportedOperation;
            }
            state.values[compare.getResult()] = {compare.getResult(), ContentsKind::NonBigInt};
            return ArrayContentsFailure::None;
        }
        if (auto convert = llvm::dyn_cast<ctjs::ConvertOp>(&op)) {
            // ToBoolean, like Truthy, is total and noncapturing, but returns a
            // !ctjs.value Boolean. Its primitive result carries no heap alias;
            // its input does not become a known origin.
            if (convert.getKind() != ctjs::ConvertKind::ToBoolean) {
                return ArrayContentsFailure::UnsupportedOperation;
            }
            state.values[convert.getResult()] = {convert.getResult(), ContentsKind::NonBigInt};
            return ArrayContentsFailure::None;
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(&op)) {
            // Not, TypeOf and Void never invoke user code or retain their
            // operand (Operators.td, VM coerce.cpp). Not yields a Boolean,
            // TypeOf a String and Void Undefined. TypeOf allocates a VM String,
            // which may hit the fatal allocation limit but holds no operand
            // identity. The result is a fresh primitive with no recorded value.
            ContentsValue result{unary.getResult(), ContentsKind::NonBigInt};
            switch (unary.getKind()) {
            case ctjs::UnaryKind::Not:
            case ctjs::UnaryKind::Void: break;
            case ctjs::UnaryKind::TypeOf: result.kind = ContentsKind::String; break;
            case ctjs::UnaryKind::Neg:
            case ctjs::UnaryKind::Plus:
            case ctjs::UnaryKind::BitNot: {
                const ContentsValue input = held(unary.getOperand());
                if (input.bigInt()) {
                    if (unary.getKind() == ctjs::UnaryKind::Plus) {
                        // to_number_value rejects BigInt before any lookup or
                        // user conversion; its TypeError references no input
                        // or local object. Keep the NonBigInt result the VM
                        // would carry and keep exploring every continuation.
                        break;
                    }
                    // negate_value/bit_not_value allocate fresh BigInt digits
                    // before any conversion or user callback. Record the BigInt
                    // category only, never a Number or concrete value.
                    if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                    result.kind = ContentsKind::BigInt;
                    break;
                }
                if (!input.nonBigInt()) { return ArrayContentsFailure::UnsupportedOperation; }
                // A known non-BigInt primitive cannot invoke object conversion
                // or reach Plus's BigInt TypeError, and all three ops return a
                // fresh Number. Neg/Plus still enter a recursion guard that may
                // throw (see the file header).
                if (unary.getKind() != ctjs::UnaryKind::BitNot) {
                    // Carry exact signed Numbers through Plus/Neg, keeping
                    // negative magnitudes apart from own-index facts. Canonical
                    // literal Strings use the same exact decimal conversion as
                    // Sub; zero stays nonnegative.
                    result.integerNumber = boundedConvertedNumber(input);
                    result.negativeIntegerNumber =
                        result.integerNumber ? std::nullopt : boundedConvertedNumber(input, true);
                    if (unary.getKind() == ctjs::UnaryKind::Neg && result.integerNumber != 0) {
                        std::swap(result.integerNumber, result.negativeIntegerNumber);
                    }
                    if (result.exactNumber() && !spend()) {
                        return ArrayContentsFailure::WorkLimit;
                    }
                    // Carry ToUint32 bits through at most 64 chained unary
                    // operations. The depth saturates at 65, so a longer chain
                    // stops carrying bits and the work stays bounded.
                    result.unaryDepth = std::min(input.unaryDepth + 1, 65U);
                    if (!result.exactNumber() && result.unaryDepth <= 64) {
                        // ToUint32(-x) is -ToUint32(x) modulo 2^32. Keep only the
                        // converted bits, not an exact Number for later
                        // arithmetic or property access.
                        if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                        result.convertedBits = boundedConvertedBits(input);
                        if (result.convertedBits && unary.getKind() == ctjs::UnaryKind::Neg) {
                            *result.convertedBits = 0U - *result.convertedBits;
                        }
                    }
                } else {
                    boundedNumberComplement(input, result);
                    if (result.exactNumber() && !spend()) {
                        return ArrayContentsFailure::WorkLimit;
                    }
                }
                break;
            }
            default: return ArrayContentsFailure::UnsupportedOperation;
            }
            if (unary.getKind() == ctjs::UnaryKind::Plus ||
                unary.getKind() == ctjs::UnaryKind::Neg) {
                boundedNumberUnary(held(unary.getOperand()),
                                   unary.getKind() == ctjs::UnaryKind::Neg, result);
                if (result.arithmeticNumber && !spend()) { return ArrayContentsFailure::WorkLimit; }
            }
            state.values[unary.getResult()] = result;
            return ArrayContentsFailure::None;
        }
        if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(&op);
            binary && !ctjs::isBitwise(binary.getKind())) {
            if (!ctjs::isArithmetic(binary.getKind()) &&
                binary.getKind() != ctjs::BinaryKind::Concat) {
                return ArrayContentsFailure::UnsupportedOperation;
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
                // Concat converts both primitives before bigint_binary; Add
                // takes its String arm before any mixed-BigInt error. Only a
                // known String operand lets Add produce a String: otherwise
                // the result could be a Number.
                if (binary.getKind() == ctjs::BinaryKind::Add) {
                    if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                }
                state.values[binary.getResult()] = {binary.getResult(), ContentsKind::String};
                return ArrayContentsFailure::None;
            }
            if (lhs && rhs && left.bigInt() && right.bigInt() &&
                ctjs::isArithmetic(binary.getKind())) {
                // bigint_binary combines digits into a fresh BigInt. Div/Mod
                // throw a RangeError for a zero divisor, and Pow (bigint_pow)
                // for a negative or oversized exponent, before computing
                // digits; no user code or local object is involved.
                if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                state.values[binary.getResult()] = {binary.getResult(), ContentsKind::BigInt};
                return ArrayContentsFailure::None;
            }
            if (ctjs::isArithmetic(binary.getKind()) && lhs && rhs &&
                ((left.bigInt() && right.nonBigInt()) || (left.nonBigInt() && right.bigInt()))) {
                // Mixing a BigInt with another primitive makes bigint_binary
                // throw a TypeError that references no input. Add may instead
                // concatenate Strings; that case was handled above, so this
                // result is recorded as a non-BigInt primitive.
                state.values[binary.getResult()] = {binary.getResult(), ContentsKind::NonBigInt};
                return ArrayContentsFailure::None;
            }
            if (!lhs || !rhs || !left.nonBigInt() || !right.nonBigInt()) {
                return ArrayContentsFailure::UnsupportedOperation;
            }
            // With known non-BigInt primitive operands, binary_op cannot call
            // object conversions or return an operand. Sub/Mul/Div/Mod/Pow
            // return a Number via to_number_value; Add uses guarded
            // to_primitive and then static Number/String operations. The result
            // is a fresh primitive; only the bounded exact Number arithmetic
            // below records its value.
            ContentsValue result{binary.getResult(), ContentsKind::NonBigInt};
            boundedNumberBinary(binary.getKind(), left, right, result);
            if (binary.getKind() == ctjs::BinaryKind::Sub && !result.exactNumber() &&
                boundedConvertedNumber(right) == 0 && left.unaryDepth <= 64) {
                // ToNumber(x) - 0 preserves ToUint32(x), including signed zero
                // and nonfinite inputs. Only the bits are kept, not a Number.
                result.convertedBits = boundedConvertedBits(left);
                result.unaryDepth = left.unaryDepth;
            }
            if (numberFact(binary.getKind(), result) && !spend()) {
                return ArrayContentsFailure::WorkLimit;
            }
            state.values[binary.getResult()] = result;
            return ArrayContentsFailure::None;
        }
        // binary_static, and bitwise ctjs.binary. Every arm below requires known
        // primitive operands. ToNumeric is the identity on those, so binary_op
        // behaves like binary_op_static, apart from a Symbol's TypeError, which
        // is one more early exit this evidence does not exclude.
        if (llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp>(&op)) {
            const ctjs::BinaryKind kind = llvm::isa<ctjs::BinaryOp>(&op)
                                              ? llvm::cast<ctjs::BinaryOp>(&op).getKind()
                                              : llvm::cast<ctjs::BinaryStaticOp>(&op).getKind();
            const mlir::Value produced = op.getResult(0);
            if (kind != ctjs::BinaryKind::Add && !ctjs::isBitwise(kind)) {
                return ArrayContentsFailure::UnsupportedOperation;
            }
            const ContentsValue left = held(op.getOperand(0));
            const ContentsValue right = held(op.getOperand(1));
            const mlir::Value lhs = left.origin();
            const mlir::Value rhs = right.origin();
            if (!lhs || !rhs) { return ArrayContentsFailure::UnknownValue; }
            if ((left.bigInt() && right.nonBigInt()) || (left.nonBigInt() && right.bigInt()) ||
                (kind == ctjs::BinaryKind::UShr && left.bigInt() && right.bigInt())) {
                // bigint_binary rejects a BigInt mixed with another primitive,
                // or two BigInts for UShr, before conversion. Its TypeError
                // references no operand, and the result is not a BigInt.
                state.values[produced] = {produced, ContentsKind::NonBigInt};
                return ArrayContentsFailure::None;
            }
            if (left.bigInt() && right.bigInt() &&
                (kind == ctjs::BinaryKind::Add || kind == ctjs::BinaryKind::BitAnd ||
                 kind == ctjs::BinaryKind::BitOr || kind == ctjs::BinaryKind::BitXor ||
                 kind == ctjs::BinaryKind::Shl || kind == ctjs::BinaryKind::Shr)) {
                // These bigint_binary arms allocate fresh digits with no input
                // alias or user conversion. Signed shifts may instead throw a
                // RangeError for an oversized left shift (including a negative
                // right-shift count).
                if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                state.values[produced] = {produced, ContentsKind::BigInt};
                return ArrayContentsFailure::None;
            }
            if (!left.nonBigInt() || !right.nonBigInt()) {
                return ArrayContentsFailure::UnsupportedOperation;
            }
            // Known primitive operands rule out user conversion. A fresh object
            // or array would not: the VM's bitwise handlers run ToNumeric on
            // it, and an inherited valueOf or toString could retain it. Only
            // exact bounded conversions or recorded Number facts give an index.
            ContentsValue result{produced, ContentsKind::NonBigInt};
            boundedNumberBinary(kind, left, right, result);
            if (numberFact(kind, result) && !spend()) { return ArrayContentsFailure::WorkLimit; }
            state.values[produced] = result;
            return ArrayContentsFailure::None;
        }
        if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(&op)) {
            if (objectSites.insert(&op).second) { out.objects.push_back(&op); }
            state.objects.try_emplace(&op);
            state.values[object.getResult()] = {object.getResult()};
            return ArrayContentsFailure::None;
        }
        if (auto array = llvm::dyn_cast<ctjs::CreateArrayOp>(&op)) {
            if (arraySites.insert(&op).second) { out.arrays.push_back(&op); }
            auto & elements = state.arrays[&op];
            for (unsigned position = 0; position < array.getElements().size(); ++position) {
                if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                const ContentsValue value = held(array.getElements()[position]);
                if (!value.origin()) { return ArrayContentsFailure::UnknownValue; }
                elements.push_back(value);
                out.writes.push_back({&op, position, &op, position, value.original});
            }
            state.values[array.getResult()] = {array.getResult()};
            return ArrayContentsFailure::None;
        }
        if (auto copy = llvm::dyn_cast<ctjs::CopyPropsOp>(&op)) {
            const mlir::Value source = origin(copy.getSource());
            const mlir::Value target = origin(copy.getTarget());
            mlir::Operation * from = source ? source.getDefiningOp() : nullptr;
            mlir::Operation * into = target ? target.getDefiningOp() : nullptr;
            auto sourceObject = state.objects.find(from);
            auto targetObject = state.objects.find(into);
            if (sourceObject == state.objects.end() || targetObject == state.objects.end()) {
                return ArrayContentsFailure::UnsupportedOperation;
            }
            // The VM copies enumerable own entries through property lookup,
            // which can invoke getters in general (objects/chain.cpp). Here both
            // objects are fresh and hold only own data properties with
            // attr_default, so no accessor or prototype is involved. Snapshot
            // the source before writing, in case both operands are one object.
            if (!spend(sourceObject->second.size())) { return ArrayContentsFailure::WorkLimit; }
            const HeldProperties snapshot = sourceObject->second;
            for (const auto & [key, value] : snapshot) {
                if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                targetObject->second[key] = value;
                out.propertyCopies.push_back({&op, from, into, key, value.original});
            }
            return ArrayContentsFailure::None;
        }
        if (llvm::isa<ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(&op)) {
            const mlir::Value base = origin(op.getOperand(0));
            mlir::Operation * container = base ? base.getDefiningOp() : nullptr;
            auto object = state.objects.find(container);
            if (object == state.objects.end()) {
                // Array deletion has different hole behavior in JS and the VM.
                // This proof covers only fresh ordinary own data properties.
                return ArrayContentsFailure::UnsupportedOperation;
            }
            mlir::StringAttr key;
            if (auto named = llvm::dyn_cast<ctjs::DeleteNamedOp>(&op)) {
                key = ownObjectKey(named.getNameAttr());
            } else {
                const mlir::Value keyValue = origin(op.getOperand(1));
                if (keyValue) { key = ownObjectKey(keyValue); }
            }
            if (!key) { return ArrayContentsFailure::UnknownPropertyKey; }
            auto & properties = object->second;
            auto found = properties.find(key);
            const mlir::Value removed =
                found == properties.end() ? mlir::Value{} : found->second.original;
            if (removed) {
                // Fresh set_property fields are configurable (attr_default), so
                // delete_own_property erases exactly that own field with no
                // prototype walk or accessor. MapVector::erase shifts the later
                // fields, so charge the whole field set before erasing.
                if (!spend(properties.size())) { return ArrayContentsFailure::WorkLimit; }
                properties.erase(key);
            }
            out.propertyDeletions.push_back({&op, container, key, removed});
            return ArrayContentsFailure::None;
        }
        if (llvm::isa<ctjs::AppendOp, ctjs::SetPropertyOp, ctjs::GetPropertyOp>(&op)) {
            if (auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(&op)) {
                if (const auto string = boundedStringRead(
                        held(load.getObject()), held(op.getOperand(1)), load.getResult())) {
                    if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                    state.values[load.getResult()] = *string;
                    return ArrayContentsFailure::None;
                }
            }
            const mlir::Value base = origin(op.getOperand(0));
            mlir::Operation * container = base ? base.getDefiningOp() : nullptr;
            auto object = state.objects.find(container);
            if (object != state.objects.end() && !llvm::isa<ctjs::AppendOp>(&op)) {
                // A setter on the implicit prototype can intercept even a first
                // write and retain its value, so without a prototype proof an
                // assignment cannot establish object contents.
                if (llvm::isa<ctjs::SetPropertyOp>(&op)) {
                    return ArrayContentsFailure::UnsupportedOperation;
                }
                const mlir::Value keyValue = origin(op.getOperand(1));
                const auto key = keyValue ? ownObjectKey(keyValue) : mlir::StringAttr{};
                if (!key) { return ArrayContentsFailure::UnknownPropertyKey; }
                auto & properties = object->second;
                auto found = properties.find(key);
                if (found == properties.end()) { return ArrayContentsFailure::MissingProperty; }
                state.values[op.getResult(0)] = found->second;
                out.propertyReads.push_back({&op, container, key, found->second.original});
                return ArrayContentsFailure::None;
            }
            auto found = state.arrays.find(container);
            if (found == state.arrays.end()) { return ArrayContentsFailure::UnknownArray; }
            auto & elements = found->second;
            if (auto append = llvm::dyn_cast<ctjs::AppendOp>(&op)) {
                const ContentsValue value = held(append.getElement());
                if (!value.origin()) { return ArrayContentsFailure::UnknownValue; }
                // JS arrays hold at most 2^32 - 1 elements; refuse to model an
                // append past that limit.
                if (elements.size() >= 4294967295ULL) {
                    return ArrayContentsFailure::MissingElement;
                }
                // The second field is the stored value's operand index: append's
                // element is operand 1, set_property's value is operand 2.
                out.writes.push_back({&op, 1, container, elements.size(), value.original});
                elements.push_back(value);
                return ArrayContentsFailure::None;
            }
            const ContentsValue keyValue = held(op.getOperand(1));
            const mlir::Value key = keyValue.origin();
            if (llvm::isa<ctjs::GetPropertyOp>(&op)) {
                const auto name = key ? ownObjectKey(key) : mlir::StringAttr{};
                if (name && name.getValue() == "length") {
                    // lookup_property returns js_length as a Number before any
                    // prototype lookup, and a tracked array has no sparse
                    // writes, deletions or accessors. Snapshot the length now so
                    // a saved read is unaffected by later appends.
                    if (elements.size() > 4294967295ULL) {
                        return ArrayContentsFailure::MissingElement;
                    }
                    if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                    state.values[op.getResult(0)] = {op.getResult(0), ContentsKind::NonBigInt,
                                                     elements.size()};
                    return ArrayContentsFailure::None;
                }
            } else if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(&op)) {
                const auto name = key ? ownObjectKey(key) : mlir::StringAttr{};
                if (name && name.getValue() == "length") {
                    const ContentsValue target = held(store.getValue());
                    const mlir::Value value = target.origin();
                    auto wanted = target.integerNumber;
                    if (!wanted && value && ctjs::constantAttr<ctjs::NumberAttr>(value)) {
                        wanted = ownArrayIndex(value);
                    }
                    if (!wanted) { return ArrayContentsFailure::UnknownIndex; }
                    if (*wanted > elements.size()) { return ArrayContentsFailure::MissingElement; }
                    // A fresh dense array has a writable length and configurable
                    // elements, and an exact Number runs no coercion hook.
                    // Earlier write records stay, so cycle detection sees them.
                    // ponytail: shrinking only; growing would need hole tracking.
                    if (!spend(elements.size() - *wanted)) {
                        return ArrayContentsFailure::WorkLimit;
                    }
                    elements.resize(*wanted);
                    return ArrayContentsFailure::None;
                }
            }
            auto index = ownArrayIndex(keyValue);
            // Inside a certified loop, LoopProof already bounded every key, so
            // an exact arithmetic Number may also serve as an index here.
            if (!index && state.loop) { index = arithmeticArrayIndex(keyValue); }
            if (!index) { return ArrayContentsFailure::UnknownIndex; }
            // Overwrite only. Extending with set_property can leave holes or
            // consult a prototype setter; append has neither behaviour.
            if (*index >= elements.size()) { return ArrayContentsFailure::MissingElement; }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(&op)) {
                const ContentsValue value = held(store.getValue());
                if (!value.origin()) { return ArrayContentsFailure::UnknownValue; }
                elements[*index] = value;
                out.writes.push_back({&op, 2, container, *index, value.original});
            } else {
                state.values[op.getResult(0)] = elements[*index];
                out.reads.push_back({&op, container, *index, elements[*index].original});
            }
            return ArrayContentsFailure::None;
        }
        if (llvm::isa<ctjs::ReturnOp>(&op)) {
            if (state.frame && !state.frameExited) { return ArrayContentsFailure::InvalidFrame; }
            const mlir::Value value = origin(op.getOperand(0));
            if (!value) { return ArrayContentsFailure::UnknownValue; }
            ArrayContentsExit exit;
            exit.by = &op;
            exit.value = value;
            llvm::SmallVector<mlir::Value, 8> pending{value};
            llvm::SmallPtrSet<mlir::Operation *, 8> visited;
            while (!pending.empty()) {
                if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                mlir::Operation * site = pending.pop_back_val().getDefiningOp();
                if (!isTrackedSite(site) || !visited.insert(site).second) { continue; }
                exit.reachableSites.push_back(site);
                auto array = state.arrays.find(site);
                if (array != state.arrays.end()) {
                    for (const ContentsValue & element : array->second) {
                        if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                        pending.push_back(element.original);
                    }
                }
                auto object = state.objects.find(site);
                if (object != state.objects.end()) {
                    for (const auto & [key, element] : object->second) {
                        (void)key;
                        if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                        pending.push_back(element.original);
                    }
                }
            }
            // The exit record keeps only original producers, not scalar facts.
            // Charge each container and its values before copying them.
            for (const auto & [array, elements] : state.arrays) {
                if (!spend() || !spend(elements.size())) { return ArrayContentsFailure::WorkLimit; }
                auto & originals = exit.arrays[array];
                for (const ContentsValue & element : elements) {
                    originals.push_back(element.original);
                }
            }
            for (const auto & [object, properties] : state.objects) {
                if (!spend() || !spend(properties.size())) {
                    return ArrayContentsFailure::WorkLimit;
                }
                auto & originals = exit.objects[object];
                for (const auto & [key, element] : properties) {
                    originals[key] = element.original;
                }
            }
            out.exits.push_back(std::move(exit));
            returned = true;
            return ArrayContentsFailure::None;
        }
        // Throw is intentionally outside the subset: uncaught diagnostic
        // formatting may reenter JavaScript through toString/prototype hooks.
        return ArrayContentsFailure::UnsupportedOperation;
    };
    while (true) {
        returned = false;
        while (state.current != nullptr) {
            mlir::Operation & op = *state.current;
            state.current = op.getNextNode();
            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
            if (const auto failure = step(op); failure != ArrayContentsFailure::None) {
                return refuse(failure, &op);
            }
        }
        if (!returned) { return refuse(ArrayContentsFailure::UnsupportedControlFlow, function); }
        if (alternatives.empty()) { break; }
        state = alternatives.pop_back_val();
    }
    out.complete = true;
    return out;
}

} // namespace ctcompile::ctnative
