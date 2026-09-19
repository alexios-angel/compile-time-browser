#include "grammar/internal.hpp"

namespace ctbrowser::style::css {

using namespace grammar_detail;

namespace detail {

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
            // A DASHED FUNCTION is a custom function call, CSS Functions and
            // Mixins 1 §2: an arbitrary substitution function like var(),
            // performed by css/substitute.cpp against the sheet's @function
            // rules.
            const bool custom = fn.starts_with("--");
            if (custom || ascii_iequals_any(fn, substitution_functions)) { out.substituted = true; }
            if (!custom && !ascii_iequals_any(fn, performed_substitutions) &&
                !ascii_iequals_any(fn, math_functions) && !ascii_iequals_any(fn, value_functions)) {
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
    if (!ascii_iequals_any(property, integer_only)) { return true; }
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
                ascii_iequals_any(function_name(ts, t), math_functions)) {
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
        // `var()` and `inherit()` both open with exactly one custom property
        // name (CSS Variables 1 §3: `var( <custom-property-name> ,
        // <declaration-value>? )`) - `var()`, `var({})`, `var(, 10px)` and
        // `var(--x {--y})` are all syntax errors (var-parsing.html).
        if (!is_ident && !ascii_iequals(fn, "inherit") && !ascii_iequals(fn, "var")) { continue; }
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
            if ((commas > 1 && !ascii_iequals(fn, "var")) || first.size() != 1) { return false; }
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
    if (!ascii_iequals_any(function_name(ts, first), math_functions)) { return false; }
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
    // A NONNEGATIVE `<integer>` IS `<integer [1,inf]>`: every row in this table that
    // marks an integer nonnegative - column-count, orphans, widows, max-lines,
    // -webkit-line-clamp - is spelled `<integer [1,inf]>` by its specification,
    // and `column-count: 0` is the assertion four of those parsing files make.
    if (p.nonnegative && p.kind == k::integer && t.number == 0) { return false; }

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
                        (p.kind == k::angle && ascii_iequals_any(unit, angle_units)) ||
                        (p.kind == k::time && ascii_iequals_any(unit, time_units));
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
        if (ascii_iequals_any(name, substitution_functions)) { return std::string_view::npos - 1; }
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
                                                 bool * invalid, std::size_t begin,
                                                 std::size_t end) {
    std::string out;
    out.reserve(text.size());
    if (end > ts.tokens.size()) { end = ts.tokens.size(); }
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
    for (std::size_t i = begin; i < end; ++i) {
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
            if (math_from == 0 && ascii_iequals_any(function_name(ts, t), math_functions)) {
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

} // namespace ctbrowser::style::css
