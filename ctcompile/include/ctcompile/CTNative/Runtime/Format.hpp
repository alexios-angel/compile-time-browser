//===- Format.hpp - std::format for the native carriers ---------*- C++ -*-===//
//
// std::formatter for every primitive carrier, so `{}` prints exactly what
// JavaScript's ToString writes (1e+21, "0" for -0, NaN, Infinity, true,
// null, undefined), through the same to_js_string overload set String(x)
// calls. ctnative::format and ctnative::println are std::format and
// std::println with that spelling: format joins the halves of a surrogate
// pair split across two pieces, as String concatenation does, and println
// prints through print_line (Output.hpp), the stream every native line uses.
// libstdc++ 13, which both of the suite's compilers build against, has no
// <print>.
//
// Only programs that format include this header: <format> costs every
// translation unit that parses it.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctnative.hpp"

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace ctnative {

/// A carrier JavaScript's ToString takes without running JavaScript. The
/// types are listed rather than tested with to_js_string, whose implicit
/// converting constructors (a pointer converts to bool, and bool to
/// nullable_scalar) would claim `const char *` from the standard formatter.
template <class T>
concept js_formattable = primitive_equality_operand<T>;

/// std::format with every argument spelled by ToString, as a String: a
/// template literal's value.
template <class... Args>
js_string format(std::format_string<const Args &...> text, const Args &... args) {
    std::string out = std::format(text, args...);
    ctbrowser::join_surrogates(out);
    return js_string{std::move(out)};
}

/// std::println with every argument spelled by ToString, printed through
/// print_line. Once both compilers' standard library has <print>, the body is
/// `std::println(text, args...);`.
template <class... Args>
void println(std::format_string<const Args &...> text, const Args &... args) {
    print_line(std::format(text, args...));
}

} // namespace ctnative

/// `{}` of a carrier is its ToString; the standard specification (fill,
/// alignment, width) applies to that text.
template <class T>
requires ctnative::js_formattable<T>
struct std::formatter<T, char> : std::formatter<std::string_view, char> {
    auto format(const T & value, std::format_context & context) const {
        return std::formatter<std::string_view, char>::format(ctnative::to_js_string(value).value(),
                                                              context);
    }
};
