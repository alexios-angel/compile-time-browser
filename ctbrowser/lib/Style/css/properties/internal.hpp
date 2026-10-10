//===- internal.hpp - Property internals ---------------------- -*- C++ -*-===//
//
// The property table, the value grammar, and the canonical serialisation.
//
// See include/ctbrowser/style/css/properties.hpp for why this is conservative
// and where the three consumers are. In short: a property this table does not
// model is accepted verbatim, and only a property with a real `value_kind` can
// refuse anything.
//
// Private to lib/Style/css/properties/; not installed and in no file set.
// include/ctbrowser/style/css/properties.hpp declares the whole public
// surface, and this header exists only so the implementation can span
// several files.
//
//===----------------------------------------------------------------------===//

#pragma once

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

/// What scan_tokens learns about a value's tokens.
struct scan {
    std::vector<std::size_t> significant; ///< Indices of the non-whitespace tokens.
    bool malformed = false;               ///< A bad string/url, or a bracket closed too often.
    bool important = false;               ///< A top-level `!`; `!important` is not a value.
    bool substituted = false;             ///< Holds a substitution function or a custom function.
    /// A function whose name this engine does not implement. Only
    /// `CSS.supports` reads it: `el.style` still stores such a value, because
    /// CSSOM lets a page set a value and read it back.
    bool unknown_function = false;
    /// The number of blocks EOF closed: `attr(data-foo type(<color>)` is one
    /// short. A serialisation must write them, or the next declaration is
    /// swallowed.
    int unclosed = 0;
};

/// Whether a percentage means anything for a property of this kind. The
/// property supplies CSS Values 4 §10.11's calculation context, so this is the
/// one question a math function cannot answer for itself: `text-indent:
/// min(1px, 0%)` resolves against a containing block, while
/// `border-left-width: min(1px, 0%)` has nothing to resolve against and is
/// invalid.
///
/// A `freeform` property answers yes: its grammar is not modelled, so refusing
/// would lose `transform: translate(50%)` and `background-position: calc(50% -
/// 1px)` to a guess.
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

// Helpers shared by the files of css/properties/. Unless marked otherwise,
// they are defined in grammar.cpp.

/// Whether `word` is in the space-separated keyword set `set`, ignoring ASCII
/// case.
[[nodiscard]] bool has_keyword(std::string_view set, std::string_view word);
/// The whitespace-separated words of `text`, a bracketed run kept whole.
[[nodiscard]] std::vector<std::string_view> words_of(std::string_view text);
/// The tokens at `at`, lowercased, when every one is an ident; nullopt otherwise.
[[nodiscard]] std::optional<std::vector<std::string>> words_of(const token_stream & ts,
                                                               std::span<const std::size_t> at);
/// One past the `)`, `]` or `}` closing the block a function or bracket at
/// `open` starts; `end`, looking no further, when it does not close.
[[nodiscard]] std::size_t matching_close(const token_stream & ts, std::size_t open,
                                         std::size_t end) noexcept;
/// A CSS string as CSSOM §2.1 "serialize a string" writes it, given the body
/// between the quotes.
[[nodiscard]] std::string string_text(std::string_view body);
/// The significant tokens of a value and what they reveal (see `scan`).
[[nodiscard]] scan scan_tokens(const token_stream & ts);
/// Checks the parse-time argument grammar of steps(), random-item(), attr(),
/// var(), inherit() and ident().
[[nodiscard]] bool substitution_grammar_ok(const token_stream & ts);
/// Rejects a non-integer literal in an <integer> slot of a freeform property.
[[nodiscard]] bool integer_slots_ok(std::string_view property, const token_stream & ts);
/// Whether the whole value is a single math function.
[[nodiscard]] bool whole_value_is_math(const token_stream & ts, const scan & found);
/// Whether a math function's result type is valid for property `p`.
[[nodiscard]] bool math_type_fits(const property_syntax & p, const math_answer & answer,
                                  std::string_view text);
/// A whole `<position>` value in canonical order.
[[nodiscard]] bool match_position(const token_stream & ts, const scan & found, std::string & out);
/// The author's tokens with numbers, strings and URLs respelled canonically.
/// `invalid`, when given, is set for a value whose `url()` modifiers are wrong.
/// [begin, end) restricts the walk to a run of tokens; `text` is what a token
/// with no source span falls back to.
[[nodiscard]] std::string normalize_value_tokens(const token_stream & ts, std::string_view text,
                                                 bool * invalid = nullptr, std::size_t begin = 0,
                                                 std::size_t end = static_cast<std::size_t>(-1));
/// One token matched as the type `p` accepts; false means "not this type".
[[nodiscard]] bool match_typed(const token_stream & ts, const css_token & t,
                               const property_syntax & p, std::string & out);
/// One component of a multi-value grammar, typed as `syntax` says: one of its
/// keywords, a single typed token, or a math function of the right type, such
/// as a `border-image-width` side or a `text-decoration-inset` half.
[[nodiscard]] bool typed_component(std::string_view part, const property_syntax & syntax,
                                   std::string & out);
/// A `<color>` value in canonical form. Defined in color.cpp.
[[nodiscard]] bool match_color(const token_stream & ts, const scan & found,
                               std::string_view normalized, std::string & out);
/// An `<image>` list: gradients, image(), cross-fade(), light-dark(), and a
/// url() or an unknown image function kept as written. False when nothing in the
/// list is modelled, so the caller keeps the author's bytes. Defined in
/// image.cpp.
[[nodiscard]] bool match_image_list(const token_stream & ts, const scan & found, std::string & out);
/// A `<filter-value-list>` for `filter` and `backdrop-filter`. Defined in
/// filter.cpp.
[[nodiscard]] bool match_filter_list(const token_stream & ts, const scan & found,
                                     std::string & out);
/// CSS Box Alignment 3's six longhands, and the split of a `place-*`
/// shorthand into its two. Defined in alignment.cpp.
[[nodiscard]] bool match_alignment(std::string_view property, const token_stream & ts,
                                   const scan & found, std::string & out);
[[nodiscard]] bool split_place(std::string_view shorthand, std::string_view value,
                               std::string & align, std::string & justify);
/// CSS Grid 2's track lists, grid lines, template areas and auto-flow; the
/// split and fold of grid-row / grid-column / grid-area. Defined in grid.cpp.
/// `match_grid` answers false for a property it does not model and an empty
/// `out` for an invalid value.
[[nodiscard]] bool match_grid(std::string_view property, const token_stream & ts,
                              const scan & found, std::string & out);
[[nodiscard]] bool split_grid_lines(std::string_view shorthand, std::string_view value,
                                    std::vector<std::string> & out);
[[nodiscard]] std::string fold_grid_lines(std::span<const std::string> lines);
/// grid-template into rows, columns and areas; grid into those and auto-rows,
/// auto-columns and auto-flow. A fold is empty when the longhands cannot be
/// one value of the shorthand.
[[nodiscard]] bool split_grid_template(std::string_view value, std::vector<std::string> & out);
[[nodiscard]] std::string fold_grid_template(std::span<const std::string> v);
[[nodiscard]] bool split_grid(std::string_view value, std::vector<std::string> & out);
[[nodiscard]] std::string fold_grid(std::span<const std::string> v);
/// The border-radius shorthand and corner longhands. Defined in shorthands.cpp.
[[nodiscard]] bool match_border_radius(std::string_view value, bool shorthand, std::string & out);
/// The border-image shorthand and its longhands. Defined in shorthands.cpp.
[[nodiscard]] bool match_border_image(std::string_view property, std::string_view value,
                                      std::string & out);
/// The CSS Multicol 2 `columns` grammar, shared by validation and CSSOM
/// expansion.
[[nodiscard]] bool split_columns(std::string_view value, std::vector<std::string> & out);
/// The keyword-combination grammars (text-decoration-line, text-transform,
/// contain, font-synthesis, font-variant-*, ...) and the will-change,
/// counter-* and scroll-snap-* lists. False for a property not modelled
/// here, an empty `out` for an invalid value. Defined in keywords.cpp.
[[nodiscard]] bool match_keywords(std::string_view property, const token_stream & ts,
                                  const scan & found, std::string & out);
/// The `unicode-range` descriptor's `<urange>#` (CSS Syntax 3 §6). False for
/// an invalid value. Defined in urange.cpp.
[[nodiscard]] bool match_unicode_range(const token_stream & ts, const scan & found,
                                       std::string & out);
/// rotate, scale, translate, transform-origin and perspective-origin.
/// Defined in transforms.cpp.
[[nodiscard]] bool match_transform_property(std::string_view property, const token_stream & ts,
                                            const scan & found, std::string & out);
/// box-shadow and text-shadow. Defined in shadows.cpp; an empty `out` is an
/// invalid list.
[[nodiscard]] bool match_shadow_list(std::string_view property, const token_stream & ts,
                                     const scan & found, std::string & out);
/// The background shorthand's validation and serialisation. Defined in
/// shorthands.cpp.
[[nodiscard]] bool match_background(std::string_view value, std::string & out);
/// The comma-separated layer lists of background-* and mask-*. Defined in
/// backgrounds.cpp; an empty `out` is an invalid list.
[[nodiscard]] bool match_background_list(std::string_view property, const token_stream & ts,
                                         const scan & found, std::string & out);
/// `display`'s two-value grammar and its short forms. Defined in display.cpp.
[[nodiscard]] bool match_display(const token_stream & ts, const scan & found, std::string & out);
/// CSS Gap Decorations 1: the six rule lists (false for another property, an
/// empty `out` for an invalid list), and the `column-rule` / `row-rule`
/// shorthand split into and folded from its width, style and colour lists.
/// Defined in gaps.cpp.
[[nodiscard]] bool match_gap_list(std::string_view property, std::string_view text,
                                  std::string & out);
[[nodiscard]] bool split_gap_rule(std::string_view value, std::vector<std::string> & out);
[[nodiscard]] std::string fold_gap_rule(std::span<const std::string> v);
/// A <basic-shape> and its box, for clip-path and shape-outside (CSS Shapes
/// 1 §3): false for another property or a value that is not a basic shape
/// (a url(), an image, a shape()), an empty `out` for an invalid one.
/// Defined in shapes.cpp.
[[nodiscard]] bool match_basic_shape(std::string_view property, std::string_view text,
                                     std::string & out);
/// The rows of the property table beyond table.cpp's core set, grouped by
/// module. Defined in table_modules.cpp.
[[nodiscard]] std::span<const property_syntax> module_properties() noexcept;

} // namespace ctbrowser::style::css::detail
