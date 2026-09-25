#pragma once

#include "ctbrowser/core/number.hpp"

#include <cmath>
#include <compare>
#include <limits>
#include <type_traits>

namespace ctnative {

// An explicit construction token; NaN remains a JavaScript Number.
struct js_nan_t {};

// Other representations need proofs that preserve JavaScript Number semantics.
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

} // namespace ctnative
