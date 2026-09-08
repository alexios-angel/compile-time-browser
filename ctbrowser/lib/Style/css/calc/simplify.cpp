// calc() - the SPECIFIED value: simplify_math, CSS Values 4 §10.12, and the
// syntax check every declaration goes through before the property's own
// grammar.
//
// One of five files carved out of a 1,810-line css/calc.cpp on 2026-09-08. The
// public surface is include/ctbrowser/style/css/calc.hpp and did not change;
// the helpers more than one of these files needs are declared in internal.hpp
// beside this, with external linkage in ctbrowser::style::css::detail.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// Can this text be simplified WHERE IT STANDS - before a font size, a viewport
// or a containing block exists?
//
// CSS Values 4 §10.11 is exact about what may NOT be simplified: `calc(10px +
// 1em)` keeps both terms because the em has no length yet. This file's evaluator
// has one mode, which resolves an `em` against whatever context it is handed, so
// the test is deliberately narrow - every dimension in the text converts by a
// constant. Anything else is reported as the author wrote it, which is where it
// was before and cannot be a regression.
//
// A PERCENTAGE IS NOT AN OBSTACLE, and excluding it was over-caution: the value
// model carries a percentage term BESIDE the pixels rather than resolving it, so
// `calc(50px + calc(40%))` simplifies to `calc(40% + 50px)` with nothing
// guessed. What genuinely cannot be decided here is COMPARING two of them - the
// basis may be negative, so `min(1%, 2%)` has no answer - and that is the
// comparison functions' own business, not this one's.
[[nodiscard]] bool context_free(std::string_view text) {
    const token_stream ts = tokenize(text);
    for (const css_token & t : ts.tokens) {
        if (t.type == token_type::dimension && !context_free_unit(ts.unit_of(t))) { return false; }
    }
    return true;
}

// A simplified math function as a SPECIFIED value: `calc()` around the answer,
// always. `serialize_calc` writes a COMPUTED value, where a bare `96px` is the
// whole of it; a specified one keeps the function, which is how a page can tell
// `width: calc(96px)` from `width: 96px` after the fact - and what every
// `test_specified_serialization` in the corpus compares against.
[[nodiscard]] std::string specified_math(const calc_result & value) {
    const std::string text = serialize_calc(value);
    // An infinity or a NaN already carries its own calc(), because there is no
    // way to write one without a function around it.
    if (text.starts_with("calc(")) { return text; }
    return "calc(" + text + ")";
}

// Is `body` exactly ONE `calc()` and nothing else? That is the test for
// §10.12's redundant-calc rule below.
//
// ONE calc() INSIDE ANOTHER, and not one math function inside another, which is
// where this was and was too wide. `clamp-length-serialize` asks for
// `calc(calc(0px + clamp(1px, 1em, 1vh)))` back as `calc(0px + clamp(1px, 1em,
// 1vh))` - a calc in a calc - four times over, and generalising that to every
// function read the same rule out of `calc(pow(2, sign(1em - 18px)))`, which
// `calc-complex-unresolved-serialize` wants back WITH its outer calc() on all
// six of its values. A math function that is not a calc() is a term like any
// other and the calc() around it is the author's, not this file's to remove.
[[nodiscard]] bool is_lone_calc(std::string_view body) {
    constexpr std::string_view calc = "calc(";
    if (!ascii_iequals(body.substr(0, calc.size()), calc)) { return false; }
    return span_of(body, 0, calc).end == body.size();
}

// Is `text` exactly one math function and nothing else, and which one?
[[nodiscard]] std::string_view lone_math_function(std::string_view text) {
    const std::string_view name = math_name_at(text, 0);
    if (name.empty() || span_of(text, 0, name).end != text.size()) { return {}; }
    return name;
}

// THE FUNCTIONS WHOSE EVERY ARGUMENT IS AN <angle>, and the one place this file
// can type a math function by WHERE IT SITS rather than by what is in it.
//
// A math function's type has to fit its surroundings and nothing here models the
// grammar of `transform` or `filter`, so all three of `rotate(min(0px))` -
// turning by a length - `rotate(tan(45deg))` - turning by a number - and
// `rotate(atan2(90px, 100%))` - turning by a ratio whose percentage has nothing
// to resolve against - were stored as written. They are sixteen assertions and,
// between them, the LAST failure in each of `minmax-angle-invalid`,
// `sin-cos-tan-invalid` and `acos-asin-atan-atan2-invalid`.
//
// <zero> IS WHY ONLY A MATH FUNCTION IS JUDGED. CSS Transforms 1 spells it
// `rotate( [ <angle> | <zero> ] )`, so `rotate(0)` is a rotation by nothing and
// `rotate(min(0))` is a syntax error, and the difference is exactly whether a
// function was written.
//
// `rotate3d()` is deliberately absent: three of its four arguments are numbers
// and only the last is an angle, so it is a different rule and not this one.
constexpr std::string_view angle_functions[] = {"rotate(", "rotatex(", "rotatey(", "rotatez(",
                                                "skew(",   "skewx(",   "skewy(",   "hue-rotate("};

// Every comma-separated argument of `body`, at bracket depth zero and with
// quoted runs skipped.
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

[[nodiscard]] bool angle_arguments_ok(std::string_view value) {
    const length_context ctx;
    std::size_t at = 0;
    while (at < value.size()) {
        if (const std::size_t quoted = end_of_string_at(value, at); quoted != at) {
            at = quoted;
            continue;
        }
        const std::string_view name = name_at(value, at, angle_functions);
        if (name.empty()) {
            ++at;
            continue;
        }
        const function_span span = span_of(value, at, name);
        const std::size_t from = at + name.size();
        const std::string_view body = value.substr(from, span.end - from - (span.closed ? 1 : 0));
        for (const std::string_view arg : top_level_arguments(body)) {
            const std::string_view one = trim(arg, html_whitespace);
            if (lone_math_function(one).empty()) { continue; }
            // A percentage would have to be a percentage of an angle, and there
            // is no such thing. This is the same rule the evaluator applies to a
            // percentage in an expression that ANSWERS with an angle; here the
            // expression need not have an answer at all - `atan2(90px, 100%)`
            // has none - and the position alone settles it.
            if (has_percentage(one)) { return false; }
            const math_answer answer = evaluate_math(one, ctx);
            if (answer.outcome == math_outcome::resolved &&
                answer.value.type != numeric_type::angle) {
                return false;
            }
        }
        at = span.end;
    }
    return true;
}

// A FUNCTION WITH NO ANSWER STILL HAS ARGUMENTS, and every one of them is a
// calculation in its own right. `min(1em, 1px)` cannot be ordered, but
// `min(10% + 30px, 5em + 5%)` is `min(10% + 30px, 5% + 5em)` - the comparison is
// undecidable and each side of it is still a sum with a canonical order.
// `minmax-length-percent-serialize` and `calc-infinity-nan-serialize-length` ask
// for exactly that, the second one through `min(NaN * 2px, NaN * 4em)`.
//
// An argument with no simplified form of its own - `none`, `nearest`,
// `sibling-index()` - is handed back to `simplify_math`, which copies what it
// cannot answer for and closes what EOF closed. That recursion terminates
// because `inner` is always shorter than the function it came out of.
[[nodiscard]] std::string simplified_arguments(std::string_view name, std::string_view inner) {
    std::string out{name};
    bool first = true;
    for (const std::string_view argument : top_level_arguments(inner)) {
        if (!first) { out += ", "; }
        first = false;
        const std::string_view one = trim(argument, html_whitespace);
        std::string text;
        if (const auto [outcome, sum] = evaluate_symbolic(one); outcome == math_outcome::resolved) {
            text = serialize_symbolic(sum);
        }
        out += text.empty() ? simplify_math(one) : text;
    }
    out += ')';
    return out;
}

} // namespace

std::string simplify_math(std::string_view value) {
    // The bases do not exist yet - that is what makes this the SPECIFIED value -
    // so `context_free` above is what decides whether a function may be touched
    // at all, and this context is only ever handed expressions that need none.
    const length_context ctx;
    std::string out;
    std::size_t at = 0;
    while (at < value.size()) {
        // A MATH FUNCTION WITH ITS OWN VOCABULARY IS COPIED OVER WHOLE, for the
        // same reason `math_syntax_ok` steps over it: `calc-size(10px, sign(size)
        // * size)` redefines `size` inside itself, so the `sign()` in there is
        // not the ordinary one and simplifying it would be answering a question
        // this file was not asked.
        if (ascii_iequals(value.substr(at, 10), "calc-size(") &&
            (at == 0 || !is_name_char(value[at - 1]))) {
            const std::size_t end = span_of(value, at, "calc-size(").end;
            out.append(value.substr(at, end - at));
            at = end;
            continue;
        }
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
        const std::string_view body = body_of(value, at, name, span);
        // The argument list alone, which is `body` for a `calc()` and `body`
        // with the name and the closing paren taken off for everything else.
        const std::size_t from = at + name.size();
        const std::string_view inner = value.substr(from, span.end - from - (span.closed ? 1 : 0));
        at = span.end;
        // A calc() AROUND ONE OTHER calc() IS REDUNDANT. §10.12's simplification
        // returns a lone child rather than wrapping it, so
        // `calc(calc(0px + clamp(...)))` loses exactly one layer.
        // `clamp-length-serialize` asserts that four times.
        //
        // ...AND SO IS A calc() AROUND A LONE COMPARISON, for a sharper reason.
        // `min()`, `max()` and `clamp()` are NODES OF THE CALCULATION TREE (CSS
        // Values 4 §10.9), not opaque leaves: simplifying `calc(clamp(1px, 1em,
        // 1vh))` leaves a tree whose root IS the Clamp node, and §10.13
        // serialises a Clamp root as `clamp(...)` with no calc() anywhere.
        //
        // That is exactly why the rule stops at the three of them. `pow()` is
        // not a node type - it is a leaf this file could not evaluate - and a
        // leaf inside a calc() keeps its calc(), which is what
        // `calc-complex-unresolved-serialize` asks for on all six of its values.
        // The two files disagree only if the distinction is "any math function".
        if (ascii_iequals(name, "calc(")) {
            const std::string_view trimmed = trim(body, html_whitespace);
            const std::string_view lone = lone_math_function(trimmed);
            if (is_lone_calc(trimmed) || ascii_iequals(lone, "min(") ||
                ascii_iequals(lone, "max(") || ascii_iequals(lone, "clamp(")) {
                out.append(simplify_math(trimmed));
                continue;
            }
        }
        const math_answer answer = evaluate_math(body, ctx);
        if (answer.outcome == math_outcome::resolved && context_free(whole)) {
            out.append(specified_math(answer.value));
            continue;
        }
        // A SUM THAT COULD NOT BE FOLDED IS STILL SIMPLIFIED. `calc(10px + 1vmin
        // + 10%)` has no single magnitude before there is a viewport and a
        // containing block, and §10.12's simplified form is not the author's
        // bytes but the three terms in §10.13's canonical order -
        // `calc(10% + 10px + 1vmin)`. There is nothing to guess: the terms are
        // added per unit and printed, and a comparison whose arguments cannot be
        // ORDERED still says so and falls through below.
        if (const auto [outcome, sum] = evaluate_symbolic(body);
            outcome == math_outcome::resolved) {
            if (const std::string text = serialize_symbolic(sum); !text.empty()) {
                out.append("calc(").append(text).append(")");
                continue;
            }
        }
        // ...AND A FUNCTION WITH NO ANSWER AT ALL STILL HAS ARGUMENTS. §10.11
        // says a comparison that cannot be ORDERED is its own computed value -
        // `min(10px, 5%)`, `min(1em, 1px)` - and says nothing about the two
        // sides of it, which are calculations like any other and simplify like
        // any other. What genuinely has no simplified form here comes back
        // through this same function and keeps the author's bytes, which is
        // where `sibling-index()` and `nearest` land.
        out.append(simplified_arguments(name, inner));
    }
    return out;
}

bool math_syntax_ok(std::string_view value) {
    // ...AND OF THE RIGHT KIND FOR WHERE IT SITS, which for the angle-only
    // functions is a question about position rather than about contents.
    if (!angle_arguments_ok(value)) { return false; }
    // The bases do not matter to a syntax question - `1em` is well formed at any
    // font size - so this asks with the defaults rather than making every caller
    // invent a context it has no use for.
    const length_context ctx;
    std::size_t at = 0;
    while (at < value.size()) {
        // A MATH FUNCTION WITH ITS OWN VOCABULARY IS STEPPED OVER WHOLE.
        // `calc-size(10px, sign(size) * size)` is a valid declaration and its
        // `size` keyword is meaningless anywhere else, so reading the `sign()`
        // inside it as ordinary arithmetic reported the whole value invalid -
        // measured against `css/css-values/calc-size/interpolate-size-parsing`.
        // Descending into a function this file does not own is right for
        // `transform: rotate(calc(...))` and wrong here, and the only thing that
        // tells the two apart is knowing which functions redefine their contents.
        if (ascii_iequals(value.substr(at, 10), "calc-size(") &&
            (at == 0 || !is_name_char(value[at - 1]))) {
            at = span_of(value, at, "calc-size(").end;
            continue;
        }
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
        if (evaluate_math(body_of(value, at, name, span), ctx).outcome == math_outcome::invalid) {
            return false;
        }
        // Past the whole function, so a nested one is not checked twice - the
        // outer evaluation already read it.
        at = span.end;
    }
    return true;
}

} // namespace ctbrowser::style::css
