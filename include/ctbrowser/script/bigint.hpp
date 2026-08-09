#pragma once
#include <optional>
#include <string>
#include <string_view>

#include <ctbrowser/script/value.hpp>

// ARBITRARY-PRECISION INTEGER ARITHMETIC, on boost::multiprecision::cpp_int.
//
// The type is signed and unbounded, which is exactly the BigInt semantic: no
// width, no wrapping, no rounding. `value.hpp` already carries the include
// because `bigint_object` stores one, so this header adds nothing to a
// consumer's cost.
//
// WHY BOOST HERE, having turned Boost.Multiprecision down for `Math` earlier:
// the objections there were that it is 400x slower than hardware and that being
// correctly rounded moves the answers AWAY from V8. Neither applies to this.
// Arbitrary precision has no hardware alternative - that is the whole point of
// the type - and integer arithmetic is exact, so there is nothing to round and
// no cross-platform question. Header-only, so the cross-build needs nothing.
//
// A `std::nullopt` return means the operation is NOT DEFINED and the caller
// must throw the error the specification names: division or remainder by zero
// (RangeError), a negative exponent (RangeError), a negative shift count that
// cannot be represented, and the conversions that refuse.

namespace ctbrowser::script {

using bigint = boost::multiprecision::cpp_int;

// --- construction ------------------------------------------------------------

// A decimal, hex, octal or binary literal WITHOUT the trailing `n`, optionally
// carrying `_` separators. nullopt when it is not an integer literal, which is
// how `1.5n` and `1e3n` are refused.
[[nodiscard]] std::optional<bigint> bigint_from_literal(std::string_view text);

// ToBigInt of a Number: only an INTEGRAL, finite double converts. 1.5, NaN and
// the infinities are a RangeError, which is nullopt here.
[[nodiscard]] std::optional<bigint> bigint_from_double(double v);

// ToBigInt of a String: the WHOLE string must be an integer literal, and an
// empty or all-whitespace string is 0n. nullopt is a SyntaxError.
[[nodiscard]] std::optional<bigint> bigint_from_string(std::string_view text);

// --- arithmetic --------------------------------------------------------------
// Division TRUNCATES toward zero and the remainder takes the sign of the
// DIVIDEND, both as JavaScript specifies and as C++ integer division already
// does: `5n / 2n` is 2n and `-5n % 3n` is -2n.

[[nodiscard]] std::optional<bigint> bigint_div(const bigint & a, const bigint & b);
[[nodiscard]] std::optional<bigint> bigint_rem(const bigint & a, const bigint & b);
[[nodiscard]] std::optional<bigint> bigint_pow(const bigint & a, const bigint & b);
[[nodiscard]] std::optional<bigint> bigint_shl(const bigint & a, const bigint & b);
[[nodiscard]] std::optional<bigint> bigint_shr(const bigint & a, const bigint & b);

// --- comparison against a Number ---------------------------------------------
// Arithmetic between a bigint and a number is refused, but COMPARISON is not -
// `1n < 2` is true and `1n == 1` is true. nullopt when the double is NaN, which
// makes every relational operator false.
//
// EXACT, deliberately: a bigint past 2^53 must not be rounded into equality
// with the double beside it, which is the one thing the type exists to prevent.
[[nodiscard]] std::optional<int> bigint_compare_double(const bigint & a, double b);

// The double NEAREST this value, for `Number(1n)`. Saturates to an infinity
// when the magnitude is past what a double can hold.
[[nodiscard]] double bigint_to_double(const bigint & a);

// The decimal text, for `String(1n)` and `(1n).toString()`.
[[nodiscard]] std::string bigint_to_string(const bigint & a, int radix = 10);

} // namespace ctbrowser::script
