#pragma once

#include <cmath>
#include <compare>
#include <type_traits>

namespace ctnative {

// Other representations need proofs that preserve JavaScript Number semantics.
template <class T>
requires std::is_same_v<T, double>
class js_basic_num {
    T number = 0.0;

public:
    constexpr js_basic_num() = default;
    template <class U>
    requires std::is_same_v<U, T>
    explicit constexpr js_basic_num(U value) : number(value) {}

    explicit constexpr operator T() const { return number; }
    explicit constexpr operator bool() const { return number != 0.0 && !std::isnan(number); }
    constexpr T value() const { return number; }

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
    friend constexpr bool operator==(js_basic_num, js_basic_num) = default;
    friend constexpr auto operator<=>(js_basic_num, js_basic_num) = default;
};

using js_num = js_basic_num<double>;

} // namespace ctnative
