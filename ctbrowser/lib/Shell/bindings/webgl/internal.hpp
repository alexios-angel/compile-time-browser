#pragma once
// Private to lib/Shell/bindings/webgl/. NOT installed and in no file set:
// include/ctbrowser/shell/bindings.hpp declares dom_bindings whole, and this
// exists only so its WebGL half can be more than one file - it was 1,431
// lines in one until 2026-09-08. The includes are webgl.cpp's, so every
// file here sees exactly what that one saw.

#include <algorithm>
#include <cstring>
#include <ctbrowser/shell/bindings.hpp>
#include <utility>

// `canvas.getContext('webgl')` - the JavaScript surface over shell/page/webgl.hpp.
//
// IN ITS OWN FILE because it is a different kind of code from the rest of the
// bindings: seventy-nine methods that almost all do one thing, plus a constant
// table. Mixed into bindings.cpp it would double that file and bury the DOM.
//
// This layer is DELIBERATELY THIN. It unpacks arguments, hands them to
// webgl_context, and packs the answer back; every decision about what a call
// MEANS lives next door in webgl.cpp, where it is testable without a page. If
// something here is more than a few lines, it is in the wrong file.
//
// A WebGL object - a buffer, a texture, a program - is a JS object carrying an
// integer id. The page only ever passes them back, so the object is a handle and
// the integer is what the context knows.

namespace ctbrowser::shell::detail {

// A double as an unsigned index, WITHOUT the undefined behaviour. Every entry
// point here takes numbers from a page, and `undefined` arrives as NaN while
// `1/0` arrives as infinity - neither of which CONVERTS to an integer. The cast
// is undefined behaviour rather than a large number, and UBSan found one
// reaching enum_at where a page passed a null location.
[[nodiscard]] inline std::uint32_t to_index(double d) noexcept {
    if (!(d >= 0.0)) { return 0; } // false for NaN as well as for negatives
    return d >= 4294967295.0 ? 4294967295u : static_cast<std::uint32_t>(d);
}

[[nodiscard]] inline std::uint32_t id_of(context & cx, value v) {
    if (!v.is_object()) { return 0; }
    const value held = cx.lookup_property(v, "__id");
    return held.is_undefined() ? 0 : to_index(context::to_number(held));
}

[[nodiscard]] inline double number_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}

[[nodiscard]] inline std::uint32_t enum_at(std::span<value> args, std::size_t i) {
    return to_index(number_at(args, i));
}

[[nodiscard]] inline int int_at(std::span<value> args, std::size_t i) {
    const double d = number_at(args, i);
    if (std::isnan(d)) { return 0; }
    return static_cast<int>(std::clamp(d, static_cast<double>(std::numeric_limits<int>::min()),
                                       static_cast<double>(std::numeric_limits<int>::max())));
}

} // namespace ctbrowser::shell::detail
