// The value grammar: the unit and function lists, the token scan, the
// substitution and math-function rules, `<position>`, and one typed component
// matched and serialised.
//
// One of three files carved out of a 1,155-line css/properties.cpp on
// 2026-09-08. The public surface is include/ctbrowser/style/css/properties.hpp
// and did not change; the helpers more than one of these files needs are
// declared in internal.hpp beside this, with external linkage in
// ctbrowser::style::css::detail.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

// --- the value grammar ---------------------------------------------------

namespace {

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
// the whole declaration is accepted without evaluating it: `calc/` already
// owns the evaluation and answers `unresolved` for the cases that have no
// answer until layout (`min(10px, 5%)`), so refusing here would condemn a
// declaration the cascade deliberately keeps.
constexpr std::array<std::string_view, 23> math_functions{
    "calc", "min",  "max",   "clamp", "round", "mod",       "rem",     "abs",
    "sign", "sin",  "cos",   "tan",   "asin",  "acos",      "atan",    "atan2",
    "pow",  "sqrt", "hypot", "log",   "exp",   "calc-size", "progress"};

// A value containing one of these is valid by construction: what it means is
// not known until substitution, so the declaration survives parsing with its
// tokens intact. CSS Variables 1 §3.
// A value holding one of these is valid by construction: what it means is not
// known until substitution, so the declaration survives parsing with its tokens
// intact. CSS Variables 1 §3.
//
// `attr()` IS IN THIS LIST AND NOT IN `performed_substitutions` below, and the
// split is the point: it is a substitution in the specification, so a
// declaration using one is not a syntax error and must survive - and this engine
// does not PERFORM it, so `CSS.supports` has to say no.
//
// `random-item()`, `inherit()` and `ident()` are three more of them, and naming
// them is what makes `width: random-item(auto, 1px, 2px, 3px)` and
// `left: inherit(--x)` declarations rather than lengths the grammar could not
// read. CSS Values 5 calls all of these ARBITRARY SUBSTITUTION FUNCTIONS: their
// specified value is their arguments and what those arguments mean is decided
// later. `substitution_grammar_ok` below is the part that IS decided now.
constexpr std::array<std::string_view, 6> substitution_functions{"var",         "env",     "attr",
                                                                 "random-item", "inherit", "ident"};
constexpr std::array<std::string_view, 2> performed_substitutions{"var", "env"};

// THE VALUE FUNCTIONS THIS ENGINE IMPLEMENTS, beside the math ones and the two
// substitutions. `CSS.supports` is "would this declaration be dropped", and a
// value calling a function nothing here can evaluate WOULD be - so answering
// true for `attr()`, `random-item()` or `type(*)` is a lie, and a measured one:
// five `css/css-values` files guard their assertions on `CSS.supports` and went
// from passing vacuously to running and failing when this function first
// existed and said yes to everything (2026-09-07).
//
// AN ALLOW-LIST rather than a list of what is missing, because the missing set
// is the whole of CSS Values 5 and grows every month while this one grows only
// when the engine does. It costs a false NEGATIVE - a page asking about a
// function the engine handles but this list has not caught up with - which makes
// a test skip rather than lie.
constexpr std::array<std::string_view, 27> value_functions{"rgb",
                                                           "rgba",
                                                           "hsl",
                                                           "hsla",
                                                           "hwb",
                                                           "color",
                                                           "url",
                                                           "src",
                                                           "linear-gradient",
                                                           "radial-gradient",
                                                           "conic-gradient",
                                                           "translate",
                                                           "translatex",
                                                           "translatey",
                                                           "translate3d",
                                                           "rotate",
                                                           "scale",
                                                           "scalex",
                                                           "scaley",
                                                           "skew",
                                                           "matrix",
                                                           "matrix3d",
                                                           "perspective",
                                                           "cubic-bezier",
                                                           "steps",
                                                           "counter",
                                                           "format"};

[[nodiscard]] bool is_length_unit(std::string_view unit) {
    return in_list(length_units, unit) || in_list(more_length_units, unit);
}

// A function token's name, without the `(` the tokenizer keeps on it.
[[nodiscard]] std::string_view function_name(const token_stream & ts, const css_token & t) {
    const std::string_view raw = ts.text_of(t);
    return raw.empty() ? raw : raw.substr(0, raw.size() - 1);
}

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

// AN ARBITRARY SUBSTITUTION FUNCTION HAS A GRAMMAR AT PARSE TIME even though
// what it MEANS has none until substitution, and two of them are tested here to
// the letter (CSS Values 5 §arbitrary-substitution):
//
//   ident( <declaration-value> )       one argument, and not an empty one
//   inherit( <custom-property-name> [, <declaration-value>]? )
//
// `ident()`, `ident( )`, `ident({})` and `ident(a, b)` are four assertions of
// `ident-function-parsing`; `inherit(, foo)` and `inherit(!!, foo)` are two of
// `inherit-function-parsing`. The other twenty-one assertions of those two files
// are values that must SURVIVE - `ident(rgb(1, 2, 3))` and `ident( myident)` and
// `inherit(--x,)` among them - so this is the grammar and nothing more, and in
// particular the argument is never re-serialised: the corpus asserts that
// `ident( myident)` keeps its space.
//
// It looks INSIDE other functions, because `calc(inherit(--x) + 1px)` is one of
// the values that must survive and `left: inherit(!!)` is not.
// `random-item( <declaration-value>, [ <declaration-value>? ]# )`, CSS Values 5
// §funcdef-random-item, and the ten remaining assertions of
// `css/css-values/random-item-invalid` are exactly this grammar.
//
// THREE RULES, and each one is a group of those assertions:
//
//  * The KEY is required and so is the comma after it. `random-item()`,
//    `random-item( )`, `random-item(auto)` and `random-item(, serif, sans-serif)`
//    are the four ways of getting that wrong. The ITEMS may each be empty -
//    `[ <declaration-value>? ]#` - so `random-item(auto,)` is fine.
//
//  * NO UNMATCHED BRACKET ANYWHERE INSIDE, which is what `<declaration-value>`
//    means and which a depth counter cannot answer: `random-item(auto, {serif)`
//    closes a `{` with a `)`, and counting brackets rather than MATCHING them
//    reads that as balanced. So this keeps a stack of what each opener expects.
//    EOF is not an error - CSS Syntax 3 §5.4.9 closes every open block - which is
//    why `random-item(auto, serif` is still a value.
//
//  * A `{}` BLOCK IS A WHOLE ITEM. Braces are how an item that contains a comma
//    is written, so `{Times, serif}` is one item and `{Times, serif} extra` is
//    not an item at all.
[[nodiscard]] bool random_item_arguments_ok(const token_stream & ts, std::size_t open) {
    std::vector<token_type> expect{token_type::close_paren};
    std::size_t arguments = 1; // the key, plus one per top-level comma
    std::size_t in_item = 0;   // significant tokens in the CURRENT argument
    std::size_t blocks = 0;    // ...and how many of them were `{}` blocks
    bool key_empty = true;
    bool item_mixed = false;
    for (std::size_t j = open + 1; j < ts.tokens.size(); ++j) {
        const css_token & t = ts.tokens[j];
        if (t.type == token_type::eof) { break; }
        if (t.type == token_type::whitespace) { continue; }
        const bool top = expect.size() == 1;
        if (t.type == token_type::close_paren || t.type == token_type::close_square ||
            t.type == token_type::close_curly) {
            if (t.type != expect.back()) { return false; } // an UNMATCHED bracket
            expect.pop_back();
            if (expect.empty()) { break; } // the function's own `)`
            if (expect.size() == 1 && t.type == token_type::close_curly) { ++blocks; }
            continue;
        }
        if (top && t.type == token_type::comma) {
            if (arguments == 1) { key_empty = in_item == 0; }
            item_mixed = item_mixed || (blocks != 0 && in_item != blocks);
            ++arguments;
            in_item = 0;
            blocks = 0;
            continue;
        }
        if (top && t.type == token_type::semicolon) { return false; }
        if (top && t.type == token_type::delim && ts.text_of(t) == "!") { return false; }
        if (top) { ++in_item; }
        if (t.type == token_type::function || t.type == token_type::open_paren) {
            expect.push_back(token_type::close_paren);
        } else if (t.type == token_type::open_square) {
            expect.push_back(token_type::close_square);
        } else if (t.type == token_type::open_curly) {
            expect.push_back(token_type::close_curly);
        }
    }
    if (arguments == 1) { key_empty = in_item == 0; }
    item_mixed = item_mixed || (blocks != 0 && in_item != blocks);
    return arguments >= 2 && !key_empty && !item_mixed;
}

// --- `<position>` --------------------------------------------------------
//
// CSS Values 5 §position, and the shape of it is THREE FORMS AND NOT FOUR:
//
//   <position-one>  = [ <h-side> | <v-side> | center | <length-percentage> ]
//   <position-two>  = [ <h-side> | center | <lp> ] [ <v-side> | center | <lp> ]
//                   | [ <h-side> | center ] && [ <v-side> | center ]
//   <position-four> = [ <h-side> <lp> ] && [ <v-side> <lp> ]
//
// THE THREE-VALUE FORM IS GONE. Backgrounds 3 still allows `left 4px top` for
// `background-position`, and level 5's `<position>` does not - so
// `object-position: left 4px top` is invalid where the same text is a valid
// `background-position`. Eight of `position/position-invalid.tentative`'s
// twenty-one assertions are three-value forms and turn on nothing else.
//
// `x-start`/`x-end` are horizontal and `y-start`/`y-end` vertical, which is the
// whole of what level 5 added beside removing that form.
enum class position_axis : std::uint8_t {
    none,       // not a position keyword at all
    horizontal, // left, right, x-start, x-end
    vertical,   // top, bottom, y-start, y-end
    center,     // fits either half
    offset,     // a <length-percentage>
};

[[nodiscard]] position_axis position_axis_of(const token_stream & ts, const css_token & t,
                                             std::string & serialized) {
    if (t.type == token_type::ident) {
        const std::string_view word = ts.text_of(t);
        serialized = ascii_lower_copy(word);
        if (ascii_iequals(word, "center")) { return position_axis::center; }
        if (ascii_iequals(word, "left") || ascii_iequals(word, "right") ||
            ascii_iequals(word, "x-start") || ascii_iequals(word, "x-end")) {
            return position_axis::horizontal;
        }
        if (ascii_iequals(word, "top") || ascii_iequals(word, "bottom") ||
            ascii_iequals(word, "y-start") || ascii_iequals(word, "y-end")) {
            return position_axis::vertical;
        }
        return position_axis::none;
    }
    // A <length-percentage>, serialised as everything else here is: a unitless
    // zero is a length and gains its `px`, and a unit folds to lowercase.
    if (t.type == token_type::number && t.number == 0) {
        serialized = "0px";
        return position_axis::offset;
    }
    if (t.type == token_type::percentage) {
        serialized = number_text(t.number) + "%";
        return position_axis::offset;
    }
    if (t.type == token_type::dimension && is_length_unit(ts.unit_of(t))) {
        serialized = number_text(t.number) + ascii_lower_copy(ts.unit_of(t));
        return position_axis::offset;
    }
    return position_axis::none;
}

} // namespace

namespace detail {

[[nodiscard]] bool in_list(std::span<const std::string_view> list, std::string_view name) {
    return std::any_of(list.begin(), list.end(),
                       [&](std::string_view one) { return ascii_iequals(one, name); });
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
            const std::string_view fn = function_name(ts, t);
            if (in_list(substitution_functions, fn)) { out.substituted = true; }
            if (!in_list(performed_substitutions, fn) && !in_list(math_functions, fn) &&
                !in_list(value_functions, fn)) {
                out.unknown_function = true;
            }
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
    // A BLOCK STILL OPEN AT THE END IS NOT MALFORMED. CSS Syntax 3 §5.4.9 says
    // EOF closes every open block, so `calc(1px` is `calc(1px)` and not a parse
    // error - `css/css-values/minmax-length-computed` relies on it four times
    // over with `calc(min(1em, 21px) * 2`, and refusing it deleted a declaration
    // every browser folds to 40px. A block closed too MANY times still is
    // malformed, because there is no rule that invents an opener.
    return out;
}

[[nodiscard]] bool substitution_grammar_ok(const token_stream & ts) {
    for (std::size_t i = 0; i < ts.tokens.size(); ++i) {
        if (ts.tokens[i].type != token_type::function) { continue; }
        const std::string_view fn = function_name(ts, ts.tokens[i]);
        if (ascii_iequals(fn, "random-item")) {
            if (!random_item_arguments_ok(ts, i)) { return false; }
            continue;
        }
        const bool is_ident = ascii_iequals(fn, "ident");
        if (!is_ident && !ascii_iequals(fn, "inherit")) { continue; }
        // Everything about the argument list that either grammar asks: how many
        // top-level commas there are, what the first argument's significant
        // tokens are, and whether a `{}` block sits at the top of it.
        int depth = 1;
        std::size_t commas = 0;
        std::vector<std::size_t> first;
        bool curly = false;
        for (std::size_t j = i + 1; j < ts.tokens.size() && depth > 0; ++j) {
            const css_token & t = ts.tokens[j];
            if (t.type == token_type::eof) { break; }
            if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                t.type == token_type::close_curly) {
                if (--depth == 0) { break; }
                continue;
            }
            if (t.type == token_type::whitespace) { continue; }
            if (depth == 1 && t.type == token_type::comma) {
                ++commas;
                continue;
            }
            if (depth == 1 && commas == 0) { first.push_back(j); }
            if (depth == 1 && t.type == token_type::open_curly) { curly = true; }
            if (t.type == token_type::function || t.type == token_type::open_paren ||
                t.type == token_type::open_square || t.type == token_type::open_curly) {
                ++depth;
            }
        }
        if (first.empty()) { return false; }
        if (is_ident && (commas != 0 || curly)) { return false; }
        if (!is_ident) {
            // A CUSTOM PROPERTY NAME and nothing else: `inherit(!!, foo)` names
            // no property, and `inherit(--x, foo)` has its fallback after the
            // comma rather than beside the name.
            if (commas > 1 || first.size() != 1) { return false; }
            const css_token & name = ts.tokens[first.front()];
            if (name.type != token_type::ident || !ts.text_of(name).starts_with("--")) {
                return false;
            }
        }
    }
    return true;
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
    //
    // It asks whether the SECOND token closes the first rather than counting to
    // three, because two tokens is also what an unterminated `calc(1px` has and
    // that one is a value: EOF closes it.
    if (found.significant.size() < 2) { return false; }
    if (ts.tokens[found.significant[1]].type == token_type::close_paren) { return false; }
    // The matching `)` must be the last significant token; anything after it is
    // a second value. A function left OPEN at the end of the value is closed by
    // EOF and is therefore also the whole value.
    int depth = 0;
    for (const std::size_t at : found.significant) {
        const token_type type = ts.tokens[at].type;
        if (type == token_type::function || type == token_type::open_paren) { ++depth; }
        if (type == token_type::close_paren) {
            if (--depth == 0) { return at == found.significant.back(); }
        }
    }
    return depth > 0;
}

// DOES THIS MATH FUNCTION'S ANSWER FIT THE PROPERTY? CSS Values 4 §10.2: a math
// function is valid where its RESOLVED TYPE is, so `width: calc(2 * 3)` is a
// syntax error for the same reason `width: 3` is, and `rotate: calc(1s)` for the
// same reason `rotate: 1s` is.
//
// AN UNRESOLVED ANSWER IS ACCEPTED, always. `min(10px, 5%)` and `calc(1px +
// 1cqw)` are well formed and have no answer until layout; §10.11 says their
// computed value is the function as written, and refusing them here would delete
// declarations that work today.
[[nodiscard]] bool math_type_fits(const property_syntax & p, const math_answer & answer) {
    if (answer.outcome != math_outcome::resolved) { return true; }
    const calc_result & v = answer.value;
    // A PERCENTAGE travels as a length carrying an unresolved part, so "this
    // answer is a bare percentage" is the pair below rather than a type tag.
    const bool bare_percentage = v.has_percent && v.px == 0;
    const bool length = !v.is_number && v.type == numeric_type::length;
    switch (p.kind) {
    // A `<length>` and not a `<length-percentage>`: `border-left-width:
    // calc(10%)` is invalid where `text-indent: calc(10%)` is not.
    case k::length: return length && !v.has_percent;
    case k::length_percentage: return length;
    case k::number_length: return (length && !v.has_percent) || v.is_number;
    case k::number_length_percentage: return length || v.is_number;
    case k::number:
    case k::integer: return v.is_number;
    case k::number_percentage: return v.is_number || bare_percentage;
    case k::percentage: return bare_percentage;
    case k::angle: return v.type == numeric_type::angle;
    case k::time: return v.type == numeric_type::time;
    // A `<position>` is `<length-percentage>`s and keywords, so a math function
    // in one answers with a length exactly as `length_percentage` does.
    case k::position: return length;
    case k::freeform:
    case k::keyword_only: return true;
    }
    return true;
}

// The whole value, read and written back in canonical order: the horizontal
// half, then the vertical one, with an absent half spelled `center`.
[[nodiscard]] bool match_position(const token_stream & ts, const scan & found, std::string & out) {
    boost::container::small_vector<position_axis, 4> axis;
    boost::container::small_vector<std::string, 4> text;
    for (const std::size_t i : found.significant) {
        std::string one;
        const position_axis kind = position_axis_of(ts, ts.tokens[i], one);
        if (kind == position_axis::none) { return false; }
        axis.push_back(kind);
        text.push_back(std::move(one));
    }
    const auto fits = [&](std::size_t i, position_axis want) {
        return axis[i] == want || axis[i] == position_axis::center;
    };
    if (axis.size() == 1) {
        // The missing half is `center`, and which half is missing depends on
        // what the one component was: `top` is `center top`, `10%` is
        // `10% center`.
        if (axis[0] == position_axis::vertical) {
            out = "center " + text[0];
        } else {
            out = text[0] + " center";
        }
        return true;
    }
    if (axis.size() == 2) {
        // Written in order...
        if ((fits(0, position_axis::horizontal) || axis[0] == position_axis::offset) &&
            (fits(1, position_axis::vertical) || axis[1] == position_axis::offset)) {
            out = text[0] + " " + text[1];
            return true;
        }
        // ...or the other way round, which the `&&` branch allows for KEYWORDS
        // only: `bottom right` is a position and `10px right` is not.
        if (fits(0, position_axis::vertical) && fits(1, position_axis::horizontal)) {
            out = text[1] + " " + text[0];
            return true;
        }
        return false;
    }
    if (axis.size() != 4) { return false; }
    // Four components are two `<side> <offset>` pairs, one per axis, in either
    // order. `center` takes no offset, so it cannot appear in this form at all.
    if (axis[1] != position_axis::offset || axis[3] != position_axis::offset) { return false; }
    const std::string first = text[0] + " " + text[1];
    const std::string second = text[2] + " " + text[3];
    if (axis[0] == position_axis::horizontal && axis[2] == position_axis::vertical) {
        out = first + " " + second;
        return true;
    }
    if (axis[0] == position_axis::vertical && axis[2] == position_axis::horizontal) {
        out = second + " " + first;
        return true;
    }
    return false;
}

// One typed component, matched and serialised. `false` means "not this type",
// never "malformed" - the caller decides what an unmatched value means.
[[nodiscard]] bool match_typed(const token_stream & ts, const css_token & t,
                               const property_syntax & p, std::string & out) {
    const bool takes_length = p.kind == k::length || p.kind == k::length_percentage ||
                              p.kind == k::number_length || p.kind == k::number_length_percentage;
    const bool takes_percentage = takes_percentage_of(p.kind);
    const bool takes_number = p.kind == k::number || p.kind == k::integer ||
                              p.kind == k::number_percentage || p.kind == k::number_length ||
                              p.kind == k::number_length_percentage;
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

} // namespace detail

} // namespace ctbrowser::style::css
