#include "LoopProof.hpp"

namespace ctcompile::ctnative::escape_detail {

ArrayContentsFailure LoopProof::countedLoop(mlir::Block * header, mlir::Block * body,
                                            mlir::ValueRange initial, mlir::Value condition,
                                            mlir::ValueRange intoBody, mlir::ValueRange backedge) {
    constexpr auto unsupported = ArrayContentsFailure::UnsupportedControlFlow;
    if (state.loop || body == header || initial.size() != header->getNumArguments() ||
        intoBody.size() != body->getNumArguments() ||
        backedge.size() != header->getNumArguments()) {
        return unsupported;
    }
    auto truthy = condition.getDefiningOp<ctjs::TruthyOp>();
    auto negation = truthy ? truthy.getValue().getDefiningOp<ctjs::UnaryOp>() : ctjs::UnaryOp{};
    auto compare = truthy ? (negation ? negation.getOperand() : truthy.getValue())
                                .getDefiningOp<ctjs::CompareOp>()
                          : ctjs::CompareOp{};
    const auto forwardKind = negation ? ctjs::CompareKind::Ge : ctjs::CompareKind::Lt;
    const auto reversedKind = negation ? ctjs::CompareKind::Le : ctjs::CompareKind::Gt;
    if (!compare || (compare.getKind() != forwardKind && compare.getKind() != reversedKind) ||
        (negation &&
         (negation.getKind() != ctjs::UnaryKind::Not || negation->getBlock() != header)) ||
        truthy->getBlock() != header || compare->getBlock() != header) {
        return unsupported;
    }
    // Normalize only the proof operands. The original comparison and its
    // evaluation order stay intact. Negated inclusive comparisons require
    // both operands independently proved bounded Numbers below: NaN would
    // invalidate !(index >= length) == (index < length).
    const bool reversed = compare.getKind() == reversedKind;
    auto index =
        llvm::dyn_cast<mlir::BlockArgument>(reversed ? compare.getRhs() : compare.getLhs());
    auto length =
        (reversed ? compare.getLhs() : compare.getRhs()).getDefiningOp<ctjs::GetPropertyOp>();
    const mlir::Value array = length ? length.getObject() : mlir::Value{};
    auto carriedArray = llvm::dyn_cast_if_present<mlir::BlockArgument>(array);
    auto directArray = array ? array.getDefiningOp<ctjs::CreateArrayOp>() : ctjs::CreateArrayOp{};
    if (!index || index.getOwner() != header ||
        (!(carriedArray && carriedArray.getOwner() == header) && !directArray) ||
        length->getBlock() != header ||
        ownObjectKey(length->getOperand(1)) !=
            mlir::StringAttr::get(function.getContext(), "length")) {
        return unsupported;
    }
    // Read actual/formal transport, not register numbers or source names.
    const auto fromHeader = [&](mlir::Value value) -> mlir::Value {
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
        if (!argument || argument.getOwner() != body) { return {}; }
        return intoBody[argument.getArgNumber()];
    };
    const auto unchanged = [&](mlir::Value value) {
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
        if (!argument || argument.getOwner() != header) { return true; }
        const mlir::Value next = backedge[argument.getArgNumber()];
        return next == value || fromHeader(next) == value;
    };
    const mlir::Value base = origin(array);
    const auto * guardSite = base ? base.getDefiningOp() : nullptr;
    auto invariantFailure = unsupported;
    llvm::SmallDenseSet<std::size_t, 4> guardReloads;
    const auto invariant = [&](auto && self, mlir::Value operand,
                               unsigned depth) -> std::optional<ContentsValue> {
        if (!spend()) {
            invariantFailure = ArrayContentsFailure::WorkLimit;
            return std::nullopt;
        }
        if (mlir::Value forwarded = fromHeader(operand)) { operand = forwarded; }
        if (!unchanged(operand)) { return std::nullopt; }
        auto literal = operand.getDefiningOp<ctjs::ConstantOp>();
        if (literal) {
            return ContentsValue{operand, llvm::isa<ctjs::StringAttr>(literal.getValue())
                                              ? ContentsKind::String
                                              : ContentsKind::Identity};
        }
        auto * definition = operand.getDefiningOp();
        if (!definition || (definition->getBlock() != header && definition->getBlock() != body)) {
            return held(operand);
        }
        // Every visited value spends the shared proof budget. Keep a stack
        // ceiling; repeated reads need unchanged base and key identities.
        if (depth == 64) { return std::nullopt; }
        ContentsValue result{operand, ContentsKind::NonBigInt};
        if (auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(definition)) {
            const auto base = self(self, load.getObject(), depth + 1);
            const auto key = self(self, load->getOperand(1), depth + 1);
            if (!base || !key || !base->origin() || !key->origin()) { return std::nullopt; }
            if (const auto string = boundedStringRead(*base, *key, operand)) { return string; }
            const auto array = state.arrays.find(base->origin().getDefiningOp());
            if (array == state.arrays.end()) { return std::nullopt; }
            // The complete loop census below rejects effects and any writes
            // when a proof operand depends on an element that could change.
            // Replay still checks every own read and snapshots its value.
            const auto name = ownObjectKey(key->origin());
            if (name && name.getValue() == "length") {
                if (array->second.size() > 4294967295ULL) { return std::nullopt; }
                result.integerNumber = array->second.size();
                return result;
            }
            const auto position = ownArrayIndex(*key);
            if (!position || *position >= array->second.size()) { return std::nullopt; }
            // Check every recursive read against the exact allocation, so
            // a distinct outer array cannot hide a selected guard alias.
            if (array->first == guardSite) { guardReloads.insert(*position); }
            // Retaining the original element keeps primitive keys intact.
            return array->second[*position];
        } else if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(definition)) {
            if (unary.getKind() != ctjs::UnaryKind::Plus &&
                unary.getKind() != ctjs::UnaryKind::Neg &&
                unary.getKind() != ctjs::UnaryKind::BitNot) {
                return std::nullopt;
            }
            const auto input = self(self, unary.getOperand(), depth + 1);
            if (!input) { return std::nullopt; }
            if (unary.getKind() == ctjs::UnaryKind::BitNot) {
                boundedNumberComplement(*input, result);
            } else {
                result.integerNumber = boundedConvertedNumber(*input);
                if (!result.integerNumber) {
                    result.negativeIntegerNumber = boundedConvertedNumber(*input, true);
                }
                if (unary.getKind() == ctjs::UnaryKind::Neg && result.integerNumber != 0) {
                    std::swap(result.integerNumber, result.negativeIntegerNumber);
                }
            }
        } else if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(definition);
                   binary && (binary.getKind() == ctjs::BinaryKind::Add ||
                              binary.getKind() == ctjs::BinaryKind::Sub ||
                              binary.getKind() == ctjs::BinaryKind::Mul ||
                              binary.getKind() == ctjs::BinaryKind::Div ||
                              binary.getKind() == ctjs::BinaryKind::Mod ||
                              binary.getKind() == ctjs::BinaryKind::Pow)) {
            const auto left = self(self, binary.getLhs(), depth + 1);
            const auto right = self(self, binary.getRhs(), depth + 1);
            if (!left || !right) { return std::nullopt; }
            if (binary.getKind() == ctjs::BinaryKind::Add) {
                boundedNumberSum(*left, *right, result);
            } else if (binary.getKind() == ctjs::BinaryKind::Sub) {
                boundedNumberDifference(*left, *right, result);
            } else if (binary.getKind() == ctjs::BinaryKind::Mul) {
                boundedNumberProduct(*left, *right, result);
            } else if (binary.getKind() == ctjs::BinaryKind::Pow) {
                boundedNumberPower(*left, *right, result);
            } else {
                boundedNumberDivision(*left, *right, binary.getKind() == ctjs::BinaryKind::Mod,
                                      result);
            }
        } else if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(definition)) {
            const auto left = self(self, binary.getLhs(), depth + 1);
            const auto right = self(self, binary.getRhs(), depth + 1);
            if (!left || !right) { return std::nullopt; }
            boundedNumberBitwise(*left, *right, binary.getKind(), result);
        } else {
            return std::nullopt;
        }
        return result;
    };
    auto * step = backedge[index.getArgNumber()].getDefiningOp();
    auto dynamic = llvm::dyn_cast_or_null<ctjs::BinaryOp>(step);
    auto numeric = llvm::dyn_cast_or_null<ctjs::BinaryStaticOp>(step);
    if ((!dynamic && !numeric) || step->getBlock() != body ||
        (carriedArray && fromHeader(backedge[carriedArray.getArgNumber()]) != array)) {
        return unsupported;
    }
    const bool subtract = dynamic && dynamic.getKind() == ctjs::BinaryKind::Sub;
    if (!subtract && (dynamic ? dynamic.getKind() : numeric.getKind()) != ctjs::BinaryKind::Add) {
        return unsupported;
    }
    // Normalize only Add proof operands; subtraction requires index - stride.
    // The original source order and exact Number requirements stay intact.
    const unsigned indexOperand = subtract || fromHeader(step->getOperand(0)) == index ? 0U : 1U;
    if (fromHeader(step->getOperand(indexOperand)) != index) { return unsupported; }
    // A held positive step must survive every backedge unchanged. Read a body
    // formal through its actual header operand before the body has executed.
    // Add excludes String concatenation; Sub converts a bounded negative primitive.
    const auto value = invariant(invariant, step->getOperand(1U - indexOperand), 0);
    if (!value) { return invariantFailure; }
    const auto stride =
        subtract || !value->string() ? boundedConvertedNumber(*value, subtract) : std::nullopt;
    if (!stride || *stride == 0) { return unsupported; }
    // Initialization may be a saved length or an exact arithmetic result.
    // The original input and its transported snapshot must independently
    // supply the same bounded Number on this exact path.
    std::optional<std::size_t> start;
    for (mlir::Value value : {initial[index.getArgNumber()], mlir::Value{index}}) {
        if (!spend()) { return ArrayContentsFailure::WorkLimit; }
        const ContentsValue input = held(value);
        const auto number =
            input.integerNumber ? input.integerNumber : boundedNumber(input.origin());
        if (!number || (start && start != number)) { return unsupported; }
        start = number;
    }
    auto found = state.arrays.find(base ? base.getDefiningOp() : nullptr);
    if (found == state.arrays.end() || found->second.size() > 4294967295ULL) { return unsupported; }
    const std::size_t size = found->second.size();
    const std::size_t last = *start < size ? size - 1 - (size - 1 - *start) % *stride : *start;
    // ponytail: one header/body pair, with writes only to its current,
    // invariant or composed affine own element. Other mutations need a
    // termination proof; each affine operation must have one varying operand.
    // Primitive kinds and every element still pass the ordinary operation transfers.
    llvm::SmallDenseSet<std::size_t, 4> guardStores;
    struct StoreRange {
        std::size_t first, last, stride;
    };
    llvm::SmallVector<StoreRange, 4> guardStoreRanges;
    struct IndexRange {
        ContentsValue first, last;
        std::size_t stride;
    };
    const auto signedBand = [](const ContentsValue & endpoint) {
        if (endpoint.integerNumber && *endpoint.integerNumber > 2147483647ULL) { return 1; }
        return endpoint.negativeIntegerNumber && *endpoint.negativeIntegerNumber > 2147483648ULL
                   ? -1
                   : 0;
    };
    const auto indexRange = [&](auto && self, mlir::Value operand,
                                unsigned depth) -> std::optional<IndexRange> {
        if (!spend()) {
            invariantFailure = ArrayContentsFailure::WorkLimit;
            return std::nullopt;
        }
        if (fromHeader(operand) == index) {
            return IndexRange{{index, ContentsKind::NonBigInt, *start},
                              {index, ContentsKind::NonBigInt, last},
                              *stride};
        }
        if (depth == 64) { return std::nullopt; }
        auto * expression = operand.getDefiningOp();
        if (auto unary = llvm::dyn_cast_or_null<ctjs::UnaryOp>(expression)) {
            if (unary->getBlock() != body || (unary.getKind() != ctjs::UnaryKind::Plus &&
                                              unary.getKind() != ctjs::UnaryKind::Neg &&
                                              unary.getKind() != ctjs::UnaryKind::BitNot)) {
                return std::nullopt;
            }
            auto range = self(self, unary.getOperand(), depth + 1);
            if (!range) { return std::nullopt; }
            if (unary.getKind() == ctjs::UnaryKind::BitNot) {
                // Magnitudes are already bounded by 2^32-1. Complement stays
                // affine within each ToInt32 band, but jumps at its boundaries.
                if (signedBand(range->first) != signedBand(range->last)) { return std::nullopt; }
            }
            // The recursive range already proves exact bounded Numbers. Negation
            // changes their sign and order; both signed zeros remain own key zero.
            for (ContentsValue * endpoint : {&range->first, &range->last}) {
                if (!spend()) {
                    invariantFailure = ArrayContentsFailure::WorkLimit;
                    return std::nullopt;
                }
                endpoint->original = operand;
                if (unary.getKind() == ctjs::UnaryKind::BitNot) {
                    ContentsValue result{operand, ContentsKind::NonBigInt};
                    boundedNumberComplement(*endpoint, result);
                    if (!result.integerNumber && !result.negativeIntegerNumber) {
                        return std::nullopt;
                    }
                    *endpoint = result;
                } else if (unary.getKind() == ctjs::UnaryKind::Neg &&
                           endpoint->integerNumber != 0) {
                    std::swap(endpoint->integerNumber, endpoint->negativeIntegerNumber);
                }
            }
            if (unary.getKind() != ctjs::UnaryKind::Plus) { std::swap(range->first, range->last); }
            return range;
        }
        auto addition = llvm::dyn_cast_or_null<ctjs::BinaryStaticOp>(expression);
        auto binary = llvm::dyn_cast_or_null<ctjs::BinaryOp>(expression);
        const bool subtract = binary && binary.getKind() == ctjs::BinaryKind::Sub;
        const bool divide = binary && binary.getKind() == ctjs::BinaryKind::Div;
        const bool multiply = binary && binary.getKind() == ctjs::BinaryKind::Mul;
        const bool leftShift = addition && addition.getKind() == ctjs::BinaryKind::Shl;
        const bool shift =
            leftShift || (addition && (addition.getKind() == ctjs::BinaryKind::Shr ||
                                       addition.getKind() == ctjs::BinaryKind::UShr));
        if (!expression || expression->getBlock() != body ||
            !(subtract || divide || multiply || shift ||
              (binary && binary.getKind() == ctjs::BinaryKind::Add) ||
              (addition && addition.getKind() == ctjs::BinaryKind::Add))) {
            return std::nullopt;
        }
        unsigned offsetOperand = 1;
        auto range = self(self, expression->getOperand(0), depth + 1);
        if (!range && !divide && !shift && invariantFailure != ArrayContentsFailure::WorkLimit) {
            range = self(self, expression->getOperand(1), depth + 1);
            offsetOperand = 0;
        }
        if (!range) { return std::nullopt; }
        const auto offset = invariant(invariant, expression->getOperand(offsetOperand), 0);
        // Only Number operands: String Add concatenates, and other primitive
        // conversions need their own source proof before composing an index.
        if (!offset ||
            (!offset->integerNumber && !offset->negativeIntegerNumber &&
             !boundedNumber(offset->origin()) && !boundedNumber(offset->origin(), true))) {
            return std::nullopt;
        }
        bool descending = subtract && offsetOperand == 0;
        if (shift) {
            if (!spend()) {
                invariantFailure = ArrayContentsFailure::WorkLimit;
                return std::nullopt;
            }
            const auto positive = boundedConvertedNumber(*offset);
            const auto negative = boundedConvertedNumber(*offset, true);
            const bool unsignedShift = addition.getKind() == ctjs::BinaryKind::UShr;
            const bool signedShift = addition.getKind() == ctjs::BinaryKind::Shr;
            // Exact negative magnitudes are bounded by 2^32-1. Within this
            // band ToUint32 adds 2^32; crossing zero would break its order.
            const bool negativeBand = unsignedShift && range->first.negativeIntegerNumber &&
                                      range->last.negativeIntegerNumber;
            if ((!positive && !negative) ||
                (signedShift ? signedBand(range->first) != signedBand(range->last)
                             : !negativeBand &&
                                   (!range->first.integerNumber || !range->last.integerNumber ||
                                    *range->last.integerNumber >
                                        (unsignedShift ? 4294967295ULL : 2147483647ULL)))) {
                return std::nullopt;
            }
            const auto count = positive ? static_cast<std::uint32_t>(*positive)
                                        : 0U - static_cast<std::uint32_t>(*negative);
            const auto factor = std::size_t{1} << (count & 31U);
            if (leftShift) {
                // ponytail: nonnegative signed-i32 results exclude every wrap.
                // Other conversion bands need their own affine range proof.
                if (*range->last.integerNumber > 2147483647ULL / factor ||
                    range->stride > 4294967295ULL / factor) {
                    return std::nullopt;
                }
                range->stride *= factor;
            } else {
                // ponytail: one conversion band and a divisible stride keep
                // floor division affine. Crossing conversion bands and repeated keys
                // need a separate proof; an unaligned first endpoint is safe.
                if (range->stride % factor != 0) { return std::nullopt; }
                range->stride /= factor;
            }
        } else if (divide) {
            if (!spend()) {
                invariantFailure = ArrayContentsFailure::WorkLimit;
                return std::nullopt;
            }
            auto divisor = boundedConvertedNumber(*offset);
            descending = !divisor.has_value();
            if (!divisor) { divisor = boundedConvertedNumber(*offset, true); }
            const auto first = range->first.integerNumber ? range->first.integerNumber
                                                          : range->first.negativeIntegerNumber;
            // Divide the positive stride magnitude; divisor sign only reverses
            // endpoint order. Integral endpoints can miss fractional positions.
            if (!divisor || *divisor == 0 || *first % *divisor != 0 ||
                range->stride % *divisor != 0) {
                return std::nullopt;
            }
            range->stride /= *divisor;
        } else if (multiply) {
            if (!spend()) {
                invariantFailure = ArrayContentsFailure::WorkLimit;
                return std::nullopt;
            }
            ContentsValue product;
            boundedNumberProduct({index, ContentsKind::NonBigInt, range->stride}, *offset, product);
            const auto magnitude =
                product.integerNumber ? product.integerNumber : product.negativeIntegerNumber;
            if (!magnitude || *magnitude == 0) { return std::nullopt; }
            range->stride = *magnitude;
            descending = product.negativeIntegerNumber.has_value();
        }
        // Preserve each source operation: reassociating (i + large) - large
        // could hide an inexact intermediate. Signed bounded intermediates are
        // allowed; only the final key must be a nonnegative own element.
        for (ContentsValue * endpoint : {&range->first, &range->last}) {
            if (!spend()) {
                invariantFailure = ArrayContentsFailure::WorkLimit;
                return std::nullopt;
            }
            ContentsValue result{operand, ContentsKind::NonBigInt};
            if (shift) {
                boundedNumberBitwise(*endpoint, *offset, addition.getKind(), result);
            } else if (divide) {
                boundedNumberDivision(*endpoint, *offset, false, result);
            } else if (multiply) {
                boundedNumberProduct(*endpoint, *offset, result);
            } else if (subtract) {
                if (offsetOperand == 0) {
                    boundedNumberDifference(*offset, *endpoint, result);
                } else {
                    boundedNumberDifference(*endpoint, *offset, result);
                }
            } else {
                boundedNumberSum(*endpoint, *offset, result);
            }
            if (!result.integerNumber && !result.negativeIntegerNumber) { return std::nullopt; }
            *endpoint = result;
        }
        // Keep the set of visited positions ordered even when the source
        // visits them backwards. The stride keeps its positive magnitude.
        if (descending) { std::swap(range->first, range->last); }
        return range;
    };
    for (mlir::Block * block : {header, body}) {
        for (mlir::Operation & operation : *block) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            if (&operation == block->getTerminator()) { continue; }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                if (block != body) { return unsupported; }
                if (const auto range = indexRange(indexRange, store.getKey(), 0)) {
                    if (*start < size) {
                        if (!range->first.integerNumber || !range->last.integerNumber ||
                            *range->first.integerNumber >= size ||
                            *range->last.integerNumber >= size) {
                            return unsupported;
                        }
                        guardStoreRanges.push_back({*range->first.integerNumber,
                                                    *range->last.integerNumber, range->stride});
                    }
                } else {
                    if (invariantFailure == ArrayContentsFailure::WorkLimit) {
                        return invariantFailure;
                    }
                    const auto key = invariant(invariant, store.getKey(), 0);
                    if (!key) { return invariantFailure; }
                    const auto position = ownArrayIndex(*key);
                    if (!position || *position >= size) { return unsupported; }
                    guardStores.insert(*position);
                }
                // A saved or reloaded receiver must keep the same allocation
                // across transport, without reading an overwritten element.
                const auto receiver = invariant(invariant, store.getObject(), 0);
                if (!receiver) { return invariantFailure; }
                if (receiver->origin() != base) { return unsupported; }
                continue;
            }
            if (!llvm::isa<ctjs::ConstantOp, ctjs::GetPropertyOp, ctjs::CompareOp, ctjs::TruthyOp,
                           ctjs::UnaryOp, ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::ConvertOp,
                           ctjs::RootOp>(&operation)) {
                return unsupported;
            }
        }
    }
    // Check after every key and receiver was resolved: a later store can
    // reveal a reload overlapping an earlier write. Each range carries the
    // actual stride between its proved first/last own positions.
    for (const auto position : guardReloads) {
        if (!spend()) { return ArrayContentsFailure::WorkLimit; }
        if (guardStores.contains(position)) { return unsupported; }
        for (const auto & [first, last, writeStride] : guardStoreRanges) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            if (position >= first && position <= last && (position - first) % writeStride == 0) {
                return unsupported;
            }
        }
    }
    // SCF can eliminate an invariant array parameter. A direct allocation
    // already executed on this exact path needs no backedge transport;
    // the body census above still excludes repeated allocation and resizing.
    if (directArray &&
        (base != array || directArray->getBlock() == header || directArray->getBlock() == body)) {
        return unsupported;
    }
    if (!spend()) { return ArrayContentsFailure::WorkLimit; }
    // A zero-trip loop preserves its original index. Otherwise find the last
    // visited index relative to the start, and bound its final update before
    // addition; overshooting length must stay in the exact Number range.
    std::size_t finalIndex = *start;
    if (*start < size) {
        if (*stride > 4294967295ULL - last) { return unsupported; }
        finalIndex = last + *stride;
    }
    state.loop = CountedLoop{header, body, index, array, found->first, size, finalIndex};
    return ArrayContentsFailure::None;
}
ArrayContentsFailure LoopProof::cfgCountedLoop(mlir::cf::CondBranchOp branch,
                                               mlir::cf::BranchOp latch) {
    constexpr auto unsupported = ArrayContentsFailure::UnsupportedControlFlow;
    mlir::Block * header = branch->getBlock();
    mlir::Block * body = branch.getTrueDest();
    if (branch.getFalseDest() == header || branch.getFalseDest() == body ||
        body->getParent() != &function.getBody()) {
        return unsupported;
    }
    mlir::Block * entry = nullptr;
    unsigned predecessors = 0;
    for (mlir::Block * predecessor : header->getPredecessors()) {
        if (!spend()) { return ArrayContentsFailure::WorkLimit; }
        ++predecessors;
        if (predecessor != body) { entry = predecessor; }
    }
    if (predecessors != 2 || entry == nullptr) { return unsupported; }
    predecessors = 0;
    for (mlir::Block * predecessor : body->getPredecessors()) {
        if (!spend()) { return ArrayContentsFailure::WorkLimit; }
        if (predecessor != header) { return unsupported; }
        ++predecessors;
    }
    if (predecessors != 1) { return unsupported; }
    auto incoming = llvm::dyn_cast<mlir::cf::BranchOp>(entry->getTerminator());
    if (!incoming || incoming.getDest() != header) { return unsupported; }
    return countedLoop(header, body, incoming.getDestOperands(), branch.getCondition(),
                       branch.getTrueDestOperands(), latch.getDestOperands());
}
std::optional<bool> LoopProof::loopContinues() {
    const CountedLoop & loop = *state.loop;
    const ContentsValue index = held(loop.index);
    const auto number = index.integerNumber ? index.integerNumber : boundedNumber(index.origin());
    const mlir::Value base = origin(loop.array);
    if (!number || *number > loop.finalIndex || !base || base.getDefiningOp() != loop.site) {
        return std::nullopt;
    }
    return *number < loop.length;
}

} // namespace ctcompile::ctnative::escape_detail
