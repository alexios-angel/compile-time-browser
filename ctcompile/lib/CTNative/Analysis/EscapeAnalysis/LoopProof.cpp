#include "LoopProof.hpp"
#include <bit>
#include <numeric>

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
    // invariant or bounded own element. Other mutations need a
    // termination proof; correlated operands need independently bounded ranges.
    // Primitive kinds and every element still pass the ordinary operation transfers.
    llvm::SmallDenseSet<std::size_t, 4> guardStores;
    struct StoreRange {
        std::size_t first, last, stride;
        mlir::Value mixedShiftKey;
    };
    llvm::SmallVector<StoreRange, 4> guardStoreRanges;
    struct IndexRange {
        ContentsValue first, last;
        // A positive lattice enclosing all visits; replay proves actual writes.
        std::size_t stride;
        // Preserve eligibility for exact whole-key refinement through composition.
        bool mixedShift = false;
    };
    const auto endpointNumber = [](const ContentsValue & endpoint) {
        return endpoint.integerNumber ? static_cast<std::int64_t>(*endpoint.integerNumber)
                                      : -static_cast<std::int64_t>(*endpoint.negativeIntegerNumber);
    };
    const auto boundEndpointPair = [&](IndexRange & range) {
        if (endpointNumber(range.first) > endpointNumber(range.last)) {
            std::swap(range.first, range.last);
        }
        range.stride = static_cast<std::size_t>(
            std::max<std::int64_t>(1, endpointNumber(range.last) - endpointNumber(range.first)));
    };
    const auto signedBand = [](const ContentsValue & endpoint) {
        if (endpoint.integerNumber && *endpoint.integerNumber > 2147483647ULL) { return 1; }
        return endpoint.negativeIntegerNumber && *endpoint.negativeIntegerNumber > 2147483648ULL
                   ? -1
                   : 0;
    };
    const auto numberBits = [](const ContentsValue & endpoint) {
        return endpoint.integerNumber
                   ? static_cast<std::uint32_t>(*endpoint.integerNumber)
                   : 0U - static_cast<std::uint32_t>(*endpoint.negativeIntegerNumber);
    };
    const auto convertedLattice = [&](mlir::Value operand, std::size_t period, std::size_t residue,
                                      bool unsignedOutput = false) -> std::optional<IndexRange> {
        // Intersect the full converted output interval with a proved residue.
        // Conversion can introduce gaps even without earlier mixed rounding.
        // Keep the enclosing lattice eligible for bounded whole-key refinement.
        // ponytail: one enclosing lattice; unions if precision needs them.
        IndexRange range{
            {operand, ContentsKind::NonBigInt}, {operand, ContentsKind::NonBigInt}, period, true};
        const auto lower = unsignedOutput ? 0LL : -2147483648LL;
        const auto upper = unsignedOutput ? 4294967295LL : 2147483647LL;
        const auto first = lower + static_cast<std::int64_t>(
                                       (residue + static_cast<std::size_t>(-lower)) % period);
        const auto last = first + (upper - first) / static_cast<std::int64_t>(period) *
                                      static_cast<std::int64_t>(period);
        for (auto [endpoint, value] :
             {std::pair{&range.first, first}, std::pair{&range.last, last}}) {
            if (!spend()) {
                invariantFailure = ArrayContentsFailure::WorkLimit;
                return std::nullopt;
            }
            if (value < 0) {
                endpoint->negativeIntegerNumber = static_cast<std::size_t>(-value);
            } else {
                endpoint->integerNumber = static_cast<std::size_t>(value);
            }
        }
        return range;
    };
    std::pair<std::size_t, std::size_t> indexBounds{*start, last};
    bool refinableEnclosure = false;
    const auto indexRange = [&](auto && self, mlir::Value operand,
                                unsigned depth) -> std::optional<IndexRange> {
        if (!spend()) {
            invariantFailure = ArrayContentsFailure::WorkLimit;
            return std::nullopt;
        }
        if (fromHeader(operand) == index) {
            return IndexRange{{index, ContentsKind::NonBigInt, indexBounds.first},
                              {index, ContentsKind::NonBigInt, indexBounds.second},
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
            const bool wrappingComplement = unary.getKind() == ctjs::UnaryKind::BitNot &&
                                            signedBand(range->first) != signedBand(range->last);
            // Complement is affine within one ToInt32 band. Across bands,
            // endpoint images suffice only when the lattice has no interior point.
            if (wrappingComplement && endpointNumber(range->last) - endpointNumber(range->first) >
                                          static_cast<std::int64_t>(range->stride)) {
                // Complement negates each step modulo 2^32. Both the steps
                // and every conversion wrap preserve this output residue.
                const auto period = std::gcd(range->stride, std::size_t{4294967296ULL});
                return convertedLattice(operand, period, ~numberBits(range->first) % period);
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
            if (wrappingComplement) {
                boundEndpointPair(*range);
            } else if (unary.getKind() != ctjs::UnaryKind::Plus) {
                std::swap(range->first, range->last);
            }
            return range;
        }
        auto addition = llvm::dyn_cast_or_null<ctjs::BinaryStaticOp>(expression);
        auto binary = llvm::dyn_cast_or_null<ctjs::BinaryOp>(expression);
        const bool add = (binary && binary.getKind() == ctjs::BinaryKind::Add) ||
                         (addition && addition.getKind() == ctjs::BinaryKind::Add);
        const bool subtract = binary && binary.getKind() == ctjs::BinaryKind::Sub;
        const bool divide = binary && binary.getKind() == ctjs::BinaryKind::Div;
        const bool remainder = binary && binary.getKind() == ctjs::BinaryKind::Mod;
        const bool multiply = binary && binary.getKind() == ctjs::BinaryKind::Mul;
        const bool power = binary && binary.getKind() == ctjs::BinaryKind::Pow;
        const bool bitAnd = addition && addition.getKind() == ctjs::BinaryKind::BitAnd;
        const bool bitOr = addition && addition.getKind() == ctjs::BinaryKind::BitOr;
        const bool bitXor = addition && addition.getKind() == ctjs::BinaryKind::BitXor;
        const bool leftShift = addition && addition.getKind() == ctjs::BinaryKind::Shl;
        const bool shift =
            leftShift || (addition && (addition.getKind() == ctjs::BinaryKind::Shr ||
                                       addition.getKind() == ctjs::BinaryKind::UShr));
        if (!expression || expression->getBlock() != body ||
            !(add || subtract || divide || remainder || multiply || power || shift || bitAnd ||
              bitOr || bitXor)) {
            return std::nullopt;
        }
        unsigned offsetOperand = 1;
        auto range = self(self, expression->getOperand(0), depth + 1);
        if (!range && !divide && !remainder && !shift &&
            invariantFailure != ArrayContentsFailure::WorkLimit) {
            range = self(self, expression->getOperand(1), depth + 1);
            offsetOperand = 0;
        }
        if (!range) { return std::nullopt; }
        auto offset = invariant(invariant, expression->getOperand(offsetOperand), 0);
        // A varying offset, factor, divisor, shift count or mask may have one exact bounded value.
        // Reuse its range proof; every producer and reload remains in the census.
        if (!offset &&
            (add || subtract || multiply || divide || remainder || shift || bitAnd || bitOr ||
             bitXor) &&
            invariantFailure != ArrayContentsFailure::WorkLimit) {
            const auto other = self(self, expression->getOperand(offsetOperand), depth + 1);
            if (other && endpointNumber(other->first) == endpointNumber(other->last)) {
                offset = other->first;
            } else if (other && (bitAnd || bitOr || bitXor) &&
                       endpointNumber(range->first) == endpointNumber(range->last)) {
                offset = range->first;
                range = other;
                offsetOperand = 1U - offsetOperand;
            } else if (other) {
                // Both operands are bounded but correlated. Split the whole key
                // until the existing singleton operand transfer proves each visit.
                refinableEnclosure = true;
            }
        }
        const auto exponent = !offset && power && offsetOperand == 1 &&
                                      invariantFailure != ArrayContentsFailure::WorkLimit
                                  ? self(self, expression->getOperand(1), depth + 1)
                                  : std::nullopt;
        // A syntactically varying operand can still have one proved value.
        // Reuse the invariant power transfer; its producers and reloads remain
        // subject to the complete census and ordinary replay below.
        if (exponent && endpointNumber(exponent->first) == endpointNumber(exponent->last)) {
            offset = exponent->first;
        } else if (exponent && endpointNumber(range->first) == endpointNumber(range->last)) {
            offset = range->first;
            range = exponent;
            offsetOperand = 0;
        }
        // Each transfer proves bounded primitive conversion; boundedNumberSum
        // separately excludes String concatenation for Add.
        if (!offset && power && offsetOperand == 1 &&
            invariantFailure != ArrayContentsFailure::WorkLimit) {
            if (!exponent) { return std::nullopt; }
            // Correlated visits may use only scalar identities even when these
            // independent ranges include general powers or zero to a negative
            // exponent. Subdivision must prove every visit with the same transfer.
            refinableEnclosure = true;
            if (endpointNumber(range->first) < -1 || endpointNumber(range->last) > 1) {
                if (exponent->first.integerNumber != 0 || exponent->last.integerNumber != 1) {
                    return std::nullopt;
                }
                // Every bounded base to exponent zero or one is exactly one or
                // itself. Enclose both images; whole-key refinement below keeps
                // reloads in correlated gaps, and replay records actual writes.
                const ContentsValue unit{operand, ContentsKind::NonBigInt, 1};
                // The union keeps every congruence shared by the base and one,
                // including exact divisibility in later key operations.
                const auto period = std::gcd(static_cast<std::int64_t>(range->stride),
                                             endpointNumber(range->first) - 1);
                range->first.original = operand;
                range->last.original = operand;
                return IndexRange{endpointNumber(range->first) < 1 ? range->first : unit,
                                  endpointNumber(range->last) > 1 ? range->last : unit,
                                  static_cast<std::size_t>(period), true};
            }
            // A signed-unit lattice may skip zero. Negative exponents are exact
            // only when every possible base is a unit, including interior visits.
            const bool zeroBase = endpointNumber(range->first) <= 0 &&
                                  endpointNumber(range->last) >= 0 &&
                                  (range->first.integerNumber == 0 || range->stride == 1);
            if (!exponent->first.integerNumber && zeroBase) { return std::nullopt; }
            // Every combination is an exact scalar identity. An odd exponent
            // can retain a negative unit; zero to zero must still include one.
            const bool varyingParity =
                endpointNumber(exponent->first) != endpointNumber(exponent->last) &&
                exponent->stride % 2 != 0;
            const bool odd = endpointNumber(exponent->first) % 2 != 0 || varyingParity;
            const bool even = endpointNumber(exponent->first) % 2 == 0 || varyingParity;
            ContentsValue first{operand, ContentsKind::NonBigInt, zeroBase ? 0U : 1U};
            if (range->first.negativeIntegerNumber && odd) {
                first.integerNumber.reset();
                first.negativeIntegerNumber = 1;
            }
            // ponytail: independent ranges lose correlation; bounded whole-key
            // refinement checks reload gaps, while replay records actual writes.
            // Without a positive unit base or an even exponent (including zero),
            // the largest image is zero. Do not invent an out-of-bounds +1 key.
            return IndexRange{first,
                              {operand, ContentsKind::NonBigInt,
                               range->last.integerNumber == 1 || even ? 1U : 0U},
                              zeroBase ? 1U : 2U,
                              true};
        }
        if (!offset) { return std::nullopt; }
        // At most two varying values need only their exact scalar power proofs;
        // no interior value can introduce another extremum or unproved power.
        const bool twoPointPower =
            power && endpointNumber(range->last) - endpointNumber(range->first) <=
                         static_cast<std::int64_t>(range->stride);
        if (power && !twoPointPower) {
            if (!spend()) {
                invariantFailure = ArrayContentsFailure::WorkLimit;
                return std::nullopt;
            }
            if (offsetOperand == 0 && boundedConvertedNumber(*offset, true) == 1) {
                ContentsValue result{operand, ContentsKind::NonBigInt};
                boundedNumberPower(*offset, range->first, result);
                if (!result.integerNumber && !result.negativeIntegerNumber) { return std::nullopt; }
                // Even strides preserve parity, including negative exponents.
                // Odd strides may alternate despite equal endpoint powers;
                // enclose both signs and keep the unwritten zero gap.
                if (range->stride % 2 == 0 ||
                    endpointNumber(range->first) == endpointNumber(range->last)) {
                    return IndexRange{result, result, 1, range->mixedShift};
                }
                result.integerNumber.reset();
                result.negativeIntegerNumber = 1;
                return IndexRange{
                    result, {operand, ContentsKind::NonBigInt, 1}, 2, range->mixedShift};
            }
            const auto number = boundedConvertedNumber(*offset);
            if (offsetOperand == 1 && number && *number > 0 &&
                range->first.negativeIntegerNumber == 1 && range->last.integerNumber == 1) {
                // The only interior integer base is zero. Positive exponents
                // map it to zero; even powers otherwise have equal endpoints.
                ContentsValue first{operand, ContentsKind::NonBigInt};
                boundedNumberPower(range->first, *offset, first);
                if (!first.integerNumber && !first.negativeIntegerNumber) { return std::nullopt; }
                if (first.integerNumber) { first.integerNumber = 0; }
                return IndexRange{
                    first, {operand, ContentsKind::NonBigInt, 1}, 1, range->mixedShift};
            }
            // ponytail: only zero/unit exponents or bases. A zero base maps
            // exponent zero to one and positive exponents to zero; its full
            // exponent range must be nonnegative. General powers need an
            // enclosure for every interior visit.
            if (!number || *number > 1 ||
                (offsetOperand == 0 && *number == 0 && !range->first.integerNumber)) {
                // A mixed lattice may include unvisited bases or poles; only
                // subdivision through the same scalar identities can remove them.
                refinableEnclosure |= range->mixedShift;
                return std::nullopt;
            }
            if (offsetOperand == 0 || *number == 0) { range->stride = 1; }
        }
        if (remainder) {
            if (!spend()) {
                invariantFailure = ArrayContentsFailure::WorkLimit;
                return std::nullopt;
            }
            auto divisor = boundedConvertedNumber(*offset);
            if (!divisor) { divisor = boundedConvertedNumber(*offset, true); }
            if (!divisor || *divisor == 0) { return std::nullopt; }
            const auto quotient = [&](const ContentsValue & endpoint) {
                return endpoint.integerNumber
                           ? static_cast<std::int64_t>(*endpoint.integerNumber / *divisor)
                           : -static_cast<std::int64_t>(*endpoint.negativeIntegerNumber / *divisor);
            };
            // Between consecutive multiples, remainder is affine, including
            // the central band across zero. Preserve the enclosing lattice.
            if (quotient(range->first) == quotient(range->last)) {
                for (ContentsValue * endpoint : {&range->first, &range->last}) {
                    if (!spend()) {
                        invariantFailure = ArrayContentsFailure::WorkLimit;
                        return std::nullopt;
                    }
                    ContentsValue result{operand, ContentsKind::NonBigInt};
                    boundedNumberDivision(*endpoint, *offset, true, result);
                    if (!result.integerNumber && !result.negativeIntegerNumber) {
                        return std::nullopt;
                    }
                    *endpoint = result;
                }
                return range;
            }
            // Remainder takes the dividend's sign. Its separate magnitudes are
            // already bounded by 2^32-1, including across zero; no signed abs.
            const auto negative =
                std::min(range->first.negativeIntegerNumber.value_or(0), *divisor - 1);
            const auto positive = std::min(range->last.integerNumber.value_or(0), *divisor - 1);
            // Subtracting any multiple of the divisor preserves the dividend's
            // congruence modulo gcd(stride, divisor), including across zero.
            const auto period = static_cast<std::int64_t>(std::gcd(range->stride, *divisor));
            const auto phase =
                range->first.integerNumber
                    ? static_cast<std::int64_t>(*range->first.integerNumber)
                    : -static_cast<std::int64_t>(*range->first.negativeIntegerNumber);
            auto first = -static_cast<std::int64_t>(negative);
            auto last = static_cast<std::int64_t>(positive);
            first += ((phase - first) % period + period) % period;
            last -= ((last - phase) % period + period) % period;
            const auto endpoint = [&](std::int64_t value) {
                ContentsValue result{operand, ContentsKind::NonBigInt};
                if (value < 0) {
                    result.negativeIntegerNumber = static_cast<std::size_t>(-value);
                } else {
                    result.integerNumber = static_cast<std::size_t>(value);
                }
                return result;
            };
            // Wrapping can leave gaps beyond the gcd lattice. Reuse the
            // budgeted whole-key refinement before rejecting a reload there.
            return IndexRange{endpoint(first), endpoint(last), static_cast<std::size_t>(period),
                              true};
        }
        if (bitAnd || bitOr || bitXor) {
            if (!spend()) {
                invariantFailure = ArrayContentsFailure::WorkLimit;
                return std::nullopt;
            }
            const auto positive = boundedConvertedNumber(*offset);
            const auto negative = boundedConvertedNumber(*offset, true);
            if (!positive && !negative) { return std::nullopt; }
            const auto mask = positive ? static_cast<std::uint32_t>(*positive)
                                       : 0U - static_cast<std::uint32_t>(*negative);
            // With no interior lattice point, both endpoint images enclose
            // every visit even across a ToInt32 discontinuity. Magnitudes are
            // bounded by 2^32-1, so their signed difference fits int64_t.
            const bool twoPoints = endpointNumber(range->last) - endpointNumber(range->first) <=
                                   static_cast<std::int64_t>(range->stride);
            const auto inputStride = std::size_t{1} << std::countr_zero(range->stride);
            const auto fixed = static_cast<std::uint32_t>(inputStride - 1);
            const auto lower = numberBits(range->first);
            const auto upper = numberBits(range->last);
            // All bits above the highest differing bit are fixed throughout
            // this unsigned interval. The input lattice also fixes its low bits.
            auto varying = static_cast<std::uint32_t>(
                std::bit_ceil(static_cast<std::uint64_t>(lower ^ upper) + 1) - 1);
            const bool complement = bitXor && (~mask & varying & ~fixed) == 0;
            const bool affine = complement || ((bitAnd ? ~mask : mask) & varying & ~fixed) == 0;
            // Fixed high bits only translate the result; fixed low bits may
            // fill gaps in the suffix without changing any visited position.
            const auto roundedBits = ((bitAnd ? ~mask : mask) & varying) | fixed;
            const bool rounding =
                !bitXor && std::has_single_bit(static_cast<std::uint64_t>(roundedBits) + 1);
            if (twoPoints ||
                (signedBand(range->first) == signedBand(range->last) && (affine || rounding))) {
                // Changing only fixed input bits is a translation; flipping all
                // varying bits reverses it. Both preserve the full stride within
                // one ToInt32 band, including signed zero crossings.
                // Clearing/setting a low suffix instead rounds monotonically;
                // its endpoints stay exact, but only fixed low bits survive.
                if (!affine && !twoPoints) {
                    const auto roundedStride =
                        std::max(inputStride, static_cast<std::size_t>(roundedBits) + 1);
                    range->mixedShift |= range->stride % roundedStride != 0;
                    range->stride = roundedStride;
                }
                for (ContentsValue * endpoint : {&range->first, &range->last}) {
                    if (!spend()) {
                        invariantFailure = ArrayContentsFailure::WorkLimit;
                        return std::nullopt;
                    }
                    ContentsValue result{operand, ContentsKind::NonBigInt};
                    boundedNumberBitwise(*endpoint, *offset, addition.getKind(), result);
                    if (!result.integerNumber && !result.negativeIntegerNumber) {
                        return std::nullopt;
                    }
                    *endpoint = result;
                }
                if (twoPoints) {
                    boundEndpointPair(*range);
                } else if (complement) {
                    std::swap(range->first, range->last);
                }
                return range;
            }
            // Enclose the varying bits densely: endpoint bitwise results alone
            // miss interior extrema when the mask modifies a varying bit.
            if (signedBand(range->first) != signedBand(range->last) || varying > 2147483647U) {
                // Clearing the sign bit with AND or setting it with OR keeps
                // every output in one signed interval even if all input bits vary.
                // Other sign crossings preserve the transformed low input bits;
                // use the existing signed enclosure and refine gaps as needed.
                if (!((bitAnd && mask <= 2147483647U) || (bitOr && mask > 2147483647U))) {
                    const auto residue = (bitAnd  ? lower & mask
                                          : bitOr ? lower | mask
                                                  : lower ^ mask) %
                                         inputStride;
                    return convertedLattice(operand, inputStride, residue);
                }
                varying = 4294967295U;
            }
            // An input lattice fixes its low bits. Every bitwise operation
            // preserves their transformed residue, even when higher bits vary.
            varying &= ~fixed;
            // Input and mask bits can interleave: stride two and AND five
            // fix both low bits. Use the first bit that can actually vary.
            // Constant results need no width-sized shift.
            const auto outputVarying = bitAnd ? varying & mask : bitOr ? varying & ~mask : varying;
            const auto writeStride = outputVarying != 0
                                         ? std::size_t{1} << std::countr_zero(outputVarying)
                                         : inputStride;
            const auto first = bitAnd  ? (lower & ~varying) & mask
                               : bitOr ? (lower & ~varying) | mask
                                       : (lower ^ mask) & ~varying;
            const auto last = first | (bitAnd ? varying & mask : varying);
            // Clearing the sign bit bounds every ToInt32 input, including
            // conversions across a signed boundary for a low-bit AND mask.
            // AND preserves the mask's low zero bits, including signed results.
            // This enclosure can introduce gaps even without an earlier shift.
            // Keep bounded whole-key refinement available; replay records writes.
            IndexRange result{{operand, ContentsKind::NonBigInt},
                              {operand, ContentsKind::NonBigInt},
                              writeStride,
                              true};
            boundedNumberBitwise({operand, ContentsKind::NonBigInt, first},
                                 {operand, ContentsKind::NonBigInt, 0}, ctjs::BinaryKind::BitOr,
                                 result.first);
            boundedNumberBitwise({operand, ContentsKind::NonBigInt, last},
                                 {operand, ContentsKind::NonBigInt, 0}, ctjs::BinaryKind::BitOr,
                                 result.last);
            return result;
        }
        // Two input points need no monotone conversion or output band. Their
        // exact images also retain gaps lost by uneven right-shift rounding.
        const bool twoPoints = endpointNumber(range->last) - endpointNumber(range->first) <=
                               static_cast<std::int64_t>(range->stride);
        const bool twoPointShift = shift && twoPoints;
        const bool twoPointProduct = multiply && twoPoints;
        std::int64_t roundedIntervals = 0;
        bool descending = (subtract || power) && offsetOperand == 0;
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
            if (!positive && !negative) { return std::nullopt; }
            const bool conversionJump =
                ((signedShift || leftShift) &&
                 signedBand(range->first) != signedBand(range->last)) ||
                (unsignedShift && !negativeBand &&
                 (!range->first.integerNumber || !range->last.integerNumber ||
                  *range->last.integerNumber > 4294967295ULL));
            if (!twoPointShift && conversionJump && !leftShift) {
                // A conversion jump can hide interior extrema. Enclose every
                // converted input before the monotone right shift; replay still
                // records only actual writes.
                const auto period = std::gcd(range->stride, std::size_t{4294967296ULL});
                range = convertedLattice(operand, period, numberBits(range->first) % period,
                                         unsignedShift);
                if (!range) { return std::nullopt; }
            }
            const auto count = positive ? static_cast<std::uint32_t>(*positive)
                                        : 0U - static_cast<std::uint32_t>(*negative);
            const auto factor = std::size_t{1} << (count & 31U);
            if (leftShift && !twoPointShift) {
                // Within one input and output conversion band, left shift
                // stays affine. The i32 endpoints times at most 2^31 fit i64.
                const auto scale = static_cast<std::int64_t>(factor);
                const auto outputBand = [&](const ContentsValue & endpoint) {
                    const auto biased =
                        static_cast<std::int64_t>(static_cast<std::int32_t>(numberBits(endpoint))) *
                            scale +
                        2147483648LL;
                    // Floor division also handles negative products below INT32_MIN.
                    return biased / 4294967296LL - (biased < 0 && biased % 4294967296LL != 0);
                };
                if (conversionJump || outputBand(range->first) != outputBand(range->last) ||
                    range->stride > 4294967295ULL / factor) {
                    // Each input step and every 2^32 wrap preserve this residue;
                    // endpoint images alone can miss intermediate wraps.
                    const auto period =
                        std::gcd(range->stride, std::size_t{4294967296ULL} / factor) * factor;
                    const auto residue =
                        static_cast<std::size_t>(numberBits(range->first) << (count & 31U)) %
                        period;
                    return convertedLattice(operand, period, residue);
                }
                range->stride *= factor;
            } else if (!twoPointShift) {
                // Within one conversion band, floor division is monotone.
                const auto span = endpointNumber(range->last) - endpointNumber(range->first);
                const auto stride = static_cast<std::int64_t>(range->stride);
                if (range->stride % factor != 0 && span > 0 && span % stride == 0) {
                    roundedIntervals = span / stride;
                }
                range->stride = range->stride % factor == 0 ? range->stride / factor : 1;
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
            const bool singleton = endpointNumber(range->first) == endpointNumber(range->last);
            // Divide the positive stride magnitude; divisor sign only reverses
            // endpoint order. Distinct endpoints can hide fractional positions;
            // a singleton has no adjacent visit whose stride needs dividing.
            if (!divisor || *divisor == 0 || *first % *divisor != 0 ||
                (!singleton && range->stride % *divisor != 0)) {
                // Correlated unions can include fractional quotients absent
                // from every actual visit. Only subdivision may discharge this
                // failure; no fractional value becomes an integer range fact.
                refinableEnclosure |= divisor && *divisor != 0 && range->mixedShift;
                return std::nullopt;
            }
            range->stride = singleton ? 1 : range->stride / *divisor;
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
            } else if (power) {
                if (offsetOperand == 0) {
                    boundedNumberPower(*offset, *endpoint, result);
                } else {
                    boundedNumberPower(*endpoint, *offset, result);
                }
            } else if (subtract) {
                if (offsetOperand == 0) {
                    boundedNumberDifference(*offset, *endpoint, result);
                } else {
                    boundedNumberDifference(*endpoint, *offset, result);
                }
            } else {
                boundedNumberSum(*endpoint, *offset, result);
            }
            if (!result.integerNumber && !result.negativeIntegerNumber) {
                // A mixed enclosure may overstate an actual intermediate. Only
                // subdivision can discharge it; every visited scalar must pass.
                refinableEnclosure |= range->mixedShift;
                return std::nullopt;
            }
            *endpoint = result;
        }
        if (multiply && !twoPointProduct) {
            if (!spend()) {
                invariantFailure = ArrayContentsFailure::WorkLimit;
                return std::nullopt;
            }
            if (endpointNumber(range->first) == endpointNumber(range->last)) {
                // Proven singleton products have no adjacent visit to scale.
                range->stride = 1;
            } else {
                ContentsValue product;
                boundedNumberProduct({index, ContentsKind::NonBigInt, range->stride}, *offset,
                                     product);
                const auto magnitude =
                    product.integerNumber ? product.integerNumber : product.negativeIntegerNumber;
                if (!magnitude) { return std::nullopt; }
                range->stride = *magnitude;
                descending = product.negativeIntegerNumber.has_value();
            }
        }
        // Keep the set of visited positions ordered even when the source
        // visits them backwards. The stride keeps its positive magnitude.
        if (twoPointShift || twoPointPower || twoPointProduct) {
            // The distance between two bounded signed products may exceed the
            // scalar bound. It is a lattice stride, not a source multiplication.
            boundEndpointPair(*range);
        } else if (roundedIntervals) {
            // Each rounded increment is q or q+1. An integral average must
            // equal one endpoint, so every increment has that same size.
            const auto span = endpointNumber(range->last) - endpointNumber(range->first);
            if (span % roundedIntervals == 0) {
                range->stride =
                    static_cast<std::size_t>(std::max<std::int64_t>(1, span / roundedIntervals));
            } else {
                range->mixedShift = true;
            }
        } else if (descending) {
            std::swap(range->first, range->last);
        }
        return range;
    };
    const auto refineIndexRange = [&](mlir::Value key, const auto & accepts) {
        const auto reloadCount = guardReloads.size();
        // Correlated operands and conversions can leave gaps in one enclosing
        // lattice. Split only at aligned visits; bounded indices limit depth
        // to 32 and every reproof spends the shared work budget.
        const auto refine = [&](auto && self, std::size_t begin, std::size_t end) -> bool {
            if (!spend()) {
                invariantFailure = ArrayContentsFailure::WorkLimit;
                return false;
            }
            indexBounds = {begin, end};
            refinableEnclosure = false;
            const auto actual = indexRange(indexRange, key, 0);
            if (guardReloads.size() != reloadCount ||
                (!actual &&
                 (!refinableEnclosure || invariantFailure == ArrayContentsFailure::WorkLimit))) {
                return false;
            }
            if (actual && accepts(*actual)) { return true; }
            if (begin == end) { return false; }
            const auto middle = begin + (end - begin) / *stride / 2 * *stride;
            return self(self, begin, middle) && self(self, middle + *stride, end);
        };
        const bool accepted = refine(refine, *start, last);
        indexBounds = {*start, last};
        return accepted;
    };
    for (mlir::Block * block : {header, body}) {
        for (mlir::Operation & operation : *block) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            if (&operation == block->getTerminator()) { continue; }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                if (block != body) { return unsupported; }
                refinableEnclosure = false;
                if (auto range = indexRange(indexRange, store.getKey(), 0);
                    range || (refinableEnclosure && *start < size)) {
                    if (*start < size) {
                        const auto ownBounds = [&](const IndexRange & candidate) {
                            return candidate.first.integerNumber && candidate.last.integerNumber &&
                                   *candidate.first.integerNumber < size &&
                                   *candidate.last.integerNumber < size;
                        };
                        if (!range || !ownBounds(*range)) {
                            if ((range && !range->mixedShift) ||
                                !refineIndexRange(store.getKey(), ownBounds)) {
                                return invariantFailure;
                            }
                            // Every visit is now proved own. Keep a conservative
                            // footprint; reload gaps still need their own reproof.
                            range = IndexRange{{store.getKey(), ContentsKind::NonBigInt, 0},
                                               {store.getKey(), ContentsKind::NonBigInt, size - 1},
                                               1,
                                               true};
                        }
                        guardStoreRanges.push_back(
                            {*range->first.integerNumber, *range->last.integerNumber, range->stride,
                             range->mixedShift ? store.getKey() : mlir::Value{}});
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
    // reveal a reload overlapping an earlier write. Each range carries
    // a lattice enclosing all writes between its proved first/last own positions.
    for (const auto position : guardReloads) {
        if (!spend()) { return ArrayContentsFailure::WorkLimit; }
        if (guardStores.contains(position)) { return unsupported; }
        for (const auto & [first, storeLast, writeStride, mixedShiftKey] : guardStoreRanges) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            if (position >= first && position <= storeLast &&
                (position - first) % writeStride == 0) {
                if (!mixedShiftKey) { return unsupported; }
                const auto excludesReload = [&](const IndexRange & actual) {
                    return actual.first.integerNumber && actual.last.integerNumber &&
                           (position < *actual.first.integerNumber ||
                            position > *actual.last.integerNumber ||
                            (position - *actual.first.integerNumber) % actual.stride != 0);
                };
                if (!refineIndexRange(mixedShiftKey, excludesReload)) { return invariantFailure; }
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
