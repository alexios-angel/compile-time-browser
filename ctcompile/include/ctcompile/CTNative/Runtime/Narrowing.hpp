//===- Narrowing.hpp - Values a dominating test narrowed --------*- C++ -*-===//
//
// The conversions `ctnative.refine` lowers to: a value whose carrier holds
// several kinds, inside a branch whose `typeof` test or truthiness proved one
// of them. Generated code reads `ctnative::as_string(x)` or
// `ctnative::as_number(x)`, and a Map key or write a proof narrowed reads
// `ctnative::as_boolean(x)` too.
//
// ctnative.hpp includes this header last, after the carriers it converts, and
// it does not include ctnative.hpp back: a test driver that inlines
// ctnative.hpp's text into the generated file (CTNative/harness.py,
// inline_runtime) would otherwise get the runtime a second time from the real
// header.
//
//===----------------------------------------------------------------------===//

#pragma once

namespace ctnative {

/// The String a dominating `typeof x == "string"` test (ECMA-262 13.5.3, the
/// typeof operator) or truthiness branch proved `x` to be. A String carrier is
/// already one; an optional or a String union gives its String, which the
/// proof makes the kind it holds. A union holding another kind terminates.
inline const js_string & as_string(const js_string & value) {
    return value;
}
inline js_string as_string(const nullable_string & value) {
    return js_string{value.value};
}
inline const js_string & as_string(const number_string & value) {
    return detail::present<js_string>(value);
}
inline const js_string & as_string(const nullable_number_string & value) {
    return detail::present<js_string>(value);
}
inline const js_string & as_string(const boolean_string & value) {
    return detail::present<js_string>(value);
}

/// The Number a dominating `typeof x == "number"` test proved `x` to be.
inline js_num as_number(js_num value) {
    return value;
}
inline js_num as_number(const number_string & value) {
    return detail::present<js_num>(value);
}
inline js_num as_number(const nullable_number_string & value) {
    return detail::present<js_num>(value);
}

/// The Boolean a proof about a Boolean-or-String value names.
inline js_boolean_t as_boolean(const boolean_string & value) {
    return detail::present<js_boolean_t>(value);
}

} // namespace ctnative
