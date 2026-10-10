//===- internal.hpp - WebGL binding helpers ---------------------*- C++ -*-===//
//
// Helpers shared by the files that implement `canvas.getContext('webgl')`, the
// JavaScript surface over shell/page/webgl.hpp. Private to
// lib/Shell/bindings/webgl/ and not installed.
//
// This layer is deliberately thin: it unpacks arguments, hands them to
// webgl_context, and packs the result back. Every decision about what a call
// means lives in lib/Shell/page/webgl.cpp, where it is testable without a page.
//
// A WebGL object (a buffer, a texture, a program) is a JS object carrying an
// integer id in its `__id` property. The page only ever passes these objects
// back, so the object is a handle and the integer is what the context knows.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstring>
#include <ctbrowser/shell/bindings.hpp>
#include <utility>

namespace ctbrowser::shell::detail {

/// Converts a page-supplied double to an unsigned index without undefined
/// behaviour: NaN (from `undefined`) and negatives give 0, and values at or
/// above 2^32 - 1 (including infinity) saturate to 4294967295. A plain cast of
/// NaN or an out-of-range double is undefined behaviour.
[[nodiscard]] inline std::uint32_t to_index(double d) noexcept {
    if (!(d >= 0.0)) { return 0; } // false for NaN as well as for negatives
    return d >= 4294967295.0 ? 4294967295u : static_cast<std::uint32_t>(d);
}

/// The integer id of a WebGL object handle (its `__id` property), or 0 when
/// `v` is not an object or has no id. 0 is the GL "no object" name.
[[nodiscard]] inline std::uint32_t id_of(context & cx, value v) {
    if (!v.is_object()) { return 0; }
    const value held = cx.lookup_property(v, "__id");
    return held.is_undefined() ? 0 : to_index(context::to_number(held));
}

/// Argument `i` as a GL enum (missing or invalid numbers become 0).
[[nodiscard]] inline std::uint32_t enum_at(std::span<value> args, std::size_t i) {
    return to_index(arg_number(args, i));
}

/// enum_at() as an `int`, the type raster::gl::device takes GL enums as.
[[nodiscard]] inline int int_enum_at(std::span<value> args, std::size_t i) {
    return static_cast<int>(enum_at(args, i));
}

/// Converts a double to an int, saturating at the int range; NaN gives 0.
[[nodiscard]] inline int to_int(double d) noexcept {
    if (std::isnan(d)) { return 0; }
    return static_cast<int>(std::clamp(d, static_cast<double>(std::numeric_limits<int>::min()),
                                       static_cast<double>(std::numeric_limits<int>::max())));
}

/// Argument `i` as a saturated int (see to_int()).
[[nodiscard]] inline int int_at(std::span<value> args, std::size_t i) {
    return to_int(arg_number(args, i));
}

} // namespace ctbrowser::shell::detail
