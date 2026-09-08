#pragma once
// Private to lib/Shell/bindings/computed_style/. NOT installed and in no file
// set: include/ctbrowser/shell/bindings.hpp declares dom_bindings whole, and
// this exists only so getComputedStyle can be more than one file - it was
// 1,326 lines in one until 2026-09-08. The includes are computed_style.cpp's,
// so every file here sees exactly what that one saw.

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
// (include/ctbrowser/style/computed.hpp, and unittests/unit/style_basics.cpp asserts
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
// from the current trees. It was a snapshot, so a test holding
// `let cs = gcs(el)` across a write read the state from before it -
// getComputedStyle-display-none-001, -002 and -resolved-min-max-clamping are
// all written exactly that way and could not pass at all. What outlives the
// call is the element's node_id and a shared cache; the raw pointers into the
// box and fragment trees are gathered inside `computed_style_entries` and never
// escape it, because the next layout - which now happens INSIDE a script turn -
// frees them.
//
// INHERITANCE IS NOT DONE HERE ANY MORE. This file used to walk DOM ancestors for an
// inherited property, because the cascade produced only the declarations that MATCHED
// and inheritance happened in four other places downstream. That made this the fifth
// ad-hoc mechanism for one idea. The cascade inherits now, so an inherited property is
// a plain `get` on the element's own style, and the walk is gone.

namespace ctbrowser::shell::detail {

// --- helpers shared by more than one file of bindings/computed_style/ --------
//
// Everything here was in an anonymous namespace of computed_style.cpp. It
// gained external linkage when that file was split, and nothing else: the
// bodies are in serialize.cpp, which explains each one, and entries.cpp is
// what calls them.
[[nodiscard]] float border_width_px(std::string_view text, float font_size);
[[nodiscard]] std::string px_text(float value);
[[nodiscard]] std::string used_px_text(float value);
[[nodiscard]] std::string color_text(color c);
[[nodiscard]] std::string font_family_text(std::string_view text);
[[nodiscard]] std::string collapse_keyword(std::string_view text);

} // namespace ctbrowser::shell::detail
