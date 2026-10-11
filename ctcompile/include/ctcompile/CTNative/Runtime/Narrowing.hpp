//===- Narrowing.hpp - Values a dominating test narrowed --------*- C++ -*-===//
//
// The conversions `ctnative.refine` lowers to: a value whose carrier holds
// several kinds, inside a branch whose `typeof` test or truthiness proved one
// of them. Generated code reads `ctnative::as_string(x)` or
// `ctnative::as_number(x)`.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTNative/Runtime/ctnative.hpp"

#include <variant>

namespace ctnative {

/// The String a dominating `typeof x == "string"` test (ECMA-262 13.5.3, the
/// typeof operator) or truthiness branch proved `x` to be. A String carrier is
/// already one; an optional or a variant gives its String alternative, which
/// the proof makes the one it holds.
inline const js_string & as_string(const js_string & value) {
    return value;
}
inline js_string as_string(const nullable_string & value) {
    return js_string{value.value};
}
inline const js_string & as_string(const number_string & value) {
    return std::get<js_string>(value);
}
inline const js_string & as_string(const nullable_number_string & value) {
    return std::get<js_string>(value);
}
inline const js_string & as_string(const boolean_string & value) {
    return std::get<js_string>(value);
}

/// The Number a dominating `typeof x == "number"` test proved `x` to be.
inline js_num as_number(js_num value) {
    return value;
}
inline js_num as_number(const number_string & value) {
    return std::get<js_num>(value);
}
inline js_num as_number(const nullable_number_string & value) {
    return std::get<js_num>(value);
}

} // namespace ctnative
