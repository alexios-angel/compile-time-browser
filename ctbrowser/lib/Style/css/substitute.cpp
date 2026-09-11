#include <ctbrowser/style/css/substitute.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/token.hpp>

namespace ctbrowser::style::css {
namespace {

// §3: twenty substitutions deep is the spec's limit, and a byte budget as well -
// `--a: var(--b) var(--b)` doubles on every level, so a depth limit alone still
// allows an exponential blow-up.
constexpr int max_depth = 20;
constexpr std::size_t max_bytes = 64 * 1024;

// One substitution function, located inside a token stream. `var()` and
// `attr()` share the shape: a name, an optional type (attr only), and a
// fallback after the first top-level comma.
struct call {
    bool is_attr = false;
    std::size_t open = 0;     // the `var(` or `attr(` token
    std::size_t close = 0;    // the matching `)`, or the eof token if none
    std::size_t name_at = 0;  // the `--x` ident
    std::size_t comma_at = 0; // the first top-level comma, or `close` if none
};

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
        if (!is_var && !is_attr) { continue; }
        out = call{};
        out.is_attr = is_attr;
        out.open = i;
        int depth = 1;
        std::size_t j = i + 1;
        bool have_name = false;
        bool after_name = false;
        for (; j < s.tokens.size() && depth > 0; ++j) {
            const css_token & t = s.tokens[j];
            if (t.type == token_type::eof) { break; }
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
            if (is_attr || out.comma_at != 0) { continue; }
            // `var( <custom-property-name> , <declaration-value>? )`: the name is
            // the FIRST token and nothing but whitespace may follow it before
            // the comma. `var(--foo type(*))` names no property and is invalid,
            // which `attr-all-types` asks for by name.
            if (t.type == token_type::whitespace) { continue; }
            if (!have_name && t.type == token_type::ident) {
                out.name_at = j;
                have_name = true;
            } else {
                after_name = true;
            }
        }
        out.close = j < s.tokens.size() ? j : s.tokens.size() - 1;
        if (out.comma_at == 0) { out.comma_at = out.close; }
        if (is_attr) { return true; } // its argument list is read after substitution
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

// --- attr(), CSS Values 5 §attr ------------------------------------------------
//
//   attr( <attr-name> <attr-type>? , <declaration-value>? )
//   <attr-type> = type( <syntax> ) | raw-string | <attr-unit>
//
// THE ATTRIBUTE'S TEXT IS NOT CSS UNTIL THE TYPE SAYS SO. With no type, or
// `raw-string`, the substitution is a <string> holding the text verbatim - which
// is why `content: attr(title)` has always been safe. With a unit, the text
// must be a bare number and gains the unit. With `type()`, the text is parsed
// against the syntax and substituted only if it matches; what does not match
// takes the fallback, and without one the declaration is invalid at
// computed-value time. A malformed argument list - `attr(!)`, a `<syntax>`
// naming a type that does not exist - is a syntax error whatever the attribute
// holds.

// One alternative of a `<syntax>`: a type name, or a keyword.
struct syntax_alternative {
    std::string name; // `length`, `number`, ... or the keyword itself
    bool keyword = false;
    char multiplier = 0; // 0, '+' (space list) or '#' (comma list)
};

// The type names `type()` may ask for. `<url>` and `<image>` are NOT here on
// purpose: an attribute may not produce a URL (§attr's security note), so
// asking for one is a syntax error rather than a type this cannot match.
constexpr std::string_view syntax_types[] = {
    "length",       "number",         "percentage", "length-percentage", "color",
    "integer",      "angle",          "time",       "resolution",        "transform-function",
    "custom-ident", "transform-list", "string"};

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

// The significant tokens of a stream, as index ranges of the items a
// multiplier separates: the whole value, its space-separated parts, or its
// comma-separated parts, each [first, last] over significant tokens with
// brackets kept whole.
struct item {
    std::size_t first = 0;
    std::size_t last = 0; // inclusive
};

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

[[nodiscard]] std::string number_of(double value) {
    calc_result n;
    n.px = value;
    n.is_number = true;
    n.type = numeric_type::number;
    return serialize_calc(n);
}

[[nodiscard]] bool is_wide_keyword(std::string_view word) {
    for (const std::string_view k :
         {"initial", "inherit", "unset", "revert", "revert-layer", "default"}) {
        if (ascii_iequals(word, k)) { return true; }
    }
    return false;
}

[[nodiscard]] bool in_names(std::string_view name, std::initializer_list<std::string_view> list) {
    for (const std::string_view one : list) {
        if (ascii_iequals(name, one)) { return true; }
    }
    return false;
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
            return number_of(t.number) + ascii_lower_copy(s.unit_of(t));
        }
        return math_fits([family](const calc_result & v) {
            return !v.is_number && v.type == family && !v.has_percent;
        });
    };
    if (type == "number") {
        if (single && t.type == token_type::number) { return number_of(t.number); }
        return math_fits([](const calc_result & v) { return v.is_number; });
    }
    if (type == "integer") {
        if (single && t.type == token_type::number && (t.flags & flag_integer) != 0) {
            return number_of(t.number);
        }
        return math_fits([](const calc_result & v) { return v.is_number; });
    }
    if (type == "percentage") {
        if (single && t.type == token_type::percentage) { return number_of(t.number) + "%"; }
        return math_fits([](const calc_result & v) { return v.has_percent && v.px == 0; });
    }
    if (type == "length" || type == "length-percentage") {
        if (single && t.type == token_type::number && t.number == 0) { return "0px"; }
        if (type == "length-percentage" && single && t.type == token_type::percentage) {
            return number_of(t.number) + "%";
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
    if (type == "string") {
        if (single && t.type == token_type::string) { return text; }
        return std::nullopt;
    }
    if (type == "custom-ident") {
        if (single && t.type == token_type::ident && !is_wide_keyword(s.text_of(t))) {
            return text;
        }
        return std::nullopt;
    }
    if (type == "color") {
        if (single && (t.type == token_type::hash || t.type == token_type::ident)) { return text; }
        if (t.type == token_type::function) {
            std::string_view name = s.text_of(t);
            name.remove_suffix(1);
            if (in_names(name, {"rgb", "rgba", "hsl", "hsla", "hwb", "lab", "lch", "oklab", "oklch",
                                "color", "color-mix", "light-dark"})) {
                return text;
            }
        }
        return std::nullopt;
    }
    if (type == "transform-function" || type == "transform-list") {
        if (t.type != token_type::function) { return std::nullopt; }
        std::string_view name = s.text_of(t);
        name.remove_suffix(1);
        if (in_names(name, {"matrix",     "translate", "translatex",  "translatey", "scale",
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

class substituter {
public:
    substituter(const custom_lookup & lookup, atom_table & atoms,
                const attribute_lookup & attributes)
        : lookup_(&lookup), atoms_(&atoms), attributes_(&attributes) {}

    [[nodiscard]] bool run(std::string_view value, std::string & out, int depth) {
        if (depth > max_depth || value.size() > max_bytes) { return false; }
        const token_stream s = tokenize(value);
        call found;
        if (!find_call(s, found)) {
            // No call, or a `var()` with no name - which is a call and invalid.
            // Telling the two apart is one scan for the function token.
            for (const css_token & t : s.tokens) {
                if (is_function_named(s, t, "var")) { return false; }
            }
            out.assign(value);
            return true;
        }
        // AN attr() WITHOUT AN ELEMENT IS LEFT AS WRITTEN: the text survives and
        // whoever reads it decides. Everything after it is still substituted.
        if (found.is_attr && !*attributes_) {
            std::string result = text_between(s, 0, found.close + 1);
            std::string expanded_tail;
            if (!run(text_between(s, found.close + 1, s.tokens.size()), expanded_tail, depth + 1)) {
                return false;
            }
            out = std::move(result) + std::move(expanded_tail);
            return true;
        }
        // Everything before the call, unchanged.
        std::string result = text_between(s, 0, found.open);
        std::string expansion;
        if (found.is_attr) {
            if (!attribute(s, found, depth, expansion)) { return false; }
        } else if (!variable(s, found, depth, expansion)) {
            return false;
        }
        result += expansion;
        // And everything after the call, which may contain more calls - so the
        // remainder is substituted rather than copied.
        const std::string tail = text_between(s, found.close + 1, s.tokens.size());
        std::string expanded_tail;
        if (!run(tail, expanded_tail, depth + 1)) { return false; }
        result += expanded_tail;
        if (result.size() > max_bytes) { return false; }
        out = std::move(result);
        return true;
    }

private:
    // THE FALLBACK is everything after the FIRST comma, commas included:
    // `var(--a, 1px, 2px)` has the fallback `1px, 2px`, because a custom
    // property's value may itself be a comma list.
    [[nodiscard]] bool fallback(const token_stream & s, const call & found, int depth,
                                std::string & expansion) {
        if (found.comma_at >= found.close) { return false; } // no fallback: invalid
        const std::string text = text_between(s, found.comma_at + 1, found.close);
        return run(trim(text, html_whitespace), expansion, depth + 1);
    }

    [[nodiscard]] bool variable(const token_stream & s, const call & found, int depth,
                                std::string & expansion) {
        const std::string_view name_text = s.text_of(s.tokens[found.name_at]);
        // A custom property's name must start with `--`; anything else in that
        // position is not a custom property and the call is invalid.
        if (!name_text.starts_with("--")) { return false; }
        const atom name = atoms_->intern(name_text);

        // CYCLE DETECTION. `--a: var(--b); --b: var(--a)` must make BOTH invalid rather
        // than recursing to the depth limit, and a set of the properties currently
        // being resolved is what says so.
        const bool cyclic =
            std::find(resolving_.begin(), resolving_.end(), name.id) != resolving_.end();
        bool ok = false;
        if (!cyclic) {
            if (const std::optional<std::string_view> held = (*lookup_)(name)) {
                resolving_.push_back(name.id);
                // A custom property's own value may itself contain var().
                ok = run(*held, expansion, depth + 1);
                resolving_.pop_back();
            }
        }
        if (ok) { return true; }
        return fallback(s, found, depth, expansion);
    }

    // attr(): the argument list is substituted FIRST - `attr(var(--x))` - and
    // read after, and what the attribute holds is then judged by the type.
    [[nodiscard]] bool attribute(const token_stream & outer, const call & found, int depth,
                                 std::string & expansion) {
        std::string inner;
        if (!run(text_between(outer, found.open + 1, found.close), inner, depth + 1)) {
            return false;
        }
        const token_stream s = tokenize(inner);
        // The head - everything before the first top-level comma - as significant
        // tokens at depth zero, with a `type()` kept whole.
        std::vector<std::size_t> head;
        std::size_t type_open = 0;
        std::size_t type_close = 0;
        call args;
        args.close = s.tokens.size() - 1;
        args.comma_at = args.close;
        {
            int depth_here = 0;
            for (std::size_t i = 0; i + 1 < s.tokens.size(); ++i) {
                const css_token & t = s.tokens[i];
                if (t.type == token_type::whitespace) { continue; }
                if (depth_here == 0 && t.type == token_type::comma) {
                    args.comma_at = i;
                    break;
                }
                if (depth_here == 0) { head.push_back(i); }
                if (t.type == token_type::function || t.type == token_type::open_paren ||
                    t.type == token_type::open_square || t.type == token_type::open_curly) {
                    if (depth_here == 0 && is_function_named(s, t, "type")) { type_open = i; }
                    ++depth_here;
                } else if (t.type == token_type::close_paren ||
                           t.type == token_type::close_square ||
                           t.type == token_type::close_curly) {
                    --depth_here;
                    if (depth_here == 0 && type_open != 0 && type_close == 0) { type_close = i; }
                }
            }
        }
        if (head.empty() || head.size() > 2 || s.tokens[head[0]].type != token_type::ident) {
            return false;
        }
        const std::string name{s.text_of(s.tokens[head[0]])};
        enum class kind : std::uint8_t {
            raw,
            unit,
            syntax
        };
        kind type = kind::raw;
        bool typed = false; // any <attr-type> at all, `raw-string` included
        std::string unit;
        std::vector<syntax_alternative> syntax;
        if (head.size() == 2) {
            typed = true;
            const css_token & t = s.tokens[head[1]];
            if (t.type == token_type::ident && ascii_iequals(s.text_of(t), "raw-string")) {
                type = kind::raw;
            } else if (t.type == token_type::ident) {
                type = kind::unit;
                unit = ascii_lower_copy(s.text_of(t));
            } else if (t.type == token_type::delim && s.text_of(t) == "%") {
                type = kind::unit;
                unit = "%";
            } else if (head[1] == type_open && type_open != 0) {
                type = kind::syntax;
                const std::size_t end = type_close != 0 ? type_close : s.tokens.size() - 1;
                std::optional<std::vector<syntax_alternative>> parsed =
                    parse_syntax(text_between(s, type_open + 1, end));
                if (!parsed) { return false; }
                syntax = std::move(*parsed);
            } else {
                return false;
            }
        }

        // THE ATTRIBUTE. Absent: the fallback, else the empty string when no
        // type was asked for, else invalid.
        const std::optional<std::string> held = (*attributes_)(name);
        if (!held) {
            if (args.comma_at < args.close) { return fallback(s, args, depth, expansion); }
            if (typed) { return false; }
            expansion = "\"\"";
            return true;
        }
        std::optional<std::string> value;
        switch (type) {
        case kind::raw: value = quoted(*held); break;
        case kind::unit: {
            // A BARE NUMBER AND NOTHING ELSE - not a calc(), not a var() - which
            // then gains the unit. `number` is the unit that adds nothing.
            const token_stream v = tokenize(*held);
            const std::vector<item> parts = items_of(v, 0);
            if (parts.size() == 1 && parts.front().first == parts.front().last &&
                v.tokens[parts.front().first].type == token_type::number) {
                const std::string number = number_of(v.tokens[parts.front().first].number);
                if (unit == "number") {
                    value = number;
                } else if (unit == "%" || evaluate_math(number + unit, length_context{}).outcome !=
                                              math_outcome::invalid) {
                    value = number + unit;
                }
            }
            break;
        }
        case kind::syntax: {
            // The text is a <declaration-value> with substitutions of its own
            // to perform before it is parsed - `attr(data-x type(*))` may hold
            // a `var()`, and a cycle through it is a cycle.
            std::string substituted;
            if (run(*held, substituted, depth + 1)) { value = match_syntax(substituted, syntax); }
            break;
        }
        }
        if (value) {
            expansion = std::move(*value);
            return true;
        }
        return fallback(s, args, depth, expansion);
    }

    const custom_lookup * lookup_;
    atom_table * atoms_;
    const attribute_lookup * attributes_;
    // The properties currently being expanded, innermost last. A vector rather than a
    // set because it is never more than a handful deep and a linear scan of four
    // integers beats hashing one.
    std::vector<std::uint32_t> resolving_;
};

} // namespace

bool may_have_var(std::string_view value) noexcept {
    for (std::size_t i = 0; i + 4 <= value.size(); ++i) {
        if (ascii_iequals(value.substr(i, 4), "var(")) { return true; }
        if (i + 5 <= value.size() && ascii_iequals(value.substr(i, 5), "attr(")) { return true; }
    }
    return false;
}

std::optional<std::string> substitute_var(std::string_view value, const custom_lookup & lookup,
                                          atom_table & atoms, const attribute_lookup & attributes) {
    substituter engine{lookup, atoms, attributes};
    std::string out;
    if (!engine.run(value, out, 0)) { return std::nullopt; }
    if (introduced_structure(out)) { return std::nullopt; }
    return out;
}

} // namespace ctbrowser::style::css
