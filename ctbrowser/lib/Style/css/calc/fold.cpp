// calc() - finding the math functions in a value's text, and folding them: the
// name scan that skips quoted runs, the function span, which properties take
// which answer, and fold_math itself.
//
// One of five files carved out of a 1,810-line css/calc.cpp on 2026-09-08. The
// public surface is include/ctbrowser/style/css/calc.hpp and did not change;
// the helpers more than one of these files needs are declared in internal.hpp
// beside this, with external linkage in ctbrowser::style::css::detail.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// EVERY MATH FUNCTION THIS FILE EVALUATES, longest first so that a scan which
// stops at the first match cannot mistake `atan(` for the start of `atan2(`, or
// `min(` for the start of `minmax(` - which it cannot anyway, because `minmax(`
// fails the identifier-boundary test below, but the ordering costs nothing and
// states the intent.
//
// `calc-size()` is deliberately ABSENT although CSS Values 5 lists it as a math
// function: this file cannot evaluate it, and a name here is a promise to try.
constexpr std::string_view math_names[] = {
    "progress(", "clamp(", "atan2(", "hypot(", "round(", "sqrt(", "asin(", "acos(",
    "atan(",     "sign(",  "calc(",  "min(",   "max(",   "mod(",  "rem(",  "abs(",
    "pow(",      "log(",   "exp(",   "sin(",   "cos(",   "tan("};

} // namespace

namespace detail {

[[nodiscard]] std::string_view name_at(std::string_view value, std::size_t at,
                                       std::span<const std::string_view> names) noexcept {
    if (at != 0 && is_name_char(value[at - 1])) { return {}; }
    for (const std::string_view name : names) {
        if (ascii_iequals(value.substr(at, name.size()), name)) { return name; }
    }
    return {};
}

[[nodiscard]] std::string_view math_name_at(std::string_view value, std::size_t at) noexcept {
    return name_at(value, at, math_names);
}

// ONE PAST THE END OF THE STRING THAT STARTS AT `at`, or `at` itself when no
// string does. A QUOTED RUN IS NOT CODE: `content: "calc(1px + 1px)"` and
// `font-family: "round()"` are strings whose bytes happen to spell a function,
// and reading one as arithmetic either rewrites what the page says or - for
// `round()` - deletes the declaration for a syntax error inside a literal.
// `span_of` already skips quotes while it matches parentheses; every scan that
// looks for a NAME has to as well, and all three of them below do.
//
// An unterminated string runs to the end of the value, CSS Syntax 3 §4.3.5.
[[nodiscard]] std::size_t end_of_string_at(std::string_view value, std::size_t at) noexcept {
    const char quote = value[at];
    if (quote != '"' && quote != '\'') { return at; }
    std::size_t scan = at + 1;
    while (scan < value.size()) {
        if (value[scan] == '\\' && scan + 1 < value.size()) {
            ++scan;
        } else if (value[scan] == quote) {
            return scan + 1;
        }
        ++scan;
    }
    return value.size();
}

[[nodiscard]] function_span span_of(std::string_view value, std::size_t at,
                                    std::string_view name) noexcept {
    std::size_t scan = at + name.size();
    int depth = 1;
    char quote = 0;
    while (scan < value.size() && depth > 0) {
        const char c = value[scan];
        if (quote != 0) {
            if (c == '\\' && scan + 1 < value.size()) {
                ++scan;
            } else if (c == quote) {
                quote = 0;
            }
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
        }
        ++scan;
    }
    return function_span{scan, depth == 0};
}

// What the evaluator should be handed for the function at [at, span.end): the
// WHOLE function, name included, for everything but `calc(`. The evaluator reads
// `min(...)` as a term, and handing it only the argument list would turn
// `min(1px, 2px)` into the comma-separated nonsense `1px, 2px`.
[[nodiscard]] std::string_view body_of(std::string_view value, std::size_t at,
                                       std::string_view name, const function_span & span) {
    if (!ascii_iequals(name, "calc(")) { return value.substr(at, span.end - at); }
    const std::size_t from = at + name.size();
    return value.substr(from, span.end - from - (span.closed ? 1 : 0));
}

[[nodiscard]] bool has_percentage(std::string_view text) {
    const token_stream ts = tokenize(text);
    for (const css_token & t : ts.tokens) {
        if (t.type == token_type::percentage) { return true; }
    }
    return false;
}

} // namespace detail

bool may_have_math(std::string_view value) noexcept {
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (!math_name_at(value, i).empty()) { return true; }
    }
    return false;
}

// THE PROPERTIES WHOSE WHOLE VALUE IS LENGTHS. Sorted and searched linearly,
// because the list is short and this runs once per math function rather than
// once per declaration.
//
// The rule for being ON it is narrow on purpose: every component of the value
// must be a <length>, a <percentage> or a keyword, so that a <number> answer
// from a math function can only ever be a syntax error. Anything compound -
// `box-shadow`, `background-position`, `transform`, `border` - is deliberately
// absent, because math_context applies to EVERY math function in the value and
// one of them may legitimately be a number (`transform: scale(calc(1 / 2))`).
//
// `line-height` is the one that looks like it belongs here and does not: a
// unitless `line-height: 1.5` is a number and is the commonest spelling of it.
math_context math_context_of(std::string_view property) noexcept {
    static constexpr std::string_view lengths[] = {
        "block-size",
        "border-bottom-left-radius",
        "border-bottom-right-radius",
        "border-bottom-width",
        "border-end-end-radius",
        "border-end-start-radius",
        "border-left-width",
        "border-radius",
        "border-right-width",
        "border-spacing",
        "border-start-end-radius",
        "border-start-start-radius",
        "border-top-left-radius",
        "border-top-right-radius",
        "border-top-width",
        "border-width",
        "bottom",
        "column-gap",
        "column-rule-width",
        "column-width",
        "flex-basis",
        "font-size",
        "gap",
        "height",
        "inline-size",
        "inset",
        "inset-block",
        "inset-block-end",
        "inset-block-start",
        "inset-inline",
        "inset-inline-end",
        "inset-inline-start",
        "left",
        "letter-spacing",
        "margin",
        "margin-block",
        "margin-block-end",
        "margin-block-start",
        "margin-bottom",
        "margin-inline",
        "margin-inline-end",
        "margin-inline-start",
        "margin-left",
        "margin-right",
        "margin-top",
        "max-block-size",
        "max-height",
        "max-inline-size",
        "max-width",
        "min-block-size",
        "min-height",
        "min-inline-size",
        "min-width",
        "outline-offset",
        "outline-width",
        "padding",
        "padding-block",
        "padding-block-end",
        "padding-block-start",
        "padding-bottom",
        "padding-inline",
        "padding-inline-end",
        "padding-inline-start",
        "padding-left",
        "padding-right",
        "padding-top",
        "right",
        "row-gap",
        "text-indent",
        "top",
        "width",
        "word-spacing",
    };
    for (const std::string_view one : lengths) {
        if (ascii_iequals(one, property)) { return math_context::length; }
    }
    // THE PROPERTIES WHOSE WHOLE VALUE IS AN `<integer>`, which is the same list
    // `properties/table.cpp` marks `k::integer` - kept here rather than asked of that
    // table because this file must not depend on it, and two names are cheaper to
    // repeat than a dependency is to add. Adding a third belongs in both.
    if (ascii_iequals(property, "z-index") || ascii_iequals(property, "order")) {
        return math_context::integer;
    }
    return math_context::any;
}

bool math_uses_percentage(std::string_view value) {
    std::size_t at = 0;
    while (at < value.size()) {
        if (const std::size_t quoted = end_of_string_at(value, at); quoted != at) {
            at = quoted;
            continue;
        }
        const std::string_view name = math_name_at(value, at);
        if (name.empty()) {
            ++at;
            continue;
        }
        const function_span span = span_of(value, at, name);
        if (has_percentage(value.substr(at, span.end - at))) { return true; }
        at = span.end;
    }
    return false;
}

folded_value fold_math(std::string_view value, const length_context & ctx, math_context accepts) {
    std::string out;
    bool ok = true;
    std::size_t at = 0;
    while (at < value.size()) {
        if (const std::size_t quoted = end_of_string_at(value, at); quoted != at) {
            out.append(value.substr(at, quoted - at));
            at = quoted;
            continue;
        }
        const std::string_view name = math_name_at(value, at);
        if (name.empty()) {
            out.push_back(value[at]);
            ++at;
            continue;
        }
        const function_span span = span_of(value, at, name);
        const std::string_view whole = value.substr(at, span.end - at);
        const bool is_calc = ascii_iequals(name, "calc(");
        const math_answer answer = evaluate_math(body_of(value, at, name, span), ctx);
        // A NUMBER WHERE THE PROPERTY WANTS A LENGTH IS A SYNTAX ERROR. This is
        // the guard that makes it safe for the evaluator to answer with numbers
        // at all: `width: calc(2 * 3)` stays invalid, as CSS says and as this
        // engine already behaved, while `opacity: calc(2 * 3)` becomes `6`.
        const bool wrong_kind = answer.outcome == math_outcome::resolved &&
                                answer.value.is_number && accepts == math_context::length;
        if (answer.outcome == math_outcome::resolved && !wrong_kind) {
            calc_result computed = answer.value;
            // AN `<integer>` PROPERTY ROUNDS ITS ANSWER, and CSS Values 4 §10.10
            // says which way: to the nearest integer, with a value exactly halfway
            // going toward POSITIVE INFINITY. That is `floor(x + 0.5)` and not
            // `std::round`, which rounds a half away from zero - the two disagree
            // on every negative half, so `z-index: calc(-3 / 2)` is -1 and not -2.
            //
            // Rounding HERE and not in the evaluator is what makes
            // `calc(calc(1 / 3) * 3)` come out as 1: only the finished conversion
            // rounds, never an intermediate.
            if (accepts == math_context::integer && computed.is_number &&
                std::isfinite(computed.px)) {
                computed.px = std::floor(computed.px + 0.5);
            }
            out.append(serialize_calc(computed));
            at = span.end;
            continue;
        }
        // AN INVALID CALC KEEPS ITS TEXT AND SAYS SO. Keeping the text is what
        // lets a caller with no better answer carry on; saying so is what lets the
        // cascade do the right thing instead, which is to treat the declaration as
        // invalid. Layout reading the text was the wrong outcome: parse_length
        // cannot read `calc(-1 * 0)` and answers `auto`, where CSS says the
        // property takes its initial value.
        //
        // NO OTHER MATH FUNCTION EVER SAYS SO. `min()`, `max()` and `clamp()`
        // were kept verbatim by every version of this file before they could be
        // parsed, so "keep the text, do not condemn the declaration" is the one
        // answer that cannot regress a page - and for `min(10px, 5%)` it is also
        // the answer CSS Values 4 §10.11 gives. The same reasoning covers the
        // fourteen functions added beside them: a `round()` this file cannot fold
        // is refused where the corpus looks for it, in `check_declaration`, and
        // not by quietly deleting a stylesheet's declaration.
        if (!is_calc || answer.outcome == math_outcome::unresolved) {
            out.append(whole);
            at = span.end;
            continue;
        }
        ok = false;
        out.append(whole);
        at = span.end;
    }
    return folded_value{std::move(out), ok};
}

} // namespace ctbrowser::style::css
