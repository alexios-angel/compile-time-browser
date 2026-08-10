#include <ctbrowser/style/css/media.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include <ctbrowser/core/algorithms.hpp>

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
// there is no element here. 16 is the same figure layout uses, and both move together
// when the cascade folds units.
[[nodiscard]] float length_in_px(const css_token & t, std::string_view unit) {
    if (ascii_iequals(unit, "px") || unit.empty()) { return static_cast<float>(t.number); }
    if (ascii_iequals(unit, "em") || ascii_iequals(unit, "rem")) {
        return static_cast<float>(t.number) * 16.0f;
    }
    if (ascii_iequals(unit, "pt")) { return static_cast<float>(t.number) * 4.0f / 3.0f; }
    if (ascii_iequals(unit, "dppx") || ascii_iequals(unit, "x")) {
        return static_cast<float>(t.number);
    }
    if (ascii_iequals(unit, "dpi")) { return static_cast<float>(t.number) / 96.0f; }
    return static_cast<float>(t.number);
}

// One `(...)` block: `(min-width: 576px)`, `(hover)`, `(orientation: portrait)`.
[[nodiscard]] media_feature parse_feature(const stylesheet & sheet,
                                          std::span<const component_value> inner) {
    media_feature out;
    out.which = media_feature::name::unknown;
    const auto tok = [&](const component_value & v) -> const css_token & {
        return sheet.tokens[v.token];
    };
    const auto is_ws = [&](const component_value & v) {
        return v.kind == cv_kind::token && tok(v).type == token_type::whitespace;
    };
    while (!inner.empty() && is_ws(inner.front())) { inner = inner.subspan(1); }
    while (!inner.empty() && is_ws(inner.back())) { inner = inner.subspan(0, inner.size() - 1); }
    if (inner.empty() || inner.front().kind != cv_kind::token) { return out; }
    if (tok(inner.front()).type != token_type::ident) { return out; }

    std::string_view feature = sheet.text_of(tok(inner.front()));
    out.op = media_feature::compare::boolean;
    // The `min-`/`max-` prefixes. Stripped here so the evaluator only knows about
    // twelve features and three comparisons.
    if (feature.size() > 4 && ascii_iequals(feature.substr(0, 4), "min-")) {
        out.op = media_feature::compare::at_least;
        feature.remove_prefix(4);
    } else if (feature.size() > 4 && ascii_iequals(feature.substr(0, 4), "max-")) {
        out.op = media_feature::compare::at_most;
        feature.remove_prefix(4);
    }
    out.which = name_of(feature);
    if (out.which == media_feature::name::unknown) { return out; }
    inner = inner.subspan(1);

    while (!inner.empty() && is_ws(inner.front())) { inner = inner.subspan(1); }
    if (inner.empty()) {
        // `(hover)` - the BOOLEAN context, which asks whether the feature's value is
        // anything other than zero or `none`. A range feature with no value is
        // malformed, so `min-` or `max-` with nothing after it fails.
        if (out.op != media_feature::compare::boolean) { out.which = media_feature::name::unknown; }
        return out;
    }
    if (inner.front().kind != cv_kind::token || tok(inner.front()).type != token_type::colon) {
        out.which = media_feature::name::unknown; // range syntax (`width <= 700px`) is not read
        return out;
    }
    inner = inner.subspan(1);
    while (!inner.empty() && is_ws(inner.front())) { inner = inner.subspan(1); }
    if (inner.empty() || inner.front().kind != cv_kind::token) {
        out.which = media_feature::name::unknown;
        return out;
    }
    if (out.op == media_feature::compare::boolean) { out.op = media_feature::compare::equal; }
    const css_token & value = tok(inner.front());
    if (value.type == token_type::ident) {
        out.keyword = std::string{sheet.text_of(value)};
        return out;
    }
    if (value.type == token_type::number || value.type == token_type::dimension) {
        out.value = length_in_px(value, sheet.unit_of(value));
        return out;
    }
    out.which = media_feature::name::unknown;
    return out;
}

} // namespace

std::vector<media_query> parse_media_query_list(const stylesheet & sheet,
                                                std::span<const component_value> prelude) {
    std::vector<media_query> out;
    const auto tok = [&](const component_value & v) -> const css_token & {
        return sheet.tokens[v.token];
    };
    const auto is_ws = [&](const component_value & v) {
        return v.kind == cv_kind::token && tok(v).type == token_type::whitespace;
    };
    const auto is_comma = [&](const component_value & v) {
        return v.kind == cv_kind::token && tok(v).type == token_type::comma;
    };

    std::size_t at = 0;
    while (at <= prelude.size()) {
        std::size_t end = at;
        while (end < prelude.size() && !is_comma(prelude[end])) { ++end; }
        std::span<const component_value> run = prelude.subspan(at, end - at);

        media_query query;
        bool saw_type = false;
        bool expect_term = true; // a term, or `and` between terms
        for (std::size_t i = 0; i < run.size(); ++i) {
            const component_value & v = run[i];
            if (is_ws(v)) { continue; }
            if (v.kind == cv_kind::block && v.open == '(') {
                if (!expect_term) {
                    query.malformed = true; // two terms with no `and`
                    break;
                }
                const media_feature feature = parse_feature(sheet, sheet.children_of(v));
                if (feature.which == media_feature::name::unknown) {
                    // An unmodelled or unreadable feature makes the whole query false
                    // rather than being skipped. Skipping would APPLY rules the author
                    // gated on something this engine does not understand.
                    query.malformed = true;
                    break;
                }
                query.features.push_back(feature);
                expect_term = false;
                continue;
            }
            if (v.kind != cv_kind::token || tok(v).type != token_type::ident) {
                query.malformed = true;
                break;
            }
            const std::string_view word = sheet.text_of(tok(v));
            if (ascii_iequals(word, "and")) {
                if (expect_term) {
                    query.malformed = true;
                    break;
                }
                expect_term = true;
                continue;
            }
            if (ascii_iequals(word, "not")) {
                query.negated = !query.negated;
                continue;
            }
            if (ascii_iequals(word, "only")) {
                // `only` exists to hide a query from a pre-CSS2 parser. It has no
                // effect on one that understands media queries at all.
                continue;
            }
            if (saw_type || !expect_term) {
                query.malformed = true;
                break;
            }
            if (ascii_iequals(word, "all")) {
                query.type = media_type::all;
            } else if (ascii_iequals(word, "screen")) {
                query.type = media_type::screen;
            } else if (ascii_iequals(word, "print")) {
                query.type = media_type::print;
            } else {
                // An unknown media type - `tv`, `aural`, or a typo. Per spec the query
                // is false rather than being ignored.
                query.malformed = true;
                break;
            }
            saw_type = true;
            expect_term = false;
        }
        if (expect_term && (!query.features.empty() || saw_type)) {
            query.malformed = true; // a trailing `and`
        }
        out.push_back(std::move(query));
        if (end >= prelude.size()) { break; }
        at = end + 1;
    }
    return out;
}

namespace {

[[nodiscard]] bool compare_number(const media_feature & f, float have) {
    switch (f.op) {
    case media_feature::compare::at_least: return have >= f.value;
    case media_feature::compare::at_most: return have <= f.value;
    case media_feature::compare::equal: return have > f.value - 1e-4f && have < f.value + 1e-4f;
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
    switch (f.which) {
    case n::width: return compare_number(f, env.viewport_width);
    case n::height: return compare_number(f, env.viewport_height);
    case n::orientation: return matches_keyword(f, env.portrait() ? "portrait" : "landscape");
    case n::prefers_color_scheme: return matches_keyword(f, env.dark ? "dark" : "light");
    case n::prefers_reduced_motion:
        return matches_keyword(f, env.reduced_motion ? "reduce" : "no-preference");
    case n::resolution: return compare_number(f, env.resolution_dppx);
    case n::hover:
    case n::any_hover: return matches_keyword(f, env.hover ? "hover" : "none");
    case n::pointer:
    case n::any_pointer: return matches_keyword(f, env.fine_pointer ? "fine" : "coarse");
    // `(monochrome)` and `(color)` are BIT DEPTHS, not booleans: a colour screen is
    // `monochrome: 0` and `color: 8`, which is why they go through the number path.
    case n::monochrome: return compare_number(f, env.monochrome ? 8.0f : 0.0f);
    case n::color: return compare_number(f, env.monochrome ? 0.0f : 8.0f);
    case n::unknown: return false;
    }
    return false;
}

[[nodiscard]] bool query_holds(const media_query & q, const media_environment & env) {
    if (q.malformed) { return false; } // `not all`, per spec
    bool holds = true;
    if (q.type != media_type::all && q.type != env.type) { holds = false; }
    if (holds) {
        for (const media_feature & f : q.features) {
            if (!feature_holds(f, env)) {
                holds = false;
                break;
            }
        }
    }
    // `not` applies to the WHOLE query, type and features together - which is why it
    // is applied here and not per feature.
    return q.negated ? !holds : holds;
}

} // namespace

bool evaluate(std::span<const media_query> queries, const media_environment & env) {
    if (queries.empty()) { return true; } // `@media { }` is `all`
    for (const media_query & q : queries) {
        if (query_holds(q, env)) { return true; } // a comma list is an OR
    }
    return false;
}

} // namespace ctbrowser::style::css
