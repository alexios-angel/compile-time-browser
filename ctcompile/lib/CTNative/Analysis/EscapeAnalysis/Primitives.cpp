//===- Primitives.cpp - Exact primitive folding for contents --------------===//
//
// Exact constant folding of JavaScript primitive operations for the contents
// query. The helpers recognise literal operands and bounded arithmetic whose
// result is provably exact, so array indices and object keys can be known at
// compile time. Every walk is bounded by a depth cap or the caller's work
// budget. 4294967295 (2^32 - 1) is the largest array length, so valid indices
// are strictly below it.
//
//===----------------------------------------------------------------------===//

#include "../StringNumber.h"
#include "Contents.hpp"
#include "ctbrowser/core/number.hpp"
#include "llvm/ADT/APFloat.h"

#include <limits>

namespace ctcompile::ctnative::escape_detail {

/// A literal Number that is an integer in [0, 2^32 - 1]; -0 counts as zero.
/// With `negate`, also accepts the frontend's single Neg of such a literal.
std::optional<std::size_t> boundedNumber(mlir::Value value, bool negate) {
    if (!value) { return std::nullopt; }
    if (negate) {
        if (auto unary = value.getDefiningOp<ctjs::UnaryOp>();
            unary && unary.getKind() == ctjs::UnaryKind::Neg) {
            return boundedNumber(unary.getOperand());
        }
    }
    if (auto number = ctjs::constantAttr<ctjs::NumberAttr>(value)) {
        const double integer = negate ? -number.getDouble() : number.getDouble();
        if (std::isfinite(integer) && integer >= 0 && integer <= 4294967295.0 &&
            std::floor(integer) == integer) {
            return static_cast<std::size_t>(integer);
        }
    }
    return std::nullopt;
}

/// The own array index a literal key names, with no conversion or prototype
/// lookup. Number -0 and canonical String/BigInt "0" are index zero; 2^32 - 1
/// is not an index. Accepts literals, or one subtraction of two BigInt literals.
std::optional<std::size_t> ownArrayIndex(mlir::Value value) {
    if (auto binary = value.getDefiningOp<ctjs::BinaryOp>();
        binary && binary.getKind() == ctjs::BinaryKind::Sub) {
        if (!ctjs::constantAttr<ctjs::BigIntAttr>(binary.getLhs()) ||
            !ctjs::constantAttr<ctjs::BigIntAttr>(binary.getRhs())) {
            return std::nullopt;
        }
        // ponytail: folds only `bigintLiteral - bigintLiteral`. A loaded operand or
        // a longer chain would need a walk that charges the shared work budget.
        const auto left = ownArrayIndex(binary.getLhs());
        const auto right = ownArrayIndex(binary.getRhs());
        if (left && right && *left >= *right) { return *left - *right; }
        return std::nullopt;
    }
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return std::nullopt; }
    if (const auto number = boundedNumber(value); number && *number < 4294967295ULL) {
        return number;
    }
    llvm::StringRef key;
    unsigned radix = 10;
    if (auto string = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())) {
        key = string.getValue();
    } else if (auto bigint = llvm::dyn_cast<ctjs::BigIntAttr>(constant.getValue())) {
        // compile/expressions.cpp strips the source 'n' before load_bigint, and
        // the VM converts literal digits to an index without object hooks. Do
        // not strip a suffix here: the literal parser rejects it and uses 0n.
        key = bigint.getText();
        if (key.consume_front_insensitive("0x")) {
            radix = 16;
        } else if (key.consume_front_insensitive("0o")) {
            radix = 8;
        } else if (key.consume_front_insensitive("0b")) {
            radix = 2;
        }
    }
    std::uint32_t index = 0;
    // ponytail: at most 32 nondecimal digits; longer keys would need parsing
    // that charges the work budget.
    if (!key.empty() && key.size() <= (radix == 10 ? 10U : 32U) &&
        (radix != 10 || key.size() == 1 || key.front() != '0') && !key.getAsInteger(radix, index) &&
        index < 4294967295ULL) {
        return static_cast<std::size_t>(index);
    }
    return std::nullopt;
}

/// A literal key that names an ordinary own property: not `__proto__` (which
/// can reach the prototype) and at most 256 bytes long. Null otherwise.
mlir::StringAttr ownObjectKey(mlir::StringAttr key) {
    if (!key || key.getValue().size() > 256 || key.getValue() == "__proto__") { return {}; }
    return key;
}

/// ownObjectKey for a String constant operand.
mlir::StringAttr ownObjectKey(mlir::Value value) {
    auto string = ctjs::constantAttr<ctjs::StringAttr>(value);
    if (!string || string.getValue().size() > 256 || string.getValue() == "__proto__") {
        return {};
    }
    return mlir::StringAttr::get(value.getContext(), string.getValue());
}

/// ownArrayIndex for a held value: its exact Number, a single held ASCII digit,
/// or its literal origin.
std::optional<std::size_t> ownArrayIndex(const ContentsValue & key) {
    if (!key.origin()) { return std::nullopt; }
    if (key.integerNumber && *key.integerNumber < 4294967295ULL) { return key.integerNumber; }
    // A saved ASCII digit is already a canonical String property key. Keep its
    // original String identity; this does not supply a Number fact or coerce it.
    if (key.string() && key.asciiCharacter && *key.asciiCharacter >= '0' &&
        *key.asciiCharacter <= '9') {
        return *key.asciiCharacter - '0';
    }
    return ownArrayIndex(key.origin());
}

/// The own index an exact arithmetic result names, if it is an integer below
/// 2^32 - 1.
std::optional<std::size_t> arithmeticArrayIndex(const ContentsValue & key) {
    if (!key.origin() || !key.arithmeticNumber) { return std::nullopt; }
    const double number = *key.arithmeticNumber;
    if (!std::isfinite(number) || number < 0 || number >= 4294967295.0 ||
        std::trunc(number) != number) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(number);
}

/// Folds `base[key]` or `base.length` for a literal ASCII String (or a held
/// single character) and a Number key. Null when it cannot be folded.
std::optional<ContentsValue> boundedStringRead(const ContentsValue & base,
                                               const ContentsValue & key, mlir::Value result) {
    if (!base.string() || !base.origin() || !key.origin()) { return std::nullopt; }
    auto string = ctjs::constantAttr<ctjs::StringAttr>(base.origin());
    if (!string && !base.asciiCharacter) { return std::nullopt; }
    const auto text = string ? string.getValue() : llvm::StringRef{};
    // ponytail: only literal ASCII strings of up to 256 bytes are read. A longer or
    // computed string would need a budget-charged walk, and non-ASCII text would
    // need to agree with how the ctbrowser VM counts string length.
    if (text.size() > 256 || !llvm::all_of(text, [](unsigned char c) { return c < 128; })) {
        return std::nullopt;
    }
    const auto size = base.asciiCharacter ? 1 : text.size();
    const auto name = ownObjectKey(key.origin());
    if (name && name.getValue() == "length") {
        return ContentsValue{result, ContentsKind::NonBigInt, size};
    }
    // The ctbrowser VM (context::lookup_index) returns string characters only for
    // Number keys, and its String and BigInt key lookups differ from JS, so only
    // Number keys are folded here.
    const auto index = key.integerNumber ? key.integerNumber : boundedNumber(key.origin());
    if (!index || *index >= size) { return std::nullopt; }
    ContentsValue character{result, ContentsKind::String};
    character.asciiCharacter =
        base.asciiCharacter ? *base.asciiCharacter : static_cast<unsigned char>(text[*index]);
    return character;
}

namespace {

/// ToNumber of a held literal String of at most 32 bytes.
std::optional<double> boundedStringNumber(const ContentsValue & input) {
    if (!input.string() || !input.origin() || input.asciiCharacter) { return std::nullopt; }
    auto string = ctjs::constantAttr<ctjs::StringAttr>(input.origin());
    // ponytail: this path is not charged to the work budget, so it stops at 32
    // bytes; longer Strings need the caller's budget threaded through.
    if (!string || string.getValue().size() > 32) { return std::nullopt; }
    return literalStringNumber(string.getValue());
}

} // namespace

/// The magnitude of an exact ToNumber of `input`: the nonnegative value, or with
/// `negate` the magnitude of a negative one. Facts attach only to the operation
/// result; primitive origins keep their property keys.
std::optional<std::size_t> boundedConvertedNumber(const ContentsValue & input, bool negate) {
    const auto snapshot = negate ? input.negativeIntegerNumber : input.integerNumber;
    if (snapshot) { return snapshot; }
    const auto origin = input.origin();
    if (!origin) { return std::nullopt; }
    if (auto number = boundedNumber(origin, negate)) { return number; }
    if (input.string()) {
        if (input.asciiCharacter) { return negate ? std::nullopt : ownArrayIndex(input); }
        const auto converted = boundedStringNumber(input);
        if (!converted || !std::isfinite(*converted) || std::abs(*converted) > 4294967295.0) {
            return std::nullopt;
        }
        const double magnitude = negate ? -*converted : *converted;
        if (magnitude >= 0 && std::floor(magnitude) == magnitude) {
            return static_cast<std::size_t>(magnitude);
        }
        return std::nullopt;
    }
    if (negate) { return std::nullopt; }
    if (auto boolean = ctjs::constantAttr<ctjs::BooleanAttr>(origin)) {
        return boolean.getValue() ? 1 : 0;
    }
    if (ctjs::constantAttr<ctjs::NullAttr>(origin)) { return 0; }
    return std::nullopt;
}

std::optional<std::int64_t> signedConvertedNumber(const ContentsValue & input) {
    if (const auto positive = boundedConvertedNumber(input)) {
        return static_cast<std::int64_t>(*positive);
    }
    if (const auto negative = boundedConvertedNumber(input, true)) {
        return -static_cast<std::int64_t>(*negative);
    }
    return std::nullopt;
}

/// ToUint32 of `input` for bitwise operators: accepts a literal Number or a
/// short String, through up to 64 unary +/-. Arithmetic and property keys still
/// need their own exact Number or spelling proofs.
std::optional<std::uint32_t> boundedConvertedBits(const ContentsValue & input) {
    if (input.convertedBits) { return input.convertedBits; }
    if (const auto number = signedConvertedNumber(input)) {
        return static_cast<std::uint32_t>(*number);
    }
    if (const auto number = boundedStringNumber(input)) {
        return ctbrowser::number_to_uint32(*number);
    }
    auto origin = input.origin();
    if (!origin) { return std::nullopt; }
    bool negate = false;
    // ponytail: follow at most 64 unary operations back to a literal, the same cap
    // LoopProof uses. Longer chains would need a budget-charged walk; loads and
    // arithmetic stop the walk.
    for (unsigned depth = 0; depth < 64; ++depth) {
        auto unary = origin.getDefiningOp<ctjs::UnaryOp>();
        if (!unary ||
            (unary.getKind() != ctjs::UnaryKind::Neg && unary.getKind() != ctjs::UnaryKind::Plus)) {
            break;
        }
        negate ^= unary.getKind() == ctjs::UnaryKind::Neg;
        origin = unary.getOperand();
    }
    const auto number = ctjs::constantAttr<ctjs::NumberAttr>(origin);
    // ctbrowser::number_to_uint32 maps NaN and infinities to zero and reduces
    // finite Numbers modulo 2^32 (ToUint32).
    if (number) {
        return ctbrowser::number_to_uint32(negate ? -number.getDouble() : number.getDouble());
    }
    if (origin == input.origin()) { return std::nullopt; }
    if (ctjs::isUndefinedConstant(origin)) {
        return ctbrowser::number_to_uint32(std::numeric_limits<double>::quiet_NaN());
    }
    if (const auto converted = boundedStringNumber({origin, ContentsKind::String})) {
        return ctbrowser::number_to_uint32(negate ? -*converted : *converted);
    }
    return std::nullopt;
}

void boundedNumberComplement(const ContentsValue & input, ContentsValue & result) {
    const auto converted = boundedConvertedBits(input);
    if (!converted) { return; }
    // Complement exact ToUint32 bits, then recover the signed ToInt32 Number.
    // Facts attach only to the result's identity.
    result.setSigned(static_cast<std::int32_t>(~*converted));
}

void boundedNumberBitwise(const ContentsValue & left, const ContentsValue & right,
                          ctjs::BinaryKind kind, ContentsValue & result) {
    if (!ctjs::isBitwise(kind)) { return; }
    const auto a = boundedConvertedBits(left);
    const auto b = boundedConvertedBits(right);
    // Bounded primitive operands use ctbrowser's ToUint32. Mask shift counts;
    // C++23 signed right shift preserves the sign. Only UShr keeps an unsigned
    // result; negative magnitudes never become own-index facts.
    if (a && b) {
        const auto x = *a;
        const auto y = *b;
        const auto bits =
            kind == ctjs::BinaryKind::BitAnd   ? x & y
            : kind == ctjs::BinaryKind::BitOr  ? x | y
            : kind == ctjs::BinaryKind::BitXor ? x ^ y
            : kind == ctjs::BinaryKind::Shl    ? static_cast<std::uint32_t>(x << (y & 31U))
            : kind == ctjs::BinaryKind::Shr
                ? static_cast<std::uint32_t>(static_cast<std::int32_t>(x) >> (y & 31U))
                : x >> (y & 31U);
        result.setSigned(kind == ctjs::BinaryKind::UShr ? static_cast<std::int64_t>(bits)
                                                        : static_cast<std::int32_t>(bits));
    }
}

namespace {

/// The exact binary64 value of `input`: a recorded arithmetic result or a
/// Number literal.
std::optional<double> arithmeticNumber(const ContentsValue & input) {
    if (input.arithmeticNumber) { return input.arithmeticNumber; }
    const auto value =
        input.origin() ? ctjs::constantAttr<ctjs::NumberAttr>(input.origin()) : ctjs::NumberAttr{};
    return value ? std::optional<double>{value.getDouble()} : std::nullopt;
}

/// Records the exact binary64 result (and its ToUint32 bits) of `left kind
/// right` when both are exact Numbers and the chain is under 64 operations.
void boundedNumberArithmetic(const ContentsValue & left, const ContentsValue & right,
                             ctjs::BinaryKind kind, ContentsValue & result) {
    const auto a = arithmeticNumber(left);
    const auto b = arithmeticNumber(right);
    const auto depth = std::max(left.arithmeticDepth, right.arithmeticDepth);
    const bool negativeUnit = kind == ctjs::BinaryKind::Pow && a && b && *a == -1 &&
                              std::isfinite(*b) && std::trunc(*b) == *b;
    if (a && b && depth < 64 &&
        (kind != ctjs::BinaryKind::Pow || *b == 0 || *b == 1 || negativeUnit ||
         (*a == 1 && std::isfinite(*b)))) {
        // ponytail: at most 64 Number operations; Pow only preserves the exact
        // zero/unit-exponent and finite-exponent +/-unit-base identities. Other powers
        // need their own binary64 proof. Number x ** +/-0 is one, including NaN x;
        // x ** 1 preserves x, but 1 ** a nonfinite exponent is NaN.
        llvm::APFloat computed(kind == ctjs::BinaryKind::Pow && *b == 0 ? 1.0 : *a);
        if (kind == ctjs::BinaryKind::Pow && negativeUnit) {
            // Finite integral binary64 exponents have exact parity, including
            // negative exponents and represented even integers beyond 2^53.
            computed = llvm::APFloat(std::fmod(*b, 2.0) == 0 ? 1.0 : -1.0);
        } else if (kind == ctjs::BinaryKind::Mul) {
            computed.multiply(llvm::APFloat(*b), llvm::APFloat::rmNearestTiesToEven);
        } else if (kind == ctjs::BinaryKind::Div) {
            computed.divide(llvm::APFloat(*b), llvm::APFloat::rmNearestTiesToEven);
        } else if (kind == ctjs::BinaryKind::Mod) {
            computed.mod(llvm::APFloat(*b));
        } else if (kind == ctjs::BinaryKind::Sub) {
            computed.subtract(llvm::APFloat(*b), llvm::APFloat::rmNearestTiesToEven);
        } else if (kind == ctjs::BinaryKind::Add) {
            computed.add(llvm::APFloat(*b), llvm::APFloat::rmNearestTiesToEven);
        }
        result.arithmeticNumber = computed.convertToDouble();
        result.arithmeticDepth = depth + 1;
        result.convertedBits = ctbrowser::number_to_uint32(*result.arithmeticNumber);
    }
}

} // namespace

void boundedNumberUnary(const ContentsValue & input, bool negate, ContentsValue & result) {
    const auto number = arithmeticNumber(input);
    if (!number || input.arithmeticDepth >= 64) { return; }
    // Unary sign/identity preserves binary64, including signed zero and nonfinite
    // values. Converted bits and String coercions cannot supply this Number fact.
    result.arithmeticNumber = negate ? -*number : *number;
    result.arithmeticDepth = input.arithmeticDepth + 1;
    result.convertedBits = ctbrowser::number_to_uint32(*result.arithmeticNumber);
}

void boundedNumberProduct(const ContentsValue & left, const ContentsValue & right,
                          ContentsValue & result) {
    boundedNumberArithmetic(left, right, ctjs::BinaryKind::Mul, result);
    const auto a = signedConvertedNumber(left);
    const auto b = signedConvertedNumber(right);
    // Original Boolean/null and canonical Strings share unary's exact
    // conversion. A bounded product excludes rounding and wrap; zero keeps
    // its original signed value as the origin.
    if (a && b &&
        (*b == 0 || std::abs(*a) <= static_cast<std::int64_t>(4294967295ULL) / std::abs(*b))) {
        result.setSigned(*a * *b);
    }
}

void boundedNumberDivision(const ContentsValue & left, const ContentsValue & right, bool remainder,
                           ContentsValue & result) {
    boundedNumberArithmetic(left, right, remainder ? ctjs::BinaryKind::Mod : ctjs::BinaryKind::Div,
                            result);
    const auto a = signedConvertedNumber(left);
    const auto b = signedConvertedNumber(right);
    // Original Boolean/null and canonical Strings share unary's exact
    // conversion. Bounded operands give an exact remainder; division also
    // needs zero remainder. C++'s % keeps the dividend's sign, as Mod does,
    // regardless of divisor sign. Keep the original signed zero.
    if (a && b && *b != 0 && (remainder || *a % *b == 0)) {
        result.setSigned(remainder ? *a % *b : *a / *b);
    }
}

void boundedNumberPower(const ContentsValue & left, const ContentsValue & right,
                        ContentsValue & result) {
    boundedNumberArithmetic(left, right, ctjs::BinaryKind::Pow, result);
    const auto exponent = boundedConvertedNumber(right);
    const auto positive = boundedConvertedNumber(left);
    const auto negative = boundedConvertedNumber(left, true);
    const auto negativeExponent = boundedConvertedNumber(right, true);
    // ponytail: only exact zero/unit identities; general powers need Number's
    // implementation-approximated result proof. Boolean/null and canonical
    // Strings share unary's exact conversion; keep the original signed zero.
    if (exponent && *exponent <= 1 && (positive || negative)) {
        result.integerNumber = *exponent == 0 ? std::optional<std::size_t>{1} : positive;
        if (*exponent == 1 && !positive) { result.negativeIntegerNumber = negative; }
    } else if ((positive == 0 && exponent) || (positive == 1 && (exponent || negativeExponent))) {
        // Zero needs a positive exponent; exponent zero was handled above.
        // One still requires a finite Number.
        result.integerNumber = positive;
    } else if (negative == 1 && (exponent || negativeExponent)) {
        // The sign of an integer exponent does not change parity.
        const auto magnitude = exponent ? *exponent : *negativeExponent;
        if (magnitude % 2 == 0) {
            result.integerNumber = 1;
        } else {
            result.negativeIntegerNumber = 1;
        }
    }
}

void boundedNumberSum(const ContentsValue & left, const ContentsValue & right,
                      ContentsValue & result) {
    // Add selects concatenation before Number conversion; even canonical
    // Strings cannot borrow the numeric proof used by the other operations.
    if (left.string() || right.string()) { return; }
    boundedNumberArithmetic(left, right, ctjs::BinaryKind::Add, result);
    // Both original primitives must convert exactly; setSigned's bound keeps
    // neither dynamic nor static Add from borrowing rounding or wrap.
    // Cancellation stays exact. Keep negative magnitudes separate from
    // indices; the original result still carries the sign of zero.
    const auto a = signedConvertedNumber(left);
    const auto b = signedConvertedNumber(right);
    if (a && b) { result.setSigned(*a + *b); }
}

void boundedNumberDifference(const ContentsValue & left, const ContentsValue & right,
                             ContentsValue & result) {
    boundedNumberArithmetic(left, right, ctjs::BinaryKind::Sub, result);
    // Boolean/null and canonical Strings share unary's exact conversion.
    // Saved operands retain their values; setSigned bounds the difference.
    // Cancellation remains exact; keep the original result's signed zero.
    const auto original = signedConvertedNumber(left);
    const auto offset = signedConvertedNumber(right);
    if (original && offset) { result.setSigned(*original - *offset); }
}

void boundedNumberBinary(ctjs::BinaryKind kind, const ContentsValue & left,
                         const ContentsValue & right, ContentsValue & result) {
    switch (kind) {
    case ctjs::BinaryKind::Add: return boundedNumberSum(left, right, result);
    case ctjs::BinaryKind::Sub: return boundedNumberDifference(left, right, result);
    case ctjs::BinaryKind::Mul: return boundedNumberProduct(left, right, result);
    case ctjs::BinaryKind::Div:
    case ctjs::BinaryKind::Mod:
        return boundedNumberDivision(left, right, kind == ctjs::BinaryKind::Mod, result);
    case ctjs::BinaryKind::Pow: return boundedNumberPower(left, right, result);
    default: return boundedNumberBitwise(left, right, kind, result);
    }
}

std::pair<mlir::Value, bool> conditionChoiceKey(mlir::Value condition,
                                                llvm::function_ref<bool(std::size_t)> spend) {
    // Truthy, ToBoolean and logical Not are total, without object conversion. The flag
    // records whether the branch reverses the exact input's truthiness.
    auto truthy = condition.getDefiningOp<ctjs::TruthyOp>();
    if (!truthy) { return {condition, false}; }
    const auto directBoolean = [](mlir::Value value) {
        auto comparison = value.getDefiningOp<ctjs::CompareOp>();
        auto conversion = value.getDefiningOp<ctjs::ConvertOp>();
        auto unary = value.getDefiningOp<ctjs::UnaryOp>();
        return (comparison && comparison.getKind() == ctjs::CompareKind::StrictEq) ||
               (conversion && conversion.getKind() == ctjs::ConvertKind::ToBoolean) ||
               (unary && unary.getKind() == ctjs::UnaryKind::Not) ||
               ctjs::constantAttr<ctjs::BooleanAttr>(value);
    };
    const auto stripIdentity = [&](mlir::Value value) -> mlir::Value {
        while (true) {
            // The shared budget bounds traversal; exhaustion supplies no partial key.
            if (!spend(1)) { return {}; }
            if (!value.getDefiningOp<mlir::scf::IfOp>()) { return value; }
            mlir::Value common;
            for (mlir::Value arm : armYields(llvm::cast<mlir::OpResult>(value))) {
                if (!spend(1)) { return {}; }
                if (common && common != arm) {
                    common = {};
                    break;
                }
                common = arm;
            }
            // An identical saved SSA value keeps its tag and snapshot.
            // Every arm effect is checked separately.
            if (common) {
                value = common;
                continue;
            }
            // Nested scf.if results may forward the same SSA value through
            // different intermediate results. Accept only when every leaf is
            // that one value. This proves the key only: replay still checks
            // every arm's producers and side effects.
            const auto walked = ifLeaves(value, spend, [&](mlir::Value leaf) {
                if (common && common != leaf) { return false; }
                common = leaf;
                return true;
            });
            if (!walked) { return {}; }
            return *walked && common ? common : value;
        }
    };
    const auto stripBoolean = [&](mlir::Value value) -> std::pair<mlir::Value, bool> {
        bool negated = false;
        while (true) {
            value = stripIdentity(value);
            if (!value) { return {}; }
            if (auto convert = value.getDefiningOp<ctjs::ConvertOp>();
                convert && convert.getKind() == ctjs::ConvertKind::ToBoolean) {
                value = convert.getOperand();
                continue;
            }
            if (auto compare = value.getDefiningOp<ctjs::CompareOp>();
                compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
                for (bool reversed : {false, true}) {
                    auto input = reversed ? compare.getRhs() : compare.getLhs();
                    ctjs::BooleanAttr boolean;
                    bool exactBoolean = true;
                    for (bool literalSide : {true, false}) {
                        if (!exactBoolean) { break; }
                        const auto walked = ifLeaves(
                            literalSide ? (reversed ? compare.getLhs() : compare.getRhs()) : input,
                            spend, [&](mlir::Value leaf) {
                                if (!literalSide) { return directBoolean(leaf); }
                                auto literal = ctjs::constantAttr<ctjs::BooleanAttr>(leaf);
                                if (!literal || (boolean && boolean != literal)) { return false; }
                                boolean = literal;
                                return true;
                            });
                        if (!walked) { return {}; }
                        exactBoolean = *walked;
                    }
                    // Equality with a Boolean preserves or reverses truthiness
                    // only when the other operand is already exactly Boolean.
                    // An opaque value, Number or read cannot borrow that tag.
                    // Literal leaves must agree on one exact Boolean attribute;
                    // no arm-local SSA value becomes a dominating representative.
                    // Every nested input leaf must prove it is a Boolean; arm
                    // correspondence and side effects are checked separately.
                    if (exactBoolean && boolean) {
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
        left = stripIdentity(left);
        right = stripIdentity(right);
        if (!left || !right) {
            complete = false;
            return false;
        }
        // Strict comparison operands keep their exact value; Boolean conversion
        // and logical Not cannot be stripped here.
        if (left == right) { return true; }
        // One exact SSA leaf or immutable primitive attribute supplies the same input.
        // No snapshot, coercion, signed-zero or BigInt spelling equivalence is inferred.
        // All selected leaves must agree; no arm-local SSA representative escapes.
        // Replay still checks every arm's producers and side effects.
        mlir::Attribute common;
        mlir::Value identity;
        const auto leaf = [&](mlir::Value current) {
            if (auto literal = current.getDefiningOp<ctjs::ConstantOp>()) {
                auto attribute = literal.getValue();
                if (identity ||
                    !llvm::isa<ctjs::UndefinedAttr, ctjs::NullAttr, ctjs::BooleanAttr,
                               ctjs::NumberAttr, ctjs::StringAttr, ctjs::BigIntAttr>(attribute) ||
                    (common && common != attribute)) {
                    return false;
                }
                common = attribute;
                return true;
            }
            if (common || (identity && identity != current)) { return false; }
            identity = current;
            return true;
        };
        // Visit right's leaves before left's (the pop order of one stack seeded
        // with {left, right}).
        auto walked = ifLeaves(right, spend, leaf);
        if (walked && *walked) { walked = ifLeaves(left, spend, leaf); }
        if (!walked) {
            complete = false;
            return false;
        }
        return *walked && (bool(common) || bool(identity));
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
            // outermost first match, so local duplicates share its held value.
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
                        for (mlir::Value arm : armYields(result)) {
                            if (!spend(1)) { return {}; }
                            const auto incoming = stripBoolean(arm);
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
            // This selects no arm: replay still checks every side effect.
            std::pair<mlir::Value, bool> common;
            for (mlir::Value arm : armYields(llvm::cast<mlir::OpResult>(input))) {
                if (!spend(1)) { return {}; }
                const auto incoming = booleanInput(arm);
                if (!incoming.first) { return {}; }
                if (common.first && common != incoming) {
                    auto thenValue = ctjs::constantAttr<ctjs::BooleanAttr>(common.first);
                    auto elseValue = ctjs::constantAttr<ctjs::BooleanAttr>(incoming.first);
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

} // namespace ctcompile::ctnative::escape_detail
