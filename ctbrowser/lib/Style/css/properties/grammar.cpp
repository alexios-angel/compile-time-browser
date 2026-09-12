// The value grammar: the unit and function lists, the token scan, the
// substitution and math-function rules, `<position>`, and one typed component
// matched and serialised.

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
constexpr std::array<std::string_view, 27> math_functions{
    "calc",      "min",      "max",           "clamp",         "round",  "mod",     "rem",
    "abs",       "sign",     "sin",           "cos",           "tan",    "asin",    "acos",
    "atan",      "atan2",    "pow",           "sqrt",          "hypot",  "log",     "exp",
    "calc-size", "progress", "sibling-index", "sibling-count", "random", "calc-mix"};

// A value containing one of these is valid by construction: what it means is
// not known until substitution, so the declaration survives parsing with its
// tokens intact. CSS Variables 1 §3.
// A value holding one of these is valid by construction: what it means is not
// known until substitution, so the declaration survives parsing with its tokens
// intact. CSS Variables 1 §3.
//
// `random-item()`, `inherit()` and `ident()` are three more of them, and naming
// them is what makes `width: random-item(auto, 1px, 2px, 3px)` and
// `left: inherit(--x)` declarations rather than lengths the grammar could not
// read. CSS Values 5 calls all of these ARBITRARY SUBSTITUTION FUNCTIONS: their
// specified value is their arguments and what those arguments mean is decided
// later. `substitution_grammar_ok` below is the part that IS decided now.
//
// EVERY ONE OF THEM IS PERFORMED - css/substitute.cpp runs all six - so
// `CSS.supports` says yes to all six. `attr()` and `random-item()` were kept
// off the performed list from before the engine could substitute them, and
// random-item-computed guards twenty-three assertions on the answer.
constexpr std::array<std::string_view, 6> substitution_functions{"var",         "env",     "attr",
                                                                 "random-item", "inherit", "ident"};
constexpr std::array<std::string_view, 6> performed_substitutions{
    "var", "env", "attr", "random-item", "inherit", "ident"};

// THE VALUE FUNCTIONS THIS ENGINE IMPLEMENTS, beside the math ones and the
// substitutions. `CSS.supports` is "would this declaration be dropped", and a
// value calling a function nothing here can evaluate WOULD be - so answering
// true for `type(*)` or `image-set()` is a lie: five `css/css-values` files
// guard their assertions on `CSS.supports`.
//
// AN ALLOW-LIST rather than a list of what is missing, because the missing set
// is the whole of CSS Values 5 and grows every month while this one grows only
// when the engine does. It costs a false NEGATIVE - a page asking about a
// function the engine handles but this list has not caught up with - which makes
// a test skip rather than lie.
constexpr std::array<std::string_view, 30> value_functions{"cross-origin",
                                                           "integrity",
                                                           "referrer-policy",
                                                           "rgb",
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
//
// `std::to_chars` in fixed format IS that definition: the shortest decimal that
// reads back as the same double, with no exponent. `std::to_string` was six
// fixed decimals, which printed `0.1234567` as `0.123457` - a value the author
// wrote, altered on the way to `el.style` - and could not say `1e-7` at all.
[[nodiscard]] std::string number_text(double value) {
    if (!std::isfinite(value)) { return value > 0 ? "infinity" : "-infinity"; }
    if (value == 0) { return "0"; } // catches -0, which serialises as 0
    std::array<char, 400> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                       value, std::chars_format::fixed);
    if (written.ec != std::errc{}) { return "0"; }
    return std::string{buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())};
}

// A CSS string as CSSOM §2.1 "serialize a string" writes it: double-quoted,
// with `"` and `\` escaped and a control character as a hex escape.
[[nodiscard]] std::string string_text(std::string_view body) {
    std::string out{"\""};
    for (const char c : body) {
        const auto code = static_cast<unsigned char>(c);
        if (code == 0) {
            out += "\xEF\xBF\xBD";
        } else if (code <= 0x1F || code == 0x7F) {
            static constexpr char digits[] = "0123456789abcdef";
            out += '\\';
            if (code >= 16) { out += digits[code >> 4]; }
            out += digits[code & 0xF];
            out += ' ';
        } else {
            if (c == '"' || c == '\\') { out += '\\'; }
            out += c;
        }
    }
    out += '"';
    return out;
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
        // A `!` AT THE TOP LEVEL ONLY. Inside a block it is a delim like any
        // other - `if(style(--x!): a; else: b)` is a value whose condition is
        // false, not a declaration with a priority (CSS Syntax 3 §5.4.6).
        if (depth == 0 && t.type == token_type::delim && ts.text_of(t) == "!") {
            out.important = true;
        }
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
    out.unclosed = depth > 0 ? depth : 0;
    return out;
}

// <integer>-ONLY SLOTS IN GRAMMARS THIS TABLE DOES NOT MODEL. CSS Values 4
// §5.2: `1e1` and `10.1` are <number-token>s, and a slot that takes an
// <integer> refuses them where it accepts `calc(1e1)`, which rounds
// (§10.10). The counter properties, the grid lines, `repeat()`'s count,
// `font-feature-settings`' value and `text-combine-upright: digits N` are all
// such slots and all in properties kept freeform - so the rule is asked of the
// bare numbers in the value rather than of a grammar: outside a math function
// every one must be an integer literal. `initial-letter`'s FIRST value is a
// <number> and only its second an <integer> (calc-rounds-to-integer).
[[nodiscard]] bool integer_slots_ok(std::string_view property, const token_stream & ts) {
    constexpr std::array<std::string_view, 17> integer_only{"counter-increment",
                                                            "counter-reset",
                                                            "counter-set",
                                                            "font-feature-settings",
                                                            "grid-row",
                                                            "grid-column",
                                                            "grid-area",
                                                            "grid-row-start",
                                                            "grid-row-end",
                                                            "grid-column-start",
                                                            "grid-column-end",
                                                            "grid-template-rows",
                                                            "grid-template-columns",
                                                            "grid-template",
                                                            "grid",
                                                            "text-combine-upright",
                                                            "initial-letter"};
    if (!in_list(integer_only, property)) { return true; }
    bool number_first = ascii_iequals(property, "initial-letter");
    int depth = 0;
    int math_until = -1; // the depth a math function opened at, or -1 outside one
    for (const css_token & t : ts.tokens) {
        if (t.type == token_type::eof) { break; }
        const bool opens = t.type == token_type::function || t.type == token_type::open_paren ||
                           t.type == token_type::open_square || t.type == token_type::open_curly;
        const bool closes = t.type == token_type::close_paren ||
                            t.type == token_type::close_square || t.type == token_type::close_curly;
        if (opens) {
            if (math_until < 0 && t.type == token_type::function &&
                in_list(math_functions, function_name(ts, t))) {
                math_until = depth;
            }
            ++depth;
        } else if (closes) {
            --depth;
            if (math_until >= 0 && depth <= math_until) { math_until = -1; }
        } else if (t.type == token_type::number && math_until < 0) {
            if (number_first) {
                number_first = false;
            } else if ((t.flags & flag_integer) == 0) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool substitution_grammar_ok(const token_stream & ts) {
    for (std::size_t i = 0; i < ts.tokens.size(); ++i) {
        if (ts.tokens[i].type != token_type::function) { continue; }
        const std::string_view fn = function_name(ts, ts.tokens[i]);
        // `steps( <integer>, <step-position>? )`, CSS Easing 1 §3.2: a
        // <number-token> that is not an integer - `1e1`, `10.1` - is a
        // syntax error where a math function still rounds
        // (calc-rounds-to-integer).
        if (ascii_iequals(fn, "steps")) {
            std::size_t j = i + 1;
            while (j < ts.tokens.size() && ts.tokens[j].type == token_type::whitespace) { ++j; }
            if (j < ts.tokens.size() && ts.tokens[j].type == token_type::number &&
                (ts.tokens[j].flags & flag_integer) == 0) {
                return false;
            }
            continue;
        }
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
    //
    // ...EXCEPT FOR THE TREE-COUNTING FUNCTIONS, whose argument list is empty by
    // definition: `z-index: sibling-index()` is the whole of CSS Values 5
    // §tree-counting's example.
    const std::string_view fn = function_name(ts, first);
    const bool takes_nothing =
        ascii_iequals(fn, "sibling-index") || ascii_iequals(fn, "sibling-count");
    if (found.significant.size() < 2) { return false; }
    if (!takes_nothing && ts.tokens[found.significant[1]].type == token_type::close_paren) {
        return false;
    }
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
// AN UNRESOLVED ANSWER IS ACCEPTED unless its TYPE is already known and wrong.
// `min(10px, 5%)` and `calc(1px + 1cqw)` are well formed and have no answer
// until layout; §10.11 says their computed value is the function as written, and
// refusing them here would delete declarations that work today. But `rotate:
// calc(1px * sibling-index())` has no answer AND is a length, and §10.2 types it
// before anything is measured - `math_type_of` is that reading.
[[nodiscard]] bool math_type_fits(const property_syntax & p, const math_answer & answer,
                                  std::string_view text) {
    if (answer.outcome == math_outcome::invalid) { return true; }
    std::optional<calc_result> typed;
    if (answer.outcome == math_outcome::unresolved) {
        typed = math_type_of(text);
        if (!typed) { return true; }
    }
    const calc_result & v = typed ? *typed : answer.value;
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
    case k::color:
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

// THE AUTHOR'S TOKENS, WITH THE NUMBERS, STRINGS AND URLS WRITTEN CANONICALLY.
//
// A value whose grammar this table does not model is stored as written - that
// is deliberate, see check_declaration - but CSSOM §6.7.2 serialises a number,
// a string and a URL the same way in every value, and `css/cssom/serialize-values`
// asks for `0.5%` where `.5%` was written, `0px` for `-0px`, `"x"` for `'x'` and
// `url("x")` for `url(x)`, through `background-position`, `content` and every
// other shorthand this file leaves freeform. So the token stream is rebuilt
// with only those three token kinds respelled; whitespace, idents, commas and
// functions keep their bytes, which is what keeps `ident( myident)` its space
// and `random-item(auto ,serif)` its odd comma.
//
// An ident or function name with an ESCAPE in it was decoded by the tokenizer
// and has no source span left to copy - it would need serialize-an-identifier
// to write back - so a value holding one is returned as written.
//
// ...AND `counter()`/`counters()` LOSE A `decimal` STYLE, which is the one
// function argument CSSOM canonicalises: `decimal` is the default and
// `counter(par-num, decimal)` reads back as `counter(par-num)` in every engine
// (serialize-values asks for it through `content`). Token-level: the trailing
// `, decimal` before the function's own `)` is simply not written out.
[[nodiscard]] std::pair<std::size_t, std::size_t> default_counter_style_in(const token_stream & ts,
                                                                           std::size_t open) {
    const std::string_view fn = function_name(ts, ts.tokens[open]);
    if (!ascii_iequals(fn, "counter") && !ascii_iequals(fn, "counters")) { return {0, 0}; }
    int depth = 1;
    std::size_t close = open + 1;
    for (; close < ts.tokens.size() && depth > 0; ++close) {
        const token_type type = ts.tokens[close].type;
        if (type == token_type::eof) { return {0, 0}; }
        if (type == token_type::function || type == token_type::open_paren) { ++depth; }
        if (type == token_type::close_paren && --depth == 0) { break; }
    }
    if (depth != 0) { return {0, 0}; }
    const auto back = [&ts](std::size_t at) {
        while (at > 0 && ts.tokens[at - 1].type == token_type::whitespace) { --at; }
        return at;
    };
    const std::size_t style = back(close);
    if (style == 0 || ts.tokens[style - 1].type != token_type::ident ||
        !ascii_iequals(ts.text_of(ts.tokens[style - 1]), "decimal")) {
        return {0, 0};
    }
    const std::size_t comma = back(style - 1);
    if (comma == 0 || ts.tokens[comma - 1].type != token_type::comma) { return {0, 0}; }
    return {comma - 1, close};
}

// url( <string> <url-modifier>* ), CSS Values 4 §4.5.1 and §4.5.4: the
// modifiers this engine knows written in one order, an unknown one dropped, a
// duplicate of either kind a syntax error, and anything that is not an ident
// or a function one too (urls/url-request-modifiers-*). Answers the index of
// the token after the closing paren, or `npos` when the url is invalid; a
// `url()` whose first argument is not a string is copied through as it came,
// because `url(var(--x))` is decided elsewhere.
[[nodiscard]] std::size_t canonical_url(const token_stream & ts, std::size_t open,
                                        std::string & out) {
    std::size_t i = open + 1;
    const auto skip_ws = [&] {
        while (ts.tokens[i].type == token_type::whitespace) { ++i; }
    };
    skip_ws();
    if (ts.tokens[i].type != token_type::string) { return std::string_view::npos - 1; }
    const std::string_view quoted = ts.text_of(ts.tokens[i]);
    if (quoted.size() < 2) { return std::string_view::npos; }
    std::string url = "url(" + string_text(quoted.substr(1, quoted.size() - 2));
    ++i;
    std::string cross, integrity, referrer;
    std::vector<std::string> seen; // lowercased name, `(` appended for the function form
    for (;;) {
        skip_ws();
        const css_token & t = ts.tokens[i];
        if (t.type == token_type::close_paren || t.type == token_type::eof) { break; }
        if (t.type == token_type::ident) {
            const std::string key = ascii_lower_copy(ts.text_of(t));
            if (std::ranges::find(seen, key) != seen.end()) { return std::string_view::npos; }
            seen.push_back(key);
            ++i;
            continue;
        }
        if (t.type != token_type::function) { return std::string_view::npos; }
        const std::string name = ascii_lower_copy(function_name(ts, t));
        // A substitution among the modifiers is read once it has been made.
        if (in_list(substitution_functions, name)) { return std::string_view::npos - 1; }
        const std::string key = name + '(';
        if (std::ranges::find(seen, key) != seen.end()) { return std::string_view::npos; }
        seen.push_back(key);
        // The argument list, to its matching paren.
        std::vector<std::size_t> args;
        int depth = 1;
        for (++i; depth > 0; ++i) {
            const css_token & a = ts.tokens[i];
            if (a.type == token_type::eof) { break; }
            if (a.type == token_type::function || a.type == token_type::open_paren ||
                a.type == token_type::open_square || a.type == token_type::open_curly) {
                ++depth;
            } else if (a.type == token_type::close_paren || a.type == token_type::close_square ||
                       a.type == token_type::close_curly) {
                if (--depth == 0) { break; }
            }
            if (a.type != token_type::whitespace) { args.push_back(i); }
        }
        if (ts.tokens[i].type == token_type::close_paren) { ++i; }
        const bool known =
            name == "cross-origin" || name == "integrity" || name == "referrer-policy";
        if (!known) { continue; }
        if (args.size() != 1) { return std::string_view::npos; }
        const css_token & arg = ts.tokens[args.front()];
        if (name == "integrity") {
            if (arg.type != token_type::string) { return std::string_view::npos; }
            const std::string_view body = ts.text_of(arg);
            integrity = string_text(body.substr(1, body.size() - 2));
            continue;
        }
        if (arg.type != token_type::ident) { return std::string_view::npos; }
        const std::string word = ascii_lower_copy(ts.text_of(arg));
        if (name == "cross-origin") {
            if (word != "anonymous" && word != "use-credentials") { return std::string_view::npos; }
            cross = word;
        } else {
            if (!has_keyword("no-referrer no-referrer-when-downgrade same-origin origin "
                             "strict-origin origin-when-cross-origin "
                             "strict-origin-when-cross-origin unsafe-url",
                             word)) {
                return std::string_view::npos;
            }
            referrer = word;
        }
    }
    if (ts.tokens[i].type == token_type::close_paren) { ++i; }
    if (!cross.empty()) { url += " cross-origin(" + cross + ")"; }
    if (!integrity.empty()) { url += " integrity(" + integrity + ")"; }
    if (!referrer.empty()) { url += " referrer-policy(" + referrer + ")"; }
    out += url + ')';
    return i;
}

[[nodiscard]] std::string normalize_value_tokens(const token_stream & ts, std::string_view text,
                                                 bool * invalid) {
    std::string out;
    out.reserve(text.size());
    std::pair<std::size_t, std::size_t> skip{0, 0};
    // A NEGATIVE ZERO INSIDE A MATH FUNCTION KEEPS ITS SIGN. A lone `-0` is
    // `0` (serialize-values), but `sign(calc(-0))` is -0 and `1 / sign(...)`
    // tells the two apart - `signed-zero` reads it back through `scale` - and
    // the arithmetic has not happened yet when this runs. The math function's
    // own serialisation prints what it computed; this only has to not destroy
    // the input. A PERCENTAGE IS EXCLUDED: nothing reads the sign of `-0%`
    // back, and `min(-0%, 0%)` serialises as `min(0%, 0%)` in every engine
    // (minmax-percentage-serialize).
    int depth = 0;
    int math_from = 0; // the depth at which the outermost math function opened
    const auto number = [&](double value) {
        return math_from != 0 && value == 0 && std::signbit(value) ? std::string{"-0"}
                                                                   : number_text(value);
    };
    for (std::size_t i = 0; i < ts.tokens.size(); ++i) {
        const css_token & t = ts.tokens[i];
        if (t.type == token_type::eof) { break; }
        if (i >= skip.first && i < skip.second) { continue; }
        if (t.type == token_type::function && ascii_iequals(function_name(ts, t), "url")) {
            const std::size_t after = canonical_url(ts, i, out);
            if (after == std::string_view::npos) {
                if (invalid != nullptr) { *invalid = true; }
                return std::string{text};
            }
            if (after != std::string_view::npos - 1) {
                skip = {i, after};
                continue;
            }
        }
        if (t.type == token_type::function) {
            skip = default_counter_style_in(ts, i);
            ++depth;
            if (math_from == 0 && in_list(math_functions, function_name(ts, t))) {
                math_from = depth;
            }
        } else if (t.type == token_type::open_paren) {
            ++depth;
        } else if (t.type == token_type::close_paren) {
            if (depth == math_from) { math_from = 0; }
            --depth;
        }
        const std::string_view body = ts.text_of(t);
        switch (t.type) {
        case token_type::number: out += number(t.number); break;
        case token_type::percentage: out += number_text(t.number) + "%"; break;
        case token_type::dimension:
            out += number(t.number) + ascii_lower_copy(ts.unit_of(t));
            break;
        case token_type::string:
            if (body.size() < 2) { return std::string{text}; }
            out += string_text(body.substr(1, body.size() - 2));
            break;
        case token_type::url: out += "url(" + string_text(body) + ")"; break;
        default:
            if (t.text >= ts.source_length) { return std::string{text}; }
            out += body;
            break;
        }
    }
    return out;
}

} // namespace detail

// --- the computed `<position>` -----------------------------------------------

namespace {

// One component of a position as the computed value reads it: a keyword, or an
// offset with its text - a percentage keeping its number so `right 30%` can fold
// to `70%` without re-parsing the string.
struct position_component {
    position_axis kind = position_axis::none;
    std::string text;
    bool is_percent = false;
    double number = 0;
};

// The components, functions kept whole. `position_axis_of` reads one token;
// a `calc()` is several, and its text is the slice of the source from the
// function token to its matching close paren.
[[nodiscard]] bool position_components(const token_stream & ts,
                                       std::vector<position_component> & out) {
    for (std::size_t i = 0; i < ts.tokens.size(); ++i) {
        const css_token & t = ts.tokens[i];
        if (t.type == token_type::eof) { break; }
        if (t.type == token_type::whitespace) { continue; }
        position_component one;
        if (t.type == token_type::function) {
            int depth = 1;
            std::size_t j = i + 1;
            for (; j < ts.tokens.size() && depth > 0; ++j) {
                const token_type type = ts.tokens[j].type;
                if (type == token_type::eof) { return false; }
                if (type == token_type::function || type == token_type::open_paren) { ++depth; }
                if (type == token_type::close_paren) { --depth; }
            }
            const css_token & last = ts.tokens[j - 1];
            // A rebuilt (escaped) token is not a slice of the source, and a
            // position with an escape in a calc() is not worth a second path.
            if (t.text >= ts.source_length || last.text >= ts.source_length) { return false; }
            one.kind = position_axis::offset;
            one.text = std::string_view{ts.pool}.substr(t.text, last.text + last.length - t.text);
            out.push_back(std::move(one));
            i = j - 1;
            continue;
        }
        one.kind = position_axis_of(ts, t, one.text);
        if (one.kind == position_axis::none) { return false; }
        one.is_percent = t.type == token_type::percentage;
        one.number = t.number;
        out.push_back(std::move(one));
    }
    return !out.empty();
}

enum class box_side : std::uint8_t {
    start,
    center,
    end
};

// The physical side a keyword names, the flow-relative ones folded through
// `flipped` - which is what the writing mode and direction reduce to for one
// axis.
[[nodiscard]] box_side physical_side(std::string_view word, bool flipped) {
    if (word == "center") { return box_side::center; }
    const bool is_end = word == "right" || word == "bottom" || word == "x-end" || word == "y-end";
    const bool flow = word.starts_with("x-") || word.starts_with("y-");
    return (is_end != (flow && flipped)) ? box_side::end : box_side::start;
}

// `<side> <offset>?` as a percentage or a length from the START edge. A
// percentage offset from the end folds; a length from the end is a calc(),
// which is how every browser writes `right 20px`.
[[nodiscard]] std::string computed_half(box_side side, const position_component * offset) {
    if (offset == nullptr) {
        return side == box_side::start ? "0%" : (side == box_side::center ? "50%" : "100%");
    }
    if (side == box_side::start) { return offset->text; }
    if (offset->is_percent) { return number_text(100.0 - offset->number) + "%"; }
    return "calc(100% - " + offset->text + ")";
}

} // namespace

std::string computed_position(std::string_view specified, std::string_view writing_mode,
                              std::string_view direction) {
    const token_stream ts = tokenize(specified);
    std::vector<position_component> parts;
    if (!position_components(ts, parts)) { return {}; }

    const std::string mode = ascii_lower_copy(trim(writing_mode, html_whitespace));
    const bool rtl = ascii_iequals(trim(direction, html_whitespace), "rtl");
    const bool vertical_rl = mode == "vertical-rl" || mode == "sideways-rl";
    const bool vertical_lr = mode == "vertical-lr";
    const bool sideways_lr = mode == "sideways-lr";
    const bool horizontal = !vertical_rl && !vertical_lr && !sideways_lr;
    // Where `x-start` and `y-start` fall. In a horizontal box the inline axis
    // follows `direction`; in a vertical one the block axis runs right-to-left
    // for `vertical-rl`, and the inline axis follows `direction` except in
    // `sideways-lr`, whose lines run bottom-to-top.
    const bool flip_x = horizontal ? rtl : vertical_rl;
    const bool flip_y = horizontal ? false : (sideways_lr ? !rtl : rtl);

    const auto fits = [&](const position_component & c, position_axis want) {
        return c.kind == want || c.kind == position_axis::center;
    };
    // One `<side> <offset>?` pair, resolved along one axis.
    const auto half = [&](const position_component & keyword, const position_component * offset,
                          bool flipped) -> std::string {
        if (keyword.kind == position_axis::offset) { return keyword.text; }
        return computed_half(physical_side(keyword.text, flipped), offset);
    };
    const position_component center{position_axis::center, "center", false, 0};

    if (parts.size() == 1) {
        if (parts[0].kind == position_axis::vertical) {
            return half(center, nullptr, false) + " " + half(parts[0], nullptr, flip_y);
        }
        return half(parts[0], nullptr, flip_x) + " " + half(center, nullptr, false);
    }
    if (parts.size() == 2) {
        if ((fits(parts[0], position_axis::horizontal) || parts[0].kind == position_axis::offset) &&
            (fits(parts[1], position_axis::vertical) || parts[1].kind == position_axis::offset)) {
            return half(parts[0], nullptr, flip_x) + " " + half(parts[1], nullptr, flip_y);
        }
        if (fits(parts[0], position_axis::vertical) && fits(parts[1], position_axis::horizontal)) {
            return half(parts[1], nullptr, flip_x) + " " + half(parts[0], nullptr, flip_y);
        }
        return {};
    }
    if (parts.size() != 4 || parts[1].kind != position_axis::offset ||
        parts[3].kind != position_axis::offset) {
        return {};
    }
    if (parts[0].kind == position_axis::horizontal && parts[2].kind == position_axis::vertical) {
        return half(parts[0], &parts[1], flip_x) + " " + half(parts[2], &parts[3], flip_y);
    }
    if (parts[0].kind == position_axis::vertical && parts[2].kind == position_axis::horizontal) {
        return half(parts[2], &parts[3], flip_x) + " " + half(parts[0], &parts[1], flip_y);
    }
    return {};
}

} // namespace ctbrowser::style::css
