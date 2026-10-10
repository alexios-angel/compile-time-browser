//===- bigint.hpp - BigInt arithmetic ---------------------------*- C++ -*-===//
//
// Arbitrary-precision integer arithmetic for BigInt, on Boost.Multiprecision.
//
// The type is signed and unbounded, which is exactly BigInt's semantics: no
// width, no wrapping, no rounding. value/base.hpp already carries the include
// and declares `bigint` itself, because `bigint_object` stores one, so this
// header adds nothing to a consumer's cost.
//
// `bigint` is `cpp_int`; the functions below are written against the shared
// Boost.Multiprecision interface, not cpp_int specifics, so the backend stays
// swappable.
//
// Boost.Multiprecision was rejected for `Math` because it is much slower than
// hardware floats and its correct rounding differs from V8. Neither applies
// here: arbitrary-precision integers have no hardware alternative and integer
// arithmetic is exact. It is header-only, so the cross-build needs nothing.
//
// A `std::nullopt` return means the operation is not defined and the caller
// must throw the error the specification names: division or remainder by zero
// (RangeError), a negative exponent (RangeError), a negative shift count that
// cannot be represented, and the conversions that refuse.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <ctbrowser/script/value.hpp>

namespace ctbrowser::script {

// `bigint` itself is declared in value/base.hpp, beside the object that stores one.

// --- Construction ------------------------------------------------------===//

/// Parses a decimal, hex, octal or binary literal without the trailing `n`,
/// optionally carrying `_` separators. Returns nullopt when it is not an
/// integer literal, which is how `1.5n` and `1e3n` are refused.
[[nodiscard]] std::optional<bigint> bigint_from_literal(std::string_view text);

/// ToBigInt of a Number: only an integral, finite double converts. 1.5, NaN
/// and the infinities are a RangeError, which is nullopt here.
[[nodiscard]] std::optional<bigint> bigint_from_double(double v);

/// ToBigInt of a String: the whole string must be an integer literal, and an
/// empty or all-whitespace string is 0n. nullopt is a SyntaxError.
[[nodiscard]] std::optional<bigint> bigint_from_string(std::string_view text);

// --- Arithmetic --------------------------------------------------------===//
// Division truncates toward zero and the remainder takes the sign of the
// dividend, both as JavaScript specifies and as C++ integer division already
// does: `5n / 2n` is 2n and `-5n % 3n` is -2n.

/// The widest result allowed, 2^30 bits. A wider result is a RangeError
/// (nullopt), estimated from the operands before computing: `3n ** 4294967295n`
/// would be 6.8 billion bits. V8 draws the same line.
inline constexpr std::uint64_t max_bigint_bits = std::uint64_t{1} << 30;

[[nodiscard]] std::optional<bigint> bigint_mul(const bigint & a, const bigint & b);
[[nodiscard]] std::optional<bigint> bigint_div(const bigint & a, const bigint & b);
[[nodiscard]] std::optional<bigint> bigint_rem(const bigint & a, const bigint & b);
[[nodiscard]] std::optional<bigint> bigint_pow(const bigint & a, const bigint & b);
[[nodiscard]] std::optional<bigint> bigint_shl(const bigint & a, const bigint & b);
[[nodiscard]] std::optional<bigint> bigint_shr(const bigint & a, const bigint & b);

// --- Comparison against a Number ---------------------------------------===//
/// Three-way comparison of a bigint with a double (-1, 0 or 1). Arithmetic
/// between a bigint and a number is refused, but comparison is not: `1n < 2`
/// and `1n == 1` are true. Returns nullopt when the double is NaN, which makes
/// every relational operator false.
///
/// The comparison is exact: a bigint past 2^53 must not be rounded into
/// equality with the double beside it.
[[nodiscard]] std::optional<int> bigint_compare_double(const bigint & a, double b);

/// ToBigInt64 / ToBigUint64 (ECMA-262 7.1.15-16) share this: the value modulo
/// 2^64 as the raw word a BigInt64Array element or a DataView's setBigInt64 stores.
/// `bigint{static_cast<std::int64_t>(raw)}` and `bigint{raw}` are the reads.
[[nodiscard]] std::uint64_t bigint_to_uint64_wrap(const bigint & a);

class context;
/// ToBigInt (ECMA-262 7.1.13): ToPrimitive with hint number, then a BigInt is
/// itself, a Boolean 0n/1n, a String parses (SyntaxError when it is not an
/// integer), and a Number (unlike `BigInt(1)`) is a TypeError, as are
/// undefined, null and a Symbol. Returns false with the exception pending.
[[nodiscard]] bool to_bigint(context & cx, value v, bigint & out);

/// The double nearest this value, for `Number(1n)`. Saturates to an infinity
/// when the magnitude is past what a double can hold.
[[nodiscard]] double bigint_to_double(const bigint & a);

/// The text in `radix` (decimal by default), for `String(1n)` and
/// `(1n).toString(radix)`.
[[nodiscard]] std::string bigint_to_string(const bigint & a, int radix = 10);

} // namespace ctbrowser::script
