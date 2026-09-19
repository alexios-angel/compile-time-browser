#pragma once

// The CSSOM declaration block - CSSOM §6.6 - and the shorthand table it needs:
// which longhands each shorthand expands to, how a value is split into them,
// and how they fold back for `cssText` and `getPropertyValue(shorthand)`.
//
// A SHORTHAND IS STORED AS ITS LONGHANDS, because that is what every CSSOM
// question is asked of: `length` counts longhands, `item(i)` names them,
// `removeProperty("margin")` removes four, and `cssText` reconstructs the
// shorthand only when all of them are there with one priority. The table below
// splits the shorthands whose grammar is positional or a `||` of typed parts;
// one it does not model - `background`, `font`, `grid` - or any value holding a
// `var()` is kept whole, exactly as every property was before this existed.

#include "../expansion.hpp"
#include "../internal.hpp"

#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/easing.hpp>

namespace ctbrowser::style::css::shorthand_detail {

using namespace detail;

// HOW A SHORTHAND'S PARTS MAP ONTO ITS LONGHANDS.
enum class shape : std::uint8_t {
    sides,       // 1-4 values: top, right, bottom, left, in `margin`'s way
    pair,        // 1-2 values: start/end, x/y or row/column; one sets both
    place,       // `place-*`: an align value then a justify one, each of several words
    grid_lines,  // grid-row / grid-column / grid-area: `/`-separated grid lines
    slash_pair,  // `container`: `<'a'> [ / <'b'> ]?`, the second at its initial when omitted
    bar,         // `a || b || c`: each part goes to the longhand that takes it
    columns,     // width/count and optional `/ column-height`; resets column-wrap
    flex,        // Flexbox 1 §7.1.1's own defaults
    font,        // CSS Fonts 4 §3.1: the four keywords, the size, `/ line-height`, the family
    border,      // `bar` over width/style/color, applied to four sides, plus
                 // border-image reset to its initial values
    border_axis, // `border-block` / `border-inline`: `bar` over width/style/
                 // color applied to the axis's two sides (CSS Logical 1 §4.4)
    white_space, // CSS Text 4 §3: one of six keywords, or `<'white-space-collapse'>
                 // || <'text-wrap-mode'> || <'white-space-trim'>`
    animation,   // `<single-animation>#`, CSS Animations 1 §5.9: per item, the
                 // first <time> is the duration and the second the delay, an
                 // easing, a count, and the keywords of direction, fill mode
                 // and play state before anything is a name
    all,         // every longhand; CSS-wide keywords only
    whole,       // a grammar this table does not split: only a CSS-wide keyword
                 // reaches the longhands, and only one folds back
};

struct shorthand_syntax {
    std::string_view name;
    shape kind;
    std::string_view longhands; // space-separated, in canonical order
    // What a `bar` shorthand serialises as when every part is its initial
    // value: `border: none` and `border: medium` both mean "all initial", and
    // one spelling has to be picked.
    std::string_view all_initial;
};

inline constexpr std::string_view border_longhands =
    "border-top-width border-right-width border-bottom-width border-left-width "
    "border-top-style border-right-style border-bottom-style border-left-style "
    "border-top-color border-right-color border-bottom-color border-left-color "
    "border-image-source border-image-slice border-image-width border-image-outset "
    "border-image-repeat";

inline constexpr shorthand_syntax table[] = {
    {"all", shape::all, "", ""},
    {"border", shape::border, border_longhands, "none"},
    {"margin", shape::sides, "margin-top margin-right margin-bottom margin-left", ""},
    {"padding", shape::sides, "padding-top padding-right padding-bottom padding-left", ""},
    {"inset", shape::sides, "top right bottom left", ""},
    {"border-width", shape::sides,
     "border-top-width border-right-width border-bottom-width border-left-width", ""},
    {"border-style", shape::sides,
     "border-top-style border-right-style border-bottom-style border-left-style", ""},
    {"border-color", shape::sides,
     "border-top-color border-right-color border-bottom-color border-left-color", ""},
    {"border-image", shape::bar,
     "border-image-source border-image-slice border-image-width border-image-outset "
     "border-image-repeat",
     "none"},
    {"border-top", shape::bar, "border-top-width border-top-style border-top-color", "none"},
    {"border-right", shape::bar, "border-right-width border-right-style border-right-color",
     "none"},
    {"border-bottom", shape::bar, "border-bottom-width border-bottom-style border-bottom-color",
     "none"},
    {"border-left", shape::bar, "border-left-width border-left-style border-left-color", "none"},
    // The flow-relative borders, CSS Logical 1 §4.4: a side, an axis's two
    // sides, and one component across an axis.
    {"border-block-start", shape::bar,
     "border-block-start-width border-block-start-style border-block-start-color", "none"},
    {"border-block-end", shape::bar,
     "border-block-end-width border-block-end-style border-block-end-color", "none"},
    {"border-inline-start", shape::bar,
     "border-inline-start-width border-inline-start-style border-inline-start-color", "none"},
    {"border-inline-end", shape::bar,
     "border-inline-end-width border-inline-end-style border-inline-end-color", "none"},
    {"border-block", shape::border_axis,
     "border-block-start-width border-block-start-style border-block-start-color "
     "border-block-end-width border-block-end-style border-block-end-color",
     "none"},
    {"border-inline", shape::border_axis,
     "border-inline-start-width border-inline-start-style border-inline-start-color "
     "border-inline-end-width border-inline-end-style border-inline-end-color",
     "none"},
    {"border-block-width", shape::pair, "border-block-start-width border-block-end-width", ""},
    {"border-block-style", shape::pair, "border-block-start-style border-block-end-style", ""},
    {"border-block-color", shape::pair, "border-block-start-color border-block-end-color", ""},
    {"border-inline-width", shape::pair, "border-inline-start-width border-inline-end-width", ""},
    {"border-inline-style", shape::pair, "border-inline-start-style border-inline-end-style", ""},
    {"border-inline-color", shape::pair, "border-inline-start-color border-inline-end-color", ""},
    {"outline", shape::bar, "outline-color outline-style outline-width", "none"},
    {"flex", shape::flex, "flex-grow flex-shrink flex-basis", ""},
    {"flex-flow", shape::bar, "flex-direction flex-wrap", "row"},
    {"list-style", shape::bar, "list-style-position list-style-image list-style-type", "outside"},
    {"margin-inline", shape::pair, "margin-inline-start margin-inline-end", ""},
    {"margin-block", shape::pair, "margin-block-start margin-block-end", ""},
    {"padding-inline", shape::pair, "padding-inline-start padding-inline-end", ""},
    {"padding-block", shape::pair, "padding-block-start padding-block-end", ""},
    {"inset-inline", shape::pair, "inset-inline-start inset-inline-end", ""},
    {"inset-block", shape::pair, "inset-block-start inset-block-end", ""},
    {"overflow", shape::pair, "overflow-x overflow-y", ""},
    {"gap", shape::pair, "row-gap column-gap", ""},
    {"scroll-padding", shape::sides,
     "scroll-padding-top scroll-padding-right scroll-padding-bottom scroll-padding-left", ""},
    {"scroll-margin", shape::sides,
     "scroll-margin-top scroll-margin-right scroll-margin-bottom scroll-margin-left", ""},
    {"scroll-padding-block", shape::pair, "scroll-padding-block-start scroll-padding-block-end",
     ""},
    {"scroll-padding-inline", shape::pair, "scroll-padding-inline-start scroll-padding-inline-end",
     ""},
    {"scroll-margin-block", shape::pair, "scroll-margin-block-start scroll-margin-block-end", ""},
    {"scroll-margin-inline", shape::pair, "scroll-margin-inline-start scroll-margin-inline-end",
     ""},
    {"overscroll-behavior", shape::pair, "overscroll-behavior-x overscroll-behavior-y", ""},
    {"grid-gap", shape::pair, "row-gap column-gap", ""},
    {"columns", shape::columns, "column-width column-count column-height column-wrap", "auto"},
    {"column-rule", shape::bar, "column-rule-width column-rule-style column-rule-color", "medium"},
    {"text-emphasis", shape::bar, "text-emphasis-style text-emphasis-color", "none"},
    {"text-wrap", shape::bar, "text-wrap-mode text-wrap-style", "wrap"},
    {"white-space", shape::white_space, "white-space-collapse text-wrap-mode white-space-trim",
     "normal"},
    {"grid-row", shape::grid_lines, "grid-row-start grid-row-end", ""},
    {"grid-column", shape::grid_lines, "grid-column-start grid-column-end", ""},
    {"grid-area", shape::grid_lines,
     "grid-row-start grid-column-start grid-row-end grid-column-end", ""},
    {"container", shape::slash_pair, "container-name container-type", ""},
    {"place-content", shape::place, "align-content justify-content", ""},
    {"place-items", shape::place, "align-items justify-items", ""},
    {"place-self", shape::place, "align-self justify-self", ""},
    {"font", shape::font,
     "font-style font-variant font-weight font-stretch font-size line-height font-family", ""},
    {"background", shape::whole,
     "background-color background-image background-position background-size "
     "background-repeat background-attachment background-origin background-clip",
     ""},
    {"text-decoration", shape::whole,
     "text-decoration-line text-decoration-style text-decoration-color", ""},
    {"transition", shape::whole,
     "transition-property transition-duration transition-timing-function transition-delay", ""},
    {"animation", shape::animation,
     "animation-duration animation-timing-function animation-delay animation-iteration-count "
     "animation-direction animation-fill-mode animation-play-state animation-name "
     "animation-timeline animation-range-start animation-range-end",
     "none"},
    {"border-radius", shape::whole,
     "border-top-left-radius border-top-right-radius border-bottom-right-radius "
     "border-bottom-left-radius",
     ""},
};

struct expansion {
    const shorthand_syntax * syntax = nullptr;
    std::vector<std::string_view> longhands;
};

const std::vector<expansion> & expansions();

[[nodiscard]] const expansion * expansion_of(std::string_view name);

[[nodiscard]] std::vector<const expansion *> shorthands_for(std::string_view longhand);

[[nodiscard]] std::string_view initial_of(std::string_view longhand);

// --- the logical property groups, CSS Logical 1 §1.1 ------------------------
//
// `margin-top` and `margin-inline-start` are one group with two MAPPING
// LOGICS, and a block that has one written between the other's shorthand parts
// cannot fold the shorthand without changing what wins. The group is the name
// with its side or axis removed; the logic is which of the three it was.
enum class mapping : std::uint8_t {
    none,
    physical,
    block,
    inline_
};

struct logical_group {
    std::string group;
    mapping logic = mapping::none;
};

[[nodiscard]] logical_group group_of(std::string_view name);

// --- splitting a shorthand value into its longhands ---------------------------

enum class split : std::uint8_t {
    ok,
    whole,  // a value this table cannot divide: kept as one declaration
    invalid // refused, as the cascade would refuse it
};

[[nodiscard]] std::string canonical(std::string_view longhand, std::string_view part, bool & valid);

split split_positional(const expansion & e, std::span<const std::string_view> parts,
                       std::vector<std::string> & out);

[[nodiscard]] bool looks_like_image(std::string_view part);

split split_bar(const expansion & e, std::span<const std::string_view> parts,
                std::vector<std::string> & out);

split split_flex(std::span<const std::string_view> parts, std::vector<std::string> & out);

split split_font(std::span<const std::string_view> parts, std::vector<std::string> & out);

split split_border(std::span<const std::string_view> parts, std::vector<std::string> & out);

split split_border_axis(const expansion & e, std::span<const std::string_view> parts,
                        std::vector<std::string> & out);

// CSS Text 4 §3.1: the six `white-space` keywords as their collapse and wrap
// longhands; each has `white-space-trim: none`.
inline constexpr std::array<std::string_view, 3> white_space_keywords[] = {
    {"normal", "collapse", "wrap"},           {"pre", "preserve", "nowrap"},
    {"nowrap", "collapse", "nowrap"},         {"pre-wrap", "preserve", "wrap"},
    {"break-spaces", "break-spaces", "wrap"}, {"pre-line", "preserve-breaks", "wrap"},
};

split split_white_space(std::span<const std::string_view> parts, std::vector<std::string> & out);

// The eight per-item longhands of `animation`, in the shorthand's canonical
// order, with what an omitted one is. The three after them - timeline and the
// range - are reset to their initial values by the shorthand and take no
// value from it (CSS Animations 2 §5.9).
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 8> animation_items = {
    {{"animation-duration", "auto"},
     {"animation-timing-function", "ease"},
     {"animation-delay", "0s"},
     {"animation-iteration-count", "1"},
     {"animation-direction", "normal"},
     {"animation-fill-mode", "none"},
     {"animation-play-state", "running"},
     {"animation-name", "none"}}};

[[nodiscard]] bool animation_default(std::size_t i, std::string_view text);

[[nodiscard]] std::string canonical_easing(std::string text);

split split_animation(std::string_view value, std::vector<std::string> & out);

split split_slash_pair(const expansion & e, std::string_view value, std::vector<std::string> & out);

split split_value(const expansion & e, std::string_view text, std::vector<std::string> & out);

[[nodiscard]] std::string join(std::span<const std::string> parts);

[[nodiscard]] std::string fold_sides(std::span<const std::string> v);

[[nodiscard]] std::string fold_font(std::span<const std::string> v);

[[nodiscard]] std::string fold_white_space(std::span<const std::string> v);

[[nodiscard]] std::string fold_animation(std::span<const std::string> v);

[[nodiscard]] std::string fold_bar(const expansion & e, std::span<const std::string> v);

[[nodiscard]] std::string fold(const expansion & e, std::span<const std::string> v);

[[nodiscard]] std::size_t index_of(const declaration_block & block, std::string_view name);

bool set_one(declaration_block & block, std::string_view name, std::string value, bool important);

bool add_parsed(declaration_block & block, std::string_view name, std::string value,
                bool important);

bool erase_named(declaration_block & block, std::span<const std::string_view> names);

bool put(declaration_block & block, std::string_view name, std::string_view text, bool important,
         bool parsed);

} // namespace ctbrowser::style::css::shorthand_detail
