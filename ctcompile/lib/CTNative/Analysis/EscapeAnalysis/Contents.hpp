#pragma once

#include "ctcompile/CTNative/Analysis/EscapeAnalysis.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSEscapeEffects.h"
#include "ctcompile/CTJS/IR/CTJSTypes.h"

#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Location.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <utility>

namespace ctcompile::ctnative::escape_detail {

enum class ContentsKind {
    Identity,
    Opaque,
    NonBigInt,
    BigInt,
    String
};

// Facts belong to the held value, not mutable metadata on its SSA producer.
// Copies into successors and containers keep the read-time scalar snapshot.
// The original producer remains the identity used by public evidence records.
struct ContentsValue {
    mlir::Value original;
    ContentsKind kind = ContentsKind::Identity;
    // Exact bounded Numbers survive simultaneous successor transport and replay.
    std::optional<std::size_t> integerNumber = std::nullopt;
    // A negative Number's magnitude is never an own index or nonnegative start.
    std::optional<std::size_t> negativeIntegerNumber = std::nullopt;
    // An indexed ASCII String read keeps its character and its own SSA identity.
    std::optional<unsigned char> asciiCharacter = std::nullopt;
    // Result-only ToUint32 snapshot; never an arithmetic value or property key.
    std::optional<std::uint32_t> convertedBits = std::nullopt;
    unsigned unaryDepth = 0;
    // Independent binary64 arithmetic result, never reconstructed from converted bits.
    std::optional<double> arithmeticNumber = std::nullopt;
    unsigned arithmeticDepth = 0;
    // A selected truthiness edge in a certified loop. The input's producer or
    // transport replaces this snapshot; recomputing Truthy or Not does not.
    std::optional<bool> conditionChoice = std::nullopt;

    mlir::Value origin() const { return kind == ContentsKind::Opaque ? mlir::Value{} : original; }
    bool nonBigInt() const {
        return kind == ContentsKind::NonBigInt || kind == ContentsKind::String;
    }
    bool bigInt() const { return kind == ContentsKind::BigInt; }
    bool string() const { return kind == ContentsKind::String; }
};

inline std::pair<mlir::Value, bool> conditionChoiceKey(
    mlir::Value condition, llvm::function_ref<bool(std::size_t)> spend) {
    // Truthy, ToBoolean and logical Not are total, without object conversion. The flag
    // records whether the branch reverses the exact input's truthiness.
    auto truthy = condition.getDefiningOp<ctjs::TruthyOp>();
    if (!truthy) { return {condition, false}; }
    const auto directBoolean = [](mlir::Value value) {
        auto comparison = value.getDefiningOp<ctjs::CompareOp>();
        auto conversion = value.getDefiningOp<ctjs::ConvertOp>();
        auto unary = value.getDefiningOp<ctjs::UnaryOp>();
        auto literal = value.getDefiningOp<ctjs::ConstantOp>();
        return (comparison && comparison.getKind() == ctjs::CompareKind::StrictEq) ||
               (conversion && conversion.getKind() == ctjs::ConvertKind::ToBoolean) ||
               (unary && unary.getKind() == ctjs::UnaryKind::Not) ||
               (literal && llvm::isa<ctjs::BooleanAttr>(literal.getValue()));
    };
    const auto stripBoolean = [&](mlir::Value value) -> std::pair<mlir::Value, bool> {
        bool negated = false;
        while (true) {
            // The shared budget bounds traversal; exhaustion supplies no partial key.
            if (!spend(1)) { return {}; }
            if (auto convert = value.getDefiningOp<ctjs::ConvertOp>();
                convert && convert.getKind() == ctjs::ConvertKind::ToBoolean) {
                value = convert.getOperand();
                continue;
            }
            if (auto compare = value.getDefiningOp<ctjs::CompareOp>();
                compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
                for (bool reversed : {false, true}) {
                    auto input = reversed ? compare.getRhs() : compare.getLhs();
                    auto literal = (reversed ? compare.getLhs() : compare.getRhs())
                                       .getDefiningOp<ctjs::ConstantOp>();
                    auto boolean = literal ? llvm::dyn_cast<ctjs::BooleanAttr>(literal.getValue())
                                           : ctjs::BooleanAttr{};
                    if (!boolean) { continue; }
                    bool exactBoolean = true;
                    llvm::SmallVector<mlir::Value, 4> pending{input};
                    while (!pending.empty() && exactBoolean) {
                        if (!spend(1)) { return {}; }
                        auto current = pending.pop_back_val();
                        if (directBoolean(current)) { continue; }
                        auto selected = current.getDefiningOp<mlir::scf::IfOp>();
                        if (!selected) {
                            exactBoolean = false;
                            break;
                        }
                        const auto position = llvm::cast<mlir::OpResult>(current).getResultNumber();
                        for (mlir::Region & region : selected->getRegions()) {
                            if (!spend(1)) { return {}; }
                            if (!llvm::hasSingleElement(region) || region.front().empty() ||
                                region.front().getNumArguments() != 0) {
                                exactBoolean = false;
                                break;
                            }
                            auto yield =
                                llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                            if (!yield || yield.getNumOperands() != selected.getNumResults()) {
                                exactBoolean = false;
                                break;
                            }
                            pending.push_back(yield.getOperand(position));
                        }
                    }
                    // Equality with a Boolean preserves or reverses truthiness
                    // only when the other operand is already exactly Boolean.
                    // An opaque value, Number or read cannot borrow that tag.
                    // Every nested carrier leaf must prove its Boolean tag;
                    // correspondence and all arm effects are checked separately.
                    if (exactBoolean) {
                        value = input;
                        negated ^= !boolean.getValue();
                        break;
                    }
                }
                if (value != compare.getResult()) { continue; }
            }
            auto unary = value.getDefiningOp<ctjs::UnaryOp>();
            if (!unary || unary.getKind() != ctjs::UnaryKind::Not) { break; }
            value = unary.getOperand();
            negated = !negated;
        }
        return {value, negated};
    };
    bool complete = true;
    const auto sameOperand = [&](mlir::Value left, mlir::Value right) {
        if (left == right) { return true; }
        if (!spend(2)) {
            complete = false;
            return false;
        }
        auto first = left.getDefiningOp<ctjs::ConstantOp>();
        auto second = right.getDefiningOp<ctjs::ConstantOp>();
        // Exact immutable primitive attributes supply the same comparison input.
        // No snapshot, coercion, signed-zero or BigInt spelling equivalence is inferred.
        return first && second &&
               llvm::isa<ctjs::UndefinedAttr, ctjs::NullAttr, ctjs::BooleanAttr, ctjs::NumberAttr,
                         ctjs::StringAttr, ctjs::BigIntAttr>(first.getValue()) &&
               first.getValue() == second.getValue();
    };
    const auto sameComparison = [&](mlir::Value left, mlir::Value right) {
        auto first = left.getDefiningOp<ctjs::CompareOp>();
        auto second = right.getDefiningOp<ctjs::CompareOp>();
        return first && second && first.getKind() == ctjs::CompareKind::StrictEq &&
               second.getKind() == ctjs::CompareKind::StrictEq &&
               ((sameOperand(first.getLhs(), second.getLhs()) &&
                 sameOperand(first.getRhs(), second.getRhs())) ||
                (sameOperand(first.getLhs(), second.getRhs()) &&
                 sameOperand(first.getRhs(), second.getLhs())));
    };
    const auto booleanInput = [&](mlir::Value operand) -> std::pair<mlir::Value, bool> {
        auto [value, negated] = stripBoolean(operand);
        if (!value) { return {}; }
        const bool inputNegated = negated;
        auto compare = value.getDefiningOp<ctjs::CompareOp>();
        if (compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
            // Strict equality is total and noncoercing, including opaque objects.
            // Prefixes before enclosing Ifs dominate this producer. Choose the
            // outermost first match, so local duplicates share its replay snapshot.
            // An earlier If result can also represent matching comparisons in
            // both arms. Its completed result owns the choice, not a sibling op.
            // Never cross a loop or borrow a following producer's choice.
            // ponytail: linear prefix scans; cache keys only if profiling warrants it.
            mlir::Operation * boundary = compare;
            while (true) {
                for (mlir::Operation & operation : *boundary->getBlock()) {
                    if (!spend(1)) { return {}; }
                    if (&operation == boundary) { break; }
                    auto previous = llvm::dyn_cast<ctjs::CompareOp>(operation);
                    if (previous && sameComparison(previous.getResult(), compare.getResult())) {
                        value = previous.getResult();
                        negated = inputNegated;
                        break;
                    }
                    auto selected = llvm::dyn_cast<mlir::scf::IfOp>(operation);
                    if (!selected) { continue; }
                    for (mlir::OpResult result : selected.getResults()) {
                        std::optional<bool> polarity;
                        for (mlir::Region & region : selected->getRegions()) {
                            if (!spend(1)) { return {}; }
                            if (!llvm::hasSingleElement(region) || region.front().empty() ||
                                region.front().getNumArguments() != 0) {
                                polarity.reset();
                                break;
                            }
                            auto yield =
                                llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                            if (!yield || yield.getNumOperands() != selected.getNumResults()) {
                                polarity.reset();
                                break;
                            }
                            const auto incoming =
                                stripBoolean(yield.getOperand(result.getResultNumber()));
                            if (!incoming.first) { return {}; }
                            if (!sameComparison(incoming.first, compare.getResult()) ||
                                (polarity && *polarity != incoming.second)) {
                                polarity.reset();
                                break;
                            }
                            polarity = incoming.second;
                        }
                        if (polarity) {
                            value = result;
                            negated = inputNegated != *polarity;
                            break;
                        }
                    }
                    if (value.getDefiningOp() == selected) { break; }
                }
                auto enclosing = llvm::dyn_cast<mlir::scf::IfOp>(boundary->getParentOp());
                if (!enclosing) { break; }
                boundary = enclosing;
            }
        }
        return {value, negated};
    };
    mlir::Value input = truthy.getValue();
    bool negated = false;
    while (true) {
        const auto [value, reversed] = booleanInput(input);
        if (!value) { return {}; }
        input = value;
        negated ^= reversed;
        if (auto branch = input.getDefiningOp<mlir::scf::IfOp>()) {
            // Both incoming chains must retain one SSA representative and polarity.
            // This selects no arm: census and replay still check every effect.
            const auto position = llvm::cast<mlir::OpResult>(input).getResultNumber();
            std::pair<mlir::Value, bool> common;
            for (mlir::Region & region : branch->getRegions()) {
                if (!spend(1)) { return {}; }
                if (!llvm::hasSingleElement(region) || region.front().empty() ||
                    region.front().getNumArguments() != 0) {
                    common = {};
                    break;
                }
                auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                if (!yield || yield.getNumOperands() != branch.getNumResults()) {
                    common = {};
                    break;
                }
                const auto incoming = booleanInput(yield.getOperand(position));
                if (!incoming.first) { return {}; }
                if (common.first && common != incoming) {
                    auto first = common.first.getDefiningOp<ctjs::ConstantOp>();
                    auto second = incoming.first.getDefiningOp<ctjs::ConstantOp>();
                    auto thenValue = first ? llvm::dyn_cast<ctjs::BooleanAttr>(first.getValue())
                                           : ctjs::BooleanAttr{};
                    auto elseValue = second ? llvm::dyn_cast<ctjs::BooleanAttr>(second.getValue())
                                            : ctjs::BooleanAttr{};
                    // Complementary Boolean yields encode the condition itself.
                    // Keep exact Boolean literals; numeric truthiness proves nothing here.
                    if (thenValue && elseValue &&
                        (thenValue.getValue() != common.second) !=
                            (elseValue.getValue() != incoming.second)) {
                        auto guard = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
                        common = {guard ? mlir::Value(guard.getValue())
                                        : mlir::Value(branch.getCondition()),
                                  thenValue.getValue() == common.second};
                    } else if (common.second == incoming.second &&
                               sameComparison(common.first, incoming.first)) {
                        // Both arms compute this same Boolean. Retain the If
                        // result as a dominating snapshot for later comparisons.
                        common = {input, false};
                    } else {
                        common = {};
                    }
                    break;
                }
                common = incoming;
            }
            if (common.first && common.first != input) {
                input = common.first;
                negated ^= common.second;
                continue;
            }
        }
        break;
    }
    if (!complete) { return {}; }
    return {input, negated};
}

using HeldProperties = llvm::MapVector<mlir::StringAttr, ContentsValue>;
struct CountedLoop {
    mlir::Block * header;
    mlir::Block * body;
    mlir::Value index;
    mlir::Value array;
    mlir::Operation * site;
    std::size_t length;
    std::size_t finalIndex;
};
struct State {
    // Imported successors forward every raw register, including unused
    // receiver/parameter values. Their Opaque kind keeps entry identity:
    // forwarding or testing one never proves its contents or retention.
    llvm::DenseMap<mlir::Value, ContentsValue> values;
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<ContentsValue, 4>> arrays;
    llvm::MapVector<mlir::Operation *, HeldProperties> objects;
    // First incoming edge on this exact path; ordinary CFG revisits refuse.
    llvm::DenseMap<mlir::Block *, mlir::Block *> predecessors;
    ctjs::FrameEnterOp frame;
    bool frameExited = false;
    mlir::Operation * current = nullptr;
    std::optional<CountedLoop> loop;
};

std::optional<std::size_t> boundedNumber(mlir::Value value, bool negate = false);
std::optional<std::size_t> ownArrayIndex(mlir::Value value);
mlir::StringAttr ownObjectKey(mlir::StringAttr key);
mlir::StringAttr ownObjectKey(mlir::Value value);
std::optional<std::size_t> ownArrayIndex(const ContentsValue & key);
// Independent binary64 snapshots used only by certified loop keys and replay.
// This grants no general Number, array-length or String conversion authority.
std::optional<std::size_t> arithmeticArrayIndex(const ContentsValue & key);
std::optional<ContentsValue> boundedStringRead(const ContentsValue & base,
                                               const ContentsValue & key, mlir::Value result);
std::optional<std::size_t> boundedConvertedNumber(const ContentsValue & input, bool negate = false);
std::optional<std::uint32_t> boundedConvertedBits(const ContentsValue & input);
void boundedNumberUnary(const ContentsValue & input, bool negate, ContentsValue & result);
void boundedNumberComplement(const ContentsValue & input, ContentsValue & result);
void boundedNumberBitwise(const ContentsValue & left, const ContentsValue & right,
                          ctjs::BinaryKind kind, ContentsValue & result);
void boundedNumberProduct(const ContentsValue & left, const ContentsValue & right,
                          ContentsValue & result);
void boundedNumberDivision(const ContentsValue & left, const ContentsValue & right, bool remainder,
                           ContentsValue & result);
void boundedNumberPower(const ContentsValue & left, const ContentsValue & right,
                        ContentsValue & result);
void boundedNumberSum(const ContentsValue & left, const ContentsValue & right,
                      ContentsValue & result);
void boundedNumberDifference(const ContentsValue & left, const ContentsValue & right,
                             ContentsValue & result);

} // namespace ctcompile::ctnative::escape_detail
