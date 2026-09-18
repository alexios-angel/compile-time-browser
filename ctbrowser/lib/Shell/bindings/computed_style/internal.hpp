#pragma once
// Private to lib/Shell/bindings/computed_style/ - not installed.

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

// dom_bindings - `getComputedStyle`. What the cascade and layout between them
// decided about one element, which is the channel tools/check/css-parity.py
// compares against Chrome property by property.
//
// WHY THIS NEEDS THREE SOURCES. `style::computed_style` is not a computed style
// despite the name: engine::resolve produces only the declarations that MATCHED,
// still as text, with no inheritance and no initial values
// (include/ctbrowser/style/computed.hpp, and unittests/unit/style_selectors.cpp asserts
// that an unmatched element "resolves to nothing"). So:
//
//   the style map   every keyword-valued property - display, position, ...
//   the box tree    lengths, resolved the way LAYOUT resolved them
//   the fragment    used sizes, i.e. what the element actually got
//
// Reading the box tree rather than re-parsing the text is the point: asking "how
// wide is 50%" twice, in two places, is how the two answers start to differ.
// What layout used IS the computed value here.
//
// ...AND A FOURTH: style/css/properties.hpp's table says WHICH properties exist,
// which of them are shorthands, and what each one's initial value is. The three
// sources above can only answer about a property something DECLARED, so every
// other property was absent from the object entirely - which is what
// `getComputedStyle(el).someProperty === undefined` meant in 1,034 css/cssom
// subtests (docs/css-conformance.md §4). A property nothing declared has a
// computed value all the same, and it is the initial one.
//
// THE OBJECT PUBLISHES BOTH SPELLINGS. `css/css-values` asks with the hyphenated
// CSS name and `css/cssom` with the IDL one, and computed-testcommon.js asserts
// `property in getComputedStyle(target)` BEFORE it reads - so a name absent under
// either spelling fails the test before a value is ever compared. The conversion
// itself is style/css/properties.hpp's, because `el.style` needs the same one.
//
// --- WHAT IS RESOLVED AND WHAT IS COMPUTED -------------------------------
//
// CSSOM §6.7.2's resolved value is the USED value for a handful of properties
// and the COMPUTED value for everything else - AND for those same few whenever
// there is no used value to report. That "otherwise" is not an edge case: an
// element with `display: none`, one with `display: contents`, and a
// non-replaced inline all generate no box that has a width, and CSSOM says each
// of them reports the computed value. So does an inset on a static box, and so
// does an inset on a box that is OVER-CONSTRAINED. `computed_length` below is
// that half - a percentage survives, `auto` survives, and everything else
// becomes an absolute px - and it is asked for by name at each of those points
// rather than being the fallback when a pointer happens to be null.
//
// THE OBJECT IS LIVE. CSSOM says `getComputedStyle` returns a live
// CSSStyleDeclaration, and every property on it is an ACCESSOR that re-derives
// from the current trees - a test holding `let cs = gcs(el)` across a write
// must read the state after it. What outlives the call is the element's
// node_id and a shared cache; the raw pointers into the box and fragment trees
// are gathered inside `computed_style_entries` and never escape it, because
// the next layout - which happens INSIDE a script turn - frees them.
//
// INHERITANCE IS THE CASCADE'S: an inherited property is a plain `get` on the
// element's own style, not a walk of the DOM ancestors.

namespace ctbrowser::shell::detail {

// --- helpers shared by more than one file of bindings/computed_style/ --------
// Defined in serialize.cpp.
[[nodiscard]] float border_width_px(std::string_view text, float font_size);
[[nodiscard]] std::string number_text(float value);
[[nodiscard]] std::string px_text(float value);
[[nodiscard]] std::string used_px_text(float value);
[[nodiscard]] std::string color_text(color c);
[[nodiscard]] std::optional<color> system_color(std::string_view text);
[[nodiscard]] std::optional<std::array<double, 6>> transform_matrix(std::string_view text);
[[nodiscard]] std::string transform_matrix_text(std::string_view text);
[[nodiscard]] std::string collapse_keyword(std::string_view text);
[[nodiscard]] std::string shadow_text(std::string_view text, float font_size, bool box);

} // namespace ctbrowser::shell::detail
