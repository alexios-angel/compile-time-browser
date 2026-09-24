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
    // Independent binary64 Add/Sub/Mul/Div result, never reconstructed from converted bits.
    std::optional<double> arithmeticNumber = std::nullopt;
    unsigned arithmeticDepth = 0;

    mlir::Value origin() const { return kind == ContentsKind::Opaque ? mlir::Value{} : original; }
    bool nonBigInt() const {
        return kind == ContentsKind::NonBigInt || kind == ContentsKind::String;
    }
    bool bigInt() const { return kind == ContentsKind::BigInt; }
    bool string() const { return kind == ContentsKind::String; }
};

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
    llvm::SmallPtrSet<mlir::Block *, 8> visited;
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
std::optional<ContentsValue> boundedStringRead(const ContentsValue & base,
                                               const ContentsValue & key, mlir::Value result);
std::optional<std::size_t> boundedConvertedNumber(const ContentsValue & input, bool negate = false);
std::optional<std::uint32_t> boundedConvertedBits(const ContentsValue & input);
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
