// calc() - the unit table: every unit the specification names, which of them
// convert by a constant, one dimension in its family's canonical unit, and the
// pixel bases every relative length goes through.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// --- the unit table ------------------------------------------------------
//
// EVERY UNIT THE SPECIFICATION NAMES, whether or not this engine can resolve it,
// because "I have no basis for `cqw`" and "`cqw` is not a unit" are different
// answers and only the second may delete a declaration. A known unit with no
// basis makes the expression `unresolved` - well formed, no answer here - which
// is the same third outcome `min(10px, 5%)` already used.
constexpr std::string_view known_units[] = {
    // <length>, CSS Values 4 §6
    "em", "rem", "ex", "rex", "ch", "rch", "cap", "rcap", "ic", "ric", "lh", "rlh", "vw", "vh",
    "vi", "vb", "vmin", "vmax", "svw", "svh", "svi", "svb", "svmin", "svmax", "lvw", "lvh", "lvi",
    "lvb", "lvmin", "lvmax", "dvw", "dvh", "dvi", "dvb", "dvmin", "dvmax", "cqw", "cqh", "cqi",
    "cqb", "cqmin", "cqmax", "cm", "mm", "q", "in", "pt", "pc", "px",
    // <angle>, <time>, <frequency>, <resolution>, <flex>
    "deg", "grad", "rad", "turn", "s", "ms", "hz", "khz", "dpi", "dpcm", "dppx", "x", "fr"};

// The one token of a single-value length, or nullptr if the text is not exactly
// one value. `12px 4px` is a shorthand and answering with its first component
// would be a quiet wrong answer.
[[nodiscard]] const css_token * lone_value(const token_stream & tokens) {
    std::size_t at = 0;
    while (tokens.tokens[at].type == token_type::whitespace) { ++at; }
    std::size_t after = at + 1;
    while (tokens.tokens[after].type == token_type::whitespace) { ++after; }
    if (tokens.tokens[after].type != token_type::eof) { return nullptr; }
    return &tokens.tokens[at];
}

} // namespace

namespace detail {

[[nodiscard]] bool is_known_unit(std::string_view unit) noexcept {
    for (const std::string_view one : known_units) {
        if (ascii_iequals(one, unit)) { return true; }
    }
    return false;
}

// THE UNITS WHOSE VALUE IS THE SAME EVERYWHERE. An absolute length, an angle, a
// time, a frequency and a resolution all convert to their canonical unit by a
// constant; `em`, `vw`, `lh`, `cqw`, `fr` and `%` do not, and a SPECIFIED value
// is written before any of their bases exist.
[[nodiscard]] bool context_free_unit(std::string_view unit) noexcept {
    static constexpr std::string_view units[] = {"px",  "cm",   "mm",   "q",    "in", "pt", "pc",
                                                 "deg", "grad", "rad",  "turn", "s",  "ms", "hz",
                                                 "khz", "dpi",  "dpcm", "dppx", "x"};
    for (const std::string_view one : units) {
        if (ascii_iequals(one, unit)) { return true; }
    }
    return false;
}

// One dimension in its family's canonical unit. `nullopt` means this file has no
// basis for it; `is_known_unit` is what tells that from a typo.
[[nodiscard]] std::optional<term> canonical_term(double value, std::string_view unit,
                                                 const length_context & ctx) {
    term out;
    // The four families with a fixed conversion. CSS Values 4 §6.4-§6.7: the
    // canonical units are deg, s, Hz and dppx, and every member converts by a
    // constant, so there is nothing context-dependent about any of them.
    struct fixed {
        std::string_view unit;
        numeric_type type;
        double factor;
    };
    static constexpr fixed table[] = {
        {"deg", numeric_type::angle, 1.0},
        {"grad", numeric_type::angle, 0.9},
        {"rad", numeric_type::angle, 180.0 / std::numbers::pi},
        {"turn", numeric_type::angle, 360.0},
        {"s", numeric_type::time, 1.0},
        {"ms", numeric_type::time, 0.001},
        {"hz", numeric_type::frequency, 1.0},
        {"khz", numeric_type::frequency, 1000.0},
        {"dppx", numeric_type::resolution, 1.0},
        {"x", numeric_type::resolution, 1.0},
        {"dpi", numeric_type::resolution, 1.0 / 96.0},
        {"dpcm", numeric_type::resolution, 2.54 / 96.0},
        // `fr` converts to itself and to nothing else. It is here for its TYPE,
        // not for a basis: what a flex is worth is a grid track sizing question
        // and no expression can answer it, but `1fr + 1fr` is 2fr and `1px +
        // 1fr` is a type error, and both need the family named.
        {"fr", numeric_type::flex, 1.0},
    };
    for (const fixed & one : table) {
        if (ascii_iequals(one.unit, unit)) {
            out.set_type(one.type);
            out.value = value * one.factor;
            return out;
        }
    }
    // A length goes through the one function that owns the pixel bases, so there
    // is exactly one place `rem` and `vw` are defined. IN DOUBLE, like every
    // other term: `mod(18vw, 5vw)` folded at float precision came out
    // `30.720005px` against the `30.72px` a plain `3vw` prints
    // (round-mod-rem-computed), because two bases rounded to 24 bits and then
    // subtracted three times over do not cancel. The cascade's `folded()` folds
    // a plain `1cm` through this same function, so the two paths agree.
    if (const std::optional<double> px = unit_to_px(value, unit, ctx)) {
        out.set_type(numeric_type::length);
        out.value = *px;
        return out;
    }
    return std::nullopt;
}

// ONE DIMENSION WITH NO BASES AVAILABLE, which is what a SPECIFIED value is
// written against. A unit whose value is the same everywhere folds into its
// family's canonical unit exactly as it always did; one that needs a font size,
// a viewport or a container becomes a term of its OWN, keyed by the unit the
// author wrote. `nullopt` for a typo, which is a syntax error at any stage.
//
// `fr` is here rather than in `context_free_unit` because the two questions
// differ: a flex converts to nothing and never will, so `calc(1fr + 1fr)` may
// not be FOLDED against a basis - but it is still two terms of one unit and
// `calc(2fr)` is their sum.
[[nodiscard]] std::optional<term> symbolic_term(double value, std::string_view unit) {
    if (context_free_unit(unit)) {
        const std::optional<term> fixed = canonical_term(value, unit, length_context{});
        if (!fixed) { return std::nullopt; }
        term out;
        out.dims = fixed->dims;
        add_symbol(out, canonical_unit(out.type()), fixed->value);
        return out;
    }
    if (!is_known_unit(unit)) { return std::nullopt; }
    term out;
    out.set_type(ascii_iequals(unit, "fr") ? numeric_type::flex : numeric_type::length);
    add_symbol(out, ascii_lower_copy(unit), value);
    return out;
}

std::string_view canonical_unit(numeric_type type) noexcept {
    switch (type) {
    case numeric_type::number: return {};
    case numeric_type::length: return "px";
    case numeric_type::angle: return "deg";
    case numeric_type::time: return "s";
    case numeric_type::frequency: return "hz";
    case numeric_type::resolution: return "dppx";
    case numeric_type::flex: return "fr";
    }
    return {};
}

std::optional<double> unit_to_px(double value, std::string_view unit, const length_context & ctx) {
    if (unit.empty()) { return value; } // a plain number in a calc term
    if (ascii_iequals(unit, "px")) { return value; }
    if (ascii_iequals(unit, "em")) { return value * ctx.font_size; }
    if (ascii_iequals(unit, "rem")) { return value * ctx.root_font_size; }
    // `ch` is the advance of `0`, which the context carries when the shell
    // injected a measurement (engine::set_text_measure) and otherwise CSS's
    // own fallback of half an em. `ex` needs an x-height no backend here
    // exposes and takes the fallback always. Bootstrap uses neither.
    if (ascii_iequals(unit, "ch")) {
        return value * (ctx.zero_advance > 0.0f ? ctx.zero_advance : ctx.font_size / 2);
    }
    if (ascii_iequals(unit, "rch")) {
        return value *
               (ctx.root_zero_advance > 0.0f ? ctx.root_zero_advance : ctx.root_font_size / 2);
    }
    if (ascii_iequals(unit, "ex")) { return value * ctx.font_size / 2; }
    if (ascii_iequals(unit, "rex")) { return value * ctx.root_font_size / 2; }
    // `cap` is the cap height, and where that cannot be determined CSS Values 4
    // §6.1.1 says the font's ASCENT is used - which is 0.8em, layout's own
    // fallback ascent (layout/values.hpp). No backend here exposes either.
    if (ascii_iequals(unit, "cap")) { return value * ctx.font_size * 0.8; }
    if (ascii_iequals(unit, "rcap")) { return value * ctx.root_font_size * 0.8; }
    // `ic` is the advance of the CJK water ideograph, and CSS's own fallback
    // for a font without one is 1em. `lh` and `rlh` are the line heights the
    // context carries - see length_context.
    if (ascii_iequals(unit, "ic")) { return value * ctx.font_size; }
    if (ascii_iequals(unit, "ric")) { return value * ctx.root_font_size; }
    if (ascii_iequals(unit, "lh")) { return value * ctx.line_height; }
    if (ascii_iequals(unit, "rlh")) { return value * ctx.root_line_height; }
    // THE VIEWPORT UNITS, all six spellings of each axis. There is no dynamic
    // toolbar here, so the small, large, dynamic and default viewports are one
    // and the same; and the writing mode is horizontal, so `vi` is `vw` and
    // `vb` is `vh`. viewport-units-compute asks for all twenty-four.
    //
    // ponytail: horizontal-tb assumed for `vi`/`vb`; thread the writing mode
    // through length_context when a vertical page asks.
    const auto viewport_axis = [&](std::string_view suffix) -> std::optional<double> {
        const double w = ctx.viewport_width;
        const double h = ctx.viewport_height;
        if (suffix == "w" || suffix == "i") { return w; }
        if (suffix == "h" || suffix == "b") { return h; }
        if (suffix == "min") { return std::min(w, h); }
        if (suffix == "max") { return std::max(w, h); }
        return std::nullopt;
    };
    if (unit.size() >= 2 && (unit[0] == 'v' || unit[0] == 'V')) {
        if (const auto axis = viewport_axis(ascii_lower_copy(unit.substr(1)))) {
            return value * *axis / 100.0;
        }
    }
    if (unit.size() >= 3 && (unit[1] == 'v' || unit[1] == 'V') &&
        (unit[0] == 's' || unit[0] == 'l' || unit[0] == 'd' || unit[0] == 'S' || unit[0] == 'L' ||
         unit[0] == 'D')) {
        if (const auto axis = viewport_axis(ascii_lower_copy(unit.substr(2)))) {
            return value * *axis / 100.0;
        }
    }
    // THE CONTAINER QUERY UNITS, CSS Containment 3 §container-lengths: "if no
    // eligible query container is available, then use the small viewport size
    // for that axis". There are no query containers here, so that is what they
    // always are - `sign(0cqi / 1px)` is 0, not a value waiting for layout.
    if (unit.size() >= 3 && (unit[0] == 'c' || unit[0] == 'C') &&
        (unit[1] == 'q' || unit[1] == 'Q')) {
        if (const auto axis = viewport_axis(ascii_lower_copy(unit.substr(2)))) {
            return value * *axis / 100.0;
        }
    }
    // The absolute units, all defined against the CSS inch of 96px.
    if (ascii_iequals(unit, "in")) { return value * 96.0; }
    if (ascii_iequals(unit, "cm")) { return value * 96.0 / 2.54; }
    if (ascii_iequals(unit, "mm")) { return value * 96.0 / 25.4; }
    if (ascii_iequals(unit, "q")) { return value * 96.0 / 101.6; }
    if (ascii_iequals(unit, "pt")) { return value * 96.0 / 72.0; }
    if (ascii_iequals(unit, "pc")) { return value * 16.0; }
    return std::nullopt;
}

} // namespace detail

std::optional<float> length_text_to_px(std::string_view text, const length_context & ctx) {
    // Tokenized rather than scanned, so `1.5e1px` and an escaped unit behave the
    // same here as they do everywhere else in the front end.
    const token_stream tokens = tokenize(text);
    const css_token * tok = lone_value(tokens);
    if (tok == nullptr) { return std::nullopt; }
    if (tok->type == token_type::number) { return static_cast<float>(tok->number); }
    if (tok->type != token_type::dimension) { return std::nullopt; }
    const std::optional<double> px = unit_to_px(tok->number, tokens.unit_of(*tok), ctx);
    if (!px) { return std::nullopt; }
    return static_cast<float>(*px);
}

std::optional<std::string> canonical_dimension_text(std::string_view text,
                                                    const length_context & ctx) {
    const token_stream tokens = tokenize(text);
    const css_token * tok = lone_value(tokens);
    if (tok == nullptr || tok->type != token_type::dimension) { return std::nullopt; }
    const math_answer answer = evaluate_math(text, ctx);
    if (answer.outcome != math_outcome::resolved) { return std::nullopt; }
    return serialize_calc(answer.value);
}

} // namespace ctbrowser::style::css
