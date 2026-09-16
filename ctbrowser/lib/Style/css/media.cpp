#include <ctbrowser/style/css/media.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/boolean.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/token.hpp>

// Media Queries Level 4, over the token stream:
//
//   <media-query> = <media-condition>
//                 | [ not | only ]? <media-type> [ and <media-condition-without-or> ]?
//   <media-condition> = <boolean-expr[ <media-feature> ]>   (css/boolean.hpp)
//   <media-feature> = ( <mf-plain> | <mf-boolean> | <mf-range> )
//
// The query's TYPE and `not` are read here once; its condition is kept as text
// and read by `evaluate` each time the environment is asked, which is what
// keeps this file one grammar rather than a parser and a tree that must agree.

namespace ctbrowser::style::css {
namespace {

// THE FEATURES, Media Queries 4 and 5: the name, what it takes, and - for a
// discrete one - the keywords it accepts and the one this window answers.
// A window here is a browser tab on a colour screen with a mouse: the
// preferences are unset and the features that are about hardware answer as
// a desktop does. `min-width` is `width` with an operator, not a feature of
// its own.
struct feature_entry {
    std::string_view name;
    media_feature::name which;
    media_feature::kind kind;
    std::string_view keywords; // space-separated, discrete features only
    std::string_view answer;   // the environment's, for the fixed ones
};
constexpr feature_entry feature_table[] = {
    {"width", media_feature::name::width, media_feature::kind::length, {}, {}},
    {"height", media_feature::name::height, media_feature::kind::length, {}, {}},
    {"device-width", media_feature::name::width, media_feature::kind::length, {}, {}},
    {"device-height", media_feature::name::height, media_feature::kind::length, {}, {}},
    {"aspect-ratio", media_feature::name::aspect_ratio, media_feature::kind::ratio, {}, {}},
    {"device-aspect-ratio", media_feature::name::aspect_ratio, media_feature::kind::ratio, {}, {}},
    {"orientation",
     media_feature::name::orientation,
     media_feature::kind::discrete,
     "portrait landscape",
     {}},
    {"prefers-color-scheme",
     media_feature::name::prefers_color_scheme,
     media_feature::kind::discrete,
     "light dark",
     {}},
    {"prefers-reduced-motion",
     media_feature::name::prefers_reduced_motion,
     media_feature::kind::discrete,
     "no-preference reduce",
     {}},
    {"resolution", media_feature::name::resolution, media_feature::kind::resolution, {}, {}},
    {"hover", media_feature::name::hover, media_feature::kind::discrete, "none hover", {}},
    {"any-hover", media_feature::name::any_hover, media_feature::kind::discrete, "none hover", {}},
    {"pointer",
     media_feature::name::pointer,
     media_feature::kind::discrete,
     "none coarse fine",
     {}},
    {"any-pointer",
     media_feature::name::any_pointer,
     media_feature::kind::discrete,
     "none coarse fine",
     {}},
    {"monochrome", media_feature::name::monochrome, media_feature::kind::integer, {}, {}},
    {"color", media_feature::name::color, media_feature::kind::integer, {}, {}},
    {"color-index", media_feature::name::unknown, media_feature::kind::integer, {}, {}},
    {"prefers-contrast", media_feature::name::prefers_contrast, media_feature::kind::discrete,
     "no-preference more less custom", "no-preference"},
    {"prefers-reduced-data", media_feature::name::prefers_reduced_data,
     media_feature::kind::discrete, "no-preference reduce", "no-preference"},
    {"prefers-reduced-transparency", media_feature::name::prefers_reduced_transparency,
     media_feature::kind::discrete, "no-preference reduce", "no-preference"},
    {"forced-colors", media_feature::name::forced_colors, media_feature::kind::discrete,
     "none active", "none"},
    {"inverted-colors", media_feature::name::inverted_colors, media_feature::kind::discrete,
     "none inverted", "none"},
    {"dynamic-range", media_feature::name::dynamic_range, media_feature::kind::discrete,
     "standard high", "standard"},
    {"video-dynamic-range", media_feature::name::video_dynamic_range, media_feature::kind::discrete,
     "standard high", "standard"},
    {"display-mode", media_feature::name::display_mode, media_feature::kind::discrete,
     "fullscreen standalone minimal-ui browser picture-in-picture", "browser"},
    {"scripting", media_feature::name::scripting, media_feature::kind::discrete,
     "none initial-only enabled", "enabled"},
    {"update", media_feature::name::update, media_feature::kind::discrete, "none slow fast",
     "fast"},
    {"overflow-block", media_feature::name::overflow_block, media_feature::kind::discrete,
     "none scroll paged", "scroll"},
    {"overflow-inline", media_feature::name::overflow_inline, media_feature::kind::discrete,
     "none scroll", "scroll"},
    {"color-gamut", media_feature::name::color_gamut, media_feature::kind::discrete,
     "srgb p3 rec2020", "srgb"},
    {"grid", media_feature::name::grid, media_feature::kind::integer, {}, {}},
    {"scan", media_feature::name::scan, media_feature::kind::discrete, "interlace progressive",
     "progressive"},
};

// The two a CONTAINER has and a window does not (CSS Containment 3 §5.2):
// the logical sizes, which read as width and height here since nothing this
// engine lays out is vertical.
constexpr feature_entry container_features[] = {
    {"inline-size", media_feature::name::width, media_feature::kind::length, {}, {}},
    {"block-size", media_feature::name::height, media_feature::kind::length, {}, {}},
};

[[nodiscard]] const feature_entry * entry_of(std::string_view text, bool container) {
    for (const feature_entry & e : feature_table) {
        if (ascii_iequals(text, e.name)) { return &e; }
    }
    if (container) {
        for (const feature_entry & e : container_features) {
            if (ascii_iequals(text, e.name)) { return &e; }
        }
    }
    return nullptr;
}

// A length in a media query is resolved against the ROOT font size, not an element's -
// there is no element here (Media Queries 4 §1.3: the initial value). 16 is
// the same figure layout uses, and both move together when the cascade folds
// units. The viewport units are the environment's - `@media (width: 100vw)`
// is true by definition - and every other length unit goes through calc/'s
// evaluator, which knows them all; a resolution unit is dppx.
[[nodiscard]] float length_in_px(const token_stream & s, const css_token & t,
                                 const media_environment & env) {
    const std::string_view unit = s.unit_of(t);
    if (ascii_iequals(unit, "px") || unit.empty()) { return static_cast<float>(t.number); }
    if (ascii_iequals(unit, "dppx") || ascii_iequals(unit, "x")) {
        return static_cast<float>(t.number);
    }
    if (ascii_iequals(unit, "dpi")) { return static_cast<float>(t.number) / 96.0f; }
    if (ascii_iequals(unit, "dpcm")) { return static_cast<float>(t.number) / 96.0f * 2.54f; }
    length_context ctx;
    ctx.viewport_width = env.viewport_width;
    ctx.viewport_height = env.viewport_height;
    const math_answer answer = evaluate_math(s.text_of(t), ctx);
    if (answer.outcome == math_outcome::resolved && !answer.value.has_percent) {
        return static_cast<float>(answer.value.px);
    }
    return static_cast<float>(t.number);
}

// A MATH FUNCTION AS A FEATURE VALUE - `(width: calc(200vh + 5em))` - with
// the same bases: the root font size and the viewport. Media Queries 4 §2.4
// makes it a value like any other; a function calc/ cannot resolve is the
// `<general-enclosed>` the caller folds as unknown.
[[nodiscard]] std::optional<float> math_in_px(std::string_view text,
                                              const media_environment & env) {
    length_context ctx;
    ctx.viewport_width = env.viewport_width;
    ctx.viewport_height = env.viewport_height;
    const math_answer answer = evaluate_math(text, ctx);
    if (answer.outcome != math_outcome::resolved || answer.value.has_percent) {
        return std::nullopt;
    }
    return static_cast<float>(answer.value.px);
}

[[nodiscard]] bool compare_number(media_feature::compare op, float have, float want) {
    switch (op) {
    case media_feature::compare::at_least: return have >= want;
    case media_feature::compare::at_most: return have <= want;
    case media_feature::compare::less: return have < want;
    case media_feature::compare::greater: return have > want;
    case media_feature::compare::equal: return have > want - 1e-4f && have < want + 1e-4f;
    // The BOOLEAN context asks whether the value is anything other than zero.
    case media_feature::compare::boolean: return have != 0;
    }
    return false;
}

[[nodiscard]] bool matches_keyword(const media_feature & f, std::string_view have) {
    // A boolean-context test on a keyword feature - `(hover)` - is true when the value
    // is anything but `none`.
    if (f.op == media_feature::compare::boolean) { return !ascii_iequals(have, "none"); }
    return ascii_iequals(f.keyword, have);
}

[[nodiscard]] bool feature_holds(const media_feature & f, const feature_entry & e,
                                 const media_environment & env) {
    using n = media_feature::name;
    const auto number = [&](float have) { return compare_number(f.op, have, f.value); };
    switch (f.which) {
    case n::width: return number(env.viewport_width);
    case n::height: return number(env.viewport_height);
    case n::aspect_ratio:
        return env.viewport_height > 0 && number(env.viewport_width / env.viewport_height);
    case n::orientation: return matches_keyword(f, env.portrait() ? "portrait" : "landscape");
    case n::prefers_color_scheme: return matches_keyword(f, env.dark ? "dark" : "light");
    case n::prefers_reduced_motion:
        return matches_keyword(f, env.reduced_motion ? "reduce" : "no-preference");
    case n::resolution: return number(env.resolution_dppx);
    case n::hover:
    case n::any_hover: return matches_keyword(f, env.hover ? "hover" : "none");
    case n::pointer:
    case n::any_pointer: return matches_keyword(f, env.fine_pointer ? "fine" : "coarse");
    // `(monochrome)` and `(color)` are BIT DEPTHS, not booleans: a colour screen is
    // `monochrome: 0` and `color: 8`, which is why they go through the number path.
    case n::monochrome: return number(env.monochrome ? 8.0f : 0.0f);
    case n::color: return number(env.monochrome ? 0.0f : 8.0f);
    case n::grid: return number(0.0f);
    case n::unknown: return false;
    default: return matches_keyword(f, e.answer); // the fixed discrete ones
    }
}

// The tokens [from, to) as ONE feature - the inside of its parentheses - or
// nothing if they are not one. A feature this engine does not model is a
// `<general-enclosed>`: unknown, which the caller folds as the spec says.
//
//   <mf-plain>   = <mf-name> : <mf-value>       (`min-`/`max-` are the operator)
//   <mf-boolean> = <mf-name>
//   <mf-range>   = <mf-name> <op> <mf-value> | <mf-value> <op> <mf-name>
//                | <mf-value> <lt> <mf-name> <lt> <mf-value>  (and the `>` twin)
[[nodiscard]] std::optional<truth> feature(const token_stream & s, std::size_t from, std::size_t to,
                                           const media_environment & env, bool container = false) {
    std::vector<std::size_t> at; // the significant tokens
    // A FUNCTION IS ONE VALUE: its tokens up to the matching `)` are one
    // entry here, keyed on the function token, so `calc(200vh + 5em)` has
    // the shape of a dimension to the grammar below.
    std::vector<std::size_t> ends(s.tokens.size(), 0);
    for (std::size_t i = from; i < to; ++i) {
        if (s.tokens[i].type == token_type::whitespace) { continue; }
        at.push_back(i);
        if (s.tokens[i].type == token_type::function) {
            int depth = 1;
            std::size_t j = i + 1;
            for (; j < to && depth > 0; ++j) {
                if (s.tokens[j].type == token_type::function ||
                    s.tokens[j].type == token_type::open_paren) {
                    ++depth;
                } else if (s.tokens[j].type == token_type::close_paren) {
                    --depth;
                }
            }
            ends[i] = j;
            i = j - 1;
        }
    }
    if (at.empty()) { return std::nullopt; }
    const auto is_name = [&](std::size_t i) { return s.tokens[i].type == token_type::ident; };
    // WHAT A VALUE WAS WRITTEN AS, which decides whether it is a value for the
    // feature at all: `(width: foo)` and `(orientation: 0)` parse, and are
    // UNKNOWN rather than false (Media Queries 4 §2.4) - so `not all and
    // (orientation: 0)` does not match either.
    enum class written : std::uint8_t {
        keyword,
        zero,   // a unitless 0, a <length> by CSS Values 4 §6.1
        number, // any other unitless number
        length,
        resolution,
        ratio,
        other
    };
    // One value starting at significant index `i`: how many significant
    // tokens it took (0 = not a value), what it was, and its number or keyword.
    const auto value_at = [&](std::size_t i, media_feature & f, written & w) -> std::size_t {
        if (i >= at.size()) { return 0; }
        const css_token & t = s.tokens[at[i]];
        w = written::other;
        if (t.type == token_type::ident) {
            f.keyword = std::string{s.text_of(t)};
            w = written::keyword;
            return 1;
        }
        if (t.type == token_type::number || t.type == token_type::dimension) {
            // `<ratio>` = <number> [ / <number> ]?
            if (t.type == token_type::number && i + 2 < at.size() &&
                s.tokens[at[i + 1]].type == token_type::delim &&
                s.text_of(s.tokens[at[i + 1]]) == "/" &&
                s.tokens[at[i + 2]].type == token_type::number) {
                const double d = s.tokens[at[i + 2]].number;
                f.value = d == 0 ? 0.0f : static_cast<float>(t.number / d);
                w = written::ratio;
                return 3;
            }
            f.value = length_in_px(s, t, env);
            if (t.type == token_type::number) {
                w = t.number == 0 ? written::zero : written::number;
            } else {
                const std::string_view unit = s.unit_of(t);
                w = ascii_iequals_any(unit, {"dpi", "dpcm", "dppx", "x"}) ? written::resolution
                                                                          : written::length;
            }
            return 1;
        }
        if (t.type == token_type::function) {
            const std::string_view text = std::string_view{s.pool}.substr(
                t.text, s.tokens[ends[at[i]] - 1].text + s.tokens[ends[at[i]] - 1].length - t.text);
            const std::optional<float> px = math_in_px(text, env);
            if (!px) { return 0; }
            f.value = *px;
            // calc/ answers a resolution as dppx and a length as px alike;
            // the unit inside says which it was: `dpi`/`dpcm`/`dppx`, or an
            // `x` that follows a number - the one in `px` does not.
            const auto digit = [](char c) { return c >= '0' && c <= '9'; };
            const auto word = [&](char c) {
                return digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
            };
            bool resolution = text.find("dp") != std::string_view::npos;
            for (std::size_t k = 1; !resolution && k < text.size(); ++k) {
                resolution = (text[k] == 'x' || text[k] == 'X') &&
                             (digit(text[k - 1]) || text[k - 1] == '.') &&
                             (k + 1 == text.size() || !word(text[k + 1]));
            }
            w = resolution ? written::resolution : written::length;
            return 1;
        }
        return 0;
    };
    // One comparison operator at `i`, `<=` being two delim tokens; the number of
    // tokens it took, or 0.
    const auto operator_at = [&](std::size_t i, media_feature::compare & op) -> std::size_t {
        if (i >= at.size()) { return 0; }
        const std::size_t tok = at[i];
        if (s.tokens[tok].type != token_type::delim) { return 0; }
        const std::string_view d = s.text_of(s.tokens[tok]);
        if (d != "<" && d != ">" && d != "=") { return 0; }
        // `<=` is two tokens with nothing between them: the `=` must follow at
        // once in the stream, not merely be the next significant token.
        const bool equal_too = d != "=" && is_delim_text(s, tok + 1, '=');
        if (d == "=") {
            op = media_feature::compare::equal;
        } else if (d == "<") {
            op = equal_too ? media_feature::compare::at_most : media_feature::compare::less;
        } else {
            op = equal_too ? media_feature::compare::at_least : media_feature::compare::greater;
        }
        return equal_too ? 2 : 1;
    };
    const auto flipped = [](media_feature::compare op) {
        switch (op) {
        case media_feature::compare::less: return media_feature::compare::greater;
        case media_feature::compare::greater: return media_feature::compare::less;
        case media_feature::compare::at_most: return media_feature::compare::at_least;
        case media_feature::compare::at_least: return media_feature::compare::at_most;
        default: return op;
        }
    };
    // A feature and its value together: unknown for a feature this engine
    // does not model, for a value the feature does not take, and for range
    // syntax on a discrete feature.
    const auto decide = [&](const feature_entry * e, const media_feature & f, written w) -> truth {
        if (e == nullptr || e->which == media_feature::name::unknown) { return truth::unknown; }
        const bool boolean = f.op == media_feature::compare::boolean;
        bool fits = boolean;
        if (!boolean) {
            switch (e->kind) {
            case media_feature::kind::discrete: {
                fits = false;
                if (w == written::keyword && f.op == media_feature::compare::equal) {
                    for (const std::string_view k : split_top_level(e->keywords, " ")) {
                        if (ascii_iequals(k, f.keyword)) { fits = true; }
                    }
                }
                break;
            }
            case media_feature::kind::length:
                fits = w == written::length || w == written::zero;
                break;
            case media_feature::kind::resolution: fits = w == written::resolution; break;
            case media_feature::kind::integer:
                fits = (w == written::number || w == written::zero) && f.value >= 0 &&
                       f.value == static_cast<float>(static_cast<long>(f.value));
                break;
            case media_feature::kind::ratio:
                fits = w == written::ratio || w == written::number || w == written::zero;
                break;
            }
        }
        if (!fits) { return truth::unknown; }
        return feature_holds(f, *e, env) ? truth::yes : truth::no;
    };

    // <mf-boolean>: one name.
    if (at.size() == 1) {
        if (!is_name(at[0])) { return std::nullopt; }
        const feature_entry * e = entry_of(s.text_of(s.tokens[at[0]]), container);
        media_feature f;
        f.which = e == nullptr ? media_feature::name::unknown : e->which;
        f.op = media_feature::compare::boolean;
        return decide(e, f, written::other);
    }
    // <mf-plain>: name, colon, one value.
    if (is_name(at[0]) && s.tokens[at[1]].type == token_type::colon) {
        media_feature f;
        std::string_view name = s.text_of(s.tokens[at[0]]);
        f.op = media_feature::compare::equal;
        const feature_entry * plain = entry_of(name, container);
        if (plain == nullptr && ascii_istarts_with(name, "min-")) {
            f.op = media_feature::compare::at_least;
            name.remove_prefix(4);
        } else if (plain == nullptr && ascii_istarts_with(name, "max-")) {
            f.op = media_feature::compare::at_most;
            name.remove_prefix(4);
        }
        const feature_entry * e = entry_of(name, container);
        f.which = e == nullptr ? media_feature::name::unknown : e->which;
        written w = written::other;
        const std::size_t took = value_at(2, f, w);
        if (took == 0 || at.size() != 2 + took) { return std::nullopt; }
        // `min-`/`max-` on a discrete feature is no feature at all.
        if (e != nullptr && e->kind == media_feature::kind::discrete &&
            f.op != media_feature::compare::equal) {
            return truth::unknown;
        }
        return decide(e, f, w);
    }
    // <mf-range>. The name may be on either side, or in the middle of two values.
    media_feature::compare op1 = media_feature::compare::equal;
    media_feature::compare op2 = media_feature::compare::equal;
    if (is_name(at[0])) {
        // name op value
        const std::size_t took = operator_at(1, op1);
        if (took == 0) { return std::nullopt; }
        const feature_entry * e = entry_of(s.text_of(s.tokens[at[0]]), container);
        media_feature f;
        f.which = e == nullptr ? media_feature::name::unknown : e->which;
        f.op = op1;
        written w = written::other;
        const std::size_t vtook = value_at(1 + took, f, w);
        if (vtook == 0 || at.size() != 1 + took + vtook) { return std::nullopt; }
        if (e != nullptr && e->kind == media_feature::kind::discrete) { return truth::unknown; }
        return decide(e, f, w);
    }
    // value op name [op value]
    media_feature f;
    written w = written::other;
    const std::size_t vtook = value_at(0, f, w);
    if (vtook == 0) { return std::nullopt; }
    const std::size_t took = operator_at(vtook, op1);
    if (took == 0 || at.size() < vtook + took + 1 || !is_name(at[vtook + took])) {
        return std::nullopt;
    }
    const feature_entry * e = entry_of(s.text_of(s.tokens[at[vtook + took]]), container);
    f.which = e == nullptr ? media_feature::name::unknown : e->which;
    f.op = flipped(op1); // `400px < width` is `width > 400px`
    if (at.size() == vtook + took + 1) {
        if (e != nullptr && e->kind == media_feature::kind::discrete) { return truth::unknown; }
        return decide(e, f, w);
    }
    const std::size_t took2 = operator_at(vtook + took + 1, op2);
    if (took2 == 0) { return std::nullopt; }
    // Both operators must point the same way: `400px < width < 700px`.
    const bool less1 =
        op1 == media_feature::compare::less || op1 == media_feature::compare::at_most;
    const bool less2 =
        op2 == media_feature::compare::less || op2 == media_feature::compare::at_most;
    if (op1 == media_feature::compare::equal || op2 == media_feature::compare::equal ||
        less1 != less2) {
        return std::nullopt;
    }
    media_feature g;
    g.which = f.which;
    g.op = op2;
    written w2 = written::other;
    const std::size_t vtook2 = value_at(vtook + took + 1 + took2, g, w2);
    if (vtook2 == 0 || at.size() != vtook + took + 1 + took2 + vtook2) { return std::nullopt; }
    if (e != nullptr && e->kind == media_feature::kind::discrete) { return truth::unknown; }
    return both(decide(e, f, w), decide(e, g, w2));
}

// A `<media-condition>` over [from, to): nullopt when it is not one.
[[nodiscard]] std::optional<truth> condition(const token_stream & s, std::size_t from,
                                             std::size_t to, const media_environment & env) {
    // A function in a condition - `calc(...)`, or anything else - is a
    // `<general-enclosed>`.
    const auto test = [](std::string_view, std::size_t, std::size_t) { return truth::unknown; };
    const auto enclosed = [&](std::size_t a, std::size_t b) {
        return feature(s, a, b, env).value_or(truth::unknown);
    };
    return boolean_expression(s, from, to, test, enclosed);
}

[[nodiscard]] std::string_view slice(const token_stream & s, std::size_t from, std::size_t to) {
    if (from >= to) { return {}; }
    const css_token & a = s.tokens[from];
    const css_token & b = s.tokens[to - 1];
    return std::string_view{s.pool}.substr(a.text, b.text + b.length - a.text);
}

// One query's text, the type and `not` read off the front and the condition
// kept as text. `media_query::malformed` is set for anything the grammar
// refuses; a condition that parses but cannot be decided is not malformed, it
// is a condition that is false today.
[[nodiscard]] media_query parse_query(std::string_view text) {
    media_query q;
    const token_stream s = tokenize(text);
    const std::size_t end = s.tokens.size() - 1; // the eof token
    std::size_t i = 0;
    const auto skip_ws = [&] {
        while (i < end && s.tokens[i].type == token_type::whitespace) { ++i; }
    };
    skip_ws();
    if (i >= end) {
        q.malformed = true; // an empty query in a list: `screen, , print`
        return q;
    }
    const css_token & first = s.tokens[i];
    // The CONDITION form starts with a parenthesis, a function, or `not (`.
    const bool condition_form = first.type == token_type::open_paren ||
                                first.type == token_type::function ||
                                (is_ident_named(s, i, "not") && i + 2 < end &&
                                 s.tokens[i + 1].type == token_type::whitespace &&
                                 (s.tokens[i + 2].type == token_type::open_paren ||
                                  s.tokens[i + 2].type == token_type::function));
    if (condition_form) {
        q.condition = std::string{trim(slice(s, i, end), html_whitespace)};
        media_environment probe;
        if (!condition(s, i, end, probe)) { q.malformed = true; }
        return q;
    }
    // [ not | only ]? <media-type>
    if (is_ident_named(s, i, "not")) {
        q.negated = true;
        ++i;
        skip_ws();
    } else if (is_ident_named(s, i, "only")) {
        ++i; // `only` exists to hide a query from a pre-CSS2 parser
        skip_ws();
    }
    if (i >= end || s.tokens[i].type != token_type::ident) {
        q.malformed = true;
        return q;
    }
    const std::string_view type = s.text_of(s.tokens[i]);
    if (ascii_iequals_any(type, {"not", "only", "and", "or", "layer"})) {
        q.malformed = true;
        return q;
    }
    if (ascii_iequals(type, "all")) {
        q.type = media_type::all;
    } else if (ascii_iequals(type, "screen")) {
        q.type = media_type::screen;
    } else if (ascii_iequals(type, "print")) {
        q.type = media_type::print;
    } else {
        q.type = media_type::other;
    }
    ++i;
    skip_ws();
    if (i >= end) { return q; }
    // [ and <media-condition-without-or> ]?
    if (!is_ident_named(s, i, "and")) {
        q.malformed = true;
        return q;
    }
    ++i;
    skip_ws();
    q.condition = std::string{trim(slice(s, i, end), html_whitespace)};
    media_environment probe;
    if (q.condition.empty() || !condition(s, i, end, probe)) { q.malformed = true; }
    // `or` is not allowed at the top level after a type - and a condition that
    // reads as an `or` here is refused by name, because the boolean parser
    // accepts either joiner and this is the one place the grammar does not.
    const token_stream body = tokenize(q.condition);
    int depth = 0;
    for (std::size_t k = 0; k + 1 < body.tokens.size(); ++k) {
        const css_token & t = body.tokens[k];
        if (t.type == token_type::function || t.type == token_type::open_paren) { ++depth; }
        if (t.type == token_type::close_paren) { --depth; }
        if (depth == 0 && is_ident_named(body, k, "or")) { q.malformed = true; }
    }
    return q;
}

[[nodiscard]] bool query_holds(const media_query & q, const media_environment & env) {
    if (q.malformed) { return false; } // `not all`, per spec
    truth holds = truth::yes;
    if (q.type == media_type::other || (q.type != media_type::all && q.type != env.type)) {
        holds = truth::no;
    }
    if (holds != truth::no && !q.condition.empty()) {
        const token_stream s = tokenize(q.condition);
        holds = condition(s, 0, s.tokens.size() - 1, env).value_or(truth::unknown);
    }
    // `not` applies to the WHOLE query, type and features together - which is why it
    // is applied here and not per feature - and it leaves UNKNOWN unknown:
    // `not all and (orientation: 0)` matches no more than the plain form does.
    // Unknown is false only here, at the top of a query (Media Queries 4 §3).
    return (q.negated ? negate(holds) : holds) == truth::yes;
}

} // namespace

std::vector<media_query> parse_media_query_list(std::string_view text) {
    std::vector<media_query> out;
    // Split at the top-level commas; a list with nothing in it is `all`.
    const token_stream s = tokenize(text);
    const std::size_t end = s.tokens.size() - 1;
    bool anything = false;
    for (std::size_t i = 0; i < end; ++i) {
        if (s.tokens[i].type != token_type::whitespace) { anything = true; }
    }
    if (!anything) { return out; }
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t i = 0; i <= end; ++i) {
        const css_token & t = s.tokens[i];
        if (t.type == token_type::function || t.type == token_type::open_paren ||
            t.type == token_type::open_square || t.type == token_type::open_curly) {
            ++depth;
        } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                   t.type == token_type::close_curly) {
            --depth;
        }
        if (i == end || (depth == 0 && t.type == token_type::comma)) {
            out.push_back(parse_query(slice(s, start, i)));
            start = i + 1;
        }
    }
    return out;
}

std::vector<media_query> parse_media_query_list(const stylesheet & sheet,
                                                std::span<const component_value> prelude) {
    if (prelude.empty()) { return {}; }
    // The prelude's exact source, which is one contiguous run of the sheet's
    // tokens: [first.token, last.end_token).
    const std::uint32_t first = prelude.front().token;
    const std::uint32_t last = prelude.back().end_token;
    std::string text;
    for (std::uint32_t i = first; i < last && i < sheet.tokens.size(); ++i) {
        text += sheet.text_of(sheet.tokens[i]);
    }
    return parse_media_query_list(text);
}

bool evaluate(std::span<const media_query> queries, const media_environment & env) {
    if (queries.empty()) { return true; } // `@media { }` is `all`
    for (const media_query & q : queries) {
        if (query_holds(q, env)) { return true; } // a comma list is an OR
    }
    return false;
}

std::optional<truth> evaluate_container_condition(std::string_view text,
                                                  const media_environment & env,
                                                  const style_query & query) {
    const token_stream s = tokenize(text);
    const std::size_t end = s.tokens.size() - 1;
    // `style(<query>)` is the one function a container condition has (CSS
    // Containment 3 §5.3); the query is handed over as text, since only the
    // engine holds the container's computed style.
    const auto test = [&](std::string_view name, std::size_t a, std::size_t b) {
        if (!ascii_iequals(name, "style")) { return truth::unknown; }
        return query(trim(slice(s, a, b), html_whitespace));
    };
    const auto enclosed = [&](std::size_t a, std::size_t b) {
        return feature(s, a, b, env, true).value_or(truth::unknown);
    };
    return boolean_expression(s, 0, end, test, enclosed);
}

std::optional<bool> evaluate_media_condition(std::string_view text, const media_environment & env) {
    const token_stream s = tokenize(text);
    const std::size_t end = s.tokens.size() - 1;
    if (const std::optional<truth> answer = condition(s, 0, end, env)) {
        return *answer == truth::yes;
    }
    // Not a condition: perhaps a bare feature, `max-width: 1px`.
    if (const std::optional<truth> answer = feature(s, 0, end, env)) {
        return *answer == truth::yes;
    }
    return std::nullopt;
}

} // namespace ctbrowser::style::css
