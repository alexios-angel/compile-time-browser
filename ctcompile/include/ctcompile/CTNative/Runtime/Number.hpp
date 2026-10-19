//===- Number.hpp - The native JavaScript Number ----------------*- C++ -*-===//
//
// js_num, the C++ type generated native code uses for a JavaScript Number,
// with JavaScript's arithmetic, bitwise and shift semantics, and the Number
// constructor native code calls.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctbrowser/core/number.hpp"
#include "ctbrowser/core/number_format.hpp"

#include <cmath>
#include <compare>
#include <concepts>
#include <limits>
#include <type_traits>

namespace ctnative {

/// Tag type for constructing a NaN js_num explicitly; NaN is still a
/// JavaScript Number, not an absence marker.
struct js_nan_t {};

template <class T>
requires std::is_same_v<T, char>
class js_basic_string;

/// A JavaScript Number in generated native code, backed by double. Bitwise
/// operators and shifts convert through ToInt32/ToUint32 and mask shift
/// counts to five bits, as JavaScript does.
class js_num {
    double number = 0.0;

public:
    constexpr js_num() = default;
    explicit constexpr js_num(js_nan_t) : number(std::numeric_limits<double>::quiet_NaN()) {}
    template <class T>
    requires std::same_as<T, double>
    explicit constexpr js_num(T value) : number(value) {}

    explicit constexpr operator double() const { return number; }
    explicit constexpr operator bool() const { return number != 0.0 && !std::isnan(number); }
    constexpr double value() const { return number; }
    /// Number.prototype.toString with no radix (ECMA-262 21.1.3.6):
    /// Number::toString, so 1e21, "0" for -0, "NaN" and "Infinity". Defined
    /// in String.hpp.
    js_basic_string<char> toString() const;
    /// Number.prototype.toFixed (ECMA-262 21.1.3.3): `digits` after
    /// ToIntegerOrInfinity must be 0..100, else a RangeError. Defined in
    /// ctnative.hpp, since it throws: the compiler folds constants with this
    /// header and String.hpp, and builds without exceptions.
    js_basic_string<char> toFixed(js_num digits) const;
    std::int32_t to_int32() const { return ctbrowser::number_to_int32(number); }
    std::uint32_t to_uint32() const { return ctbrowser::number_to_uint32(number); }

    constexpr js_num operator+() const { return *this; }
    constexpr js_num operator-() const { return js_num{-number}; }
    friend constexpr js_num operator+(js_num left, js_num right) {
        return js_num{left.number + right.number};
    }
    friend constexpr js_num operator-(js_num left, js_num right) {
        return js_num{left.number - right.number};
    }
    friend constexpr js_num operator*(js_num left, js_num right) {
        return js_num{left.number * right.number};
    }
    friend constexpr js_num operator/(js_num left, js_num right) {
        return js_num{left.number / right.number};
    }
    /// `left % right` (ECMA-262 6.1.6.1.6, Number::remainder): the remainder
    /// with the dividend's sign, which is C's fmod. A zero divisor or an
    /// infinite dividend gives NaN, an infinite divisor leaves a finite
    /// dividend unchanged, and a -0 dividend stays -0.
    friend js_num operator%(js_num left, js_num right) {
        return js_num{std::fmod(left.number, right.number)};
    }
    js_num operator~() const { return js_num{static_cast<double>(~to_int32())}; }
    friend js_num operator&(js_num left, js_num right) {
        return js_num{static_cast<double>(left.to_int32() & right.to_int32())};
    }
    friend js_num operator|(js_num left, js_num right) {
        return js_num{static_cast<double>(left.to_int32() | right.to_int32())};
    }
    friend js_num operator^(js_num left, js_num right) {
        return js_num{static_cast<double>(left.to_int32() ^ right.to_int32())};
    }
    friend js_num operator<<(js_num left, js_num right) {
        const auto bits = left.to_uint32() << (right.to_uint32() & 31u);
        return js_num{static_cast<double>(static_cast<std::int32_t>(bits))};
    }
    friend js_num operator>>(js_num left, js_num right) {
        return js_num{static_cast<double>(left.to_int32() >> (right.to_uint32() & 31u))};
    }
    js_num unsigned_shift_right(js_num right) const {
        return js_num{static_cast<double>(to_uint32() >> (right.to_uint32() & 31u))};
    }
    friend constexpr bool operator==(js_num, js_num) = default;
    friend constexpr auto operator<=>(js_num, js_num) = default;
};

template <class T>
requires std::same_as<T, double>
using js_basic_num = js_num;

/// Number.parseInt (ECMA-262 21.1.2.13), the same function as the global
/// parseInt (19.2.5): `text` is a String carrier (its `value()` is the text)
/// and `radix` goes through ToInt32, so a missing radix is 0.
struct parse_int_function {
    template <class String>
    js_num operator()(const String & text, js_num radix = js_num{0.0}) const {
        return js_num{ctbrowser::parse_int(text.value(), radix.to_int32())};
    }
};

/// Number.parseFloat (ECMA-262 21.1.2.12), the same function as the global
/// parseFloat (19.2.4): the longest StrDecimalLiteral prefix of `text` after
/// its leading whitespace, NaN when there is none. `text` is a String carrier.
struct parse_float_function {
    template <class String> js_num operator()(const String & text) const {
        return js_num{ctbrowser::string_to_number_prefix(text.value())};
    }
};

/// The Number constructor's members native code calls: generated code reads
/// `ctnative::Number(s)`, `ctnative::Number.parseInt(s, 10)` and
/// `ctnative::Number.parseFloat(s)`.
/// `Number(value)` (ECMA-262 21.1.1.1) is ToNumber for every value a native
/// carrier holds (none is a BigInt), so the call operator forwards to the
/// to_number overload set, which ADL finds for each carrier at the call.
struct number_constructor {
    template <class T> js_num operator()(const T & value) const { return to_number(value); }
    /// Static, so the constructor stays an empty, stateless object.
    static constexpr parse_int_function parseInt{};
    static constexpr parse_float_function parseFloat{};
};
inline constexpr number_constructor Number{};

/// The global parseInt and parseFloat (ECMA-262 19.2.5 and 19.2.4), the same
/// functions as Number.parseInt and Number.parseFloat.
inline constexpr parse_int_function parseInt{};
inline constexpr parse_float_function parseFloat{};

} // namespace ctnative
