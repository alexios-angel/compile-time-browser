#pragma once
// The property table, the value grammar, and the canonical serialisation.
//
// See include/ctbrowser/style/css/properties.hpp for why this is conservative
// and where the three consumers are. The short version: a property this file
// does not model is accepted verbatim, so nothing that works today can start
// failing, and only a property with a real `value_kind` can refuse anything.
//
// Private to lib/Style/css/properties/. NOT installed and in no file set:
// include/ctbrowser/style/css/properties.hpp declares the whole public
// surface, and this exists only so the implementation can be more than one
// file.

#include <ctbrowser/style/css/properties.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/token.hpp>

#include <algorithm>
#include <array>
#include <boost/container/small_vector.hpp>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ctbrowser::style::css::detail {

using k = value_kind;

struct scan {
    std::vector<std::size_t> significant; // indices of the non-whitespace tokens
    bool malformed = false;               // a bad string/url, or unbalanced brackets
    bool important = false;               // a top-level `!`; `!important` is not a value
    bool substituted = false;             // holds a var()/env()
    // A function whose NAME this engine does not implement. Only `CSS.supports`
    // reads it: `el.style` still stores such a value, because CSSOM says a page
    // may set a property this engine has never heard of and read it back.
    bool unknown_function = false;
    // The blocks EOF closed: `attr(data-foo type(<color>)` is one short. A
    // serialisation writes them, or the next declaration is swallowed.
    int unclosed = 0;
};

// DOES A PERCENTAGE MEAN ANYTHING FOR THIS PROPERTY? It is the property that
// supplies §10.11's calculation context, so this is the one question a math
// function cannot answer for itself: `text-indent: min(1px, 0%)` resolves
// against a containing block and `border-left-width: min(1px, 0%)` has nothing
// to resolve against and is a syntax error, however alike the two look.
//
// A `freeform` property answers YES, and has to: the grammar is not modelled, so
// `transform: translate(50%)` and `background-position: calc(50% - 1px)` would
// both be lost to a guess.
[[nodiscard]] constexpr bool takes_percentage_of(value_kind kind) noexcept {
    switch (kind) {
    case k::length_percentage:
    case k::percentage:
    case k::number_percentage:
    case k::number_length_percentage:
    case k::position:
    case k::freeform:
    case k::color:
    case k::keyword_only: return true;
    case k::length:
    case k::number:
    case k::integer:
    case k::number_length:
    case k::angle:
    case k::time: return false;
    }
    return true;
}

// --- helpers shared by more than one file of css/properties/ ------------------
//
// Everything here was in an anonymous namespace of properties.cpp. It gained
// external linkage when that file was split, and nothing else: the bodies are
// where they were, in grammar.cpp, and this declares them.

[[nodiscard]] bool has_keyword(std::string_view set, std::string_view word);
[[nodiscard]] scan scan_tokens(const token_stream & ts);
[[nodiscard]] bool substitution_grammar_ok(const token_stream & ts);
[[nodiscard]] bool integer_slots_ok(std::string_view property, const token_stream & ts);
[[nodiscard]] bool whole_value_is_math(const token_stream & ts, const scan & found);
[[nodiscard]] bool math_type_fits(const property_syntax & p, const math_answer & answer,
                                  std::string_view text);
[[nodiscard]] bool match_position(const token_stream & ts, const scan & found, std::string & out);
// `invalid`, when given, is set for a value whose `url()` modifiers are wrong.
// [begin, end) restricts the walk to a run of tokens; `text` is what a token
// with no source span falls back to.
[[nodiscard]] std::string normalize_value_tokens(const token_stream & ts, std::string_view text,
                                                 bool * invalid = nullptr, std::size_t begin = 0,
                                                 std::size_t end = static_cast<std::size_t>(-1));
[[nodiscard]] bool match_typed(const token_stream & ts, const css_token & t,
                               const property_syntax & p, std::string & out);
// Defined in color.cpp.
[[nodiscard]] bool match_color(const token_stream & ts, const scan & found,
                               std::string_view normalized, std::string & out);
// An `<image>` list - gradients, image(), cross-fade(), light-dark(), a url()
// or an unknown image function kept as written. False when nothing in the
// list is modelled, so the caller keeps the author's bytes. Defined in
// image.cpp.
[[nodiscard]] bool match_image_list(const token_stream & ts, const scan & found, std::string & out);
// A `<filter-value-list>` for `filter` and `backdrop-filter`. Defined in
// filter.cpp.
[[nodiscard]] bool match_filter_list(const token_stream & ts, const scan & found,
                                     std::string & out);
// CSS Box Alignment 3's six longhands, and the split of a `place-*`
// shorthand into its two. Defined in alignment.cpp.
[[nodiscard]] bool match_alignment(std::string_view property, const token_stream & ts,
                                   const scan & found, std::string & out);
[[nodiscard]] bool split_place(std::string_view shorthand, std::string_view value,
                               std::string & align, std::string & justify);
// CSS Grid 2's track lists, grid lines, template areas and auto-flow; the
// split and fold of grid-row / grid-column / grid-area. Defined in grid.cpp.
// `match_grid` answers false for a property it does not model and an empty
// `out` for an invalid value.
[[nodiscard]] bool match_grid(std::string_view property, const token_stream & ts,
                              const scan & found, std::string & out);
[[nodiscard]] bool split_grid_lines(std::string_view shorthand, std::string_view value,
                                    std::vector<std::string> & out);
[[nodiscard]] std::string fold_grid_lines(std::span<const std::string> lines);
// `display`'s two-value grammar and its short forms. Defined in display.cpp.
[[nodiscard]] bool match_display(const token_stream & ts, const scan & found, std::string & out);
// The rows of the property table beyond table.cpp's core set, grouped by
// module. Defined in table_modules.cpp.
[[nodiscard]] std::span<const property_syntax> module_properties() noexcept;

} // namespace ctbrowser::style::css::detail
