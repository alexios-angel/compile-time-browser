//===- Number.hpp - The native JavaScript Number ----------------*- C++ -*-===//
//
// js_num, the C++ type generated native code uses for a JavaScript Number,
// with JavaScript's arithmetic, bitwise and shift semantics, and the Math
// functions native code calls by name (math_min).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctbrowser/core/number.hpp"

#include <cmath>
#include <compare>
#include <limits>
#include <type_traits>

namespace ctnative {

/// Tag type for constructing a NaN js_num explicitly; NaN is still a
/// JavaScript Number, not an absence marker.
struct js_nan_t {};

/// A JavaScript Number in generated native code. Only `double` is allowed for
/// now: any other representation would need a proof that it preserves Number
/// semantics. Bitwise operators and shifts convert through ToInt32/ToUint32
/// and mask shift counts to five bits, as JavaScript does.
template <class T>
requires std::is_same_v<T, double>
class js_basic_num {
    T number = 0.0;

public:
    constexpr js_basic_num() = default;
    explicit constexpr js_basic_num(js_nan_t) : number(std::numeric_limits<T>::quiet_NaN()) {}
    template <class U>
    requires std::is_same_v<U, T>
    explicit constexpr js_basic_num(U value) : number(value) {}

    explicit constexpr operator T() const { return number; }
    explicit constexpr operator bool() const { return number != 0.0 && !std::isnan(number); }
    constexpr T value() const { return number; }
    std::int32_t to_int32() const { return ctbrowser::number_to_int32(number); }
    std::uint32_t to_uint32() const { return ctbrowser::number_to_uint32(number); }

    constexpr js_basic_num operator+() const { return *this; }
    constexpr js_basic_num operator-() const { return js_basic_num{-number}; }
    friend constexpr js_basic_num operator+(js_basic_num left, js_basic_num right) {
        return js_basic_num{left.number + right.number};
    }
    friend constexpr js_basic_num operator-(js_basic_num left, js_basic_num right) {
        return js_basic_num{left.number - right.number};
    }
    friend constexpr js_basic_num operator*(js_basic_num left, js_basic_num right) {
        return js_basic_num{left.number * right.number};
    }
    friend constexpr js_basic_num operator/(js_basic_num left, js_basic_num right) {
        return js_basic_num{left.number / right.number};
    }
    js_basic_num operator~() const { return js_basic_num{static_cast<T>(~to_int32())}; }
    friend js_basic_num operator&(js_basic_num left, js_basic_num right) {
        return js_basic_num{static_cast<T>(left.to_int32() & right.to_int32())};
    }
    friend js_basic_num operator|(js_basic_num left, js_basic_num right) {
        return js_basic_num{static_cast<T>(left.to_int32() | right.to_int32())};
    }
    friend js_basic_num operator^(js_basic_num left, js_basic_num right) {
        return js_basic_num{static_cast<T>(left.to_int32() ^ right.to_int32())};
    }
    friend js_basic_num operator<<(js_basic_num left, js_basic_num right) {
        const auto bits = left.to_uint32() << (right.to_uint32() & 31u);
        return js_basic_num{static_cast<T>(static_cast<std::int32_t>(bits))};
    }
    friend js_basic_num operator>>(js_basic_num left, js_basic_num right) {
        return js_basic_num{static_cast<T>(left.to_int32() >> (right.to_uint32() & 31u))};
    }
    js_basic_num unsigned_shift_right(js_basic_num right) const {
        return js_basic_num{static_cast<T>(to_uint32() >> (right.to_uint32() & 31u))};
    }
    friend constexpr bool operator==(js_basic_num, js_basic_num) = default;
    friend constexpr auto operator<=>(js_basic_num, js_basic_num) = default;
};

using js_num = js_basic_num<double>;

/// Math.min (ECMA-262 21.3.2.25) of two Numbers: NaN when either is NaN, and
/// -0 is smaller than +0, which `std::min` gets wrong on both counts.
inline js_num math_min(js_num left, js_num right) {
    const double a = left.value(), b = right.value();
    if (std::isnan(a) || std::isnan(b)) { return js_num{js_nan_t{}}; }
    if (a == 0.0 && b == 0.0) { return js_num{std::signbit(a) ? a : b}; }
    return js_num{a < b ? a : b};
}

} // namespace ctnative
