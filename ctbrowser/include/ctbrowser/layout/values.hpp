//===- values.hpp - Style values parsed for layout ------------ -*- C++ -*-===//
//
// Turning style strings into layout numbers: lengths, display, position,
// transforms and the flex properties, plus the text measure layout is given.
//
// "12px" becomes 12 here rather than in the style engine on purpose: a
// computed style holds many declarations the box tree never asks about, and
// parsing every value at resolution time would be work done for properties
// nobody reads. Layout parses what it needs, when it needs it. This header
// depends on no other layout header, so both the box tree and the fragment
// tree can use it.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/calc.hpp>

namespace ctbrowser::layout {

/// The face a run is measured in, and the one it is drawn in: paint::font_face
/// is an alias of it. It is defined in layout because paint may depend on
/// layout but layout must not depend on paint.
struct text_face {
    std::string family; // "" = the backend's default
    bool bold = false;
    bool italic = false;

    [[nodiscard]] friend bool operator==(const text_face &, const text_face &) = default;
};

/// How text is measured: how wide a run is, and where its baseline sits inside
/// a line. Injected rather than assumed, because the real answer needs a font
/// stack that belongs to the raster layer, and a deterministic stub makes
/// layout testable without fonts.
///
/// The three callables are bundled because they travel together through every
/// formatting context. The ascent is what makes text of different sizes line
/// up: a line's items are placed so that `y + ascent` is the same for all of
/// them, which is what sharing a baseline means.
///
/// Callable directly: `metrics(text, size, face)` measures a run. Each member
/// left empty falls back to a monospace stand-in (0.6em advance, 0.8em ascent,
/// 0.2em descent).
struct text_metrics {
    std::function<float(std::string_view, float, const text_face &)> measure;
    std::function<float(float, const text_face &)> ascent_of;
    std::function<float(float, const text_face &)> descent_of;

    [[nodiscard]] float operator()(std::string_view text, float size,
                                   const text_face & face) const {
        // The fallback is a deterministic monospace stand-in: layout has to be
        // testable with no fonts at all.
        return measure ? measure(text, size, face) : static_cast<float>(text.size()) * size * 0.6f;
    }
    [[nodiscard]] float ascent(float size, const text_face & face) const {
        return ascent_of ? ascent_of(size, face) : size * 0.8f;
    }
    [[nodiscard]] float descent(float size, const text_face & face) const {
        return descent_of ? descent_of(size, face) : size * 0.2f;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(measure); }
};

/// The name layout code uses for text_metrics. The face is part of every
/// measurement, so bold 20px text is measured in the bold face it is drawn in.
using measure_text_fn = text_metrics;

/// The unit of a length.
enum class unit : std::uint8_t {
    px,
    percent,
    em,
    rem,
    auto_,
    none, ///< A unitless number, resolved as px.
    // The intrinsic sizing keywords, CSS Sizing 3 §5. A width in one of these
    // has no number to resolve: it is the box's own min-content or max-content
    // size, or the fit-content clamp between them, which only the formatting
    // context that measures the box can answer (algorithm.cpp's
    // intrinsic_border_width). `stretch` (and its `-webkit-fill-available`
    // spelling) is the available space less the margins, which is what `auto`
    // already means for a block's width, and parses as auto.
    min_content,
    max_content,
    fit_content
};

/// The `+ 12px` half of a `calc(50% + 12px)`, read on its own so parse_length
/// stays a straight line. Anything unexpected answers zero, which degrades the
/// value to a plain percentage rather than dropping it; the percentage is the
/// part that matters for the two places Bootstrap writes one.
[[nodiscard]] inline float parse_calc_offset(std::string_view rest) {
    rest = trim(rest, " \t");
    if (rest.empty() || (rest.front() != '+' && rest.front() != '-')) { return 0; }
    const float sign = rest.front() == '-' ? -1.0f : 1.0f;
    rest = trim(rest.substr(1), " \t");
    float px = 0;
    if (std::from_chars(rest.data(), rest.data() + rest.size(), px).ec != std::errc{}) { return 0; }
    return sign * px;
}

/// A parsed CSS length: a number and its unit, plus the extra terms that
/// `calc()`, `fit-content()` and `calc-size()` add.
struct length {
    float value = 0;
    unit u = unit::auto_;
    /// A fixed offset added after the percentage resolves, and zero for every
    /// value that is not a two-term calc. It sits here rather than in a
    /// separate type so consumers of `length` need not learn a second one;
    /// `resolve()` is the only place it is read.
    float offset_px = 0;
    /// `fit-content(<length-percentage>)`, CSS Sizing 3 §5.2.2: `u` is
    /// fit_content and the argument sits in `value`/`offset_px` with its unit
    /// here, `auto` for the bare keyword, which has no argument. It is
    /// clamp(min-content, argument, max-content), where the bare keyword clamps
    /// the available space instead (fit_content_bound, algorithm/common.hpp).
    unit fit_bound = unit::auto_;
    // calc-size(<basis>, <calc-sum>), CSS Values 5 §10.2. The three fields above
    // are the basis and these four are the calculation, a linear function of the
    // keyword `size` (the basis's used size): `size_factor * size + calc_percent%
    // + calc_px + calc_em`. A plain length is the identity (factor 1, no
    // offsets), so nothing that never wrote calc-size() sees a difference, and
    // `calc-size(auto, size)` is `auto`, as the specification says.
    // `any` is a basis with no size to name: unit::none, factor 0.
    float size_factor = 1;
    float calc_percent = 0;
    float calc_px = 0;
    float calc_em = 0;

    /// Whether a calc-size() calculation sits over the basis. `calc-size(auto,
    /// size * 2)` is not `auto`: the auto size is its input, and a caller that
    /// took the keyword at face value would drop the arithmetic.
    [[nodiscard]] constexpr bool has_calculation() const noexcept {
        return size_factor != 1 || calc_percent != 0 || calc_px != 0 || calc_em != 0;
    }
    /// Whether this is plain `auto`. It is the only value a caller has to
    /// special-case: every other unit answers resolve() given a basis, so
    /// there is deliberately no "is_definite" predicate, which callers would
    /// misuse to mean "not auto" and so drop percentages and em.
    [[nodiscard]] constexpr bool is_auto() const noexcept {
        return u == unit::auto_ && !has_calculation();
    }
    /// Whether this is a size only the box's own formatting context can
    /// answer: one of the three keywords above, or a calc-size() over a keyword
    /// or over `auto`. The basis is then the box's content size or its
    /// automatic size, and the calculation runs over that
    /// (intrinsic_border_width, algorithm.cpp).
    [[nodiscard]] constexpr bool is_intrinsic() const noexcept {
        return u == unit::min_content || u == unit::max_content || u == unit::fit_content ||
               (u == unit::auto_ && has_calculation());
    }
    /// Applies the calc-size() calculation to the basis's used size `size`.
    /// `size` and the answer are both measured in the box-sizing box, the same
    /// box the author's number names, and the percentage is of the containing
    /// block `basis` like any other.
    [[nodiscard]] constexpr float apply(float size, float basis, float font_size) const noexcept {
        return size_factor * size + calc_percent * basis / 100.0f + calc_px + calc_em * font_size;
    }
    /// Resolves against a containing-block `basis`. `auto` has no answer here
    /// and returns 0: the caller decides what auto means for the property it
    /// is resolving, which differs between width (fill) and height (fit
    /// content). `rem` assumes a 16px root font size.
    [[nodiscard]] constexpr float resolve(float basis, float font_size) const noexcept {
        float size = 0;
        switch (u) {
        case unit::px:
        case unit::none: size = value; break;
        case unit::percent: size = value * basis / 100.0f + offset_px; break;
        case unit::em: size = value * font_size; break;
        case unit::rem: size = value * 16.0f; break;
        case unit::auto_:
        case unit::min_content:
        case unit::max_content:
        case unit::fit_content: return 0; // a caller asks is_intrinsic() first
        }
        return apply(size, basis, font_size);
    }
};

/// The calculation of a calc-size(), read as a linear function of `size`: a
/// <calc-sum> whose terms are `size`, a number times `size`, a length or a
/// percentage, with `*` and `/` by a number and parentheses, which is the whole
/// grammar once the cascade has simplified it (style/css/calc/simplify.cpp's
/// calc_size_text writes `30px + (0.5 * size)`). Anything else answers nullopt
/// and the declaration degrades to its basis.
struct size_calculation {
    float size = 0, percent = 0, px = 0, em = 0;
    bool number = false; // a bare <number>, legal only as a factor
};
[[nodiscard]] std::optional<size_calculation> parse_size_calculation(std::string_view text);

/// Parses calc-size(<basis>, <calc-sum>): the basis parsed as a length (a
/// keyword, `any`, a <length-percentage> or a nested calc-size()) and the
/// calculation composed over it. `text` is the whole function, `calc-size(`
/// to `)`.
[[nodiscard]] length parse_calc_size(std::string_view text);

/// Parses a CSS length, percentage or sizing keyword. An empty or unreadable
/// value is `auto`.
[[nodiscard]] inline length parse_length(std::string_view text) {
    text = trim(text, " \t");
    if (text.empty()) { return length{}; }
    if (text == "auto") { return length{0, unit::auto_}; }
    if (text == "min-content") { return length{0, unit::min_content}; }
    if (text == "max-content") { return length{0, unit::max_content}; }
    if (text == "fit-content") { return length{0, unit::fit_content}; }
    if (text.starts_with("fit-content(") && text.ends_with(')')) {
        const length bound = parse_length(text.substr(12, text.size() - 13));
        length out{bound.value, unit::fit_content};
        if (!bound.is_auto() && !bound.is_intrinsic()) {
            out.fit_bound = bound.u;
            out.offset_px = bound.offset_px;
        }
        return out;
    }
    if (text == "stretch" || text == "-webkit-fill-available" || text == "-moz-available") {
        return length{0, unit::auto_};
    }
    if (text.starts_with("calc-size(") && text.ends_with(')')) { return parse_calc_size(text); }
    // `calc(50% + 12px)` is the one calc form that reaches layout. The cascade
    // folds every calc it can into a single px value, so anything still spelled
    // calc() here carries a percentage: it needs a containing block, which is a
    // used-value question. style/css/calc/serialize.cpp serialises exactly this
    // two-term shape, percentage first, so there is one form to read rather
    // than an expression grammar in two places.
    if (text.starts_with("calc(") && text.ends_with(')')) {
        const std::string_view body = text.substr(5, text.size() - 6);
        const std::size_t percent = body.find('%');
        if (percent == std::string_view::npos) { return length{}; }
        float share = 0;
        if (std::from_chars(body.data(), body.data() + percent, share).ec != std::errc{}) {
            return length{};
        }
        return length{share, unit::percent, parse_calc_offset(body.substr(percent + 1))};
    }

    float value = 0;
    const auto [rest, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{}) { return length{}; }
    const std::string_view suffix{rest, static_cast<std::size_t>(text.data() + text.size() - rest)};
    if (suffix.starts_with("px")) { return length{value, unit::px}; }
    if (suffix.starts_with('%')) { return length{value, unit::percent}; }
    if (suffix.starts_with("rem")) { return length{value, unit::rem}; }
    if (suffix.starts_with("em")) { return length{value, unit::em}; }
    return length{value, unit::none}; // unitless: resolved as px
}

namespace detail {
/// One pass over a calc-size() calculation: style's linear <calc-sum> reader
/// with `size` a term, and `em` kept apart for the element's font size to
/// resolve.
struct size_calc_reader : style::css::linear_sum<size_calc_reader, size_calculation> {
    static float number_of(const size_calculation & c) { return c.px; }
    static void scale(size_calculation & c, float by) {
        c.size *= by;
        c.percent *= by;
        c.px *= by;
        c.em *= by;
    }
    static void add(size_calculation & c, float sign, const size_calculation & rhs) {
        c.size += sign * rhs.size;
        c.percent += sign * rhs.percent;
        c.px += sign * rhs.px;
        c.em += sign * rhs.em;
    }
    [[nodiscard]] std::optional<size_calculation> factor() {
        skip_space();
        if (at >= text.size()) { return std::nullopt; }
        if (text[at] == '(' || text.substr(at, 5) == "calc(") {
            at += text[at] == '(' ? 1 : 5;
            std::optional<size_calculation> inner = sum();
            skip_space();
            if (!inner || at >= text.size() || text[at] != ')') { return std::nullopt; }
            ++at;
            return inner;
        }
        if (text.substr(at, 4) == "size" && (at + 4 == text.size() || !is_name(text[at + 4]))) {
            at += 4;
            return size_calculation{1, 0, 0, 0, false};
        }
        float number = 0;
        const auto [rest, ec] =
            std::from_chars(text.data() + at, text.data() + text.size(), number);
        if (ec != std::errc{}) { return std::nullopt; }
        at = static_cast<std::size_t>(rest - text.data());
        const std::string_view unit_text = text.substr(at);
        if (unit_text.starts_with('%')) {
            ++at;
            return size_calculation{0, number, 0, 0, false};
        }
        if (unit_text.starts_with("px")) {
            at += 2;
            return size_calculation{0, 0, number, 0, false};
        }
        if (unit_text.starts_with("rem")) {
            at += 3;
            return size_calculation{0, 0, number * 16.0f, 0, false};
        }
        if (unit_text.starts_with("em")) {
            at += 2;
            return size_calculation{0, 0, 0, number, false};
        }
        // A bare number: a factor, carried in `px` until it multiplies something.
        return size_calculation{0, 0, number, 0, true};
    }

    [[nodiscard]] static bool is_name(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '-' || c == '_';
    }
};
} // namespace detail

inline std::optional<size_calculation> parse_size_calculation(std::string_view text) {
    detail::size_calc_reader reader{{text, " \t"}};
    std::optional<size_calculation> out = reader.sum();
    reader.skip_space();
    if (!out || reader.at != text.size() || out->number) { return std::nullopt; }
    return out;
}

inline length parse_calc_size(std::string_view text) {
    // The two arguments, split at the top-level comma: the basis may itself be a
    // calc-size() with a comma of its own.
    const std::string_view inner = text.substr(10, text.size() - 11);
    std::size_t depth = 0;
    std::size_t comma = std::string_view::npos;
    for (std::size_t i = 0; i < inner.size() && comma == std::string_view::npos; ++i) {
        if (inner[i] == '(') { ++depth; }
        if (inner[i] == ')' && depth > 0) { --depth; }
        if (inner[i] == ',' && depth == 0) { comma = i; }
    }
    if (comma == std::string_view::npos) { return length{}; }
    const std::string_view basis_text = trim(inner.substr(0, comma), " \t");
    // The basis: `any` has no size to name and the calculation is the whole
    // value; `stretch` is what `auto` means for a block's width, as parse_length
    // already says; `content` is flex-basis's name for the max-content size.
    length out;
    if (basis_text == "any") {
        out = length{0, unit::none};
        out.size_factor = 0;
    } else if (basis_text == "content") {
        out = length{0, unit::max_content};
    } else {
        out = parse_length(basis_text);
    }
    const std::optional<size_calculation> calc = parse_size_calculation(inner.substr(comma + 1));
    if (!calc) { return out; }
    // Composed over a basis that is itself a calc-size(): f(g(size)) is linear
    // in `size` too, so the nesting flattens into one function of the innermost
    // basis.
    const float factor = basis_text == "any" ? 0.0f : calc->size;
    length composed = out;
    composed.size_factor = factor * out.size_factor;
    composed.calc_percent = factor * out.calc_percent + calc->percent;
    composed.calc_px = factor * out.calc_px + calc->px;
    composed.calc_em = factor * out.calc_em + calc->em;
    return composed;
}

/// The `display` values the box tree distinguishes. Everything else collapses
/// into one of these; modelling `display` values no algorithm consumes would
/// add nothing.
enum class display_kind : std::uint8_t {
    none,
    block,
    /// A block that draws a marker. Block-level in every way that matters to
    /// layout, which is why it is not a box_kind; it needs a display value of
    /// its own because any other display removes the marker. A
    /// `<li class="d-flex">` is a flex container and not a list item, so it
    /// draws no bullet; the marker is keyed off this value, not the tag.
    list_item,
    inline_level,
    inline_block,
    flex,
    inline_flex
};

/// Parses a `display` value, returning `fallback` when it is empty.
[[nodiscard]] inline display_kind parse_display(std::string_view text, display_kind fallback) {
    if (text == "none") { return display_kind::none; }
    // The two-value syntax, CSS Display 3 §2.1: `<display-outside>
    // <display-inside>`, with `list-item` allowed beside either. The outside
    // decides the level, the inside the formatting context, so `inline
    // list-item` is an inline box (client-props-inline-list-item) and `inline
    // flow-root` is an inline-block.
    if (const std::size_t space = text.find(' '); space != std::string_view::npos) {
        const std::string_view first = text.substr(0, space);
        const std::string_view second = text.substr(space + 1);
        const bool inline_outside = first == "inline" || second == "inline";
        const std::string_view inside = first == "inline" || first == "block" ? second : first;
        if (inside == "flex") {
            return inline_outside ? display_kind::inline_flex : display_kind::flex;
        }
        if (inline_outside) {
            return inside == "flow-root" ? display_kind::inline_block : display_kind::inline_level;
        }
        return inside == "list-item" ? display_kind::list_item : display_kind::block;
    }
    if (text == "block") { return display_kind::block; }
    if (text == "inline") { return display_kind::inline_level; }
    if (text == "inline-block") { return display_kind::inline_block; }
    if (text == "list-item") { return display_kind::list_item; }
    if (text == "flex") { return display_kind::flex; }
    if (text == "inline-flex") { return display_kind::inline_flex; }
    // `grid` and `inline-grid` deliberately fall through to `block`: there is
    // no grid formatting context. A grid container degrades to a block, which
    // is what a grid with every item full width looks like (Bootstrap's
    // `.d-grid`). `table` is decided by the tag, not here.
    if (!text.empty()) { return display_kind::block; }
    return fallback;
}

/// Blockification, CSS Display 3 §2.7: a flex item's own `display` is
/// blockified, because there are no lines inside a flex container for an
/// inline-level box to sit on. `inline` and `inline-block` become `block`;
/// `inline-flex` becomes `flex`; everything else is already block-level and is
/// left alone. getComputedStyle reports the blockified value, as Chrome does
/// for an `<a class="nav-link">` inside a `.nav`.
[[nodiscard]] inline display_kind blockify(display_kind d) {
    switch (d) {
    case display_kind::inline_level:
    case display_kind::inline_block: return display_kind::block;
    case display_kind::inline_flex: return display_kind::flex;
    case display_kind::none:
    case display_kind::block:
    // `inline list-item` blockifies to `block list-item` - it keeps its marker.
    case display_kind::list_item:
    case display_kind::flex: break;
    }
    return d;
}

/// `text-align`, as the fraction of a line's leftover space that goes before
/// it.
///
/// A fraction rather than an enum because that is all any consumer wants: a
/// line is shifted by `leftover * factor`, and start/left is 0, center is a
/// half and end/right is 1. `justify` is not modelled (stretching the spaces
/// inside a line is a different operation from moving the line) and answers 0.
///
/// `start`/`end` are the writing-mode-relative spellings and there is only one
/// writing mode here, so they are `left`/`right`.
[[nodiscard]] inline float parse_text_align(std::string_view text) {
    if (text == "center") { return 0.5f; }
    if (text == "right" || text == "end") { return 1.0f; }
    return 0.0f; // left, start, justify, and anything unrecognised
}

/// `opacity`, clamped to [0, 1]. Anything unreadable is 1 (fully opaque), so a
/// value the parser does not understand leaves the element visible rather
/// than erasing it.
[[nodiscard]] inline float parse_opacity(std::string_view text) {
    text = trim(text, html_whitespace);
    if (text.empty()) { return 1.0f; }
    float value = 1;
    if (std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc{}) {
        return 1.0f;
    }
    // A percentage is also legal (`opacity: 65%`) and means the same number.
    if (text.ends_with('%')) { value /= 100.0f; }
    return value < 0 ? 0.0f : (value > 1 ? 1.0f : value);
}

/// The `position` property.
enum class position_kind : std::uint8_t {
    static_,
    relative,
    absolute,
    fixed,
    sticky
};

/// `z-index`: either the keyword `auto` (nullopt), which does not create a
/// stacking context on an ordinary positioned box, or an integer stack level.
/// The distinction matters: integer zero creates a context and traps every
/// descendant stack level inside it; `auto` lets those descendants participate
/// in the nearest ancestor context.
[[nodiscard]] inline std::optional<int> parse_z_index(std::string_view text) {
    text = trim(text, html_whitespace);
    if (text.empty() || ascii_iequals(text, "auto")) { return std::nullopt; }
    // `from_chars` does not accept a leading plus on every standard-library
    // implementation, while CSS integers do.
    if (text.front() == '+') {
        text.remove_prefix(1);
        // The optional sign belongs to the integer token; a second sign does
        // not start another integer. Without this guard `+-1` would become `-1`
        // after removing the plus.
        if (!text.empty() && (text.front() == '+' || text.front() == '-')) { return std::nullopt; }
    }
    if (text.empty()) { return std::nullopt; }
    int value = 0;
    const auto [rest, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || rest != text.data() + text.size()) { return std::nullopt; }
    return value;
}

/// Parses `position`; anything unrecognised is `static`.
[[nodiscard]] inline position_kind parse_position(std::string_view text) {
    if (text == "relative") { return position_kind::relative; }
    if (text == "absolute") { return position_kind::absolute; }
    if (text == "fixed") { return position_kind::fixed; }
    // Layout treats sticky as relative, which is what it is until the page
    // scrolls past it; this engine has no scroll-driven layout yet. Naming it
    // rather than falling through to `static` keeps a sticky box's offsets
    // applied at rest. Recorded as a known difference.
    if (text == "sticky") { return position_kind::sticky; }
    return position_kind::static_;
}

/// `translate(x, y)`, the one transform this engine reads.
///
/// Bootstrap's `.translate-middle` is `translate(-50%, -50%)`: a percentage of
/// the element's own size, which centres it on its anchor point. It is a
/// paint-time transform in CSS and a pure offset here, which is identical for a
/// translation and nothing else; a rotation or a scale needs a real transform
/// on the display list and is not modelled.
struct translation {
    length x{0, unit::px};
    length y{0, unit::px};
};

/// Parses `translate()`, `translateX()` or `translateY()`; any other transform
/// is a zero translation.
[[nodiscard]] inline translation parse_translate(std::string_view text) {
    translation out;
    const std::size_t open = text.find('(');
    if (open == std::string_view::npos || !text.ends_with(')')) { return out; }
    const std::string_view name = trim(text.substr(0, open), html_whitespace);
    // The one-axis forms are separate functions, not `translate` with a default:
    // Bootstrap writes `translateX(-50%)` for `.translate-middle-x`.
    const bool x_only = name == "translateX";
    const bool y_only = name == "translateY";
    if (!x_only && !y_only && name != "translate") { return out; }
    std::string_view args = text.substr(open + 1, text.size() - open - 2);
    const std::size_t comma = args.find(',');
    const length first = parse_length(trim(args.substr(0, comma), html_whitespace));
    out.x = y_only ? length{0, unit::px} : first;
    if (y_only) {
        out.y = first;
    } else if (comma != std::string_view::npos) {
        out.y = parse_length(trim(args.substr(comma + 1), html_whitespace));
    }
    // A missing second argument is zero, not the first one repeated.
    if (out.x.is_auto()) { out.x = length{0, unit::px}; }
    if (out.y.is_auto()) { out.y = length{0, unit::px}; }
    return out;
}

/// `flex-direction`.
enum class flex_direction : std::uint8_t {
    row,
    row_reverse,
    column,
    column_reverse
};

/// `flex-wrap`.
enum class flex_wrap : std::uint8_t {
    nowrap,
    wrap,
    wrap_reverse
};

/// `justify-content`, `align-content`, `align-items` and `align-self` spell
/// overlapping value sets, so they share one enum rather than four that have to
/// be kept in step. Which values are legal differs per property, and that is
/// the parser's business rather than the type's: `justify-content: baseline`
/// is not a thing, and neither is `align-items: space-between`.
enum class flex_align : std::uint8_t {
    auto_,  ///< `align-self` only: whatever the container's `align-items` says.
    normal, ///< The initial value: stretch for the align-* trio, start for justify.
    flex_start,
    flex_end,
    center,
    baseline,
    stretch,
    space_between,
    space_around,
    space_evenly
};

/// Every flex property one box needs, resolved once so layout does not
/// re-parse a dozen strings per box per pass.
///
/// Held by value on box_node, deliberately: box_node is copyable, and
/// normalise() and the parallel driver move whole subtrees around. Making it
/// move-only to save these ~60 bytes on a ~250-byte struct is the wrong trade.
///
/// Every box carries both halves, container and item, because every box is
/// potentially both: a `.col` is an item of its `.row` and the container of the
/// nested row inside it.
struct flex_spec {
    // The container half.
    flex_direction direction = flex_direction::row;
    flex_wrap wrap = flex_wrap::nowrap;
    flex_align justify = flex_align::normal;    // justify-content
    flex_align items = flex_align::normal;      // align-items
    flex_align line_align = flex_align::normal; // align-content
    length row_gap{0, unit::px};
    length column_gap{0, unit::px};
    // The item half.
    float grow = 0;
    float shrink = 1;
    length basis{}; // `auto`
    flex_align self = flex_align::auto_;
    int order = 0;

    /// Whether the main axis is horizontal (`row` or `row-reverse`).
    [[nodiscard]] constexpr bool horizontal() const noexcept {
        return direction == flex_direction::row || direction == flex_direction::row_reverse;
    }
    /// Whether the main axis runs backwards (`row-reverse`, `column-reverse`).
    [[nodiscard]] constexpr bool reversed() const noexcept {
        return direction == flex_direction::row_reverse ||
               direction == flex_direction::column_reverse;
    }
};

[[nodiscard]] inline flex_direction parse_flex_direction(std::string_view text) {
    if (text == "row-reverse") { return flex_direction::row_reverse; }
    if (text == "column") { return flex_direction::column; }
    if (text == "column-reverse") { return flex_direction::column_reverse; }
    return flex_direction::row;
}

[[nodiscard]] inline flex_wrap parse_flex_wrap(std::string_view text) {
    if (text == "wrap") { return flex_wrap::wrap; }
    if (text == "wrap-reverse") { return flex_wrap::wrap_reverse; }
    return flex_wrap::nowrap;
}

/// Parses an alignment value, accepting the Box Alignment spellings as well as
/// the flex ones. `start`/`end` are the axis-relative keywords and
/// `left`/`right` the physical ones; in a left-to-right row they mean what
/// `flex-start`/`flex-end` mean, and this engine has no writing modes for them
/// to differ in.
///
/// `safe`/`unsafe` prefixes and `first`/`last baseline` are handled by taking
/// the last word, which is the keyword in every one of those forms.
[[nodiscard]] inline flex_align parse_flex_align(std::string_view text, flex_align fallback) {
    if (text.empty()) { return fallback; }
    if (const std::size_t space = text.find_last_of(' '); space != std::string_view::npos) {
        text = text.substr(space + 1);
    }
    if (text == "auto") { return flex_align::auto_; }
    if (text == "normal") { return flex_align::normal; }
    if (text == "flex-start" || text == "start" || text == "left") {
        return flex_align::flex_start;
    }
    if (text == "flex-end" || text == "end" || text == "right") { return flex_align::flex_end; }
    if (text == "center") { return flex_align::center; }
    if (text == "baseline") { return flex_align::baseline; }
    if (text == "stretch") { return flex_align::stretch; }
    if (text == "space-between") { return flex_align::space_between; }
    if (text == "space-around") { return flex_align::space_around; }
    if (text == "space-evenly") { return flex_align::space_evenly; }
    return fallback;
}

/// A `<number>`, for `flex-grow`/`flex-shrink`/`order`. Negative values are
/// invalid for the two factors and the caller replaces them; `order` may be
/// negative, which is how a utility pulls an item in front of its
/// source-order siblings.
[[nodiscard]] inline float parse_flex_number(std::string_view text, float fallback) {
    text = trim(text, " \t");
    if (text.empty()) { return fallback; }
    float value = 0;
    if (std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc{}) {
        return fallback;
    }
    return value;
}

/// `row-gap`/`column-gap`. `normal` is zero for a flex container, which is
/// the spec's own wording rather than a simplification: `normal` only means
/// something other than zero for multi-column.
[[nodiscard]] inline length parse_gap(std::string_view text) {
    if (text.empty() || text == "normal") { return length{0, unit::px}; }
    const length parsed = parse_length(text);
    return parsed.is_auto() ? length{0, unit::px} : parsed;
}

/// One length per box side, read from the per-side longhands.
struct side_lengths {
    length top, right, bottom, left;
};

/// Tags that generate no box unless a sheet overrides them. Without this a
/// page's <script> source and <style> rules would render as visible text. The
/// UA sheet (style/ua.hpp) has the same display:none rules; this copy is what
/// a document with no stylesheet at all gets.
[[nodiscard]] inline bool generates_no_box(std::string_view tag) {
    constexpr std::string_view hidden[] = {"head", "style", "script", "title",
                                           "meta", "link",  "base",   "template"};
    return std::ranges::find(hidden, tag) != std::ranges::end(hidden);
}

/// The tag list HTML renders inline by default, when the sheet says nothing.
[[nodiscard]] inline bool is_inline_by_default(std::string_view tag) {
    constexpr std::string_view inline_tags[] = {
        "a", "span", "b", "i", "u", "s", "em", "strong", "code", "small", "big", "mark", "sub",
        "sup", "tt", "kbd", "samp", "cite", "var", "dfn", "abbr", "ins", "del", "img", "q", "time",
        "output", "label", "br",
        // An icon in a sentence shares the line with it, the same as an <img>.
        // Left to the unknown-element default this would be block-level, and a
        // graphic mid-paragraph would break the line before and after itself.
        "svg"};
    return std::ranges::find(inline_tags, tag) != std::ranges::end(inline_tags);
}

/// Elements sized by what they are rather than by what they contain. A
/// <canvas> is its pixel buffer; an <input> is a field wide enough to type in.
/// Laying either out from its children would give a box of zero height.
[[nodiscard]] inline bool is_replaced_tag(std::string_view tag) {
    // <svg> is replaced for the same reason as the rest, and also because
    // being replaced stops build_children descending into it, so the shapes
    // inside a graphic (an <svg><text>, say) generate no boxes.
    // <button> is deliberately not replaced: its box comes from the UA sheet's
    // padding and border (style/ua.hpp) and its label from the ordinary text
    // path, so a stylesheet can override either. `<input type=button|submit|
    // reset>` stays replaced and is drawn by the widget painter.
    constexpr std::string_view names[] = {"canvas", "img",    "input", "select", "textarea",
                                          "video",  "iframe", "embed", "object", "svg"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

/// What `display` a tag has before any sheet speaks.
[[nodiscard]] inline display_kind default_display_for(std::string_view tag) {
    if (generates_no_box(tag)) { return display_kind::none; }
    // An <li> is a list item before any sheet speaks. The UA sheet says so too;
    // this function is what a document with no stylesheet at all gets, and a
    // bare `<li>` has a bullet in every browser.
    if (tag == "li") { return display_kind::list_item; }
    return is_inline_by_default(tag) ? display_kind::inline_level : display_kind::block;
}

} // namespace ctbrowser::layout
