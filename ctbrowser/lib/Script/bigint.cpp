#include <ctbrowser/script/bigint.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/number_format.hpp>
#include <ctbrowser/script/vm.hpp>

// The header says what each of these is for. This says how, and records the two
// places where cpp_int and JavaScript disagree about what an operation means.

namespace ctbrowser::script {

namespace {

// Digits of a literal, with the radix prefix and any `_` separators taken off.
// Returns the radix and the bare digits, or 0 when the text is not an integer
// literal at all.
struct scanned {
    unsigned radix = 0;
    std::string digits;
    bool negative = false;
};

[[nodiscard]] scanned scan_literal(std::string_view text) {
    scanned out;
    std::string_view rest = text;
    if (!rest.empty() && (rest.front() == '+' || rest.front() == '-')) {
        out.negative = rest.front() == '-';
        rest.remove_prefix(1);
    }
    out.radix = 10;
    if (rest.size() > 2 && rest[0] == '0') {
        switch (ascii_lower(rest[1])) {
        case 'x':
            out.radix = 16;
            rest.remove_prefix(2);
            break;
        case 'o':
            out.radix = 8;
            rest.remove_prefix(2);
            break;
        case 'b':
            out.radix = 2;
            rest.remove_prefix(2);
            break;
        default: break;
        }
    }
    if (rest.empty()) { return {0, {}, false}; }
    for (const char c : rest) {
        if (c == '_') { continue; }     // a numeric separator, already legal in the lexer
        const int digit = hex_value(c); // a '.', an 'e', anything else - not an integer
        if (digit < 0 || static_cast<unsigned>(digit) >= out.radix) { return {0, {}, false}; }
        out.digits += c;
    }
    if (out.digits.empty()) { return {0, {}, false}; }
    return out;
}

[[nodiscard]] bigint from_digits(const scanned & s) {
    // cpp_int parses 0x/0b prefixes itself for 16 and 2, and nothing for 8, so
    // the digits are accumulated rather than handed over as text. It is linear
    // in the digit count either way and this needs no special cases.
    bigint out = 0;
    for (const char c : s.digits) {
        out *= s.radix;
        out += static_cast<unsigned>(hex_value(c));
    }
    return s.negative ? -out : out;
}

} // namespace

std::optional<bigint> bigint_from_literal(std::string_view text) {
    const scanned s = scan_literal(text);
    if (s.radix == 0) { return std::nullopt; }
    return from_digits(s);
}

std::optional<bigint> bigint_from_double(double v) {
    // ONLY AN INTEGER CONVERTS. `BigInt(1.5)` is a RangeError rather than a
    // truncation, which is the type refusing to lose the thing it exists to
    // keep - and `BigInt(NaN)` and `BigInt(Infinity)` are the same refusal.
    if (!std::isfinite(v) || v != std::trunc(v)) { return std::nullopt; }
    return bigint{v};
}

std::optional<bigint> bigint_from_string(std::string_view text) {
    const std::string_view body = trim_js_space(text);
    if (body.empty()) { return bigint{0}; } // BigInt("") and BigInt(" ") are 0n
    // StringIntegerLiteral has no separators; only decimal digits may carry a sign.
    if (body.find('_') != std::string_view::npos) { return std::nullopt; }
    const scanned s = scan_literal(body);
    if (s.radix == 0 || (s.radix != 10 && (body.front() == '+' || body.front() == '-'))) {
        return std::nullopt;
    }
    return from_digits(s);
}

std::optional<bigint> bigint_div(const bigint & a, const bigint & b) {
    if (b == 0) { return std::nullopt; } // RangeError, not Infinity - there is no bigint infinity
    return a / b;
}

std::optional<bigint> bigint_rem(const bigint & a, const bigint & b) {
    if (b == 0) { return std::nullopt; }
    return a % b;
}

std::optional<bigint> bigint_pow(const bigint & a, const bigint & b) {
    // A NEGATIVE EXPONENT IS A RangeError. There are no fractions here, so
    // `2n ** -1n` has no value to give rather than 0.5.
    if (b < 0) { return std::nullopt; }
    // The exponent is arbitrary precision but the result must fit in memory, so
    // an absurd one is refused rather than attempted. 2**32 bits is already a
    // 512 MB number; nothing legitimate reaches it.
    if (b > 0xFFFFFFFFu) { return std::nullopt; }
    return boost::multiprecision::pow(a, static_cast<unsigned>(b));
}

std::optional<bigint> bigint_shl(const bigint & a, const bigint & b) {
    // A NEGATIVE SHIFT COUNT SHIFTS THE OTHER WAY - `1n << -1n` is `1n >> 1n` -
    // which is the specification, and not what C++ `<<` does with a negative.
    if (b < 0) { return bigint_shr(a, -b); }
    if (b > 0x10000000u) { return std::nullopt; }
    return a << static_cast<unsigned>(b);
}

std::optional<bigint> bigint_shr(const bigint & a, const bigint & b) {
    if (b < 0) { return bigint_shl(a, -b); }
    if (b > 0x10000000u) {
        // Shifting further than the value is wide leaves 0 for a positive
        // number and -1 for a negative one: an ARITHMETIC shift, which is what
        // JavaScript specifies and what an unbounded two's-complement value
        // means. Returning 0 for both would lose the sign.
        return a < 0 ? bigint{-1} : bigint{0};
    }
    return a >> static_cast<unsigned>(b);
}

std::optional<int> bigint_compare_double(const bigint & a, double b) {
    if (std::isnan(b)) { return std::nullopt; }
    if (std::isinf(b)) { return b > 0 ? -1 : 1; } // every bigint is between the infinities
    // EXACTLY, which is the whole point. Converting the bigint to a double
    // would round it, and two values that differ by one past 2^53 would compare
    // equal - precisely the loss BigInt exists to avoid. So the DOUBLE is split
    // into its integer part and a fraction instead, and the fraction breaks the
    // tie when the integer parts agree.
    const double whole = std::trunc(b);
    const bigint rounded{whole};
    if (a < rounded) { return -1; }
    if (a > rounded) { return 1; }
    if (b > whole) { return -1; } // a == trunc(b) but b has a fraction above it
    if (b < whole) { return 1; }
    return 0;
}

double bigint_to_double(const bigint & a) {
    // Past the double range this saturates to an infinity rather than wrapping,
    // which is what `Number(10n ** 400n)` is specified to give.
    return a.convert_to<double>();
}

std::string bigint_to_string(const bigint & a, int radix) {
    if (radix == 10) { return a.str(); }
    // Only radix 10 has a direct spelling on cpp_int, so the rest is long
    // division. `toString(16)` on a hash is the case that matters.
    if (a == 0) { return "0"; }
    const bool negative = a < 0;
    bigint rest = negative ? -a : a;
    const bigint base{radix};
    static constexpr std::string_view alphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string out;
    while (rest > 0) {
        const auto digit = static_cast<std::size_t>(static_cast<std::uint64_t>(rest % base));
        out += alphabet[digit];
        rest /= base;
    }
    if (negative) { out += '-'; }
    std::ranges::reverse(out);
    return out;
}

std::uint64_t bigint_to_uint64_wrap(const bigint & a) {
    static const bigint modulus = bigint{1} << 64;
    bigint r = a % modulus;
    if (r < 0) { r += modulus; }
    return r.convert_to<std::uint64_t>();
}

bool to_bigint(context & cx, value v, bigint & out) {
    value prim = v;
    if (v.is_object_like()) {
        if (!cx.to_primitive_hint(v, "number", prim)) { return false; }
    }
    if (prim.is_kind(heap_kind::bigint)) {
        out = static_cast<bigint_object *>(prim.as_heap())->digits;
        return true;
    }
    if (prim.is_boolean()) {
        out = prim.as_boolean() ? 1 : 0;
        return true;
    }
    if (prim.is_string()) {
        const std::optional<bigint> parsed =
            bigint_from_string(static_cast<string_object *>(prim.as_heap())->text);
        if (!parsed) {
            cx.throw_error("SyntaxError", "Cannot convert this string to a BigInt");
            return false;
        }
        out = *parsed;
        return true;
    }
    cx.throw_error("TypeError",
                   "Cannot convert " + std::string{context::type_of(prim)} + " to a BigInt");
    return false;
}

} // namespace ctbrowser::script
