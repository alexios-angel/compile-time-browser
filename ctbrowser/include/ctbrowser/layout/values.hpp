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

// Turning style strings into layout numbers.
//
// This is where "12px" becomes 12, and it happens HERE rather than in the
// style engine on purpose: a computed style holds many declarations the box
// tree never asks about, and parsing every value at resolution time would be
// work done for properties nobody reads. Layout parses what it needs, when it
// needs it.

namespace ctbrowser::layout {

// How text is measured. Injected rather than assumed, because the real answer
// needs a font stack that belongs to the raster layer - and because a
// deterministic stub is what makes layout testable without fonts at all.
//
// It lives HERE, in the partition that depends on nothing, because both the box
// tree and the fragment tree need it. Putting it with the fragments made :box
// import :fragment, which imports :box - a cycle the module system rejects
// outright rather than letting it become a subtle build-order problem.
// The face a run is measured in, and the one it is drawn in: paint::font_face
// is an alias of it. It lives HERE because values.hpp depends on nothing, and
// layout naming a paint type would invert the dependency the whole pipeline is
// built on; paint naming a layout type is the direction the pipeline already runs.
struct text_face {
    std::string family; // "" = the backend's default
    bool bold = false;
    bool italic = false;

    [[nodiscard]] friend bool operator==(const text_face &, const text_face &) = default;
};

// What layout needs to know about a font: how wide a run is, and where its
// BASELINE sits inside a line.
//
// Bundled rather than passed as separate callables because they travel
// together through every formatting context - adding a second parameter to
// measure/arrange and to every wrap helper would be the same information with
// more places to get it wrong.
//
// The ascent is what makes text of different sizes line up: a line's items are
// placed so that `y + ascent` is the same for all of them, which is the
// definition of sharing a baseline. Aligning their boxes instead - tops or
// bottoms - is only right when every item has the same metrics.
//
// Callable directly, so a measurement reads the same as it did when this was a
// bare std::function.
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

// The FACE is part of the question. It was not - the signature was
// (text, size) - so a page could ask for bold 20px Fira Sans, get measured in
// whatever the rasterizer felt like, and lay its text out at the wrong width.
using measure_text_fn = text_metrics;

enum class unit : std::uint8_t {
    px,
    percent,
    em,
    rem,
    auto_,
    none,
    // THE INTRINSIC SIZING KEYWORDS, CSS Sizing 3 §5. A width in one of these
    // has no number to resolve: it is the box's own min-content or max-content
    // size, or the fit-content clamp between them, which only the formatting
    // context that measures the box can answer - algorithm.cpp's
    // intrinsic_border_width. `stretch` (and its `-webkit-fill-available`
    // spelling) is the available space less the margins, which is what `auto`
    // already means for a block's width, and parses as auto.
    min_content,
    max_content,
    fit_content
};

// The `+ 12px` half of a `calc(50% + 12px)`, read on its own so parse_length stays
// a straight line. Anything unexpected answers zero, which degrades the value to a
// plain percentage rather than dropping it - the percentage is the part that
// matters for the two places Bootstrap writes one.
[[nodiscard]] inline float parse_calc_offset(std::string_view rest) {
    rest = trim(rest, " \t");
    if (rest.empty() || (rest.front() != '+' && rest.front() != '-')) { return 0; }
    const float sign = rest.front() == '-' ? -1.0f : 1.0f;
    rest = trim(rest.substr(1), " \t");
    float px = 0;
    if (std::from_chars(rest.data(), rest.data() + rest.size(), px).ec != std::errc{}) { return 0; }
    return sign * px;
}

struct length {
    float value = 0;
    unit u = unit::auto_;
    // A FIXED OFFSET ADDED AFTER the percentage resolves, and zero for every value
    // that is not a two-term calc. It sits here rather than in a separate type
    // because every consumer of `length` would otherwise have to learn about a
    // second one, and `resolve()` is the only place it is read.
    float offset_px = 0;
    // `fit-content(<length-percentage>)`, CSS Sizing 3 §5.2.2: `u` is
    // fit_content and the argument sits in `value`/`offset_px` with ITS unit
    // here - `auto` for the bare keyword, which has no argument. It is
    // clamp(min-content, argument, max-content), where the bare keyword clamps
    // the available space instead (fit_content_bound, algorithm.hpp).
    unit fit_bound = unit::auto_;
    // calc-size(<basis>, <calc-sum>), CSS Values 5 §10.2. The three fields above
    // are the BASIS and these four are the calculation, a linear function of the
    // keyword `size` (the basis's used size): `size_factor * size + calc_percent%
    // + calc_px + calc_em`. A plain length is the identity - factor 1, no offsets
    // - so nothing that never wrote calc-size() sees a difference, and
    // `calc-size(auto, size)` IS `auto`, which is what the specification says.
    // `any` is a basis with no size to name: unit::none, factor 0.
    float size_factor = 1;
    float calc_percent = 0;
    float calc_px = 0;
    float calc_em = 0;

    // Does a calc-size() calculation sit over the basis? `calc-size(auto, size *
    // 2)` is not `auto`: the auto size is its INPUT, and a caller that took the
    // keyword at face value would drop the arithmetic.
    [[nodiscard]] constexpr bool has_calculation() const noexcept {
        return size_factor != 1 || calc_percent != 0 || calc_px != 0 || calc_em != 0;
    }
    // `auto` is the ONLY value a caller has to special-case. Every other unit
    // answers resolve() given a basis, so there is deliberately no
    // "is_definite" predicate here - one existed, and every call site used it
    // to mean "not auto", which silently dropped percentages and em.
    [[nodiscard]] constexpr bool is_auto() const noexcept {
        return u == unit::auto_ && !has_calculation();
    }
    // A size only the box's own formatting context can answer: one of the three
    // keywords above, or a calc-size() over a keyword or over `auto` - the
    // basis is the box's content size or its automatic size, and the
    // calculation runs over that (intrinsic_border_width, algorithm.cpp).
    [[nodiscard]] constexpr bool is_intrinsic() const noexcept {
        return u == unit::min_content || u == unit::max_content || u == unit::fit_content ||
               (u == unit::auto_ && has_calculation());
    }
    // THE CALCULATION over the basis's used size. `size` and the answer are both
    // measured in the box-sizing box, the same box the author's number names,
    // and the percentage is of the containing block like any other.
    [[nodiscard]] constexpr float apply(float size, float basis, float font_size) const noexcept {
        return size_factor * size + calc_percent / 100.0f * basis + calc_px + calc_em * font_size;
    }
    // Resolve against a containing-block basis. `auto` has no answer here -
    // the caller decides what auto means for the property it is resolving,
    // which differs between width (fill) and height (fit content).
    [[nodiscard]] constexpr float resolve(float basis, float font_size) const noexcept {
        float size = 0;
        switch (u) {
        case unit::px:
        case unit::none: size = value; break;
        case unit::percent: size = value / 100.0f * basis + offset_px; break;
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

// THE CALCULATION OF A calc-size(), read as a linear function of `size`: a
// <calc-sum> whose terms are `size`, a number times `size`, a length or a
// percentage, with `*` and `/` by a number and parentheses - which is the whole
// grammar once the cascade has simplified it (style/css/calc/simplify.cpp's
// calc_size_text writes `30px + (0.5 * size)`). Anything else answers nullopt
// and the declaration degrades to its basis.
struct size_calculation {
    float size = 0, percent = 0, px = 0, em = 0;
    bool number = false; // a bare <number>, legal only as a factor
};
[[nodiscard]] std::optional<size_calculation> parse_size_calculation(std::string_view text);

// calc-size(<basis>, <calc-sum>): the basis parsed as a length (a keyword, `any`,
// a <length-percentage> or a nested calc-size()) and the calculation composed
// over it. `text` is the whole function, `calc-size(` to `)`.
[[nodiscard]] length parse_calc_size(std::string_view text);

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
    // `calc(50% + 12px)` - THE ONE CALC FORM THAT REACHES LAYOUT. The cascade folds
    // every calc it can into a single px value, so anything still spelled calc()
    // here carries a percentage: it had no answer at computed-value time because it
    // needed a containing block, which is a used-value question and this is where
    // the answer lives. style/css/calc/serialize.cpp serialises exactly this two-term shape,
    // percentage first, so there is one form to read rather than an expression
    // grammar in two places.
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
    return length{value, unit::none}; // unitless: treated as px, like the previous engine did
}

namespace detail {
// One pass of a recursive-descent reader over a calc-size() calculation. The
// grammar is CSS Values 4 §10.1's <calc-sum> with `size` as a term, and the
// result is kept linear: a product may have at most one non-number side.
struct size_calc_reader {
    std::string_view text;
    std::size_t at = 0;

    void skip_space() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) { ++at; }
    }
    [[nodiscard]] std::optional<size_calculation> sum() {
        std::optional<size_calculation> left = product();
        while (left) {
            skip_space();
            if (at >= text.size() || (text[at] != '+' && text[at] != '-')) { break; }
            const float sign = text[at] == '-' ? -1.0f : 1.0f;
            ++at;
            const std::optional<size_calculation> right = product();
            if (!right || left->number != right->number) { return std::nullopt; }
            left->size += sign * right->size;
            left->percent += sign * right->percent;
            left->px += sign * right->px;
            left->em += sign * right->em;
        }
        return left;
    }
    [[nodiscard]] std::optional<size_calculation> product() {
        std::optional<size_calculation> left = factor();
        while (left) {
            skip_space();
            if (at >= text.size() || (text[at] != '*' && text[at] != '/')) { break; }
            const bool divide = text[at] == '/';
            ++at;
            const std::optional<size_calculation> right = factor();
            if (!right) { return std::nullopt; }
            // Linear: a number scales the other side; two lengths cannot multiply
            // and a length cannot divide anything.
            if (divide) {
                if (!right->number || right->px == 0) { return std::nullopt; }
                scale(*left, 1.0f / right->px);
            } else if (right->number) {
                scale(*left, right->px);
            } else if (left->number) {
                const float by = left->px;
                *left = *right;
                scale(*left, by);
            } else {
                return std::nullopt;
            }
        }
        return left;
    }
    static void scale(size_calculation & c, float by) {
        c.size *= by;
        c.percent *= by;
        c.px *= by;
        c.em *= by;
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
    detail::size_calc_reader reader{text};
    std::optional<size_calculation> out = reader.sum();
    reader.skip_space();
    if (!out || reader.at != text.size() || out->number) { return std::nullopt; }
    return out;
}

inline length parse_calc_size(std::string_view text) {
    // The two arguments, split at the top-level comma - the basis may itself be a
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
    // THE BASIS: `any` has no size to name and the calculation is the whole
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
    // COMPOSED over a basis that is itself a calc-size(): f(g(size)) is linear
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

// The `display` values the box tree distinguishes. Everything else collapses
// into one of these - a box tree that models `display` exhaustively before there
// is an algorithm to consume it would be modelling nothing.
enum class display_kind : std::uint8_t {
    none,
    block,
    // A BLOCK THAT DRAWS A MARKER. Block-level in every way that matters to
    // layout, which is why it is not a box_kind - the marker is a paint
    // question, and the only reason it needs a display value of its own is that
    // ANY OTHER display takes it away. A `<li class="d-flex">` is a flex
    // container and not a list item, so Bootstrap's list groups and navs show no
    // bullets; keying the marker off the TAG instead drew one beside every one
    // of them.
    list_item,
    inline_level,
    inline_block,
    flex,
    inline_flex
};

[[nodiscard]] inline display_kind parse_display(std::string_view text, display_kind fallback) {
    if (text == "none") { return display_kind::none; }
    if (text == "block") { return display_kind::block; }
    if (text == "inline") { return display_kind::inline_level; }
    if (text == "inline-block") { return display_kind::inline_block; }
    if (text == "list-item") { return display_kind::list_item; }
    if (text == "flex") { return display_kind::flex; }
    if (text == "inline-flex") { return display_kind::inline_flex; }
    // `grid` and `inline-grid` fall through to `block` DELIBERATELY, and the
    // comment is the point rather than the code: there is no grid formatting
    // context, so naming the value here would be a promise the pipeline cannot
    // keep. A grid container degrades to a block, which is what a grid with every
    // item full width looks like - Bootstrap's `.d-grid` is one utility and that
    // is exactly its shape. `table` is decided by the TAG, above this.
    if (!text.empty()) { return display_kind::block; }
    return fallback;
}

// BLOCKIFICATION, CSS Display 3 §2.7: a flex item's own `display` is blockified,
// because there are no lines inside a flex container for an inline-level box to
// sit on. `inline` and `inline-block` become `block`; `inline-flex` becomes
// `flex`; everything else is already block-level and is left alone.
//
// It is not cosmetic. `<a class="nav-link">` inside a `.nav` is an inline element
// in a formatting context with no inline algorithm, and Chrome reports `block`
// for it - on 50 of one fixture's 189 elements.
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

// `text-align`, as the FRACTION of a line's leftover space that goes before it.
//
// A fraction rather than an enum because that is all any consumer wants: a line
// is shifted by `leftover * factor`, and start/left is 0, center is a half and
// end/right is 1. `justify` is not modelled - stretching the spaces inside a line
// is a different operation from moving the line, and answering 0 for it is the
// honest subset rather than a wrong guess.
//
// `start`/`end` are the writing-mode-relative spellings and there is only one
// writing mode here, so they are `left`/`right`. Bootstrap writes `.text-start`,
// `.text-center` and `.text-end` and never the physical pair.
[[nodiscard]] inline float parse_text_align(std::string_view text) {
    if (text == "center") { return 0.5f; }
    if (text == "right" || text == "end") { return 1.0f; }
    return 0.0f; // left, start, justify, and anything unrecognised
}

// `opacity`, clamped to [0, 1]. Anything unreadable is 1 - fully opaque - so a
// value the parser does not understand leaves the element visible rather than
// erasing it.
[[nodiscard]] inline float parse_opacity(std::string_view text) {
    text = trim(text, html_whitespace);
    if (text.empty()) { return 1.0f; }
    float value = 1;
    if (std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc{}) {
        return 1.0f;
    }
    // A PERCENTAGE IS ALSO LEGAL - `opacity: 65%` - and is the same number.
    if (text.ends_with('%')) { value /= 100.0f; }
    return value < 0 ? 0.0f : (value > 1 ? 1.0f : value);
}

// --- position -------------------------------------------------------------

enum class position_kind : std::uint8_t {
    static_,
    relative,
    absolute,
    fixed,
    sticky
};

// `z-index`: either the keyword `auto`, which does NOT create a stacking
// context on an ordinary positioned box, or an integer stack level. Keeping
// that distinction is load-bearing: integer zero creates a context and traps
// every descendant stack level inside it; `auto` lets those descendants
// participate in the nearest ancestor context.
[[nodiscard]] inline std::optional<int> parse_z_index(std::string_view text) {
    text = trim(text, html_whitespace);
    if (text.empty() || ascii_iequals(text, "auto")) { return std::nullopt; }
    // `from_chars` does not accept a leading plus on every standard-library
    // implementation, while CSS integers do.
    if (text.front() == '+') {
        text.remove_prefix(1);
        // The optional sign belongs to the integer token; a second sign does
        // not start another integer. Without this guard `+-1` became `-1`
        // after removing the plus and silently created the wrong context.
        if (!text.empty() && (text.front() == '+' || text.front() == '-')) { return std::nullopt; }
    }
    if (text.empty()) { return std::nullopt; }
    int value = 0;
    const auto [rest, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || rest != text.data() + text.size()) { return std::nullopt; }
    return value;
}

[[nodiscard]] inline position_kind parse_position(std::string_view text) {
    if (text == "relative") { return position_kind::relative; }
    if (text == "absolute") { return position_kind::absolute; }
    if (text == "fixed") { return position_kind::fixed; }
    // STICKY IS TREATED AS RELATIVE, which is exactly what it is until the page
    // scrolls past it - and this engine has no scroll-driven layout yet. Naming
    // it rather than falling through to `static` is the difference between a
    // sticky header sitting where it belongs at rest and sitting in the wrong
    // place always. Recorded as a known difference.
    if (text == "sticky") { return position_kind::sticky; }
    return position_kind::static_;
}

// `translate(x, y)`, the one transform this engine reads.
//
// Bootstrap's `.translate-middle` is `translate(-50%, -50%)` and it is how every
// centred overlay in the framework is positioned - a percentage of the element's
// OWN size, which is what makes it centre on its anchor point rather than on
// anything about its parent. It is a paint-time transform in CSS and a pure
// offset here, which is identical for a translation and nothing else; a rotation
// or a scale needs a real transform on the display list and is not modelled.
struct translation {
    length x{0, unit::px};
    length y{0, unit::px};
};

[[nodiscard]] inline translation parse_translate(std::string_view text) {
    translation out;
    const std::size_t open = text.find('(');
    if (open == std::string_view::npos || !text.ends_with(')')) { return out; }
    const std::string_view name = trim(text.substr(0, open), html_whitespace);
    // THE ONE-AXIS FORMS ARE SEPARATE FUNCTIONS, not `translate` with a default.
    // Bootstrap writes `translateX(-50%)` for `.translate-middle-x` and
    // `translate(-50%, -50%)` for `.translate-middle`, so reading only the
    // two-argument spelling centred one of them and left the other where it was -
    // which looks like a positioning bug and is a parsing one.
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

// --- flex -----------------------------------------------------------------

enum class flex_direction : std::uint8_t {
    row,
    row_reverse,
    column,
    column_reverse
};

enum class flex_wrap : std::uint8_t {
    nowrap,
    wrap,
    wrap_reverse
};

// `justify-content`, `align-content`, `align-items` and `align-self` spell
// overlapping value sets, so they share ONE enum rather than four that have to
// be kept in step. Which values are legal differs per property, and that is the
// parser's business rather than the type's: `justify-content: baseline` is not a
// thing, and neither is `align-items: space-between`.
enum class flex_align : std::uint8_t {
    auto_,  // `align-self` only: whatever the container's `align-items` says
    normal, // the initial value: stretch for the align-* trio, start for justify
    flex_start,
    flex_end,
    center,
    baseline,
    stretch,
    space_between,
    space_around,
    space_evenly
};

// Every flex property one box needs, resolved once for the same reason the
// lengths are: layout would otherwise re-parse eleven strings per box per pass.
//
// BY VALUE on box_node, and deliberately: box_node is copyable today, and
// normalise() and the parallel driver move whole subtrees around. Making it
// move-only to save these ~60 bytes on a ~250-byte struct is the wrong trade.
// If this ever grows a container - a `flex-flow` list, say - that changes.
//
// Every box carries both halves, container and item, because every box is
// potentially both: a `.col` is an item of its `.row` and the container of the
// nested row inside it.
struct flex_spec {
    // The CONTAINER half.
    flex_direction direction = flex_direction::row;
    flex_wrap wrap = flex_wrap::nowrap;
    flex_align justify = flex_align::normal;    // justify-content
    flex_align items = flex_align::normal;      // align-items
    flex_align line_align = flex_align::normal; // align-content
    length row_gap{0, unit::px};
    length column_gap{0, unit::px};
    // The ITEM half.
    float grow = 0;
    float shrink = 1;
    length basis{}; // `auto`
    flex_align self = flex_align::auto_;
    int order = 0;

    // The main axis is horizontal for `row`, vertical for `column`. Asked often
    // enough that spelling the comparison at each site would be four chances to
    // forget `row-reverse`.
    [[nodiscard]] constexpr bool horizontal() const noexcept {
        return direction == flex_direction::row || direction == flex_direction::row_reverse;
    }
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

// The Box Alignment spellings as well as the flex ones. `start`/`end` are the
// axis-relative keywords and `left`/`right` the physical ones; in a
// left-to-right row they mean what `flex-start`/`flex-end` mean, and this engine
// has no writing modes for them to differ in. Saying so here rather than
// answering `normal` to them is what stops a page written in the newer spelling
// silently losing its alignment.
//
// `safe`/`unsafe` prefixes and `first`/`last baseline` are consumed by taking the
// LAST word, which is the keyword in every one of those forms.
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

// A `<number>`, for `flex-grow`/`flex-shrink`/`order`. Negative values are
// invalid for the two factors and the caller clamps; `order` may be negative,
// which is how a utility pulls an item in front of its source-order siblings.
[[nodiscard]] inline float parse_flex_number(std::string_view text, float fallback) {
    text = trim(text, " \t");
    if (text.empty()) { return fallback; }
    float value = 0;
    if (std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc{}) {
        return fallback;
    }
    return value;
}

// `row-gap`/`column-gap`. `normal` is zero for a flex container, which is the
// spec's own wording rather than a simplification: `normal` only means something
// other than zero for multi-column.
[[nodiscard]] inline length parse_gap(std::string_view text) {
    if (text.empty() || text == "normal") { return length{0, unit::px}; }
    const length parsed = parse_length(text);
    return parsed.is_auto() ? length{0, unit::px} : parsed;
}

// The CSS box sides, from a 1-to-4-value shorthand plus per-side overrides.
struct side_lengths {
    length top, right, bottom, left;
};

// Tags that generate NO box unless a sheet overrides them. This is not an
// optimisation - without it a page's <script> source and <style> rules render
// as visible text, which is what the previous engine's layout::detail::skipped_tag existed to
// prevent. It is a stand-in for the UA stylesheet's display:none rules, which
// arrive with ua.hpp's port.
[[nodiscard]] inline bool generates_no_box(std::string_view tag) {
    constexpr std::string_view hidden[] = {"head", "style", "script", "title",
                                           "meta", "link",  "base",   "template"};
    return std::ranges::find(hidden, tag) != std::ranges::end(hidden);
}

// The tag list HTML renders inline by default, when the sheet says nothing.
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

// Elements sized by what they ARE rather than by what they contain. A <canvas>
// is its pixel buffer; an <input> is a field wide enough to type in. Laying
// either out from its children gives a box of zero height, which is what
// happens to every parser that does not know about replaced elements.
[[nodiscard]] inline bool is_replaced_tag(std::string_view tag) {
    // <svg> is replaced for the SAME reason as the rest, plus one more: being
    // replaced is what stops build_children descending into it, so the shapes
    // inside a graphic stop generating boxes. Before that, an <svg><text> put
    // its words in the document flow at body font size.
    // <button> IS NOT HERE ANY MORE. It was, and being replaced was the only
    // reason it shrink-wrapped - a replaced element takes its size from the
    // element rather than from its content - but it also meant the cascade had
    // no say in that size, so a `.btn`'s padding, border and radius did nothing
    // at all. Its box now comes from the UA sheet's real padding and border
    // (style/ua.hpp) and its label from the ordinary text path, which is what
    // lets a stylesheet override either. `<input type=button|submit|reset>`
    // stays replaced and keeps the widget painter's arm.
    constexpr std::string_view names[] = {"canvas", "img",    "input", "select", "textarea",
                                          "video",  "iframe", "embed", "object", "svg"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

// What `display` a tag has before any sheet speaks.
[[nodiscard]] inline display_kind default_display_for(std::string_view tag) {
    if (generates_no_box(tag)) { return display_kind::none; }
    // A LIST ITEM IS ONE BEFORE ANY SHEET SPEAKS. The UA sheet says so too, and
    // saying it in both places is not redundancy: this function is what a
    // document with no stylesheet at all gets, and a bare `<li>` has a bullet in
    // every browser. The sheet's copy is what a page can then override.
    if (tag == "li") { return display_kind::list_item; }
    return is_inline_by_default(tag) ? display_kind::inline_level : display_kind::block;
}

} // namespace ctbrowser::layout
