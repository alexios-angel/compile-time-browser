#include <ctbrowser/style/css/substitute.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/boolean.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/properties.hpp>
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
    bool is_if = false;
    bool is_ident = false;
    bool is_random_item = false;
    bool is_inherit = false; // `inherit(--x, fallback)`: var()'s shape, the parent's value
    bool is_custom = false;  // `--name(args)`: a custom function, CSS Functions and Mixins 1
    // A var() whose name position holds a FUNCTION - `var(ident("--" "x"))` -
    // which is read only once that function has been substituted.
    bool name_is_call = false;
    std::size_t open = 0;     // the `var(`, `attr(`, `if(` or `ident(` token
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

// The type names `type()` may ask for. `<url>` is NOT here on purpose: an
// attribute may not produce a URL (§attr's security note), so asking for one
// is a syntax error rather than a type this cannot match. `<image>` is - a
// gradient is an image with no URL in it - and a `url()` in one is refused
// for the same reason.
constexpr std::string_view syntax_types[] = {
    "length", "number", "percentage", "length-percentage",  "color",        "integer",
    "angle",  "time",   "resolution", "transform-function", "custom-ident", "transform-list",
    "string", "image",  "frequency"};

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
    if (type == "frequency") { return dimension_of(numeric_type::frequency); }
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
    if (type == "image") {
        if (t.type != token_type::function) { return std::nullopt; }
        std::string_view name = s.text_of(t);
        name.remove_suffix(1);
        if (in_names(name, {"linear-gradient", "radial-gradient", "conic-gradient",
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

class substituter {
public:
    substituter(const custom_lookup & lookup, atom_table & atoms,
                const attribute_lookup & attributes, const condition_environment * conditions)
        : lookup_(&lookup), atoms_(&atoms), attributes_(&attributes), conditions_(conditions) {}

    // TWO TOKEN STREAMS JOINED STAY TWO TOKENS. `if(style(--x): a)if(style(--x):
    // b)` substitutes to `a` beside `b`, which read back as one token `ab`;
    // CSS Syntax 3 §9 puts an empty comment between two tokens that would
    // otherwise merge, and so does every engine's serialisation of a custom
    // property (if-conditionals).
    static void join(std::string & left, std::string_view right) {
        const auto name_char = [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   c == '-' || c == '_' || c == '.' || c == '%';
        };
        if (!left.empty() && !right.empty() && name_char(left.back()) && name_char(right.front())) {
            left += "/**/";
        }
        left += right;
    }

    // THE PROPERTY THIS VALUE BELONGS TO, when it is a custom one, so that a
    // style query reading it back is a cycle: `--x: if(style(--x): a; else: b)`.
    void resolving(atom property) {
        self_ = property;
        resolving_.push_back(property.id);
    }
    [[nodiscard]] bool reached_self() const noexcept { return root_cycle_; }

    // ONE MATH FUNCTION, against the caller's bases, with the value's
    // random() functions numbered in order across every expression this
    // substitution evaluates: `style(random(0, 1) = random(0, 1))` draws
    // twice, and two `if()`s in one value draw a first and a second
    // (random-in-if).
    [[nodiscard]] math_answer math_of(std::string_view text) {
        length_context ctx = conditions_ != nullptr ? conditions_->lengths : length_context{};
        if (ctx.property.empty() && conditions_ != nullptr) {
            ctx.property = conditions_->property;
        }
        ctx.random_index += randoms_seen_;
        const math_answer answer = evaluate_math(text, ctx);
        randoms_seen_ += answer.randoms;
        return answer;
    }

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
        // AN attr() WITHOUT AN ELEMENT IS LEFT AS WRITTEN, and so is an if()
        // without an environment: the text survives and whoever reads it
        // decides. Everything after it is still substituted.
        if ((found.is_attr && !*attributes_) || (found.is_if && conditions_ == nullptr) ||
            (found.is_custom && (conditions_ == nullptr || !conditions_->functions))) {
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
        } else if (found.is_if) {
            if (!conditional(s, found, depth, expansion)) { return false; }
        } else if (found.is_ident) {
            if (!identifier(s, found, depth, expansion)) { return false; }
        } else if (found.is_random_item) {
            if (!random_item(s, found, depth, expansion)) { return false; }
        } else if (found.is_inherit) {
            if (!inherited_value(s, found, depth, expansion)) { return false; }
        } else if (found.is_custom) {
            if (!custom_call(s, found, depth, expansion)) { return false; }
        } else if (!variable(s, found, depth, expansion)) {
            return false;
        }
        join(result, expansion);
        // And everything after the call, which may contain more calls - so the
        // remainder is substituted rather than copied.
        const std::string tail = text_between(s, found.close + 1, s.tokens.size());
        std::string expanded_tail;
        if (!run(tail, expanded_tail, depth + 1)) { return false; }
        join(result, expanded_tail);
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
        std::string_view name_text = s.text_of(s.tokens[found.name_at]);
        std::string built;
        if (found.name_is_call) {
            // `var(ident("--" attr(data-name)))`: the name is what the call
            // makes, and it has to be exactly one identifier.
            if (!run(text_between(s, found.name_at, found.comma_at), built, depth + 1)) {
                return false;
            }
            const std::vector<std::pair<token_type, std::string>> tokens = significant(built);
            if (tokens.size() != 1 || tokens.front().first != token_type::ident) { return false; }
            built = tokens.front().second;
            name_text = built;
        }
        // A custom property's name must start with `--`; anything else in that
        // position is not a custom property and the call is invalid.
        if (!name_text.starts_with("--")) { return false; }
        const atom name = atoms_->intern(name_text);

        // CYCLE DETECTION. `--a: var(--b); --b: var(--a)` must make BOTH invalid rather
        // than recursing to the depth limit, and a set of the properties currently
        // being resolved is what says so.
        const bool cyclic = reached_again(name);
        bool ok = false;
        if (conditions_ != nullptr && conditions_->on_read) { conditions_->on_read(name_text); }
        if (!cyclic) {
            if (const std::optional<std::string_view> held = (*lookup_)(name)) {
                resolving_.push_back(name.id);
                // A custom property's own value may itself contain var() -
                // and its attr()s are ITS OWN, computed as they would be for
                // the property itself: `--x: attr(data-foo)` read through
                // var(--x) while data-foo is being substituted is the string
                // it always is, not a ring (attr-cycle 26, 27). A ring through
                // var() is still caught, by this stack.
                std::vector<std::string> attrs_outside;
                attrs_outside.swap(attrs_resolving_);
                ok = run(*held, expansion, depth + 1);
                attrs_resolving_.swap(attrs_outside);
                resolving_.pop_back();
            }
        }
        if (ok) { return true; }
        return fallback(s, found, depth, expansion);
    }

    // inherit( <custom-property-name> , <declaration-value>? ), CSS Values 5
    // §inherit-notation: the PARENT's computed value of the property, which
    // is what `style(--x: inherit)` already reads, and the fallback when the
    // parent has none (inherit-function-basic).
    //
    // ponytail: custom properties only, and one level - the parent's own value
    // is substituted in the parent's scope with no grandparent to ask, so
    // `--v: e2 inherit(--v)` on the parent does not accumulate through it.
    [[nodiscard]] bool inherited_value(const token_stream & s, const call & found, int depth,
                                       std::string & expansion) {
        const std::string_view name_text = s.text_of(s.tokens[found.name_at]);
        if (!name_text.starts_with("--")) { return false; }
        if (conditions_ != nullptr && conditions_->inherited) {
            if (std::optional<std::string> held = conditions_->inherited(name_text)) {
                expansion = std::move(*held);
                return true;
            }
        }
        return fallback(s, found, depth, expansion);
    }

    // attr(): the argument list is substituted FIRST - `attr(var(--x))` - and
    // read after, and what the attribute holds is then judged by the type.
    [[nodiscard]] bool attribute(const token_stream & outer, const call & found, int depth,
                                 std::string & expansion) {
        // THE HEAD IS SUBSTITUTED ON ITS OWN, and may not grow a comma: a
        // `var()` in the name or type position that expands to `data-foo
        // type(<number>), 10` has not supplied a fallback, it has broken the
        // argument list (attr-argument-grammar). The fallback keeps its own
        // text and is substituted only if it is taken.
        std::string inner;
        if (!run(text_between(outer, found.open + 1, found.comma_at), inner, depth + 1)) {
            return false;
        }
        {
            const token_stream head_tokens = tokenize(inner);
            int depth_here = 0;
            for (const css_token & t : head_tokens.tokens) {
                if (t.type == token_type::function || t.type == token_type::open_paren ||
                    t.type == token_type::open_square || t.type == token_type::open_curly) {
                    ++depth_here;
                } else if (t.type == token_type::close_paren ||
                           t.type == token_type::close_square ||
                           t.type == token_type::close_curly) {
                    --depth_here;
                } else if (depth_here == 0 && t.type == token_type::comma) {
                    return false;
                }
            }
        }
        if (found.comma_at < found.close) {
            inner += text_between(outer, found.comma_at, found.close);
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
        // AN ATTRIBUTE ALREADY BEING SUBSTITUTED IS A CYCLE however it is read
        // again - through `type(*)`, or as the raw string a bare attr() makes
        // of it: `data-foo="attr(data-bar type(*))"` with `data-bar="attr(
        // data-foo)"` is a ring, and every attr() in it is invalid, fallbacks
        // included; only the attr() the declaration itself wrote takes its
        // fallback (attr-cycle 3, 28, 29).
        if (std::ranges::find(attrs_resolving_, name) != attrs_resolving_.end()) {
            attr_cycle_ = true;
            return false;
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
            // a `var()` or another `attr()`, and a cycle through it is a cycle:
            // an attribute already being substituted makes every attr() in the
            // ring invalid, fallbacks included, and only the attr() the
            // declaration itself wrote takes its fallback (attr-cycle 3, 8, 12,
            // 17, 28, 29; attr-all-types 75-79 read one attribute through
            // another without a cycle).
            attrs_resolving_.push_back(name);
            std::string substituted;
            const bool ok = run(*held, substituted, depth + 1);
            attrs_resolving_.pop_back();
            if (attr_cycle_) {
                if (!attrs_resolving_.empty()) { return false; }
                attr_cycle_ = false;
                break;
            }
            if (ok) { value = match_syntax(substituted, syntax); }
            break;
        }
        }
        if (value) {
            expansion = std::move(*value);
            return true;
        }
        return fallback(s, args, depth, expansion);
    }

    // --- ident(), CSS Values 5 §ident ---------------------------------------
    //
    //   ident( <ident-arg>+ )
    //   <ident-arg> = <string> | <integer> | <ident>
    //
    // The arguments joined into one identifier, at computed-value time: they may
    // hold other substitution functions, and an `<integer>` may be a math
    // function - `ident("vtl-" sibling-index())` names the third child `vtl-3`.
    // Anything else among them makes the whole invalid, and so does a result
    // that is not an identifier at all.
    [[nodiscard]] bool identifier(const token_stream & outer, const call & found, int depth,
                                  std::string & expansion) {
        std::string inner;
        if (!run(text_between(outer, found.open + 1, found.close), inner, depth + 1)) {
            return false;
        }
        const token_stream s = tokenize(inner);
        std::string made;
        bool any = false;
        for (std::size_t i = 0; i + 1 < s.tokens.size(); ++i) {
            const css_token & t = s.tokens[i];
            if (t.type == token_type::whitespace) { continue; }
            any = true;
            if (t.type == token_type::string || t.type == token_type::ident) {
                made += s.value_of(t);
                continue;
            }
            if (t.type == token_type::number && (t.flags & flag_integer) != 0) {
                made += number_of(t.number);
                continue;
            }
            if (t.type == token_type::function) {
                const std::size_t close = end_of_block(s, i);
                const std::string text = text_between(s, i, close);
                if (!may_have_math(text)) { return false; }
                const math_answer answer = math_of(text);
                if (answer.outcome != math_outcome::resolved || !answer.value.is_number) {
                    return false;
                }
                // An <integer> slot rounds a math function's answer (CSS
                // Values 4 §10.10): `ident("a" random(1, 300000))` is a name.
                made += number_of(std::floor(answer.value.px + 0.5));
                i = close - 1;
                continue;
            }
            return false;
        }
        if (!any || made.empty()) { return false; }
        // The result is an IDENTIFIER, and is spelled as one: `ident(3 "abc")`
        // is `\33 abc`, because `3abc` would read back as a dimension
        // (ident-function-computed).
        expansion = serialize_identifier(made);
        return true;
    }

    // --- --name(), CSS Functions and Mixins 1 §2 ------------------------------
    //
    //   --name( <declaration-value>#? )
    //
    // The arguments are substituted in the CALLER's scope and bound to the
    // parameters: a missing one takes its default, a typed one is computed
    // like the type it names, and too many or a missing one with no default
    // makes the call invalid. The body's locals and `result` are then
    // substituted in the FUNCTION's scope - parameters and locals first, the
    // calling element's custom properties after - and a typed `result` is
    // computed like its type on the way out. What comes out replaces the
    // call, and an untyped result is a token stream the caller reads.
    //
    // A `random()` met while computing a typed parameter or result is keyed on
    // the function and the slot (`--f/result`, `--f/--x`) and on THIS
    // INVOCATION as its element: `property-index-scoped` there is the same
    // draw wherever the call sits and `element-scoped` differs per call
    // (random-in-custom-function). An untyped one escapes as text and is drawn
    // where it lands.
    [[nodiscard]] bool custom_call(const token_stream & outer, const call & found, int depth,
                                   std::string & expansion) {
        const std::string_view head = outer.text_of(outer.tokens[found.open]);
        const std::string name{head.substr(0, head.size() - 1)};
        const custom_function * fn = conditions_->functions(name);
        if (fn == nullptr) { return false; }
        const std::uint64_t invocation = ++calls_;
        std::vector<std::string> args;
        const std::string inner = text_between(outer, found.open + 1, found.close);
        if (!trim(inner, html_whitespace).empty()) {
            for (const std::string_view arg : split_top_level(inner, ",")) {
                std::string done;
                if (!run(trim(arg, html_whitespace), done, depth + 1)) { return false; }
                args.push_back(std::move(done));
            }
        }
        if (args.size() > fn->parameters.size()) { return false; }
        // The bases a typed slot computes against: the caller's, keyed on the
        // function's slot and on this invocation - which is the calling
        // element, the property the call sits in, and which call of that
        // value this is.
        std::string slot_name;
        const std::uint64_t call_key =
            (conditions_->lengths.element_key ^ (invocation * 0x9E3779B97F4A7C15ull) ^
             (std::hash<std::string_view>{}(conditions_->property) * 0xBF58476D1CE4E5B9ull)) |
            1;
        const auto slot = [&](std::string_view which) {
            slot_name = name + '/' + std::string{which};
            length_context ctx = conditions_->lengths;
            ctx.property = slot_name;
            ctx.element_key = call_key;
            return ctx;
        };
        std::vector<std::pair<std::string, std::string>> scope;
        const auto assign = [&scope](std::string_view key, std::string value) {
            for (auto & [held, text] : scope) {
                if (held == key) {
                    text = std::move(value);
                    return;
                }
            }
            scope.emplace_back(std::string{key}, std::move(value));
        };
        const auto unset = [&scope](std::string_view key) {
            std::erase_if(scope, [key](const auto & entry) { return entry.first == key; });
        };
        for (std::size_t i = 0; i < fn->parameters.size(); ++i) {
            const custom_function::parameter & param = fn->parameters[i];
            std::string value;
            if (i < args.size() && !trim(args[i], html_whitespace).empty()) {
                value = args[i];
            } else if (param.has_default) {
                if (!run(param.initial, value, depth + 1)) { return false; }
            } else {
                return false;
            }
            if (param.syntax != "*") {
                std::optional<std::string> typed =
                    compute_registered(value, param.syntax, slot(param.name));
                if (!typed) { return false; }
                value = std::move(*typed);
            }
            assign(param.name, std::move(value));
        }
        // The function's scope: its parameters and locals, then the caller's.
        const custom_lookup in_scope = [&scope, this](atom key) -> std::optional<std::string_view> {
            const std::string_view want = atoms_->text(key);
            for (const auto & [held, text] : scope) {
                if (held == want) { return std::string_view{text}; }
            }
            return (*lookup_)(key);
        };
        std::string result;
        bool have_result = false;
        for (const auto & [declared, text] : fn->body) {
            substituter body{in_scope, *atoms_, *attributes_, conditions_};
            std::string done;
            const bool ok = body.run(text, done, depth + 1);
            if (declared == "result") {
                if (!ok) { return false; }
                result = std::move(done);
                have_result = true;
                continue;
            }
            if (!ok) {
                unset(declared); // guaranteed-invalid: the local is undefined
                continue;
            }
            // A typed parameter set again in the body is computed like the
            // parameter (random-in-custom-function).
            for (const custom_function::parameter & param : fn->parameters) {
                if (param.name != declared || param.syntax == "*") { continue; }
                std::optional<std::string> typed =
                    compute_registered(done, param.syntax, slot(param.name));
                if (!typed) {
                    unset(declared);
                    done.clear();
                    break;
                }
                done = std::move(*typed);
            }
            if (!done.empty()) { assign(declared, std::move(done)); }
        }
        if (!have_result) { return false; }
        if (fn->returns != "*") {
            std::optional<std::string> typed =
                compute_registered(result, fn->returns, slot("result"));
            if (!typed) { return false; }
            result = std::move(*typed);
        }
        expansion = std::move(result);
        return true;
    }

    // --- random-item(), CSS Values 5 §random-item -----------------------------
    //
    //   random-item( <random-key> , [ <declaration-value>? ]# )
    //   <random-key> = [ auto | <dashed-ident> | fixed <number> ] || element-scoped
    //
    // One of the items, chosen by the same base `random()` uses - `fixed`
    // picks by index outright, `auto` is per element and property, a name is
    // shared - and substituted only once chosen, so an item nobody picked may
    // hold a var() nothing resolves. `{a, b}` braces keep a comma inside one
    // item and come off with it.
    [[nodiscard]] bool random_item(const token_stream & outer, const call & found, int depth,
                                   std::string & expansion) {
        if (found.comma_at >= found.close) { return false; } // no items at all
        std::string key;
        if (!run(text_between(outer, found.open + 1, found.comma_at), key, depth + 1)) {
            return false;
        }
        // The key: `fixed <number>`, or the sharing words `random_base` reads.
        double base = 0.0;
        const std::string_view trimmed = trim(key, html_whitespace);
        if (ascii_istarts_with(trimmed, "fixed")) {
            const math_answer given = evaluate_math(trimmed.substr(5), length_context{});
            if (given.outcome != math_outcome::resolved || !given.value.is_number) { return false; }
            base = std::min(std::max(given.value.px, 0.0), 1.0 - 1e-9);
        } else {
            for (const std::string_view word : split_top_level(trimmed, " \t\n\r\f")) {
                if (!word.starts_with("--") && !ascii_iequals(word, "auto") &&
                    !ascii_iequals(word, "element-scoped")) {
                    return false;
                }
            }
            length_context ctx = conditions_ != nullptr ? conditions_->lengths : length_context{};
            if (conditions_ != nullptr) { ctx.property = conditions_->property; }
            // `auto` is scoped to the element like a bare `element-scoped`: the
            // base is keyed on the element either way, and a name alone is not.
            // An auto random-item() does NOT share with an auto random() in the
            // same value, where a bare `element-scoped` one does
            // (random-item-computed): its automatic key names the function.
            std::string options{trimmed};
            std::string keyed_property{ctx.property};
            if (ascii_iequals(trimmed, "auto") || trimmed.empty()) {
                options.clear();
                keyed_property = "random-item:" + keyed_property;
                ctx.property = keyed_property;
            }
            base = random_base(options, ctx);
        }
        // The items, split at the top-level commas after the key.
        std::vector<std::pair<std::size_t, std::size_t>> items;
        std::size_t start = found.comma_at + 1;
        int block = 0;
        for (std::size_t i = start; i < found.close; ++i) {
            const css_token & t = outer.tokens[i];
            if (t.type == token_type::function || t.type == token_type::open_paren ||
                t.type == token_type::open_square || t.type == token_type::open_curly) {
                ++block;
            } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                       t.type == token_type::close_curly) {
                --block;
            } else if (block == 0 && t.type == token_type::comma) {
                items.emplace_back(start, i);
                start = i + 1;
            }
        }
        items.emplace_back(start, found.close);
        const std::size_t pick = std::min(
            items.size() - 1, static_cast<std::size_t>(base * static_cast<double>(items.size())));
        std::string_view chosen =
            trim(text_between_view(outer, items[pick].first, items[pick].second), html_whitespace);
        if (chosen.size() >= 2 && chosen.front() == '{' && chosen.back() == '}') {
            chosen = trim(chosen.substr(1, chosen.size() - 2), html_whitespace);
        }
        return run(chosen, expansion, depth + 1);
    }

    // --- if(), CSS Values 5 §if-notation ------------------------------------
    //
    //   if( [ <if-branch> ; ]* <if-branch> ;? )
    //   <if-branch> = <if-condition> : <declaration-value>?
    //   <if-condition> = <boolean-expr[ <if-test> ]> | else
    //   <if-test> = style( <style-query> ) | media( <media-condition> ) |
    //               supports( <supports-condition> | <ident> : <declaration-value> )
    //
    // The first branch whose condition holds supplies the value, substituted
    // like any other; `else` always holds. No branch holding is invalid at
    // computed-value time, exactly as a `var()` with nothing to substitute.
    //
    // A CONDITION IS SUBSTITUTED BEFORE IT IS READ - `style(--x: var(--y))`
    // compares against what `--y` holds and `if(var(--else): ...)` is an
    // `else` branch - and a condition that fails to substitute, or does not
    // parse as a condition, is simply false: `if-conditionals` writes both
    // `if(style(--x) and invalid: a; else: b)` and `if(style(--missing:
    // var(--missing)): a; else: b)` and asks for `b`. A `!` anywhere in the
    // argument list is different: no branch can contain one, so the whole
    // function is malformed.
    [[nodiscard]] bool conditional(const token_stream & outer, const call & found, int depth,
                                   std::string & expansion) {
        const std::size_t first = found.open + 1;
        const std::size_t last = found.close; // the `)` or eof, exclusive
        if (first >= last) { return false; }  // `if()`
        // The branches, split at the top-level `;`.
        struct branch {
            std::size_t from = 0;
            std::size_t colon = 0; // the first top-level `:`, or `to` if none
            std::size_t to = 0;
        };
        std::vector<branch> branches;
        {
            branch current{first, last, last};
            int block = 0;
            for (std::size_t i = first; i < last; ++i) {
                const css_token & t = outer.tokens[i];
                if (t.type == token_type::function || t.type == token_type::open_paren ||
                    t.type == token_type::open_square || t.type == token_type::open_curly) {
                    ++block;
                } else if (t.type == token_type::close_paren ||
                           t.type == token_type::close_square ||
                           t.type == token_type::close_curly) {
                    --block;
                } else if (block == 0 && t.type == token_type::delim && outer.text_of(t) == "!") {
                    return false;
                } else if (block == 0 && t.type == token_type::semicolon) {
                    current.to = i;
                    branches.push_back(current);
                    current = branch{i + 1, last, last};
                } else if (block == 0 && t.type == token_type::colon && current.colon == last) {
                    current.colon = i;
                }
            }
            // The last branch, unless the `;` before it was the optional
            // trailing one.
            bool blank = true;
            for (std::size_t i = current.from; i < last; ++i) {
                if (outer.tokens[i].type != token_type::whitespace) { blank = false; }
            }
            if (!blank) {
                current.to = last;
                branches.push_back(current);
            }
        }
        if (branches.empty()) { return false; }
        for (const branch & b : branches) {
            if (b.colon == last || b.colon > b.to) { return false; } // no `:`
            const std::string condition =
                std::string{trim(text_between(outer, b.from, b.colon), html_whitespace)};
            bool holds = false;
            std::string substituted;
            if (run(condition, substituted, depth + 1)) {
                // `else`, and a `var(--else)` that substitutes to it.
                holds = ascii_iequals(trim(substituted, html_whitespace), "else") ||
                        evaluate_condition(substituted, depth) == truth::yes;
            } else if (!cyclic_ && !root_cycle_) {
                // A var() the condition could not resolve makes only the
                // FEATURE holding it false, not the whole condition: `style(not
                // (--x: var(--y)))` with a cyclic `--y` is true (if-cycle).
                // The features substitute their own values as they are read.
                holds = evaluate_condition(condition, depth) == truth::yes;
            }
            // A query that read the property being resolved is a cycle, and a
            // cycle is invalid whichever branch it would have chosen.
            if (cyclic_ || root_cycle_) { return false; }
            if (!holds) { continue; }
            std::string_view value = text_between_view(outer, b.colon + 1, b.to);
            while (!value.empty() &&
                   html_whitespace.find(value.front()) != std::string_view::npos) {
                value.remove_prefix(1);
            }
            return run(value, expansion, depth + 1);
        }
        return false;
    }

    // `text_between` as a view over the pool, valid while `s` is: the tokens
    // of one stream are contiguous slices of it in order, escapes aside, and a
    // branch value has to keep its trailing whitespace exactly as written.
    [[nodiscard]] static std::string_view text_between_view(const token_stream & s,
                                                            std::size_t first, std::size_t last) {
        if (first >= last) { return {}; }
        const css_token & a = s.tokens[first];
        const css_token & b = s.tokens[last - 1];
        if (b.text + b.length < a.text) { return {}; }
        return std::string_view{s.pool}.substr(a.text, b.text + b.length - a.text);
    }

    [[nodiscard]] truth evaluate_condition(std::string_view condition, int depth) {
        const token_stream s = tokenize(condition);
        const auto test = [&](std::string_view name, std::size_t from, std::size_t to) -> truth {
            if (ascii_iequals(name, "style")) { return style_query(s, from, to, depth); }
            if (ascii_iequals(name, "media")) {
                if (!conditions_->media) { return truth::unknown; }
                const std::optional<bool> held = conditions_->media(text_between(s, from, to));
                return held ? (*held ? truth::yes : truth::no) : truth::unknown;
            }
            if (ascii_iequals(name, "supports")) {
                const std::string text = text_between(s, from, to);
                // `supports( <ident> : <declaration-value> )` is the one form
                // `<supports-condition>` does not already cover.
                if (supports_condition(text)) { return truth::yes; }
                if (supports_condition("(" + text + ")")) { return truth::yes; }
                return truth::no;
            }
            return truth::unknown;
        };
        // A parenthesised group that is not a condition is a
        // `<general-enclosed>`: undecidable, not an error.
        const auto enclosed = [](std::size_t, std::size_t) { return truth::unknown; };
        const std::size_t end = s.tokens.size() - 1; // the eof token
        return boolean_expression(s, 0, end, test, enclosed).value_or(truth::no);
    }

    // --- style(), CSS Conditional 5 §style-container --------------------------
    //
    //   <style-query> = <style-condition> | <style-feature>
    //   <style-feature> = <mf-plain> | <mf-boolean> | <mf-range>
    //
    // A plain feature compares a property's computed value with the query's
    // as token streams; a boolean one asks whether it has a value at all; a
    // range compares two numeric values of one type. The whole query is tried
    // as one feature first - `style(--x: 3)` - and then as a condition whose
    // parenthesised groups are features: `style((--x: 3) and (not (--y: red)))`.
    [[nodiscard]] truth style_query(const token_stream & s, std::size_t from, std::size_t to,
                                    int depth) {
        if (const std::optional<truth> one = style_feature(s, from, to, depth)) { return *one; }
        // `style(style(--x))`, `style(var(--x))`: a function is not a feature.
        const auto test = [](std::string_view, std::size_t, std::size_t) { return truth::unknown; };
        const auto enclosed = [&](std::size_t a, std::size_t b) {
            return style_feature(s, a, b, depth).value_or(truth::unknown);
        };
        return boolean_expression(s, from, to, test, enclosed).value_or(truth::unknown);
    }

    // IS `name` ALREADY BEING RESOLVED - and if so, whose cycle is it? A cycle
    // back to the property whose value this whole substitution is (`self_`) is
    // remembered for good: an `if()` whose condition reached it is invalid
    // whichever branch would have been taken, and so is a var() of it
    // (if-cycle). A cycle back to the property a style query is currently
    // computing (`owners_.back()`) makes THAT property valueless, which the
    // query then reads as false. A cycle among any other properties merely
    // leaves them without a value.
    [[nodiscard]] bool reached_again(atom name) {
        if (std::find(resolving_.begin(), resolving_.end(), name.id) == resolving_.end()) {
            return false;
        }
        if (self_ && name == self_) { root_cycle_ = true; }
        if (!owners_.empty() && name == owners_.back()) { cyclic_ = true; }
        return true;
    }

    // The computed value of a custom property on the element, substituted, or
    // nothing when it has none - unset, the guaranteed-invalid value, or a
    // value whose own substitution fails. A cycle through it is a cycle.
    [[nodiscard]] std::optional<std::string> custom_value(std::string_view name, int depth) {
        const atom key = atoms_->intern(name);
        if (reached_again(key)) { return std::nullopt; }
        const std::optional<std::string_view> held = (*lookup_)(key);
        if (!held) { return std::nullopt; }
        // The property's own value is resolved AS ITS OWN: a cycle back to it
        // found while computing it makes IT invalid, not the query that asked.
        const bool outer_cyclic = cyclic_;
        cyclic_ = false;
        owners_.push_back(key);
        resolving_.push_back(key.id);
        std::string out;
        const bool ok = run(*held, out, depth + 1);
        resolving_.pop_back();
        owners_.pop_back();
        cyclic_ = outer_cyclic;
        if (!ok) { return std::nullopt; }
        return out;
    }

    // ONE NUMERIC VALUE FOR A RANGE QUERY. A lone `--x` is the property's
    // computed value; anything else is the text as written. Either then has to
    // be one number, dimension or percentage - `calc(3px + 3px)` folds, `initial`
    // does not - and a percentage is its own family, so `1px >= 1%` is
    // undecidable rather than a comparison of magnitudes.
    struct magnitude {
        numeric_type type = numeric_type::number;
        bool percent = false;
        double value = 0.0;
    };
    [[nodiscard]] std::optional<magnitude> range_operand(std::string_view text, int depth) {
        std::string own_text;
        if (!run(text, own_text, depth + 1)) { return std::nullopt; }
        std::string_view value = trim(own_text, html_whitespace);
        std::string held;
        if (value.starts_with("--")) {
            const std::vector<std::pair<token_type, std::string>> tokens = significant(value);
            if (tokens.size() != 1 || tokens.front().first != token_type::ident) {
                return std::nullopt;
            }
            const std::optional<std::string> computed = custom_value(value, depth);
            if (!computed) { return std::nullopt; }
            held = *computed;
            value = trim(held, html_whitespace);
        }
        const math_answer answer = math_of(value);
        if (answer.outcome != math_outcome::resolved) { return std::nullopt; }
        magnitude out;
        if (answer.value.has_percent) {
            if (answer.value.px != 0.0) { return std::nullopt; } // `calc(10% + 1px)`
            out.percent = true;
            out.value = answer.value.percent;
            return out;
        }
        out.type = answer.value.type;
        out.value = answer.value.px;
        return out;
    }

    [[nodiscard]] static truth compare(magnitude a, std::string_view op, magnitude b) {
        // A UNITLESS ZERO IS A LENGTH where a length is wanted (CSS Values 4
        // §6.1 <zero>), and nowhere else: `0 = 0px` holds, `0 = 0%` does not.
        const auto zero_length = [](const magnitude & m) {
            return m.type == numeric_type::number && !m.percent && m.value == 0.0;
        };
        if (zero_length(a) && b.type == numeric_type::length && !b.percent) { a.type = b.type; }
        if (zero_length(b) && a.type == numeric_type::length && !a.percent) { b.type = a.type; }
        if (a.type != b.type || a.percent != b.percent) { return truth::unknown; }
        bool holds = false;
        if (op == "<") {
            holds = a.value < b.value;
        } else if (op == "<=") {
            holds = a.value <= b.value;
        } else if (op == ">") {
            holds = a.value > b.value;
        } else if (op == ">=") {
            holds = a.value >= b.value;
        } else {
            holds = a.value == b.value;
        }
        return holds ? truth::yes : truth::no;
    }

    // One `<style-feature>` over the tokens [from, to), or nothing when they
    // are not one - a condition, or nonsense.
    [[nodiscard]] std::optional<truth> style_feature(const token_stream & s, std::size_t from,
                                                     std::size_t to, int depth) {
        // The top-level structure: the comparison operators and the colon, with
        // a `!` anywhere making the feature invalid.
        struct op_at {
            std::size_t at = 0;
            std::size_t width = 1; // `<=` is two delim tokens
            std::string text;
        };
        std::vector<op_at> ops;
        std::size_t colon = to;
        std::size_t first_significant = to;
        std::size_t count = 0;
        int block = 0;
        for (std::size_t i = from; i < to; ++i) {
            const css_token & t = s.tokens[i];
            if (t.type == token_type::whitespace) { continue; }
            if (first_significant == to) { first_significant = i; }
            ++count;
            if (t.type == token_type::function || t.type == token_type::open_paren ||
                t.type == token_type::open_square || t.type == token_type::open_curly) {
                ++block;
                continue;
            }
            if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                t.type == token_type::close_curly) {
                --block;
                continue;
            }
            if (block != 0) { continue; }
            if (t.type == token_type::delim && s.text_of(t) == "!") { return truth::no; }
            if (t.type == token_type::colon && colon == to) { colon = i; }
            if (t.type == token_type::delim) {
                const std::string_view d = s.text_of(t);
                if (d == "<" || d == ">" || d == "=") {
                    op_at op{i, 1, std::string{d}};
                    if (d != "=" && is_delim_text(s, i + 1, '=')) {
                        op.width = 2;
                        op.text += '=';
                        ++i;
                    }
                    ops.push_back(std::move(op));
                }
            }
        }
        if (count == 0) { return std::nullopt; }
        // <mf-range>: `a < b`, `a < b < c`.
        if (!ops.empty()) {
            if (colon != to || ops.size() > 2) { return truth::no; }
            std::vector<std::string_view> sides;
            std::size_t at = from;
            for (const op_at & op : ops) {
                sides.push_back(text_between_view(s, at, op.at));
                at = op.at + op.width;
            }
            sides.push_back(text_between_view(s, at, to));
            if (ops.size() == 2) {
                // Both operators must point the same way: `3 < x <= 5`.
                const bool less_a = ops[0].text.front() == '<';
                const bool less_b = ops[1].text.front() == '<';
                if (ops[0].text == "=" || ops[1].text == "=" || less_a != less_b) {
                    return truth::no;
                }
            }
            truth result = truth::yes;
            for (std::size_t i = 0; i < ops.size(); ++i) {
                const std::optional<magnitude> a = range_operand(sides[i], depth);
                const std::optional<magnitude> b = range_operand(sides[i + 1], depth);
                if (!a || !b) { return truth::unknown; }
                result = both(result, compare(*a, ops[i].text, *b));
            }
            return result;
        }
        // <mf-plain> and <mf-boolean>: the property name first.
        const css_token & head = s.tokens[first_significant];
        if (head.type != token_type::ident) { return std::nullopt; }
        const std::string_view property = s.text_of(head);
        const bool custom = property.starts_with("--");
        if (colon == to) {
            // `style(--x)`: does it have a value? Anything else always does.
            if (count != 1) { return std::nullopt; }
            if (!custom) { return truth::yes; }
            return custom_value(property, depth) ? truth::yes : truth::no;
        }
        // The name must be the only thing before the colon.
        for (std::size_t i = first_significant + 1; i < colon; ++i) {
            if (s.tokens[i].type != token_type::whitespace) { return std::nullopt; }
        }
        std::string query_text;
        if (!run(text_between_view(s, colon + 1, to), query_text, depth + 1)) {
            return truth::no; // a value nothing can substitute matches nothing
        }
        const std::string_view query = trim(query_text, html_whitespace);
        if (custom) {
            const std::optional<std::string> own = custom_value(property, depth);
            const property_registration * registered =
                conditions_->registered ? conditions_->registered(property) : nullptr;
            const auto same = [](const std::optional<std::string> & a,
                                 const std::optional<std::string> & b) {
                if (!a && !b) { return truth::yes; }
                if (!a || !b) { return truth::no; }
                return significant(*a) == significant(*b) ? truth::yes : truth::no;
            };
            const auto parent = [&]() -> std::optional<std::string> {
                return conditions_->inherited ? conditions_->inherited(property) : std::nullopt;
            };
            // THE CSS-WIDE KEYWORDS NAME A VALUE TO COMPARE WITH rather than
            // being one: `initial` is the guaranteed-invalid value an
            // unregistered property starts from and the initial value a
            // registered one has, `inherit` is the parent's, and `unset` is
            // whichever of the two the property inherits. `revert` and
            // `revert-layer` name nothing here.
            if (ascii_iequals(query, "initial")) {
                if (registered == nullptr) { return own ? truth::no : truth::yes; }
                return same(own, compute_registered(registered->initial, registered->syntax,
                                                    conditions_->lengths));
            }
            if (ascii_iequals(query, "inherit")) { return same(own, parent()); }
            if (ascii_iequals(query, "unset")) {
                if (registered != nullptr && !registered->inherits) {
                    return same(own, compute_registered(registered->initial, registered->syntax,
                                                        conditions_->lengths));
                }
                return same(own, parent());
            }
            if (ascii_iequals(query, "revert") || ascii_iequals(query, "revert-layer")) {
                return truth::no;
            }
            if (!own) { return truth::no; }
            // A registered property compares computed values of its type, so
            // `style(--length: 1em)` holds against a `30px` under a 30px font.
            if (registered != nullptr) {
                std::optional<std::string> theirs =
                    compute_registered(query, registered->syntax, conditions_->lengths);
                if (conditions_->canonical_color && theirs &&
                    registered->syntax.find("<color>") != std::string::npos) {
                    return same(conditions_->canonical_color(*own),
                                conditions_->canonical_color(*theirs));
                }
                return same(own, theirs);
            }
            return significant(*own) == significant(query) ? truth::yes : truth::no;
        }
        if (!conditions_->computed) { return truth::unknown; }
        const std::optional<std::string> own = conditions_->computed(property);
        if (!own) { return truth::no; }
        // The query's value in the same form the computed one takes, as far as
        // a dimension goes: `style(width: 1in)` against a computed `96px`.
        std::string folded{query};
        if (may_have_math(folded)) { folded = fold_math(folded, conditions_->lengths).text; }
        if (const auto canonical = canonical_dimension_text(folded, conditions_->lengths)) {
            folded = *canonical;
        }
        return significant(*own) == significant(folded) ? truth::yes : truth::no;
    }

    const custom_lookup * lookup_;
    atom_table * atoms_;
    const attribute_lookup * attributes_;
    const condition_environment * conditions_;
    // The properties currently being expanded, innermost last. A vector rather than a
    // set because it is never more than a handful deep and a linear scan of four
    // integers beats hashing one.
    std::vector<std::uint32_t> resolving_;
    // The attributes whose values are being substituted, outermost first, and
    // whether a cycle through them was found (attr-cycle).
    std::vector<std::string> attrs_resolving_;
    bool attr_cycle_ = false;
    // The property whose value this is, the properties whose values style
    // queries are computing (innermost last), and the two cycle flags
    // `reached_again` explains.
    atom self_{};
    std::vector<atom> owners_;
    bool root_cycle_ = false;
    bool cyclic_ = false;
    std::uint32_t randoms_seen_ = 0; // the random() functions math_of has numbered
    std::uint64_t calls_ = 0;        // the custom function calls made, for their keys
};

} // namespace

bool may_have_var(std::string_view value) noexcept {
    const auto name_char = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               c == '-' || c == '_';
    };
    for (std::size_t i = 0; i + 3 <= value.size(); ++i) {
        if (i + 4 <= value.size() && ascii_iequals(value.substr(i, 4), "var(")) { return true; }
        if (i + 5 <= value.size() && ascii_iequals(value.substr(i, 5), "attr(")) { return true; }
        // `if(` and `ident(` at an identifier boundary: `notif(` and `--diff(`
        // are not it.
        const bool boundary = i == 0 || !name_char(value[i - 1]);
        if (boundary && ascii_iequals(value.substr(i, 3), "if(")) { return true; }
        if (boundary && i + 6 <= value.size() && ascii_iequals(value.substr(i, 6), "ident(")) {
            return true;
        }
        if (boundary && i + 12 <= value.size() &&
            ascii_iequals(value.substr(i, 12), "random-item(")) {
            return true;
        }
        if (boundary && i + 8 <= value.size() && ascii_iequals(value.substr(i, 8), "inherit(")) {
            return true;
        }
        // A DASHED FUNCTION - `--f(` - is a custom function call. `--diff(`
        // above is one too; what it is not is an `if()`.
        if (boundary && value.substr(i, 2) == "--") {
            std::size_t j = i + 2;
            while (j < value.size() && name_char(value[j])) { ++j; }
            if (j > i + 2 && j < value.size() && value[j] == '(') { return true; }
        }
    }
    return false;
}

std::optional<std::string> compute_registered(std::string_view text, std::string_view syntax,
                                              const length_context & ctx) {
    const std::optional<std::vector<syntax_alternative>> parsed = parse_syntax(syntax);
    // A syntax nothing here can read takes anything, as `*` does: refusing
    // every value for it would make the property's initial value the only one
    // it ever has.
    std::string matched;
    if (!parsed || parsed->empty()) {
        matched = std::string{trim(text, html_whitespace)};
    } else {
        const std::optional<std::string> found = match_syntax(text, *parsed);
        if (!found) { return std::nullopt; }
        matched = *found;
    }
    // Then computed like the type it names: a `calc()` folded, a dimension in
    // its canonical unit - `1em` under a 30px font is `30px`.
    if (may_have_math(matched)) {
        const folded_value done = fold_math(matched, ctx);
        if (!done.ok) { return std::nullopt; }
        matched = done.text;
    }
    if (const std::optional<std::string> canonical = canonical_dimension_text(matched, ctx)) {
        matched = *canonical;
    }
    return matched;
}

std::optional<std::string> substitute_var(std::string_view value, const custom_lookup & lookup,
                                          atom_table & atoms, const attribute_lookup & attributes,
                                          const condition_environment * conditions) {
    substituter engine{lookup, atoms, attributes, conditions};
    if (conditions != nullptr && conditions->property.starts_with("--")) {
        engine.resolving(atoms.intern(conditions->property));
    }
    std::string out;
    if (!engine.run(value, out, 0)) { return std::nullopt; }
    // A CYCLE IS NOT RESCUED BY A FALLBACK. CSS Variables 1 §3.1: every
    // property in a dependency cycle is invalid at computed-value time,
    // `var(--self, 3px)` included - the fallback is for a property that is
    // absent, not for one that cannot be computed (attr-argument-grammar).
    if (engine.reached_self()) { return std::nullopt; }
    if (introduced_structure(out)) { return std::nullopt; }
    return out;
}

} // namespace ctbrowser::style::css
