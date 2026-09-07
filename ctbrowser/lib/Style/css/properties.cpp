// The property table, the value grammar, and the canonical serialisation.
//
// See include/ctbrowser/style/css/properties.hpp for why this is conservative
// and where the three consumers are. The short version: a property this file
// does not model is accepted verbatim, so nothing that works today can start
// failing, and only a property with a real `value_kind` can refuse anything.

#include <ctbrowser/style/css/properties.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/token.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::style::css {
namespace {

using k = value_kind;

// The CSS-WIDE KEYWORDS, valid for every property including one this table has
// never heard of. `revert-layer` is in the list because CSS Cascade 5 defines
// it; the cascade here does not implement layers, so it is accepted and
// behaves as `revert`, which is what the specification says happens when there
// is no layer to revert to.
constexpr std::array<std::string_view, 5> wide_keywords{"inherit", "initial", "unset", "revert",
                                                        "revert-layer"};

// §6.1 of CSS Values 4, plus the viewport-relative families of §6.1.2. The
// engine RESOLVES only a subset of these (see layout/values.hpp - `rem` has a
// hardcoded 16px basis and several fall through to pixels), but resolving and
// PARSING are different questions and this one is about syntax: `width: 3ic`
// is a valid declaration whether or not this engine can place the box.
constexpr std::array<std::string_view, 39> length_units{
    "em",  "rem", "ex",    "rex",   "ch",  "rch", "cap",  "rcap", "ic",    "ric",
    "lh",  "rlh", "vw",    "vh",    "vi",  "vb",  "vmin", "vmax", "svw",   "svh",
    "svi", "svb", "svmin", "svmax", "lvw", "lvh", "lvi",  "lvb",  "lvmin", "lvmax",
    "dvw", "dvh", "dvmin", "dvmax", "cm",  "mm",  "q",    "in",   "pt"};
// The tail of the same list. Split only because the array above is at its
// declared size; `is_length_unit` consults both, so the seam is invisible.
constexpr std::array<std::string_view, 4> more_length_units{"pc", "px", "dvi", "dvb"};
constexpr std::array<std::string_view, 4> angle_units{"deg", "grad", "rad", "turn"};
constexpr std::array<std::string_view, 2> time_units{"s", "ms"};

// THE MATH FUNCTIONS, CSS Values 4 §10. A value that is one of these applied to
// the whole declaration is accepted without evaluating it: `calc.cpp` already
// owns the evaluation and answers `unresolved` for the cases that have no
// answer until layout (`min(10px, 5%)`), so refusing here would condemn a
// declaration the cascade deliberately keeps.
constexpr std::array<std::string_view, 22> math_functions{
    "calc", "min",  "max",  "clamp", "round", "mod", "rem",  "abs",   "sign", "sin", "cos",
    "tan",  "asin", "acos", "atan",  "atan2", "pow", "sqrt", "hypot", "log",  "exp", "calc-size"};

// A value containing one of these is valid by construction: what it means is
// not known until substitution, so the declaration survives parsing with its
// tokens intact. CSS Variables 1 §3.
constexpr std::array<std::string_view, 3> substitution_functions{"var", "env", "attr"};

[[nodiscard]] bool in_list(std::span<const std::string_view> list, std::string_view name) {
    return std::any_of(list.begin(), list.end(),
                       [&](std::string_view one) { return ascii_iequals(one, name); });
}

[[nodiscard]] bool is_length_unit(std::string_view unit) {
    return in_list(length_units, unit) || in_list(more_length_units, unit);
}

// A function token's name, without the `(` the tokenizer keeps on it.
[[nodiscard]] std::string_view function_name(const token_stream & ts, const css_token & t) {
    const std::string_view raw = ts.text_of(t);
    return raw.empty() ? raw : raw.substr(0, raw.size() - 1);
}

// A space-separated keyword set, matched ASCII case-insensitively. Written as
// one string rather than an array per property because there are ~90 of them
// and a `std::array` each would be ~90 more symbols for a linear scan either
// way.
[[nodiscard]] bool has_keyword(std::string_view set, std::string_view word) {
    std::size_t i = 0;
    while (i < set.size()) {
        const std::size_t end = set.find(' ', i);
        const std::string_view one = set.substr(i, end == std::string_view::npos ? end : end - i);
        if (ascii_iequals(one, word)) { return true; }
        if (end == std::string_view::npos) { break; }
        i = end + 1;
    }
    return false;
}

// --- THE TABLE ----------------------------------------------------------
//
// Ordered as CSSOM's indexed properties enumerate them, which is to say
// deliberately rather than alphabetically: the box model first, then the
// typography, then the flex and table properties. `getComputedStyle`'s `item(i)`
// walks this.
//
// EVERY SHORTHAND IS `freeform`. `margin: 10px 20px` needs the expansion to be
// refused correctly, and a shorthand this file got half right is the one way it
// could break a page that works today.
constexpr property_syntax table[] = {
    // --- the box ---------------------------------------------------------
    {"display", k::keyword_only,
     "block inline inline-block flex inline-flex grid inline-grid none table inline-table "
     "table-row table-row-group table-header-group table-footer-group table-column "
     "table-column-group table-cell table-caption list-item flow-root contents ruby",
     "inline", false, false},
    {"position", k::keyword_only, "static relative absolute fixed sticky", "static", false, false},
    {"float", k::keyword_only, "none left right inline-start inline-end", "none", false, false},
    {"clear", k::keyword_only, "none left right both inline-start inline-end", "none", false,
     false},
    {"visibility", k::keyword_only, "visible hidden collapse", "visible", true, false},
    {"overflow", k::freeform, "", "visible", false, false},
    {"overflow-x", k::keyword_only, "visible hidden clip scroll auto", "visible", false, false},
    {"overflow-y", k::keyword_only, "visible hidden clip scroll auto", "visible", false, false},
    {"box-sizing", k::keyword_only, "content-box border-box", "content-box", false, false},

    {"width", k::length_percentage, "auto min-content max-content fit-content stretch", "auto",
     false, true},
    {"height", k::length_percentage, "auto min-content max-content fit-content stretch", "auto",
     false, true},
    {"min-width", k::length_percentage, "auto min-content max-content fit-content stretch", "auto",
     false, true},
    {"min-height", k::length_percentage, "auto min-content max-content fit-content stretch", "auto",
     false, true},
    {"max-width", k::length_percentage, "none min-content max-content fit-content stretch", "none",
     false, true},
    {"max-height", k::length_percentage, "none min-content max-content fit-content stretch", "none",
     false, true},
    {"inline-size", k::length_percentage, "auto min-content max-content fit-content stretch",
     "auto", false, true},
    {"block-size", k::length_percentage, "auto min-content max-content fit-content stretch", "auto",
     false, true},

    {"margin", k::freeform, "", "0px", false, false},
    {"margin-top", k::length_percentage, "auto", "0px", false, false},
    {"margin-right", k::length_percentage, "auto", "0px", false, false},
    {"margin-bottom", k::length_percentage, "auto", "0px", false, false},
    {"margin-left", k::length_percentage, "auto", "0px", false, false},
    {"padding", k::freeform, "", "0px", false, false},
    {"padding-top", k::length_percentage, "", "0px", false, true},
    {"padding-right", k::length_percentage, "", "0px", false, true},
    {"padding-bottom", k::length_percentage, "", "0px", false, true},
    {"padding-left", k::length_percentage, "", "0px", false, true},

    {"top", k::length_percentage, "auto", "auto", false, false},
    {"right", k::length_percentage, "auto", "auto", false, false},
    {"bottom", k::length_percentage, "auto", "auto", false, false},
    {"left", k::length_percentage, "auto", "auto", false, false},
    {"inset", k::freeform, "", "auto", false, false},

    // --- borders ---------------------------------------------------------
    {"border", k::freeform, "", "medium none currentcolor", false, false},
    {"border-width", k::freeform, "", "medium", false, false},
    {"border-style", k::freeform, "", "none", false, false},
    {"border-color", k::freeform, "", "currentcolor", false, false},
    {"border-top-width", k::length, "thin medium thick", "medium", false, true},
    {"border-right-width", k::length, "thin medium thick", "medium", false, true},
    {"border-bottom-width", k::length, "thin medium thick", "medium", false, true},
    {"border-left-width", k::length, "thin medium thick", "medium", false, true},
    {"border-top-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-right-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-bottom-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-left-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-top-color", k::freeform, "", "currentcolor", false, false},
    {"border-right-color", k::freeform, "", "currentcolor", false, false},
    {"border-bottom-color", k::freeform, "", "currentcolor", false, false},
    {"border-left-color", k::freeform, "", "currentcolor", false, false},
    {"border-radius", k::freeform, "", "0px", false, false},
    {"border-top-left-radius", k::freeform, "", "0px", false, false},
    {"border-top-right-radius", k::freeform, "", "0px", false, false},
    {"border-bottom-right-radius", k::freeform, "", "0px", false, false},
    {"border-bottom-left-radius", k::freeform, "", "0px", false, false},
    {"border-collapse", k::keyword_only, "separate collapse", "separate", true, false},
    {"border-spacing", k::freeform, "", "0px", true, false},
    {"outline", k::freeform, "", "medium none currentcolor", false, false},
    {"outline-width", k::length, "thin medium thick", "medium", false, true},
    {"outline-style", k::keyword_only,
     "auto none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"outline-color", k::freeform, "", "currentcolor", false, false},
    {"outline-offset", k::length, "", "0px", false, false},

    // --- colour and background ------------------------------------------
    {"color", k::freeform, "", "rgb(0, 0, 0)", true, false},
    {"background", k::freeform, "", "none", false, false},
    {"background-color", k::freeform, "", "rgba(0, 0, 0, 0)", false, false},
    {"background-image", k::freeform, "", "none", false, false},
    {"background-position", k::freeform, "", "0% 0%", false, false},
    {"background-repeat", k::freeform, "", "repeat", false, false},
    {"background-size", k::freeform, "", "auto", false, false},
    {"background-clip", k::keyword_only, "border-box padding-box content-box text", "border-box",
     false, false},
    {"background-origin", k::keyword_only, "border-box padding-box content-box", "padding-box",
     false, false},
    {"background-attachment", k::keyword_only, "scroll fixed local", "scroll", false, false},
    {"opacity", k::number_percentage, "", "1", false, false},

    // --- typography ------------------------------------------------------
    {"font", k::freeform, "", "", true, false},
    {"font-family", k::freeform, "", "sans-serif", true, false},
    {"font-size", k::length_percentage,
     "xx-small x-small small medium large x-large xx-large xxx-large larger smaller", "medium",
     true, true},
    {"font-style", k::freeform, "", "normal", true, false},
    {"font-weight", k::number, "normal bold bolder lighter", "400", true, true},
    {"font-variant", k::freeform, "", "normal", true, false},
    {"font-stretch", k::freeform, "", "100%", true, false},
    {"line-height", k::number_length, "normal", "normal", true, true},
    {"letter-spacing", k::length, "normal", "normal", true, false},
    {"word-spacing", k::length, "normal", "normal", true, false},
    {"text-align", k::keyword_only, "start end left right center justify match-parent", "start",
     true, false},
    {"text-indent", k::length_percentage, "", "0px", true, false},
    {"text-transform", k::keyword_only,
     "none capitalize uppercase lowercase full-width full-size-kana", "none", true, false},
    {"text-decoration", k::freeform, "", "none", false, false},
    {"text-decoration-line", k::freeform, "", "none", false, false},
    {"text-decoration-color", k::freeform, "", "currentcolor", false, false},
    {"text-decoration-style", k::keyword_only, "solid double dotted dashed wavy", "solid", false,
     false},
    {"text-overflow", k::freeform, "", "clip", false, false},
    {"text-shadow", k::freeform, "", "none", true, false},
    {"white-space", k::keyword_only, "normal pre nowrap pre-wrap pre-line break-spaces", "normal",
     true, false},
    {"word-break", k::keyword_only, "normal break-all keep-all break-word", "normal", true, false},
    {"overflow-wrap", k::keyword_only, "normal break-word anywhere", "normal", true, false},
    {"direction", k::keyword_only, "ltr rtl", "ltr", true, false},
    {"unicode-bidi", k::freeform, "", "normal", false, false},
    {"tab-size", k::number_length, "", "8", true, true},
    {"vertical-align", k::length_percentage,
     "baseline sub super text-top text-bottom middle top bottom", "baseline", false, false},

    // --- flex, grid and the layout numbers -------------------------------
    {"flex", k::freeform, "", "0 1 auto", false, false},
    {"flex-grow", k::number, "", "0", false, true},
    {"flex-shrink", k::number, "", "1", false, true},
    {"flex-basis", k::length_percentage, "auto content min-content max-content fit-content", "auto",
     false, true},
    {"flex-direction", k::keyword_only, "row row-reverse column column-reverse", "row", false,
     false},
    {"flex-wrap", k::keyword_only, "nowrap wrap wrap-reverse", "nowrap", false, false},
    {"flex-flow", k::freeform, "", "row nowrap", false, false},
    {"justify-content", k::keyword_only,
     "normal stretch flex-start flex-end center space-between space-around space-evenly start end "
     "left right",
     "normal", false, false},
    {"align-items", k::keyword_only,
     "normal stretch center start end flex-start flex-end self-start self-end baseline", "normal",
     false, false},
    {"align-self", k::keyword_only,
     "auto normal stretch center start end flex-start flex-end self-start self-end baseline",
     "auto", false, false},
    {"align-content", k::keyword_only,
     "normal stretch center start end flex-start flex-end space-between space-around space-evenly",
     "normal", false, false},
    {"gap", k::freeform, "", "normal", false, false},
    {"row-gap", k::length_percentage, "normal", "normal", false, true},
    {"column-gap", k::length_percentage, "normal", "normal", false, true},
    {"order", k::integer, "", "0", false, false},
    {"z-index", k::integer, "auto", "auto", false, false},

    // --- tables and lists ------------------------------------------------
    {"table-layout", k::keyword_only, "auto fixed", "auto", false, false},
    {"caption-side", k::keyword_only, "top bottom", "top", true, false},
    {"empty-cells", k::keyword_only, "show hide", "show", true, false},
    {"list-style", k::freeform, "", "outside none disc", true, false},
    {"list-style-type", k::freeform, "", "disc", true, false},
    {"list-style-position", k::keyword_only, "inside outside", "outside", true, false},
    {"list-style-image", k::freeform, "", "none", true, false},

    // --- the rest, known to exist and not modelled -----------------------
    {"cursor", k::freeform, "", "auto", true, false},
    {"box-shadow", k::freeform, "", "none", false, false},
    {"transform", k::freeform, "", "none", false, false},
    {"transform-origin", k::freeform, "", "50% 50%", false, false},
    {"transition", k::freeform, "", "all 0s ease 0s", false, false},
    {"transition-duration", k::time, "", "0s", false, false},
    {"transition-delay", k::time, "", "0s", false, false},
    {"transition-property", k::freeform, "", "all", false, false},
    {"transition-timing-function", k::freeform, "", "ease", false, false},
    {"animation", k::freeform, "", "none", false, false},
    {"animation-duration", k::time, "", "0s", false, false},
    {"animation-delay", k::time, "", "0s", false, false},
    {"animation-name", k::freeform, "", "none", false, false},
    {"animation-iteration-count", k::freeform, "", "1", false, false},
    {"filter", k::freeform, "", "none", false, false},
    {"content", k::freeform, "", "normal", false, false},
    {"pointer-events", k::freeform, "", "auto", true, false},
    {"user-select", k::keyword_only, "auto text none contain all", "auto", false, false},
    {"resize", k::keyword_only, "none both horizontal vertical block inline", "none", false, false},
    {"object-fit", k::keyword_only, "fill contain cover none scale-down", "fill", false, false},
    {"object-position", k::freeform, "", "50% 50%", false, false},
    {"rotate", k::angle, "none", "none", false, false},
    {"scale", k::freeform, "", "none", false, false},
    {"translate", k::freeform, "", "none", false, false},
};

// --- serialisation -------------------------------------------------------

// A CSS number, shortest form. `0` rather than `-0`, `0.5` rather than
// `0.500000`, and `1` rather than `1.0` - which is what CSSOM §6.7.2 means by
// "the smallest number of digits", and what every `assert_equals(readValue,
// "1")` in the corpus compares against.
[[nodiscard]] std::string number_text(double value) {
    if (!std::isfinite(value)) { return value > 0 ? "infinity" : "-infinity"; }
    if (value == 0) { return "0"; } // catches -0, which serialises as 0
    std::string out = std::to_string(value);
    if (out.find('.') != std::string::npos) {
        while (!out.empty() && out.back() == '0') { out.pop_back(); }
        if (!out.empty() && out.back() == '.') { out.pop_back(); }
    }
    return out.empty() ? "0" : out;
}

// The token stream put back together with ONE space where whitespace was, a
// space after every comma and none before one. That is not a full CSSOM
// serialisation - it does not rebuild functions or reorder anything - but it is
// idempotent, which is what `test_valid_value`'s round-trip half checks: set
// the value that came back and get the same string again.
[[nodiscard]] std::string serialize_tokens(const token_stream & ts) {
    std::string out;
    bool pending_space = false;
    for (const css_token & t : ts.tokens) {
        if (t.type == token_type::eof) { break; }
        if (t.type == token_type::whitespace) {
            pending_space = !out.empty();
            continue;
        }
        if (t.type == token_type::close_paren || t.type == token_type::close_square ||
            t.type == token_type::comma) {
            pending_space = false;
        }
        if (pending_space) { out += ' '; }
        pending_space = false;
        out += ts.text_of(t);
        if (t.type == token_type::comma) { pending_space = true; }
    }
    return out;
}

// --- the value grammar ---------------------------------------------------

struct scan {
    std::vector<std::size_t> significant; // indices of the non-whitespace tokens
    bool malformed = false;               // a bad string/url, or unbalanced brackets
    bool important = false;               // a `!` delim; `!important` is not a value
    bool substituted = false;             // holds a var()/env()/attr()
};

[[nodiscard]] scan scan_tokens(const token_stream & ts) {
    scan out;
    int depth = 0;
    for (std::size_t i = 0; i < ts.tokens.size(); ++i) {
        const css_token & t = ts.tokens[i];
        if (t.type == token_type::eof) { break; }
        if (t.type == token_type::whitespace) { continue; }
        if (t.type == token_type::bad_string || t.type == token_type::bad_url) {
            out.malformed = true;
        }
        if (t.type == token_type::delim && ts.text_of(t) == "!") { out.important = true; }
        if (t.type == token_type::function) {
            if (in_list(substitution_functions, function_name(ts, t))) { out.substituted = true; }
            ++depth;
        } else if (t.type == token_type::open_paren || t.type == token_type::open_square ||
                   t.type == token_type::open_curly) {
            ++depth;
        } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                   t.type == token_type::close_curly) {
            if (--depth < 0) { out.malformed = true; }
        }
        out.significant.push_back(i);
    }
    if (depth != 0) { out.malformed = true; }
    return out;
}

// Whether the WHOLE value is one math function applied to everything - which is
// the only shape this file accepts one in. `calc(1px) calc(2px)` is two values
// and belongs to a property that takes two.
[[nodiscard]] bool whole_value_is_math(const token_stream & ts, const scan & found) {
    if (found.significant.empty()) { return false; }
    const css_token & first = ts.tokens[found.significant.front()];
    if (first.type != token_type::function) { return false; }
    if (!in_list(math_functions, function_name(ts, first))) { return false; }
    // AN EMPTY ARGUMENT LIST IS NOT A MATH FUNCTION. `round()` is the corpus's
    // own example of an invalid value (`css/css-values/round-mod-rem-invalid`),
    // and without this test it was accepted as "a math function over the whole
    // value" - the exact `expected "" but got "round()"` this file exists to
    // end.
    if (found.significant.size() < 3) { return false; }
    // The matching `)` must be the last significant token; anything after it is
    // a second value.
    int depth = 0;
    for (const std::size_t at : found.significant) {
        const token_type type = ts.tokens[at].type;
        if (type == token_type::function || type == token_type::open_paren) { ++depth; }
        if (type == token_type::close_paren) {
            if (--depth == 0) { return at == found.significant.back(); }
        }
    }
    return false;
}

// One typed component, matched and serialised. `false` means "not this type",
// never "malformed" - the caller decides what an unmatched value means.
[[nodiscard]] bool match_typed(const token_stream & ts, const css_token & t,
                               const property_syntax & p, std::string & out) {
    const bool takes_length =
        p.kind == k::length || p.kind == k::length_percentage || p.kind == k::number_length;
    const bool takes_percentage = p.kind == k::length_percentage || p.kind == k::percentage ||
                                  p.kind == k::number_percentage || p.kind == k::number_length;
    const bool takes_number = p.kind == k::number || p.kind == k::integer ||
                              p.kind == k::number_percentage || p.kind == k::number_length;
    if (p.nonnegative && t.number < 0) { return false; }

    switch (t.type) {
    case token_type::number:
        if (p.kind == k::integer && (t.flags & flag_integer) == 0) { return false; }
        // A UNITLESS ZERO IS A LENGTH, and only zero is. `width: 0` is valid and
        // serialises as `0px`; `width: 1` is not a length at all.
        if (!takes_number && takes_length && t.number == 0) {
            out = "0px";
            return true;
        }
        if (!takes_number) { return false; }
        out = number_text(t.number);
        return true;
    case token_type::percentage:
        if (!takes_percentage) { return false; }
        out = number_text(t.number) + "%";
        return true;
    case token_type::dimension: {
        const std::string_view unit = ts.unit_of(t);
        const bool ok = (takes_length && is_length_unit(unit)) ||
                        (p.kind == k::angle && in_list(angle_units, unit)) ||
                        (p.kind == k::time && in_list(time_units, unit));
        if (!ok) { return false; }
        out = number_text(t.number) + ascii_lower_copy(unit);
        return true;
    }
    default: return false;
    }
}

} // namespace

const property_syntax * find_property(std::string_view name) {
    for (const property_syntax & one : table) {
        if (ascii_iequals(one.name, name)) { return &one; }
    }
    return nullptr;
}

std::span<const property_syntax> known_properties() {
    return std::span<const property_syntax>{table, std::size(table)};
}

value_check check_declaration(std::string_view property, std::string_view value) {
    const std::string_view text = trim(value, html_whitespace);
    // An EMPTY value removes the declaration, which is how `test_invalid_value`
    // clears the property before setting it and how a page turns one off. It is
    // reported as invalid because the two callers want the same thing from it:
    // store nothing.
    if (text.empty()) { return {}; }

    const token_stream ts = tokenize(text);
    const scan found = scan_tokens(ts);
    if (found.malformed || found.important || found.significant.empty()) { return {}; }

    // A CSS-WIDE KEYWORD is valid for every property, including one this table
    // has never heard of, and serialises lowercased.
    if (found.significant.size() == 1) {
        const css_token & only = ts.tokens[found.significant.front()];
        if (only.type == token_type::ident && in_list(wide_keywords, ts.text_of(only))) {
            return {true, ascii_lower_copy(ts.text_of(only))};
        }
    }

    // A CUSTOM PROPERTY takes anything that tokenises, by definition (CSS
    // Variables 1 §2): its value is a token stream, not a value.
    if (property.starts_with("--")) {
        return {true, std::string{text}};
    }

    const property_syntax * p = find_property(property);
    // AN UNKNOWN PROPERTY IS STORED, NOT REFUSED. CSSOM says a page may set one
    // and read it back; refusing here would be a behaviour change for every
    // property this table has not reached yet, and the corpora write several.
    if (p == nullptr) { return {true, serialize_tokens(ts)}; }

    // A value holding var()/env()/attr() is valid by construction - what it
    // means is not known until substitution.
    if (found.substituted) { return {true, serialize_tokens(ts)}; }

    if (p->kind == k::freeform) { return {true, serialize_tokens(ts)}; }

    if (found.significant.size() == 1) {
        const css_token & only = ts.tokens[found.significant.front()];
        if (only.type == token_type::ident && has_keyword(p->keywords, ts.text_of(only))) {
            return {true, ascii_lower_copy(ts.text_of(only))};
        }
        std::string serialized;
        if (p->kind != k::keyword_only && match_typed(ts, only, *p, serialized)) {
            return {true, std::move(serialized)};
        }
    }

    // A math function over the whole value, kept as written: `calc.cpp` owns
    // the evaluation and has a third answer besides folded and invalid.
    if (p->kind != k::keyword_only && whole_value_is_math(ts, found)) {
        return {true, serialize_tokens(ts)};
    }
    return {};
}

bool supports_declaration(std::string_view property, std::string_view value) {
    // `CSS.supports` asks about a property this engine implements, so an
    // unknown name is false here even though `el.style` stores it. That is the
    // one place the two callers differ and it is what the specification says:
    // §5 of CSS Conditional 3 is "would the declaration be dropped".
    if (property.starts_with("--")) { return !trim(value, html_whitespace).empty(); }
    if (find_property(property) == nullptr) { return false; }
    return check_declaration(property, value).valid;
}

namespace {

// `not X`, `X and Y`, `X or Y`, `(...)` and a bare `( p : v )`. Written over the
// raw text with a bracket-depth counter rather than over the token stream: the
// grammar is about the SHAPE of the parentheses, and the tokens have already
// thrown away which ones were adjacent to what.
[[nodiscard]] std::size_t find_top_level(std::string_view text, std::string_view word,
                                         std::size_t from) {
    int depth = 0;
    for (std::size_t i = from; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(') { ++depth; }
        if (c == ')') { --depth; }
        if (depth != 0 || i == 0) { continue; }
        if (i + word.size() > text.size()) { break; }
        if (!ascii_iequals(text.substr(i, word.size()), word)) { continue; }
        const bool before = text[i - 1] == ' ' || text[i - 1] == ')';
        const std::size_t after_at = i + word.size();
        const bool after =
            after_at >= text.size() || text[after_at] == ' ' || text[after_at] == '(';
        if (before && after) { return i; }
    }
    return std::string_view::npos;
}

[[nodiscard]] bool condition(std::string_view text, int depth);

[[nodiscard]] bool leaf(std::string_view inner, int depth) {
    // A nested group - `((color: red))` - before a declaration, because a
    // declaration's property may not contain a parenthesis.
    const std::string_view body = trim(inner, html_whitespace);
    if (body.empty()) { return false; }
    if (body.front() == '(' || ascii_istarts_with(body, "not ")) {
        return condition(body, depth + 1);
    }
    if (find_top_level(body, "and", 0) != std::string_view::npos ||
        find_top_level(body, "or", 0) != std::string_view::npos) {
        return condition(body, depth + 1);
    }
    const std::size_t colon = body.find(':');
    if (colon == std::string_view::npos) { return false; }
    std::string_view value = trim(body.substr(colon + 1), html_whitespace);
    // `!important` is part of a <declaration> and does not change the answer.
    const std::size_t bang = value.rfind('!');
    if (bang != std::string_view::npos &&
        ascii_iequals(trim(value.substr(bang + 1), html_whitespace), "important")) {
        value = trim(value.substr(0, bang), html_whitespace);
    }
    return supports_declaration(trim(body.substr(0, colon), html_whitespace), value);
}

bool condition(std::string_view text, int depth) {
    // A page can nest these as deep as it likes; the recursion is bounded so a
    // hostile string cannot exhaust the C++ stack.
    if (depth > 32) { return false; }
    const std::string_view body = trim(text, html_whitespace);
    if (body.empty()) { return false; }

    if (ascii_istarts_with(body, "not ") || ascii_istarts_with(body, "not(")) {
        return !condition(body.substr(3), depth + 1);
    }
    for (const std::string_view op : {std::string_view{"and"}, std::string_view{"or"}}) {
        const std::size_t at = find_top_level(body, op, 0);
        if (at == std::string_view::npos) { continue; }
        const bool left = condition(body.substr(0, at), depth + 1);
        const bool right = condition(body.substr(at + op.size()), depth + 1);
        return op == "and" ? (left && right) : (left || right);
    }
    if (body.front() != '(' || body.back() != ')') { return false; }
    return leaf(body.substr(1, body.size() - 2), depth);
}

} // namespace

bool supports_condition(std::string_view text) {
    return condition(text, 0);
}

std::string css_name_of(std::string_view idl) {
    // A CUSTOM PROPERTY has no IDL name and passes through untouched, capitals
    // and all: `--myVar` and `--myvar` are two different properties.
    if (idl.starts_with("--")) { return std::string{idl}; }
    // The two exceptions CSSOM §6.7.1 names by hand.
    if (idl == "cssFloat") { return "float"; }
    std::string out;
    // `webkitTransform` -> `-webkit-transform`: the prefix's leading dash is
    // dropped in the IDL name, so a plain camel-to-hyphen loop produces
    // `webkit-transform` and misses every prefixed property.
    if (ascii_istarts_with(idl, "webkit") && idl.size() > 6 && idl[6] >= 'A' && idl[6] <= 'Z') {
        out = "-webkit";
        idl.remove_prefix(6);
    }
    for (const char c : idl) {
        if (c >= 'A' && c <= 'Z') {
            out += '-';
            out += ascii_lower(c);
        } else {
            out += c;
        }
    }
    return out;
}

std::string idl_name_of(std::string_view css) {
    if (css.starts_with("--")) { return std::string{css}; }
    if (css == "float") { return "cssFloat"; }
    std::string out;
    bool upper_next = false;
    // A leading dash is dropped rather than turned into a capital:
    // `-webkit-transform` is `webkitTransform`, not `WebkitTransform`.
    if (!css.empty() && css.front() == '-') { css.remove_prefix(1); }
    for (const char c : css) {
        if (c == '-') {
            upper_next = true;
            continue;
        }
        out += upper_next ? ascii_upper(c) : c;
        upper_next = false;
    }
    return out;
}

} // namespace ctbrowser::style::css
