#include "Contents.hpp"
#include "ctbrowser/core/algorithms.hpp"
#include "ctbrowser/core/number.hpp"
#include "ctbrowser/core/number_format.hpp"

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
    if (!string) { return std::nullopt; }
    const auto text = string.getValue();
    // ponytail: at most 32 source bytes; wider Strings need charged parsing.
    // Validate the whole grammar before Core handles overflow/underflow.
    if (text.size() > 32) { return std::nullopt; }
    llvm::StringRef digits = ctbrowser::trim_js_space({text.data(), text.size()});
    int radix = 10;
    if (digits.consume_front_insensitive("0x")) {
        radix = 16;
    } else if (digits.consume_front_insensitive("0o")) {
        radix = 8;
    } else if (digits.consume_front_insensitive("0b")) {
        radix = 2;
    } else if ((digits.consume_front("+") || digits.consume_front("-")) && digits.empty()) {
        return std::nullopt;
    }
    if (radix == 10 && !digits.empty()) {
        const auto decimal = [](char c) { return c >= '0' && c <= '9'; };
        auto rest = digits.drop_while(decimal);
        bool hasDigits = rest.size() != digits.size();
        if (rest.consume_front(".")) {
            const auto fraction = rest.drop_while(decimal);
            hasDigits |= fraction.size() != rest.size();
            rest = fraction;
        }
        if (!hasDigits) { return std::nullopt; }
        if (rest.consume_front_insensitive("e")) {
            if (!rest.consume_front("+")) { rest.consume_front("-"); }
            unsigned exponent = 0;
            // Core adds an int exponent to the mantissa order on range failure.
            // The source-byte bound limits that order's magnitude to 32.
            if (rest.empty() || !llvm::all_of(rest, decimal) || rest.getAsInteger(10, exponent) ||
                exponent > static_cast<unsigned>(std::numeric_limits<int>::max() - 32)) {
                return std::nullopt;
            }
            rest = {};
        }
        if (!rest.empty()) { return std::nullopt; }
    } else if ((radix != 10 && digits.empty()) || !llvm::all_of(digits, [radix](char c) {
                   const auto digit = ctbrowser::hex_value(c);
                   return digit >= 0 && digit < radix;
               })) {
        return std::nullopt;
    }
    const double converted = ctbrowser::string_to_number({text.data(), text.size()});
    if (!std::isfinite(converted) || std::abs(converted) > 4294967295.0) { return std::nullopt; }
    return converted;
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
        if (!converted) { return std::nullopt; }
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

// Bitwise conversion may truncate a bounded original String. Arithmetic and
// property keys still require their separate exact Number/spelling proofs.
std::optional<std::uint32_t> boundedConvertedBits(const ContentsValue & input) {
    if (const auto positive = boundedConvertedNumber(input)) {
        return static_cast<std::uint32_t>(*positive);
    }
    if (const auto negative = boundedConvertedNumber(input, true)) {
        return 0U - static_cast<std::uint32_t>(*negative);
    }
    if (const auto number = boundedStringNumber(input)) {
        return ctbrowser::number_to_uint32(*number);
    }
    return std::nullopt;
}

void boundedNumberComplement(const ContentsValue & input, ContentsValue & result) {
    const auto converted = boundedConvertedBits(input);
    if (!converted) { return; }
    // Complement exact ToUint32 bits, then recover the signed Number magnitude
    // without signed overflow. Facts attach only to the result's identity.
    const std::uint32_t bits = ~*converted;
    if (bits < 2147483648ULL) {
        result.integerNumber = bits;
    } else {
        result.negativeIntegerNumber = 4294967296ULL - bits;
    }
}

void boundedNumberBitwise(const ContentsValue & left, const ContentsValue & right,
                          ctjs::BinaryKind kind, ContentsValue & result) {
    switch (kind) {
    case ctjs::BinaryKind::BitAnd:
    case ctjs::BinaryKind::BitOr:
    case ctjs::BinaryKind::BitXor:
    case ctjs::BinaryKind::Shl:
    case ctjs::BinaryKind::Shr:
    case ctjs::BinaryKind::UShr: break;
    default: return;
    }
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
        if (kind == ctjs::BinaryKind::UShr || bits < 2147483648ULL) {
            result.integerNumber = bits;
        } else {
            result.negativeIntegerNumber = 4294967296ULL - bits;
        }
    }
}

void boundedNumberProduct(const ContentsValue & left, const ContentsValue & right,
                          ContentsValue & result) {
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
    const auto a = boundedConvertedNumber(left);
    const auto b = boundedConvertedNumber(right);
    // Both original primitives must convert exactly. Guard before adding so
    // neither dynamic nor static Add can borrow rounding or wrap.
    const auto negativeA = boundedConvertedNumber(left, true);
    const auto negativeB = boundedConvertedNumber(right, true);
    // Cancellation stays exact and bounded. Keep negative magnitudes separate
    // from indices; the original result still carries the sign of zero.
    if (a && b && *a <= 4294967295ULL - *b) {
        result.integerNumber = *a + *b;
    } else if (a && negativeB) {
        if (*a >= *negativeB) {
            result.integerNumber = *a - *negativeB;
        } else {
            result.negativeIntegerNumber = *negativeB - *a;
        }
    } else if (negativeA && b) {
        if (*b >= *negativeA) {
            result.integerNumber = *b - *negativeA;
        } else {
            result.negativeIntegerNumber = *negativeA - *b;
        }
    } else if (negativeA && negativeB && *negativeA <= 4294967295ULL - *negativeB) {
        result.negativeIntegerNumber = *negativeA + *negativeB;
    }
}

void boundedNumberDifference(const ContentsValue & left, const ContentsValue & right,
                             ContentsValue & result) {
    const auto original = boundedConvertedNumber(left);
    const auto offset = boundedConvertedNumber(right);
    // Boolean/null and canonical Strings share unary's exact conversion.
    // Saved operands retain their values; bound magnitudes before arithmetic.
    if (original && offset) {
        if (*offset <= *original) {
            result.integerNumber = *original - *offset;
        } else {
            result.negativeIntegerNumber = *offset - *original;
        }
    } else if (const auto magnitude = boundedConvertedNumber(right, true);
               original && magnitude && *magnitude <= 4294967295ULL - *original) {
        result.integerNumber = *original + *magnitude;
    } else if (const auto negative = boundedConvertedNumber(left, true); negative) {
        // Cancellation remains exact; keep the original result's signed zero.
        if (offset && *offset <= 4294967295ULL - *negative) {
            result.negativeIntegerNumber = *negative + *offset;
        } else if (magnitude) {
            if (*magnitude >= *negative) {
                result.integerNumber = *magnitude - *negative;
            } else {
                result.negativeIntegerNumber = *negative - *magnitude;
            }
        }
    }
}

} // namespace ctcompile::ctnative::escape_detail
