// calc() - finding the math functions in a value's text, and folding them: the
// name scan that skips quoted runs, the function span, which properties take
// which answer, and fold_math itself.

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
    "sibling-index(", "sibling-count(", "progress(", "random(", "clamp(", "atan2(", "hypot(",
    "calc-mix(",      "round(",         "sqrt(",     "asin(",   "acos(",  "atan(",  "sign(",
    "calc(",          "min(",           "max(",      "mod(",    "rem(",   "abs(",   "pow(",
    "log(",           "exp(",           "sin(",      "cos(",    "tan("};

} // namespace

namespace detail {

[[nodiscard]] std::string_view name_at(std::string_view value, std::size_t at,
                                       std::span<const std::string_view> names) noexcept {
    if (at != 0 && is_name(value[at - 1])) { return {}; }
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

[[nodiscard]] std::vector<std::string_view> top_level_arguments(std::string_view body) {
    std::vector<std::string_view> args;
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (const std::size_t quoted = end_of_string_at(body, i); quoted != i) {
            i = quoted - 1;
            continue;
        }
        if (body[i] == '(') { ++depth; }
        if (body[i] == ')') { --depth; }
        if (depth == 0 && body[i] == ',') {
            args.push_back(body.substr(start, i - start));
            start = i + 1;
        }
    }
    args.push_back(body.substr(start));
    return args;
}

// A FUNCTION WITH NO ANSWER STILL HAS ARGUMENTS, and every one of them is a
// calculation in its own right. `min(1em, 1px)` cannot be ordered, but
// `min(10% + 30px, 5em + 5%)` is `min(10% + 30px, 5% + 5em)` - the comparison is
// undecidable and each side of it is still a sum with a canonical order.
// `minmax-length-percent-serialize` and `calc-infinity-nan-serialize-length` ask
// for exactly that, the second one through `min(NaN * 2px, NaN * 4em)`.
[[nodiscard]] std::string rewritten_arguments(
    std::string_view name, std::string_view inner,
    const std::function<std::string(std::string_view)> & one) {
    std::vector<std::string_view> arguments = top_level_arguments(inner);
    std::string out{name};
    // A clamp() WITH AN ABSENT BOUND IS THE COMPARISON THAT IS LEFT. `clamp(none,
    // 2px, 3em)` bounds nothing below and is `min(2px, 3em)`; `clamp(1em, 2px,
    // none)` is `max(1em, 2px)`; with neither bound it is its middle argument.
    // The specification has not said how a clamp() serialises
    // (w3c/csswg-drafts#13535) and `clamp-partial-serialize.tentative` is the
    // corpus's reading of it, sixteen assertions, all nested.
    if (ascii_iequals(name, "clamp(") && arguments.size() == 3) {
        const auto absent = [&](std::size_t i) {
            return ascii_iequals(trim(arguments[i], html_whitespace), "none");
        };
        const bool no_low = absent(0);
        const bool no_high = absent(2);
        if (no_low && no_high) { return one(trim(arguments[1], html_whitespace)); }
        if (no_low) {
            out = "min(";
            arguments.erase(arguments.begin());
        } else if (no_high) {
            out = "max(";
            arguments.pop_back();
        }
    }
    bool first = true;
    for (const std::string_view argument : arguments) {
        if (!first) { out += ", "; }
        first = false;
        out += one(trim(argument, html_whitespace));
    }
    out += ')';
    return out;
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
    // table because this file must not depend on it, and four names are cheaper
    // to repeat than a dependency is to add. Adding a fifth belongs in both.
    if (ascii_iequals(property, "z-index") || ascii_iequals(property, "order") ||
        ascii_iequals(property, "orphans") || ascii_iequals(property, "widows")) {
        return math_context::integer;
    }
    return math_context::any;
}

std::string non_negative(std::string_view folded) {
    const token_stream ts = tokenize(folded);
    const css_token * lone = nullptr;
    for (const css_token & t : ts.tokens) {
        if (t.type == token_type::whitespace) { continue; }
        if (t.type == token_type::eof) { break; }
        if (lone != nullptr) { return std::string{folded}; } // two values: not this file's
        lone = &t;
    }
    if (lone == nullptr || lone->number >= 0) { return std::string{folded}; }
    switch (lone->type) {
    case token_type::number: return "0";
    case token_type::percentage: return "0%";
    case token_type::dimension: return "0" + ascii_lower_copy(ts.unit_of(*lone));
    default: return std::string{folded};
    }
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

folded_value fold_math(std::string_view value, const length_context & given, math_context accepts) {
    std::string out;
    bool ok = true;
    std::size_t at = 0;
    // WHICH random() THIS IS, for its automatic key: the ordinal among the
    // value's random() functions in source order - `margin: random(..)
    // random(..)` is two, `a, random(..)` holds the first - so two elements
    // with the same declaration share by ordinal and two ordinals never share.
    // The evaluator counts the ones inside each function and reports back.
    length_context ctx = given;
    std::uint32_t ordinal = 0;
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
        ctx.random_index = ordinal;
        const function_span span = span_of(value, at, name);
        const std::string_view whole = value.substr(at, span.end - at);
        const bool is_calc = ascii_iequals(name, "calc(");
        const math_answer answer = evaluate_math(body_of(value, at, name, span), ctx);
        ordinal += answer.randoms;
        // A NUMBER WHERE THE PROPERTY WANTS A LENGTH IS A SYNTAX ERROR. This is
        // the guard that makes it safe for the evaluator to answer with numbers
        // at all: `width: calc(2 * 3)` stays invalid, as CSS says and as this
        // engine already behaved, while `opacity: calc(2 * 3)` becomes `6`.
        const bool wrong_kind = answer.outcome == math_outcome::resolved &&
                                answer.value.is_number && accepts == math_context::length;
        if (answer.outcome == math_outcome::resolved && !wrong_kind) {
            calc_result computed = answer.value;
            // AN INFINITY OR A NaN IS CLAMPED AT COMPUTED-VALUE TIME. CSS Values 4
            // §10.10: a math function's result is clamped to the property's
            // range when the computed value is made, and an infinite one lands
            // on whichever bound it overflowed. A NaN has no bound to land on and
            // computes to zero - `width: calc(NaN * 1px)` is `0px` in every
            // browser, and `calc-infinity-nan-computed` asserts it over lengths,
            // times, numbers and percentages.
            //
            // THE BOUND IS 2^25, which is Chrome's LayoutUnit maximum and,
            // just as much to the point, small enough that a used value's
            // `x * 64` is still a finite float. The specification leaves the
            // magnitude to the implementation; every engine picks one and the
            // corpus only asks that it be at least a million. The SPECIFIED value
            // keeps `calc(infinity * 1px)` - that is simplify_math's, not this.
            //
            // ponytail: one bound for every family and every property; the
            // per-property range (opacity's [0, 1], a non-negative width) is
            // the cascade's to apply after this.
            // AN ANGLE HAS NO BOUND TO LAND ON: `rotate(calc(infinity * 1deg))`
            // is `rotate(0deg)` in Chrome, and calc-infinity-nan-computed
            // compares the two matrices fifteen times over. A 2^25 degree
            // rotation is 272 degrees, which is no more the spec's answer and
            // agrees with nobody.
            constexpr double bound = 33554432.0;
            const bool angle = computed.type == numeric_type::angle;
            const auto clamped = [angle](double v) {
                if (std::isnan(v) || (angle && std::isinf(v))) { return 0.0; }
                return std::isinf(v) ? (v > 0 ? bound : -bound) : v;
            };
            // AN INFINITE LENGTH AGAINST AN OPPOSITE INFINITE PERCENTAGE is a
            // NaN however the percentage resolves - `calc(infinity * 1px -
            // infinity * 1%)` is `infinity - infinity` for any basis - and a
            // NaN is censored to zero (CSS Values 4 §10.9). Clamping the two
            // halves separately would hand layout two bounds that do not cancel.
            if (computed.has_percent && std::isinf(computed.px) && std::isinf(computed.percent) &&
                std::signbit(computed.px) != std::signbit(computed.percent)) {
                computed.px = 0.0;
                computed.percent = 0.0;
                computed.has_percent = false;
            }
            computed.px = clamped(computed.px);
            computed.percent = clamped(computed.percent);
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
        if (!is_calc && answer.outcome == math_outcome::unresolved) {
            // ...BUT ITS ARGUMENTS ARE STILL COMPUTED. `min(1em, 10%)` cannot be
            // ordered without a containing block, and its computed value is
            // `min(16px, 10%)` all the same: each argument resolves against
            // the bases the element has (minmax-length-percent-serialize). One
            // that cannot is folded like any other value, which reaches the
            // functions inside it.
            const std::size_t from = at + name.size();
            const std::string_view inner =
                value.substr(from, span.end - from - (span.closed ? 1 : 0));
            // A `random()` THAT WAITS FOR A BASIS KEEPS ITS BASE. `random(10%,
            // 100%)` in `translate` has no answer until layout, and CSS Values
            // 5 §random says its computed value is `random(fixed 0.5, 10%,
            // 100%)`: the base is decided now and written into the value, so
            // the used value is the same one every time it is asked.
            if (ascii_iequals(name, "random(")) {
                const std::vector<std::string_view> arguments = top_level_arguments(inner);
                std::string_view options;
                std::size_t first = 0;
                // The options are the first argument when it is one: a name,
                // a UA ident, a scope keyword, `auto` or `fixed` - any
                // identifier that is not one of the numeric constants.
                if (!arguments.empty()) {
                    const std::string_view head = trim(arguments.front(), html_whitespace);
                    const token_stream head_tokens = tokenize(head);
                    const bool ident = !head_tokens.tokens.empty() &&
                                       head_tokens.tokens.front().type == token_type::ident;
                    const std::string_view word =
                        ident ? head_tokens.text_of(head_tokens.tokens.front()) : head;
                    if (ident && !ascii_iequals(word, "nan") && !ascii_iequals(word, "infinity") &&
                        !ascii_iequals(word, "-infinity") && !ascii_iequals(word, "pi") &&
                        !ascii_iequals(word, "e")) {
                        options = head;
                        first = 1;
                    }
                }
                // A `fixed` base is written out clamped to [0, 1) too, so
                // `fixed random(-2, -1)` reads back as `fixed 0`.
                std::optional<double> fixed_base;
                bool already_plain = false;
                if (ascii_istarts_with(options, "fixed")) {
                    const std::string_view rest = trim(options.substr(5), html_whitespace);
                    const token_stream given_tokens = tokenize(rest);
                    already_plain = given_tokens.tokens.size() == 2 &&
                                    given_tokens.tokens.front().type == token_type::number;
                    const math_answer given = evaluate_math(rest, ctx);
                    if (given.outcome == math_outcome::resolved && given.value.is_number) {
                        fixed_base = std::min(std::max(given.value.px, 0.0), 1.0 - 1e-9);
                    }
                }
                if (!ascii_istarts_with(options, "fixed") || (fixed_base && !already_plain)) {
                    std::string rewritten = "random(fixed ";
                    calc_result base;
                    base.px = fixed_base ? *fixed_base : random_base(options, ctx);
                    base.is_number = true;
                    base.type = numeric_type::number;
                    rewritten += serialize_calc(base);
                    for (std::size_t i = first; i < arguments.size(); ++i) {
                        rewritten += ", ";
                        rewritten += trim(arguments[i], html_whitespace);
                    }
                    rewritten += ')';
                    const folded_value again = fold_math(rewritten, ctx, accepts);
                    out.append(again.text);
                    ok = ok && again.ok;
                    at = span.end;
                    continue;
                }
            }
            out.append(rewritten_arguments(name, inner, [&](std::string_view one) {
                const math_answer arg = evaluate_math(one, ctx);
                if (arg.outcome == math_outcome::resolved) {
                    // Inside an argument list the sum is bare: `10% + 30px`,
                    // not `calc(10% + 30px)`.
                    std::string text = serialize_calc(arg.value);
                    if (text.starts_with("calc(") && text.ends_with(')')) {
                        text = text.substr(5, text.size() - 6);
                    }
                    return text;
                }
                // A sum around a comparison with no answer is simplified over
                // its tree against the bases (tree.cpp): `5em + 5%` is `5% +
                // 80px` and `(min(10%, 30px) + 10px) * 2 + 10px` is `10px + (2
                // * (10px + min(10%, 30px)))` (minmax-length-percent-serialize).
                if (arg.outcome == math_outcome::unresolved &&
                    math_name_at(trim(one, html_whitespace), 0).empty()) {
                    if (const std::optional<std::string> tree = simplify_sum_text(one, &ctx)) {
                        return *tree;
                    }
                }
                return fold_math(one, ctx).text;
            }));
            at = span.end;
            continue;
        }
        if (is_calc && answer.outcome == math_outcome::unresolved) {
            // The same tree for a calc() the bases cannot finish:
            // `calc(min(1%, 2%) + max(3%, 4%) + 10%)` computes to `calc(10% +
            // min(1%, 2%) + max(3%, 4%))` (minmax-percentage-serialize).
            if (const std::optional<std::string> tree =
                    simplify_sum_text(body_of(value, at, name, span), &ctx)) {
                out.append("calc(").append(*tree).append(")");
                at = span.end;
                continue;
            }
        }
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
