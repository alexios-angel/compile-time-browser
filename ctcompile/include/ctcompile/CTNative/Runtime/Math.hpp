//===- Math.hpp - The native JavaScript Math object -------------*- C++ -*-===//
//
// ctnative::Math, the Math functions native code calls, as stateless function
// objects: generated code reads `ctnative::Math.min(a, b)`, the JavaScript
// spelling. Only the functions the compiler lowers are here.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Number.hpp"

#include <cmath>

namespace ctnative {

/// Math.min (ECMA-262 21.3.2.25) of two Numbers: NaN when either is NaN, and
/// -0 is smaller than +0, which `std::min` gets wrong on both counts.
struct min_method {
    js_num operator()(js_num left, js_num right) const {
        const double a = left.value(), b = right.value();
        if (std::isnan(a) || std::isnan(b)) {
            return js_num{js_nan_t{}};
        }
        if (a == 0.0 && b == 0.0) { return js_num{std::signbit(a) ? a : b}; }
        return js_num{a < b ? a : b};
    }
};

/// The Math object's members native code calls.
struct math_object {
    min_method min;
};
inline constexpr math_object Math{};

} // namespace ctnative
