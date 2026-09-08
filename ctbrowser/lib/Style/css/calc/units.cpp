// calc() - the unit table: every unit the specification names, which of them
// convert by a constant, one dimension in its family's canonical unit, and the
// pixel bases every relative length goes through.
//
// One of five files carved out of a 1,810-line css/calc.cpp on 2026-09-08. The
// public surface is include/ctbrowser/style/css/calc.hpp and did not change;
// the helpers more than one of these files needs are declared in internal.hpp
// beside this, with external linkage in ctbrowser::style::css::detail.

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
            out.type = one.type;
            out.value = value * one.factor;
            return out;
        }
    }
    // A length goes through the one function that owns the pixel bases, so there
    // is exactly one place `rem` and `vw` are defined - and, deliberately, at the
    // same `float` precision the rest of the cascade folds a plain `1cm` at.
    // `test_math_used` compares the computed value of `min(1cm)` against that of
    // `1cm`, so the two paths agreeing matters far more here than either being
    // exact; widening this one alone would break every such pair. It can become a
    // double the day `folded()` in `style/engine.hpp` folds through
    // `canonical_dimension_text`.
    if (const std::optional<float> px = unit_to_px(static_cast<float>(value), unit, ctx)) {
        out.type = numeric_type::length;
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
        out.type = fixed->type;
        add_symbol(out, canonical_unit(out.type), fixed->value);
        return out;
    }
    if (!is_known_unit(unit)) { return std::nullopt; }
    term out;
    out.type = ascii_iequals(unit, "fr") ? numeric_type::flex : numeric_type::length;
    add_symbol(out, ascii_lower_copy(unit), value);
    return out;
}

} // namespace detail

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

std::optional<float> unit_to_px(float value, std::string_view unit, const length_context & ctx) {
    if (unit.empty()) { return value; } // a plain number in a calc term
    if (ascii_iequals(unit, "px")) { return value; }
    if (ascii_iequals(unit, "em")) { return value * ctx.font_size; }
    if (ascii_iequals(unit, "rem")) { return value * ctx.root_font_size; }
    // ch and ex need font metrics the style engine deliberately cannot see -
    // layout/values.hpp is explicit that measurement is injected - so both take
    // CSS's own fallback of half an em. Bootstrap uses neither.
    if (ascii_iequals(unit, "ch") || ascii_iequals(unit, "ex")) {
        return value * ctx.font_size / 2;
    }
    if (ascii_iequals(unit, "vw")) { return value * ctx.viewport_width / 100.0f; }
    if (ascii_iequals(unit, "vh")) { return value * ctx.viewport_height / 100.0f; }
    if (ascii_iequals(unit, "vmin")) {
        return value * std::min(ctx.viewport_width, ctx.viewport_height) / 100.0f;
    }
    if (ascii_iequals(unit, "vmax")) {
        return value * std::max(ctx.viewport_width, ctx.viewport_height) / 100.0f;
    }
    // The absolute units, all defined against the CSS inch of 96px.
    if (ascii_iequals(unit, "in")) { return value * 96.0f; }
    if (ascii_iequals(unit, "cm")) { return value * 96.0f / 2.54f; }
    if (ascii_iequals(unit, "mm")) { return value * 96.0f / 25.4f; }
    if (ascii_iequals(unit, "q")) { return value * 96.0f / 101.6f; }
    if (ascii_iequals(unit, "pt")) { return value * 96.0f / 72.0f; }
    if (ascii_iequals(unit, "pc")) { return value * 16.0f; }
    return std::nullopt;
}

std::optional<float> dimension_text_to_px(std::string_view text, const length_context & ctx) {
    const token_stream tokens = tokenize(text);
    const css_token * tok = lone_value(tokens);
    if (tok == nullptr || tok->type != token_type::dimension) { return std::nullopt; }
    return unit_to_px(static_cast<float>(tok->number), tokens.unit_of(*tok), ctx);
}

std::optional<float> length_text_to_px(std::string_view text, const length_context & ctx) {
    // Tokenized rather than scanned, so `1.5e1px` and an escaped unit behave the
    // same here as they do everywhere else in the front end.
    const token_stream tokens = tokenize(text);
    const css_token * tok = lone_value(tokens);
    if (tok == nullptr) { return std::nullopt; }
    if (tok->type == token_type::number) { return static_cast<float>(tok->number); }
    if (tok->type != token_type::dimension) { return std::nullopt; }
    return unit_to_px(static_cast<float>(tok->number), tokens.unit_of(*tok), ctx);
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
