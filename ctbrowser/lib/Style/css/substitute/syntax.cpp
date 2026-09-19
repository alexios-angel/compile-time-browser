#include "internal.hpp"

namespace ctbrowser::style::css::substitute_detail {

[[nodiscard]] bool is_function_named(const token_stream & s, const css_token & t,
                                     std::string_view want) {
    if (t.type != token_type::function) { return false; }
    std::string_view name = s.text_of(t);
    if (!name.empty() && name.back() == '(') { name.remove_suffix(1); }
    return ascii_iequals(name, want);
}

// The first substitution function in the stream, at the OUTERMOST level it
// appears at. Outermost because substituting an inner one first would rewrite
// text the outer one is about to replace wholesale.
[[nodiscard]] bool find_call(const token_stream & s, call & out) {
    for (std::size_t i = 0; i < s.tokens.size(); ++i) {
        const bool is_var = is_function_named(s, s.tokens[i], "var");
        const bool is_attr = !is_var && is_function_named(s, s.tokens[i], "attr");
        const bool is_if = !is_var && !is_attr && is_function_named(s, s.tokens[i], "if");
        const bool is_ident =
            !is_var && !is_attr && !is_if && is_function_named(s, s.tokens[i], "ident");
        const bool is_random_item = !is_var && !is_attr && !is_if && !is_ident &&
                                    is_function_named(s, s.tokens[i], "random-item");
        const bool is_inherit = !is_var && !is_attr && !is_if && !is_ident && !is_random_item &&
                                is_function_named(s, s.tokens[i], "inherit");
        const bool is_custom =
            s.tokens[i].type == token_type::function && s.text_of(s.tokens[i]).starts_with("--");
        if (!is_var && !is_attr && !is_if && !is_ident && !is_random_item && !is_inherit &&
            !is_custom) {
            continue;
        }
        out = call{};
        out.is_attr = is_attr;
        out.is_if = is_if;
        out.is_ident = is_ident;
        out.is_random_item = is_random_item;
        out.is_inherit = is_inherit;
        out.is_custom = is_custom;
        out.open = i;
        int depth = 1;
        std::size_t j = i + 1;
        bool have_name = false;
        bool after_name = false;
        bool name_call_closed = false;
        for (; j < s.tokens.size() && depth > 0; ++j) {
            const css_token & t = s.tokens[j];
            if (t.type == token_type::eof) { break; }
            // `var(ident(...))`: a FUNCTION in the name position is a name
            // that is not known until it has been substituted.
            if (is_var && depth == 1 && !have_name && out.comma_at == 0 &&
                t.type == token_type::function) {
                out.name_at = j;
                out.name_is_call = true;
                have_name = true;
            }
            if (t.type == token_type::function || t.type == token_type::open_paren ||
                t.type == token_type::open_square || t.type == token_type::open_curly) {
                ++depth;
            }
            if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                t.type == token_type::close_curly) {
                --depth;
                if (depth == 0) { break; }
            }
            if (depth != 1) { continue; }
            if (out.comma_at == 0 && t.type == token_type::comma) { out.comma_at = j; }
            if (is_attr || is_if || is_ident || is_random_item || is_custom || out.comma_at != 0) {
                continue;
            }
            // `var( <custom-property-name> , <declaration-value>? )`: the name is
            // the FIRST token and nothing but whitespace may follow it before
            // the comma. `var(--foo type(*))` names no property and is invalid,
            // which `attr-all-types` asks for by name.
            if (t.type == token_type::whitespace) { continue; }
            if (!have_name && t.type == token_type::ident) {
                out.name_at = j;
                have_name = true;
            } else if (out.name_is_call && !name_call_closed) {
                // The name call's own tokens: its opener, and the `)` that
                // brings the depth back to one.
                if (t.type == token_type::close_paren) { name_call_closed = true; }
            } else {
                after_name = true;
            }
        }
        out.close = j < s.tokens.size() ? j : s.tokens.size() - 1;
        if (out.comma_at == 0) { out.comma_at = out.close; }
        // The argument list is read after substitution.
        if (is_attr || is_if || is_ident || is_random_item || is_custom) { return true; }
        if (!have_name || after_name) { return false; } // `var()` with no name is invalid
        return true;
    }
    return false;
}

[[nodiscard]] std::string text_between(const token_stream & s, std::size_t first,
                                       std::size_t last) {
    std::string out;
    for (std::size_t i = first; i < last; ++i) { out += s.text_of(s.tokens[i]); }
    return out;
}

// A substituted value must not have gained a top-level `!important` or `;`. §3 says a
// var() that expands to either is invalid, and the reason is structural rather than
// pedantic: the declaration's priority and its extent were both decided before
// substitution, so a value that changes them afterwards has escaped the grammar.
[[nodiscard]] bool introduced_structure(std::string_view value) {
    const token_stream s = tokenize(value);
    int depth = 0;
    for (const css_token & t : s.tokens) {
        if (t.type == token_type::function || t.type == token_type::open_paren ||
            t.type == token_type::open_square || t.type == token_type::open_curly) {
            ++depth;
        } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                   t.type == token_type::close_curly) {
            --depth;
        } else if (depth == 0) {
            if (t.type == token_type::semicolon) { return true; }
            if (t.type == token_type::delim && s.text_of(t) == "!") { return true; }
        }
    }
    return false;
}

// `*`, or alternatives of `<type>` and keywords separated by `|`. Nothing may
// sit between the brackets and the name - `< string>` and `<string >` are both
// malformed - and a multiplier must touch its bracket. nullopt for anything
// malformed.
[[nodiscard]] std::optional<std::vector<syntax_alternative>> parse_syntax(std::string_view text) {
    const token_stream s = tokenize(text);
    std::vector<syntax_alternative> out;
    std::size_t i = 0;
    const auto skip_space = [&] {
        while (s.tokens[i].type == token_type::whitespace) { ++i; }
    };
    const auto is_delim = [&](std::size_t at, char c) {
        return s.tokens[at].type == token_type::delim &&
               s.text_of(s.tokens[at]) == std::string_view{&c, 1};
    };
    skip_space();
    if (is_delim(i, '*')) {
        ++i;
        skip_space();
        if (s.tokens[i].type != token_type::eof) { return std::nullopt; }
        return out; // empty means "anything"
    }
    for (;;) {
        skip_space();
        syntax_alternative one;
        if (is_delim(i, '<')) {
            if (s.tokens[i + 1].type != token_type::ident || !is_delim(i + 2, '>')) {
                return std::nullopt;
            }
            one.name = ascii_lower_copy(s.text_of(s.tokens[i + 1]));
            i += 3;
            bool known = false;
            for (const std::string_view t : syntax_types) { known = known || t == one.name; }
            if (!known) { return std::nullopt; }
            if (is_delim(i, '+') || is_delim(i, '#')) {
                one.multiplier = s.text_of(s.tokens[i]).front();
                ++i;
            }
        } else if (s.tokens[i].type == token_type::ident) {
            one.keyword = true;
            one.name = std::string{s.text_of(s.tokens[i])};
            ++i;
        } else {
            return std::nullopt;
        }
        out.push_back(std::move(one));
        skip_space();
        if (s.tokens[i].type == token_type::eof) { return out; }
        if (!is_delim(i, '|')) { return std::nullopt; }
        ++i;
    }
}

[[nodiscard]] std::vector<item> items_of(const token_stream & s, char multiplier) {
    std::vector<item> out;
    int depth = 0;
    bool open = false;
    for (std::size_t i = 0; i < s.tokens.size(); ++i) {
        const css_token & t = s.tokens[i];
        if (t.type == token_type::eof) { break; }
        const bool separator =
            depth == 0 && ((multiplier == '+' && t.type == token_type::whitespace) ||
                           (multiplier == '#' && t.type == token_type::comma));
        if (separator) {
            open = false;
            continue;
        }
        if (t.type == token_type::whitespace) { continue; }
        if (!open) {
            out.push_back(item{i, i});
            open = true;
        }
        out.back().last = i;
        if (t.type == token_type::function || t.type == token_type::open_paren ||
            t.type == token_type::open_square || t.type == token_type::open_curly) {
            ++depth;
        } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                   t.type == token_type::close_curly) {
            --depth;
        }
    }
    return out;
}

// The source text of one item, or empty when a token in it was rebuilt from
// an escape and is no longer a slice of the source.
[[nodiscard]] std::string slice_of(const token_stream & s, const item & it) {
    const css_token & a = s.tokens[it.first];
    const css_token & b = s.tokens[it.last];
    if (a.text >= s.source_length || b.text >= s.source_length) { return {}; }
    return std::string{std::string_view{s.pool}.substr(a.text, b.text + b.length - a.text)};
}

// One item against one type, serialised on a match: a dimension with its unit
// lowercased, a number in its shortest form, and a function as written - the
// cascade folds a `calc()` afterwards exactly as it folds one the author wrote.
[[nodiscard]] std::optional<std::string> match_item(const token_stream & s, const item & it,
                                                    std::string_view type) {
    const css_token & t = s.tokens[it.first];
    const bool single = it.first == it.last;
    const std::string text = slice_of(s, it);
    if (text.empty()) { return std::nullopt; }
    // A math function is matched by what it resolves to. An answer this cannot
    // reach here - `1lh`, `sibling-index()` - is well formed and accepted.
    const auto math_fits = [&](auto && accepts) -> std::optional<std::string> {
        if (t.type != token_type::function || !may_have_math(text)) { return std::nullopt; }
        const math_answer answer = evaluate_math(text, length_context{});
        if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
        if (answer.outcome == math_outcome::unresolved || accepts(answer.value)) { return text; }
        return std::nullopt;
    };
    const auto dimension_of = [&](numeric_type family) -> std::optional<std::string> {
        if (single && t.type == token_type::dimension) {
            const math_answer answer = evaluate_math(text, length_context{});
            if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
            if (answer.outcome == math_outcome::resolved && answer.value.type != family) {
                return std::nullopt;
            }
            return serialize_number(t.number) + ascii_lower_copy(s.unit_of(t));
        }
        return math_fits([family](const calc_result & v) {
            return !v.is_number && v.type == family && !v.has_percent;
        });
    };
    if (type == "number") {
        if (single && t.type == token_type::number) { return serialize_number(t.number); }
        return math_fits([](const calc_result & v) { return v.is_number; });
    }
    if (type == "integer") {
        if (single && t.type == token_type::number && (t.flags & flag_integer) != 0) {
            return serialize_number(t.number);
        }
        return math_fits([](const calc_result & v) { return v.is_number; });
    }
    if (type == "percentage") {
        if (single && t.type == token_type::percentage) { return serialize_number(t.number) + "%"; }
        return math_fits([](const calc_result & v) { return v.has_percent && v.px == 0; });
    }
    if (type == "length" || type == "length-percentage") {
        if (single && t.type == token_type::number && t.number == 0) { return "0px"; }
        if (type == "length-percentage" && single && t.type == token_type::percentage) {
            return serialize_number(t.number) + "%";
        }
        if (single && t.type == token_type::dimension) {
            return dimension_of(numeric_type::length);
        }
        const bool percentages = type == "length-percentage";
        return math_fits([percentages](const calc_result & v) {
            return !v.is_number && v.type == numeric_type::length &&
                   (percentages || !v.has_percent);
        });
    }
    if (type == "angle") { return dimension_of(numeric_type::angle); }
    if (type == "time") { return dimension_of(numeric_type::time); }
    if (type == "resolution") { return dimension_of(numeric_type::resolution); }
    if (type == "frequency") { return dimension_of(numeric_type::frequency); }
    if (type == "string") {
        if (single && t.type == token_type::string) { return text; }
        return std::nullopt;
    }
    if (type == "custom-ident") {
        // ...that is not a CSS-wide keyword or `default`, CSS Values 4 §4.2.
        if (single && t.type == token_type::ident && !is_wide_keyword(s.text_of(t)) &&
            !ascii_iequals(s.text_of(t), "default")) {
            return text;
        }
        return std::nullopt;
    }
    if (type == "color") {
        if (single && (t.type == token_type::hash || t.type == token_type::ident)) { return text; }
        if (t.type == token_type::function) {
            std::string_view name = s.text_of(t);
            name.remove_suffix(1);
            if (ascii_iequals_any(name, {"rgb", "rgba", "hsl", "hsla", "hwb", "lab", "lch", "oklab",
                                         "oklch", "color", "color-mix", "light-dark"})) {
                return text;
            }
        }
        return std::nullopt;
    }
    if (type == "image") {
        if (t.type != token_type::function) { return std::nullopt; }
        std::string_view name = s.text_of(t);
        name.remove_suffix(1);
        if (ascii_iequals_any(name, {"linear-gradient", "radial-gradient", "conic-gradient",
                                     "repeating-linear-gradient", "repeating-radial-gradient",
                                     "repeating-conic-gradient", "image-set", "image", "cross-fade",
                                     "element", "paint"})) {
            // ...with no url() anywhere inside it.
            for (std::size_t i = it.first; i <= it.last; ++i) {
                const css_token & inner = s.tokens[i];
                if (inner.type == token_type::url || (inner.type == token_type::function &&
                                                      ascii_iequals(s.text_of(inner), "url("))) {
                    return std::nullopt;
                }
            }
            return text;
        }
        return std::nullopt;
    }
    if (type == "transform-function" || type == "transform-list") {
        if (t.type != token_type::function) { return std::nullopt; }
        std::string_view name = s.text_of(t);
        name.remove_suffix(1);
        if (ascii_iequals_any(name,
                              {"matrix",     "translate", "translatex",  "translatey", "scale",
                               "scalex",     "scaley",    "rotate",      "skew",       "skewx",
                               "skewy",      "matrix3d",  "translate3d", "translatez", "scale3d",
                               "scalez",     "rotate3d",  "rotatex",     "rotatey",    "rotatez",
                               "perspective"})) {
            return text;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// The whole attribute text against a parsed `<syntax>`, serialised on a match.
[[nodiscard]] std::optional<std::string> match_syntax(
    std::string_view value, const std::vector<syntax_alternative> & syntax) {
    const token_stream s = tokenize(value);
    if (syntax.empty()) { return std::string{trim(value, html_whitespace)}; } // `*`
    for (const syntax_alternative & alt : syntax) {
        if (alt.keyword) {
            const std::vector<item> whole = items_of(s, 0);
            if (whole.size() == 1 && whole.front().first == whole.front().last) {
                const css_token & t = s.tokens[whole.front().first];
                if (t.type == token_type::ident && s.text_of(t) == alt.name) { return alt.name; }
            }
            continue;
        }
        // `<transform-list>` is a space list of transform functions by nature.
        const char multiplier = alt.name == "transform-list" ? '+' : alt.multiplier;
        const std::vector<item> parts = items_of(s, multiplier);
        if (parts.empty()) { continue; }
        std::string out;
        bool all = true;
        for (const item & part : parts) {
            const std::optional<std::string> one = match_item(s, part, alt.name);
            if (!one) {
                all = false;
                break;
            }
            if (!out.empty()) { out += multiplier == '#' ? ", " : " "; }
            out += *one;
        }
        if (all) { return out; }
    }
    return std::nullopt;
}

// The attribute's text as a CSS <string>, CSS Syntax 3's serialisation.
[[nodiscard]] std::string quoted(std::string_view text) {
    std::string out{"\""};
    for (const char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; }
        if (c == '\n') {
            out += "\\a ";
            continue;
        }
        out += c;
    }
    out += '"';
    return out;
}

// The significant tokens of a value as (type, text) pairs, which is what two
// token streams are compared by: `style(--x: 3)` holds when the computed value
// of `--x` is the same tokens as `3`, however either was spaced.
[[nodiscard]] std::vector<std::pair<token_type, std::string>> significant(std::string_view text) {
    const token_stream s = tokenize(text);
    std::vector<std::pair<token_type, std::string>> out;
    for (const css_token & t : s.tokens) {
        if (t.type == token_type::eof) { break; }
        if (t.type == token_type::whitespace) { continue; }
        out.emplace_back(t.type, std::string{s.text_of(t)});
    }
    return out;
}

} // namespace ctbrowser::style::css::substitute_detail
