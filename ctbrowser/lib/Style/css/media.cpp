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

// `min-width` is `width` with an operator, not a feature of its own - which is what
// keeps this table twelve entries rather than thirty.
[[nodiscard]] media_feature::name name_of(std::string_view text) {
    using n = media_feature::name;
    if (ascii_iequals(text, "width")) { return n::width; }
    if (ascii_iequals(text, "height")) { return n::height; }
    if (ascii_iequals(text, "orientation")) { return n::orientation; }
    if (ascii_iequals(text, "prefers-color-scheme")) { return n::prefers_color_scheme; }
    if (ascii_iequals(text, "prefers-reduced-motion")) { return n::prefers_reduced_motion; }
    if (ascii_iequals(text, "resolution")) { return n::resolution; }
    if (ascii_iequals(text, "hover")) { return n::hover; }
    if (ascii_iequals(text, "any-hover")) { return n::any_hover; }
    if (ascii_iequals(text, "pointer")) { return n::pointer; }
    if (ascii_iequals(text, "any-pointer")) { return n::any_pointer; }
    if (ascii_iequals(text, "monochrome")) { return n::monochrome; }
    if (ascii_iequals(text, "color")) { return n::color; }
    return n::unknown;
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

[[nodiscard]] bool feature_holds(const media_feature & f, const media_environment & env) {
    using n = media_feature::name;
    const auto number = [&](float have) { return compare_number(f.op, have, f.value); };
    switch (f.which) {
    case n::width: return number(env.viewport_width);
    case n::height: return number(env.viewport_height);
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
    case n::unknown: return false;
    }
    return false;
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
                                           const media_environment & env) {
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
    const auto value_of = [&](std::size_t i, media_feature & f) -> bool {
        const css_token & t = s.tokens[i];
        if (t.type == token_type::ident) {
            f.keyword = std::string{s.text_of(t)};
            return true;
        }
        if (t.type == token_type::number || t.type == token_type::dimension) {
            f.value = length_in_px(s, t, env);
            return true;
        }
        if (t.type == token_type::function) {
            const std::string_view text = std::string_view{s.pool}.substr(
                t.text, s.tokens[ends[i] - 1].text + s.tokens[ends[i] - 1].length - t.text);
            const std::optional<float> px = math_in_px(text, env);
            if (!px) { return false; }
            f.value = *px;
            return true;
        }
        return false;
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
    const auto decide = [&](const media_feature & f) -> truth {
        if (f.which == media_feature::name::unknown) { return truth::unknown; }
        return feature_holds(f, env) ? truth::yes : truth::no;
    };

    // <mf-boolean>: one name.
    if (at.size() == 1) {
        if (!is_name(at[0])) { return std::nullopt; }
        media_feature f;
        f.which = name_of(s.text_of(s.tokens[at[0]]));
        f.op = media_feature::compare::boolean;
        return decide(f);
    }
    // <mf-plain>: name, colon, one value.
    if (is_name(at[0]) && s.tokens[at[1]].type == token_type::colon) {
        if (at.size() != 3) { return std::nullopt; }
        media_feature f;
        std::string_view name = s.text_of(s.tokens[at[0]]);
        f.op = media_feature::compare::equal;
        if (ascii_istarts_with(name, "min-")) {
            f.op = media_feature::compare::at_least;
            name.remove_prefix(4);
        } else if (ascii_istarts_with(name, "max-")) {
            f.op = media_feature::compare::at_most;
            name.remove_prefix(4);
        }
        f.which = name_of(name);
        if (!value_of(at[2], f)) { return std::nullopt; }
        return decide(f);
    }
    // <mf-range>. The name may be on either side, or in the middle of two values.
    media_feature::compare op1 = media_feature::compare::equal;
    media_feature::compare op2 = media_feature::compare::equal;
    if (is_name(at[0])) {
        // name op value
        const std::size_t took = operator_at(1, op1);
        if (took == 0 || at.size() != 2 + took) { return std::nullopt; }
        media_feature f;
        f.which = name_of(s.text_of(s.tokens[at[0]]));
        f.op = op1;
        if (!value_of(at[1 + took], f)) { return std::nullopt; }
        return decide(f);
    }
    // value op name [op value]
    const std::size_t took = operator_at(1, op1);
    if (took == 0 || at.size() < 2 + took || !is_name(at[1 + took])) { return std::nullopt; }
    media_feature f;
    f.which = name_of(s.text_of(s.tokens[at[1 + took]]));
    f.op = flipped(op1); // `400px < width` is `width > 400px`
    if (!value_of(at[0], f)) { return std::nullopt; }
    if (at.size() == 2 + took) { return decide(f); }
    const std::size_t took2 = operator_at(2 + took, op2);
    if (took2 == 0 || at.size() != 3 + took + took2) { return std::nullopt; }
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
    if (!value_of(at[2 + took + took2], g)) { return std::nullopt; }
    return both(decide(f), decide(g));
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
    bool holds = true;
    if (q.type == media_type::other || (q.type != media_type::all && q.type != env.type)) {
        holds = false;
    }
    if (holds && !q.condition.empty()) {
        const token_stream s = tokenize(q.condition);
        const std::optional<truth> answer = condition(s, 0, s.tokens.size() - 1, env);
        // Unknown is false at the top of a query, Media Queries 4 §3.
        holds = answer.value_or(truth::no) == truth::yes;
    }
    // `not` applies to the WHOLE query, type and features together - which is why it
    // is applied here and not per feature.
    return q.negated ? !holds : holds;
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
