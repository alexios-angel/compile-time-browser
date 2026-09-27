#include "../StringNumber.h"
#include "Contents.hpp"
#include "ctbrowser/core/number.hpp"
#include "llvm/ADT/APFloat.h"

#include <limits>

namespace ctcompile::ctnative::escape_detail {

// An original Number in the exact array-length range; -0 has index value zero.
// Negation also recognizes the frontend's single Neg of an original literal.
std::optional<std::size_t> boundedNumber(mlir::Value value, bool negate) {
    if (!value) { return std::nullopt; }
    if (negate) {
        if (auto unary = value.getDefiningOp<ctjs::UnaryOp>();
            unary && unary.getKind() == ctjs::UnaryKind::Neg) {
            return boundedNumber(unary.getOperand());
        }
    }
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    auto number =
        constant ? llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue()) : ctjs::NumberAttr{};
    if (number) {
        const double integer = negate ? -number.getDouble() : number.getDouble();
        if (std::isfinite(integer) && integer >= 0 && integer <= 4294967295.0 &&
            std::floor(integer) == integer) {
            return static_cast<std::size_t>(integer);
        }
    }
    return std::nullopt;
}

// An own array element, not a property requiring conversion/prototype lookup.
// Number -0 and canonical String/BigInt "0" are index zero; 2^32-1 is not an element.
// Original literals, or one subtraction of two bounded original BigInt literals.
std::optional<std::size_t> ownArrayIndex(mlir::Value value) {
    if (auto binary = value.getDefiningOp<ctjs::BinaryOp>();
        binary && binary.getKind() == ctjs::BinaryKind::Sub) {
        auto lhs = binary.getLhs().getDefiningOp<ctjs::ConstantOp>();
        auto rhs = binary.getRhs().getDefiningOp<ctjs::ConstantOp>();
        if (!lhs || !rhs || !llvm::isa<ctjs::BigIntAttr>(lhs.getValue()) ||
            !llvm::isa<ctjs::BigIntAttr>(rhs.getValue())) {
            return std::nullopt;
        }
        // ponytail: two literal parses only; loaded operands/chains need charged provenance.
        const auto left = ownArrayIndex(lhs.getResult());
        const auto right = ownArrayIndex(rhs.getResult());
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
        // compile/expressions.cpp removes source 'n' before load_bigint. The VM
        // converts literal digits to an index without object hooks. Do not strip
        // an attribute suffix: the literal parser rejects it and substitutes 0n.
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
    // ponytail: at most 32 nondecimal digits; extend only with charged parsing.
    if (!key.empty() && key.size() <= (radix == 10 ? 10U : 32U) &&
        (radix != 10 || key.size() == 1 || key.front() != '0') && !key.getAsInteger(radix, index) &&
        index < 4294967295ULL) {
        return static_cast<std::size_t>(index);
    }
    return std::nullopt;
}

mlir::StringAttr ownObjectKey(mlir::StringAttr key) {
    if (!key || key.getValue().size() > 256 || key.getValue() == "__proto__") { return {}; }
    return key;
}

mlir::StringAttr ownObjectKey(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    auto string = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
    if (!string || string.getValue().size() > 256 || string.getValue() == "__proto__") {
        return {};
    }
    return mlir::StringAttr::get(constant.getContext(), string.getValue());
}

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

std::optional<std::size_t> arithmeticArrayIndex(const ContentsValue & key) {
    if (!key.origin() || !key.arithmeticNumber) { return std::nullopt; }
    const double number = *key.arithmeticNumber;
    if (!std::isfinite(number) || number < 0 || number >= 4294967295.0 ||
        std::trunc(number) != number) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(number);
}

std::optional<ContentsValue> boundedStringRead(const ContentsValue & base,
                                               const ContentsValue & key, mlir::Value result) {
    if (!base.string() || !base.origin() || !key.origin()) { return std::nullopt; }
    auto literal = base.origin().getDefiningOp<ctjs::ConstantOp>();
    auto string =
        literal ? llvm::dyn_cast<ctjs::StringAttr>(literal.getValue()) : ctjs::StringAttr{};
    if (!string && !base.asciiCharacter) { return std::nullopt; }
    const auto text = string ? string.getValue() : llvm::StringRef{};
    // ponytail: bound the scan to 256 original ASCII bytes. Wider/computed Strings
    // need charged provenance; Unicode needs agreement with Script's byte length.
    if (text.size() > 256 || !llvm::all_of(text, [](unsigned char c) { return c < 128; })) {
        return std::nullopt;
    }
    const auto size = base.asciiCharacter ? 1 : text.size();
    const auto name = ownObjectKey(key.origin());
    if (name && name.getValue() == "length") {
        return ContentsValue{result, ContentsKind::NonBigInt, size};
    }
    // Script's lookup_index exposes characters only for Number keys. Its String
    // and BigInt property lookups disagree with JS, so do not coerce their keys.
    const auto index = key.integerNumber ? key.integerNumber : boundedNumber(key.origin());
    if (!index || *index >= size) { return std::nullopt; }
    ContentsValue character{result, ContentsKind::String};
    character.asciiCharacter =
        base.asciiCharacter ? *base.asciiCharacter : static_cast<unsigned char>(text[*index]);
    return character;
}

namespace {

std::optional<double> boundedStringNumber(const ContentsValue & input) {
    if (!input.string() || !input.origin() || input.asciiCharacter) { return std::nullopt; }
    auto literal = input.origin().getDefiningOp<ctjs::ConstantOp>();
    auto string =
        literal ? llvm::dyn_cast<ctjs::StringAttr>(literal.getValue()) : ctjs::StringAttr{};
    // ponytail: uncharged provenance queries keep the 32-byte bound; wider
    // Strings need the caller's shared work budget threaded through this path.
    if (!string || string.getValue().size() > 32) { return std::nullopt; }
    return literalStringNumber(string.getValue());
}

} // namespace

// One signed magnitude of an exact primitive Number conversion. Facts attach
// only to the operation result; primitive origins keep their property keys.
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
    if (auto literal = origin.getDefiningOp<ctjs::ConstantOp>()) {
        if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(literal.getValue())) {
            return boolean.getValue() ? 1 : 0;
        }
        if (llvm::isa<ctjs::NullAttr>(literal.getValue())) { return 0; }
    }
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

// Bitwise conversion accepts an original Number or bounded String. Arithmetic and
// property keys still require their separate exact Number/spelling proofs.
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
    // ponytail: cap literal-only provenance at 64 operations, matching loop proofs.
    // Unbounded chains need charged traversal; loads and arithmetic still stop here.
    for (unsigned depth = 0; depth < 64; ++depth) {
        auto unary = origin.getDefiningOp<ctjs::UnaryOp>();
        if (!unary ||
            (unary.getKind() != ctjs::UnaryKind::Neg && unary.getKind() != ctjs::UnaryKind::Plus)) {
            break;
        }
        negate ^= unary.getKind() == ctjs::UnaryKind::Neg;
        origin = unary.getOperand();
    }
    auto literal = origin.getDefiningOp<ctjs::ConstantOp>();
    const auto number =
        literal ? llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue()) : ctjs::NumberAttr{};
    // Core maps nonfinite Numbers to zero and reduces finite Numbers before integer conversion.
    if (number) {
        return ctbrowser::number_to_uint32(negate ? -number.getDouble() : number.getDouble());
    }
    if (origin == input.origin()) { return std::nullopt; }
    if (literal && llvm::isa<ctjs::UndefinedAttr>(literal.getValue())) {
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
    // Bounded primitive operands use Core's ToUint32 conversion. Mask counts;
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

std::optional<double> arithmeticNumber(const ContentsValue & input) {
    if (input.arithmeticNumber) { return input.arithmeticNumber; }
    auto literal =
        input.origin() ? input.origin().getDefiningOp<ctjs::ConstantOp>() : ctjs::ConstantOp{};
    const auto value =
        literal ? llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue()) : ctjs::NumberAttr{};
    return value ? std::optional<double>{value.getDouble()} : std::nullopt;
}

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
    auto a = boundedConvertedNumber(left);
    auto b = boundedConvertedNumber(right);
    const bool negative = a.has_value() != b.has_value();
    if (!a) { a = boundedConvertedNumber(left, true); }
    if (!b) { b = boundedConvertedNumber(right, true); }
    // Original Boolean/null and canonical Strings share unary's exact
    // conversion. A bounded product excludes rounding and wrap; zero keeps
    // its original signed value as the origin.
    if (a && b && (*b == 0 || *a <= 4294967295ULL / *b)) {
        const auto product = *a * *b;
        if (negative && product != 0) {
            result.negativeIntegerNumber = product;
        } else {
            result.integerNumber = product;
        }
    }
}

void boundedNumberDivision(const ContentsValue & left, const ContentsValue & right, bool remainder,
                           ContentsValue & result) {
    boundedNumberArithmetic(left, right, remainder ? ctjs::BinaryKind::Mod : ctjs::BinaryKind::Div,
                            result);
    auto a = boundedConvertedNumber(left);
    auto b = boundedConvertedNumber(right);
    const bool negative = remainder ? !a : a.has_value() != b.has_value();
    if (!a) { a = boundedConvertedNumber(left, true); }
    if (!b) { b = boundedConvertedNumber(right, true); }
    // Original Boolean/null and canonical Strings share unary's exact
    // conversion. Bounded operands give an exact remainder; division also
    // needs zero remainder. Mod keeps the dividend's sign, regardless of
    // divisor sign. Keep the original signed zero.
    if (a && b && *b != 0 && (remainder || *a % *b == 0)) {
        const auto magnitude = remainder ? *a % *b : *a / *b;
        if (negative && magnitude != 0) {
            result.negativeIntegerNumber = magnitude;
        } else {
            result.integerNumber = magnitude;
        }
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

} // namespace ctcompile::ctnative::escape_detail
