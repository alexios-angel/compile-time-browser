//===- Number.hpp - The native JavaScript Number ----------------*- C++ -*-===//
//
// js_num, the C++ type generated native code uses for a JavaScript Number,
// with JavaScript's arithmetic, bitwise and shift semantics, and the Number
// constructor native code calls.
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

/// The Number constructor's members native code calls: generated code reads
/// `ctnative::Number(s)`. `Number(value)` (ECMA-262 21.1.1.1) is ToNumber for
/// every value a native carrier holds (none is a BigInt), so the call
/// operator forwards to the to_number overload set, which ADL finds for each
/// carrier at the call. No prototype method or static is lowered yet.
struct number_constructor {
    template <class T> js_num operator()(const T & value) const { return to_number(value); }
};
inline constexpr number_constructor Number{};

} // namespace ctnative
