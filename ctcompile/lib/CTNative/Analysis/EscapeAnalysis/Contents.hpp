//===- Contents.hpp - Shared state for the contents query -------*- C++ -*-===//
//
// Private declarations shared by ArrayContents.cpp, LoopProof.cpp and
// Primitives.cpp, the three parts of computeArrayContents.
//
// Terms used in their comments:
// - replay: computeArrayContents' path-by-path execution of the function,
//   which tracks exact container contents in a State.
// - census: LoopProof's scan of every operation in a counted loop's header and
//   body, admitting only pure operations and in-bounds stores.
// - origin: the SSA value that produced a held value; public evidence records
//   name origins, never the scalar facts below.
// - bounded: an exact integer whose magnitude is at most 2^32 - 1 (4294967295),
//   the largest JS array length; valid indices are strictly below it.
//
//===----------------------------------------------------------------------===//

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

/// What replay knows about a held value's type.
enum class ContentsKind {
    /// A known value (an allocation, a frame or a predicate); no primitive
    /// category is recorded.
    Identity,
    /// An entry argument whose contents are unknown; it has no usable origin.
    Opaque,
    /// A primitive that is not a BigInt (a String is also NonBigInt).
    NonBigInt,
    BigInt,
    String
};

/// A value as replay holds it. Facts belong to the held value, not to its SSA
/// producer: copies into successors and containers keep the snapshot taken when
/// it was read. `original` is the identity used by public evidence records.
struct ContentsValue {
    mlir::Value original;
    ContentsKind kind = ContentsKind::Identity;
    /// An exact nonnegative bounded Number, kept across block arguments.
    std::optional<std::size_t> integerNumber = std::nullopt;
    /// The magnitude of an exact negative bounded Number. Never an own index.
    std::optional<std::size_t> negativeIntegerNumber = std::nullopt;
    /// The character an indexed read of an ASCII String produced.
    std::optional<unsigned char> asciiCharacter = std::nullopt;
    /// ToUint32 of the value; never used as an arithmetic value or property key.
    std::optional<std::uint32_t> convertedBits = std::nullopt;
    /// Length of the unary chain `convertedBits` was carried through (capped).
    unsigned unaryDepth = 0;
    /// The exact binary64 result of arithmetic, never rebuilt from convertedBits.
    std::optional<double> arithmeticNumber = std::nullopt;
    /// Number of arithmetic operations behind `arithmeticNumber` (capped at 64).
    unsigned arithmeticDepth = 0;
    /// The branch arm already chosen for this condition value inside a certified
    /// loop. Reassigning the value clears it; recomputing Truthy or Not does not.
    std::optional<bool> conditionChoice = std::nullopt;

    mlir::Value origin() const { return kind == ContentsKind::Opaque ? mlir::Value{} : original; }
    bool nonBigInt() const {
        return kind == ContentsKind::NonBigInt || kind == ContentsKind::String;
    }
    bool bigInt() const { return kind == ContentsKind::BigInt; }
    bool string() const { return kind == ContentsKind::String; }
    /// Whether an exact bounded Number of either sign is recorded.
    bool exactNumber() const { return integerNumber || negativeIntegerNumber; }
    /// The exact bounded Number as one signed value.
    std::optional<std::int64_t> signedNumber() const {
        if (integerNumber) { return static_cast<std::int64_t>(*integerNumber); }
        if (negativeIntegerNumber) { return -static_cast<std::int64_t>(*negativeIntegerNumber); }
        return std::nullopt;
    }
    /// Records `value` in the magnitude its sign selects; a value whose
    /// magnitude exceeds 2^32 - 1 records nothing.
    void setSigned(std::int64_t value) {
        if (value < -4294967295LL || value > 4294967295LL) { return; }
        if (value < 0) {
            negativeIntegerNumber = static_cast<std::size_t>(-value);
        } else {
            integerNumber = static_cast<std::size_t>(value);
        }
    }
};

/// The operands both arms of `result`'s scf.if yield for it, then-arm first.
inline std::array<mlir::Value, 2> armYields(mlir::OpResult result) {
    auto branch = llvm::cast<mlir::scf::IfOp>(result.getOwner());
    const auto position = result.getResultNumber();
    return {branch.thenYield().getOperand(position), branch.elseYield().getOperand(position)};
}

/// Visits the leaves `value` selects through nested scf.if results, charging one
/// step per value and per arm. Null when the budget runs out, otherwise whether
/// every visited leaf was accepted.
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

/// The value a Truthy branch condition selects on and whether the branch
/// reverses its truthiness, charging `spend`; null when the budget runs out.
std::pair<mlir::Value, bool> conditionChoiceKey(mlir::Value condition,
                                                llvm::function_ref<bool(std::size_t)> spend);

using HeldProperties = llvm::MapVector<mlir::StringAttr, ContentsValue>;
/// A loop LoopProof certified: `index` steps through `array` (allocated by
/// `site`, `length` elements) and leaves the loop at `finalIndex`.
struct CountedLoop {
    mlir::Block * header;
    mlir::Block * body;
    mlir::Value index;
    mlir::Value array;
    mlir::Operation * site;
    std::size_t length;
    std::size_t finalIndex;
};
/// One path's replay state: held values, container contents, the incoming
/// edge of each visited block, the frame, the next operation and any
/// certified loop being stepped.
struct State {
    // Imported successors forward every raw register, including unused
    // receiver/parameter values. Their Opaque kind keeps entry identity:
    // forwarding or testing one never proves its contents or retention.
    llvm::DenseMap<mlir::Value, ContentsValue> values;
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<ContentsValue, 4>> arrays;
    llvm::MapVector<mlir::Operation *, HeldProperties> objects;
    /// First incoming edge on this exact path; ordinary CFG revisits refuse.
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
/// An own index from an exact binary64 arithmetic result. Only keys inside a
/// certified loop use this; it does not make the value a Number fact elsewhere.
std::optional<std::size_t> arithmeticArrayIndex(const ContentsValue & key);
std::optional<ContentsValue> boundedStringRead(const ContentsValue & base,
                                               const ContentsValue & key, mlir::Value result);
std::optional<std::size_t> boundedConvertedNumber(const ContentsValue & input, bool negate = false);
/// boundedConvertedNumber as one signed value, the nonnegative magnitude first.
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
/// The transfer above for `kind`; Concat proves nothing.
void boundedNumberBinary(ctjs::BinaryKind kind, const ContentsValue & left,
                         const ContentsValue & right, ContentsValue & result);

} // namespace ctcompile::ctnative::escape_detail
