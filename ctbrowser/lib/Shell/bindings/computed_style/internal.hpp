//===- computed_style/internal.hpp - Computed style helpers -----*- C++ -*-===//
//
// Private to lib/Shell/bindings/computed_style/; not installed.
//
// dom_bindings' `getComputedStyle`: what the cascade and layout between them
// decided about one element. tools/check/css-parity.py compares it against
// Chrome property by property.
//
// It reads three sources, because `style::computed_style` is not a computed
// style despite the name: engine::resolve produces only the declarations that
// matched, still as text, with no inheritance and no initial values
// (include/ctbrowser/style/computed.hpp; unittests/unit/style/selectors.cpp's
// test_unmatched_element_gets_empty_style). So:
//
//   the style map   every keyword-valued property - display, position, ...
//   the box tree    lengths, resolved the way layout resolved them
//   the fragment    used sizes, i.e. what the element actually got
//
// Reading the box tree rather than re-parsing the text matters: answering
// "how wide is 50%" in two places is how the two answers start to differ.
// What layout used is the computed value here.
//
// A fourth source: the property table in style/css/properties.hpp, which
// lists every property, whether it is a shorthand, and its initial value. The
// three sources above only know properties something declared; any other
// property still has a computed value, its initial one.
//
// The object publishes both spellings of every name. `css/css-values` asks
// with the hyphenated CSS name and `css/cssom` with the IDL one, and
// computed-testcommon.js asserts `property in getComputedStyle(target)` before
// it reads. The conversion is style/css/properties.hpp's, because `el.style`
// needs the same one.
//
// Resolved versus computed: CSSOM §6.7.2's resolved value is the used value
// for a handful of properties and the computed value for everything else, and
// also for those few whenever there is no used value: an element with
// `display: none` or `display: contents`, a non-replaced inline, an inset on a
// static box, and an inset on an over-constrained box. entries.cpp's
// `computed_length` callback is that half (a percentage or `auto` survives,
// everything else becomes absolute px), and resolved_geometry asks for it by
// name at each of those points rather than as a fallback for a null pointer.
//
// The object is live, as CSSOM requires: every property is an accessor that
// re-derives from the current trees, so a test holding `let cs = gcs(el)`
// across a write reads the state after it. What outlives the call is the
// element's node_id and a shared cache; raw pointers into the box and fragment
// trees are gathered inside `computed_style_entries` and never escape it,
// because the next layout, which can happen inside a script turn, frees them.
//
// Inheritance is the cascade's: an inherited property is a plain `get` on the
// element's own style, not a walk of the DOM ancestors.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ctbrowser::shell::detail {

// Helpers shared by more than one file of bindings/computed_style/, defined in
// serialize.cpp.

/// Resolves a border width (`thin`, `medium`, `thick` or a length) to px.
[[nodiscard]] float border_width_px(std::string_view text, float font_size);
/// Serialises a CSS number (CSSOM §6.7.2): shortest round-trip decimal.
[[nodiscard]] std::string number_text(float value);
/// number_text followed by `px`.
[[nodiscard]] std::string px_text(float value);
/// px_text for a value produced by layout arithmetic, snapped to 1/64 px first.
[[nodiscard]] std::string used_px_text(float value);
/// Serialises a colour as `rgb(r, g, b)`, or `rgba(...)` when not opaque.
[[nodiscard]] std::string color_text(color c);
/// The colour a CSS system colour keyword (or `rebeccapurple`) names, or
/// nullopt.
[[nodiscard]] std::optional<color> system_color(std::string_view text);
/// What a transform's relative arguments resolve against: the element's font
/// sizes for `em` and `rem`, and its reference box (the border box) for a
/// translation's percentage. A negative box leaves a percentage unresolved.
struct transform_basis {
    float font_size = 16, root_font_size = 16;
    float width = -1, height = -1;
};
/// The product of a transform list's functions as a column-major 4x4 matrix,
/// or nullopt for `none` or a list this reader cannot model.
[[nodiscard]] std::optional<std::array<double, 16>> transform_matrix3d(
    std::string_view text, const transform_basis & basis = {});
/// transform_matrix3d's result as `matrix()`'s six values, or nullopt when the
/// product is not 2D.
[[nodiscard]] std::optional<std::array<double, 6>> transform_matrix(
    std::string_view text, const transform_basis & basis = {});
/// The resolved `transform` value: `matrix(...)` when 2D, `matrix3d(...)`
/// otherwise, or the text unchanged when it cannot be multiplied out.
[[nodiscard]] std::string transform_matrix_text(std::string_view text,
                                                const transform_basis & basis = {});
/// Lowercases a keyword value and collapses whitespace runs to one space;
/// quoted strings keep their case and spacing. `keep_case` is for a value that
/// names something, whose idents are case-sensitive: only whitespace collapses.
[[nodiscard]] std::string collapse_keyword(std::string_view text, bool keep_case = false);
/// A box-shadow (`box`) or text-shadow list as its resolved value: colour
/// first, then px lengths, then `inset`.
[[nodiscard]] std::string shadow_text(std::string_view text, float font_size, bool box);

} // namespace ctbrowser::shell::detail
