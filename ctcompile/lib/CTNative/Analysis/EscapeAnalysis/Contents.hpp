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
#include <array>
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
    // Whether an exact bounded Number of either sign is recorded.
    bool exactNumber() const { return integerNumber || negativeIntegerNumber; }
    // The exact bounded Number as one signed value.
    std::optional<std::int64_t> signedNumber() const {
        if (integerNumber) { return static_cast<std::int64_t>(*integerNumber); }
        if (negativeIntegerNumber) { return -static_cast<std::int64_t>(*negativeIntegerNumber); }
        return std::nullopt;
    }
    // Records `value` in the magnitude its sign selects; one beyond 2^32-1 in
    // magnitude records nothing.
    void setSigned(std::int64_t value) {
        if (value < -4294967295LL || value > 4294967295LL) { return; }
        if (value < 0) {
            negativeIntegerNumber = static_cast<std::size_t>(-value);
        } else {
            integerNumber = static_cast<std::size_t>(value);
        }
    }
};

// The operands both arms of `result`'s scf.if yield for it, then-arm first.
inline std::array<mlir::Value, 2> armYields(mlir::OpResult result) {
    auto branch = llvm::cast<mlir::scf::IfOp>(result.getOwner());
    const auto position = result.getResultNumber();
    return {branch.thenYield().getOperand(position), branch.elseYield().getOperand(position)};
}

// Visits the leaves `value` selects through nested scf.if results, charging one
// step per value and per arm. Null when the budget runs out, otherwise whether
// every visited leaf was accepted.
inline std::optional<bool> ifLeaves(mlir::Value value, llvm::function_ref<bool(std::size_t)> spend,
                                    llvm::function_ref<bool(mlir::Value)> visit) {
    llvm::SmallVector<mlir::Value, 4> pending{value};
    while (!pending.empty()) {
        if (!spend(1)) { return std::nullopt; }
        const mlir::Value current = pending.pop_back_val();
        if (!current.getDefiningOp<mlir::scf::IfOp>()) {
            if (!visit(current)) { return false; }
            continue;
        }
        for (mlir::Value arm : armYields(llvm::cast<mlir::OpResult>(current))) {
            if (!spend(1)) { return std::nullopt; }
            pending.push_back(arm);
        }
    }
    return true;
}

// The value a Truthy branch condition selects on and whether the branch
// reverses its truthiness, charging `spend`; null when the budget runs out.
std::pair<mlir::Value, bool> conditionChoiceKey(mlir::Value condition,
                                                llvm::function_ref<bool(std::size_t)> spend);

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
// boundedConvertedNumber as one signed value, the nonnegative magnitude first.
std::optional<std::int64_t> signedConvertedNumber(const ContentsValue & input);
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
// The transfer above for `kind`; Concat proves nothing.
void boundedNumberBinary(ctjs::BinaryKind kind, const ContentsValue & left,
                         const ContentsValue & right, ContentsValue & result);

} // namespace ctcompile::ctnative::escape_detail
