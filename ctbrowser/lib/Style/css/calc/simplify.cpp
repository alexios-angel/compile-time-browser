//===- simplify.cpp - Simplifying math functions -------------- -*- C++ -*-===//
//
// The specified value of math functions: simplify_math (CSS Values 4 §10.12),
// canonical_random, calc_size_text, and math_syntax_ok, the syntax check every
// declaration goes through before the property's own grammar.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

/// True when every dimension in `text` converts by a constant, so the value can
/// be simplified before a font size, viewport or containing block exists (CSS
/// Values 4 §10.11: `calc(10px + 1em)` keeps both terms because the em has no
/// length yet). Anything else is kept as written. Percentages are allowed: the
/// value model keeps a percentage term beside the pixels, so
/// `calc(50px + calc(40%))` becomes `calc(40% + 50px)` without resolving it.
/// Comparing two percentages (`min(1%, 2%)`) is undecidable, but that is the
/// comparison functions' concern, not this check's.
[[nodiscard]] bool context_free(std::string_view text) {
    const token_stream ts = tokenize(text);
    for (const css_token & t : ts.tokens) {
        if (t.type == token_type::dimension && !context_free_unit(ts.unit_of(t))) { return false; }
        // A random() is drawn at computed-value time, so a specified value
        // keeps it however plain its bounds are.
        if (t.type == token_type::function && ascii_iequals(ts.text_of(t), "random(")) {
            return false;
        }
    }
    return true;
}

/// A simplified math function as a specified value: always `calc()` around the
/// answer. `serialize_calc` writes a computed value, where a bare `96px` is the
/// whole of it; a specified one keeps the function, so a page can tell
/// `width: calc(96px)` from `width: 96px`, as every WPT
/// `test_specified_serialization` expects.
[[nodiscard]] std::string specified_math(const calc_result & value) {
    const std::string text = serialize_calc(value);
    // An infinity or a NaN already carries its own calc(), because there is no
    // way to write one without a function around it.
    if (text.starts_with("calc(")) { return text; }
    return "calc(" + text + ")";
}

/// Whether `body` is exactly one `calc()` and nothing else, the test for
/// §10.12's redundant-calc rule in simplify_math.
///
/// Only a calc() directly inside calc() is redundant. Any other math function
/// is a term, and the calc() around it is kept (WPT
/// calc-complex-unresolved-serialize expects `calc(pow(...))` back unchanged).
[[nodiscard]] bool is_lone_calc(std::string_view body) {
    constexpr std::string_view calc = "calc(";
    if (!ascii_iequals(body.substr(0, calc.size()), calc)) { return false; }
    return span_of(body, 0, calc).end == body.size();
}

/// The name of the math function `text` consists of exactly, or empty when it
/// is not exactly one math function.
[[nodiscard]] std::string_view lone_math_function(std::string_view text) {
    const std::string_view name = math_name_at(text, 0);
    if (name.empty() || span_of(text, 0, name).end != text.size()) { return {}; }
    return name;
}

/// The functions whose every argument is an <angle>.
///
/// Nothing here models the grammar of `transform` or `filter`, so these
/// functions are checked by position: a math function written as an argument
/// of one of them must be an angle. `rotate(min(0px))`, `rotate(tan(45deg))`
/// and `rotate(atan2(90px, 100%))` are invalid (WPT minmax-angle-invalid,
/// sin-cos-tan-invalid, acos-asin-atan-atan2-invalid).
///
/// Only a math function is judged because of <zero>: CSS Transforms 1 spells
/// it `rotate( [ <angle> | <zero> ] )`, so `rotate(0)` is a rotation by nothing
/// while `rotate(min(0))` is a syntax error.
///
/// `rotate3d()` is deliberately absent: three of its four arguments are
/// numbers and only the last is an angle, so a different rule applies.
constexpr std::string_view angle_functions[] = {"rotate(", "rotatex(", "rotatey(", "rotatez(",
                                                "skew(",   "skewx(",   "skewy(",   "hue-rotate("};

/// Whether position `at` is inside a colour function whose first argument is
/// `from`: CSS Color 5's relative colour syntax, where `r`, `g`, `b`, `alpha`
/// and the rest are channel values a calc() may use.
[[nodiscard]] bool inside_relative_color(std::string_view value, std::size_t at) {
    constexpr std::string_view colours[] = {"rgb(", "rgba(",  "hsl(",   "hsla(",  "hwb(",  "lab(",
                                            "lch(", "oklab(", "oklch(", "color(", "alpha("};
    std::vector<bool> relative; // One entry per open bracket: is it a relative colour?
    std::size_t i = 0;
    while (i < at) {
        if (const std::size_t quoted = end_of_string_at(value, i); quoted != i) {
            i = quoted;
            continue;
        }
        if (value[i] == ')') {
            if (!relative.empty()) { relative.pop_back(); }
            ++i;
            continue;
        }
        if (value[i] == '(') {
            relative.push_back(false);
            ++i;
            continue;
        }
        const std::string_view name = name_at(value, i, colours);
        if (name.empty()) {
            ++i;
            continue;
        }
        std::size_t after = i + name.size();
        while (after < value.size() &&
               html_whitespace.find(value[after]) != std::string_view::npos) {
            ++after;
        }
        relative.push_back(ascii_iequals(value.substr(after, 4), "from") &&
                           (after + 4 >= value.size() || !is_name(value[after + 4])));
        i += name.size();
    }
    return std::ranges::any_of(relative, [](bool r) { return r; });
}

/// Whether every math function written as an argument of an angle-only
/// function (angle_functions) in `value` is an angle.
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
            // is no such thing. The evaluator applies the same rule to a
            // percentage in an expression that answers with an angle; here the
            // expression need not have an answer at all (`atan2(90px, 100%)`
            // has none), and the position alone settles it.
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

/// Simplifies each argument of a function that has no answer, since every
/// argument is a calculation in its own right. `min(1em, 1px)` cannot be
/// ordered, but `min(10% + 30px, 5em + 5%)` becomes `min(10% + 30px, 5% + 5em)`:
/// the comparison is undecidable, and each side is still a sum with a
/// canonical order (WPT minmax-length-percent-serialize and
/// calc-infinity-nan-serialize-length, the latter via
/// `min(NaN * 2px, NaN * 4em)`).
///
/// An argument with no simplified form of its own (`none`, `nearest`,
/// `sibling-index()`) is handed back to `simplify_math`, which copies what it
/// cannot answer for and closes what end of input closed. The recursion
/// terminates because `inner` is always shorter than the function it came from.
[[nodiscard]] std::string simplified_arguments(std::string_view name, std::string_view inner) {
    return rewritten_arguments(name, inner, [](std::string_view one) {
        if (const auto [outcome, sum] = evaluate_symbolic(one); outcome == math_outcome::resolved) {
            if (std::string text = serialize_symbolic(sum); !text.empty()) { return text; }
        }
        // A sum around a comparison layout has to decide is simplified over
        // its tree (tree.cpp): `(min(10%, 30px) + 10px) * 2 + 10px` is
        // `10px + (2 * (10px + min(10%, 30px)))` (WPT minmax-length-percent-serialize).
        if (lone_math_function(trim(one, html_whitespace)).empty()) {
            if (const std::optional<std::string> tree = simplify_sum_text(one)) { return *tree; }
        }
        return simplify_math(one);
    });
}

/// Normalizes a calc-mix() that could not fold (CSS Values 5 §calc-mix): each
/// weight clamped to [0%, 100%], the omitted ones sharing what is left below
/// 100% equally, a total over 100% scaled down to it, and an item weighing
/// nothing dropped. So `calc-mix(10px 75%, 3em 75%)` becomes
/// `calc-mix(10px 50%, 3em 50%)` before there is a font size. A weight written
/// as a function has no number yet, and then nothing is normalized: the items
/// are written as they came, values simplified (WPT calc-mix-serialize).
[[nodiscard]] std::string simplified_calc_mix(std::string_view inner) {
    struct item {
        std::string_view value;
        std::optional<double> weight;
        std::string_view weight_text; // As written, for when nothing is normalized.
    };
    std::vector<item> items;
    bool literal = false;
    for (std::string_view arg : top_level_arguments(inner)) {
        arg = trim(arg, html_whitespace);
        item one{arg, std::nullopt, {}};
        const token_stream ts = tokenize(arg);
        std::size_t last = ts.tokens.size();
        for (std::size_t i = ts.tokens.size(); i-- > 0;) {
            const token_type type = ts.tokens[i].type;
            if (type == token_type::eof || type == token_type::whitespace) { continue; }
            last = i;
            break;
        }
        if (last < ts.tokens.size()) {
            const css_token & t = ts.tokens[last];
            if (t.type == token_type::percentage && last > 0) {
                one.weight = std::min(std::max(t.number, 0.0), 100.0);
                one.weight_text = arg.substr(t.text);
                one.value = trim(arg.substr(0, t.text), html_whitespace);
            } else if (t.type == token_type::close_paren) {
                // The function this `)` closes, and whether it is a weight.
                int depth = 0;
                for (std::size_t i = last + 1; i-- > 0;) {
                    const token_type type = ts.tokens[i].type;
                    if (type == token_type::close_paren) { ++depth; }
                    if (type == token_type::open_paren) { --depth; }
                    if (type == token_type::function && --depth == 0) {
                        if (i > 0 && !math_name_at(arg, ts.tokens[i].text).empty()) {
                            literal = true;
                            one.weight_text = arg.substr(ts.tokens[i].text);
                            one.value = trim(arg.substr(0, ts.tokens[i].text), html_whitespace);
                        }
                        break;
                    }
                }
            }
        }
        items.push_back(one);
    }
    std::string out{"calc-mix("};
    bool first = true;
    const auto emit = [&](std::string_view value, std::string_view weight) {
        if (!first) { out += ", "; }
        first = false;
        out += simplify_math(value);
        if (!weight.empty()) { out.append(" ").append(weight); }
    };
    if (literal) {
        for (const item & one : items) { emit(one.value, one.weight_text); }
        return out + ')';
    }
    double given = 0.0;
    double missing = 0.0;
    for (const item & one : items) {
        if (one.weight) {
            given += *one.weight;
        } else {
            missing += 1.0;
        }
    }
    const double share = missing == 0.0 ? 0.0 : std::max(0.0, 100.0 - given) / missing;
    const double scale = std::max(100.0, given) / 100.0;
    for (const item & one : items) {
        const double weight = one.weight.value_or(share) / scale;
        if (weight == 0.0) { continue; }
        emit(one.value, serialize_calc(calc_result{0.0, weight, true}));
    }
    // Every weight zero: the answer is zero, of the first value's type. For a
    // value with no symbolic form, that is a percentage when one is written in it.
    if (first && !items.empty()) {
        if (const auto [outcome, sum] = evaluate_symbolic(items.front().value);
            outcome == math_outcome::resolved && sum.symbols.empty()) {
            calc_result zero;
            zero.type = sum.type();
            zero.is_number = sum.is_number();
            zero.has_percent = sum.has_percent && sum.value == 0.0;
            return specified_math(zero);
        }
        return has_percentage(items.front().value) ? "calc(0%)" : "calc(0px)";
    }
    return out + ')';
}

} // namespace

std::string simplify_math(std::string_view value) {
    // The recursion budget of the text-level passes (math_step in internal.hpp):
    // a pathologically nested value is returned unsimplified rather than taking
    // billions of calls. The outermost call checks it again on the way out.
    const math_step step;
    if (step.exhausted()) { return std::string{value}; }
    // The bases do not exist yet (that is what makes this the specified value),
    // so `context_free` decides whether a function may be touched at all, and
    // this context is only ever handed expressions that need none.
    const length_context ctx;
    std::string out;
    std::size_t at = 0;
    while (at < value.size()) {
        // A math function with its own vocabulary is copied over whole, for the
        // same reason `math_syntax_ok` steps over it:
        // `calc-size(10px, sign(size) * size)` redefines `size` inside itself,
        // so the `sign()` in there cannot be simplified as ordinary arithmetic.
        if (ascii_iequals(value.substr(at, 10), "calc-size(") &&
            (at == 0 || !is_name(value[at - 1]))) {
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
        // A calc() around one other calc() is redundant. §10.12's
        // simplification returns a lone child rather than wrapping it, so
        // `calc(calc(0px + clamp(...)))` loses exactly one layer (WPT
        // clamp-length-serialize).
        //
        // So is a calc() around a lone comparison. `min()`, `max()` and
        // `clamp()` are nodes of the calculation tree (CSS Values 4 §10.9), not
        // opaque leaves: simplifying `calc(clamp(1px, 1em, 1vh))` leaves a tree
        // whose root is the Clamp node, and §10.13 serializes a Clamp root as
        // `clamp(...)` with no calc() around it.
        //
        // The rule stops at those three. `pow()` is not a node type but a leaf
        // this file could not evaluate, and a leaf inside a calc() keeps its
        // calc(). Treating every math function this way would break WPT
        // calc-complex-unresolved-serialize, which clamp-length-serialize does
        // not require.
        if (ascii_iequals(name, "calc(")) {
            const std::string_view trimmed = trim(body, html_whitespace);
            const std::string_view lone = lone_math_function(trimmed);
            if (is_lone_calc(trimmed) || ascii_iequals(lone, "min(") ||
                ascii_iequals(lone, "max(") || ascii_iequals(lone, "clamp(")) {
                out.append(simplify_math(trimmed));
                continue;
            }
        }
        // A calc-mix() folds only symbolically (the evaluator says when a
        // specified one may), and is normalized otherwise.
        if (ascii_iequals(name, "calc-mix(")) {
            const auto [outcome, sum] = evaluate_symbolic(body);
            if (outcome == math_outcome::resolved && sum.symbols.empty()) {
                calc_result value;
                value.type = sum.type();
                value.is_number = sum.is_number();
                value.px = sum.value;
                value.percent = sum.percent;
                value.has_percent = sum.has_percent;
                out.append(specified_math(value));
            } else {
                out.append(simplified_calc_mix(inner));
            }
            continue;
        }
        const math_answer answer = evaluate_math(body, ctx);
        if (answer.outcome == math_outcome::resolved && context_free(whole)) {
            // A percentage beside a length keeps the length's term even at
            // zero: `calc(10% + calc-mix(1px 0%, 3% 0%))` is `calc(10% + 0px)`
            // (§10.12 adds the terms of one unit and drops nothing). The
            // magnitude-and-percentage answer cannot express that and the
            // symbolic sum can (WPT calc-mix-serialize).
            if (answer.value.has_percent && answer.value.px == 0.0 &&
                !ascii_iequals(name, "calc-mix(")) {
                if (const auto [outcome, sum] = evaluate_symbolic(body);
                    outcome == math_outcome::resolved && !sum.symbols.empty()) {
                    if (const std::string text = serialize_symbolic(sum); !text.empty()) {
                        out.append("calc(").append(text).append(")");
                        continue;
                    }
                }
            }
            out.append(specified_math(answer.value));
            continue;
        }
        // A sum that could not be folded is still simplified.
        // `calc(10px + 1vmin + 10%)` has no single magnitude before there is a
        // viewport and a containing block, and §10.12's simplified form is the
        // three terms in §10.13's canonical order: `calc(10% + 10px + 1vmin)`.
        // The terms are added per unit and printed; a comparison whose
        // arguments cannot be ordered falls through below.
        if (const auto [outcome, sum] = evaluate_symbolic(body);
            outcome == math_outcome::resolved) {
            if (const std::string text = serialize_symbolic(sum); !text.empty()) {
                // A lone function is the whole value: `calc(sibling-index())`
                // is `sibling-index()`.
                const bool lone_function = text.ends_with("()") &&
                                           text.find(' ') == std::string::npos &&
                                           sum.symbols.size() == 1 && !sum.has_percent;
                if (lone_function) {
                    out.append(text);
                } else {
                    out.append("calc(").append(text).append(")");
                }
                continue;
            }
        }
        // A calc() around such a comparison is still a tree, simplified by
        // §10.12 and written in §10.13's order:
        // `calc((min(10px, 20%) + max(1rem, 2%)) * 2)` is
        // `calc(2 * (min(10px, 20%) + max(1rem, 2%)))` (WPT
        // calc-serialization-002, calc-nesting-002, minmax-*-serialize).
        if (ascii_iequals(name, "calc(")) {
            if (const std::optional<std::string> tree = simplify_sum_text(body)) {
                out.append("calc(").append(*tree).append(")");
                continue;
            }
        }
        // A function with no answer at all still has arguments to simplify.
        // §10.11 says a comparison that cannot be ordered (`min(10px, 5%)`,
        // `min(1em, 1px)`) is its own computed value; its arguments are
        // calculations that simplify like any other. What has no simplified
        // form here, such as `sibling-index()` or `nearest`, comes back through
        // this same function and keeps the author's text.
        out.append(simplified_arguments(name, inner));
    }
    if (step.outermost() && step.exhausted()) { return std::string{value}; }
    return out;
}

// Writes every random() in `value` in canonical form: the sharing options
// spelled out for `property` (see random_key) and each bound simplified.
std::string canonical_random(std::string_view value, std::string_view property) {
    constexpr std::string_view name = "random(";
    // One bound, in its canonical spelling: a dimension in its canonical unit,
    // a product folded, a sum of units in §10.13's order, anything else as it
    // came through simplify_math.
    const auto bound = [](std::string_view arg) -> std::string {
        const token_stream ts = tokenize(arg);
        if (ts.tokens.size() == 2 && ts.tokens.front().type == token_type::dimension) {
            if (!context_free_unit(ts.unit_of(ts.tokens.front()))) { return std::string{arg}; }
            return canonical_dimension_text(arg, length_context{}).value_or(std::string{arg});
        }
        if (ts.tokens.size() == 2) { return std::string{arg}; }
        const auto [outcome, sum] = evaluate_symbolic(arg);
        if (outcome == math_outcome::resolved && sum.simple()) {
            if (!sum.symbols.empty() || sum.has_percent) {
                if (const std::string text = serialize_symbolic(sum); !text.empty()) {
                    return text;
                }
            } else if (sum.is_number()) {
                calc_result number;
                number.px = sum.value;
                number.is_number = true;
                number.type = numeric_type::number;
                return serialize_calc(number);
            }
        }
        return simplify_math(arg);
    };
    std::string out;
    std::size_t at = 0;
    // The ordinal of the random() being spelled, among the value's in source
    // order. fold.cpp and the evaluator count the same way.
    std::size_t position = 0;
    while (at < value.size()) {
        if (const std::size_t quoted = end_of_string_at(value, at); quoted != at) {
            out.append(value.substr(at, quoted - at));
            at = quoted;
            continue;
        }
        if (name_at(value, at, std::array<std::string_view, 1>{name}).empty()) {
            out.push_back(value[at]);
            ++at;
            continue;
        }
        const function_span span = span_of(value, at, name);
        const std::string_view inner =
            value.substr(at + name.size(), span.end - at - name.size() - (span.closed ? 1 : 0));
        at = span.end;
        std::vector<std::string_view> args = top_level_arguments(inner);
        // The sharing head: a first argument that is `fixed <number>` or made
        // of the sharing words alone. `infinity` and `NaN` are identifiers too
        // and are the first bound, not a head (WPT random-computed).
        std::string head;
        const auto is_head = [](std::string_view text) {
            if (ascii_istarts_with(text, "fixed")) { return true; }
            bool any = false;
            for (const std::string_view word : split_top_level(text, " \t\n\r\f")) {
                any = true;
                if (!word.starts_with("--") && !ascii_iequals(word, "auto") &&
                    !ascii_istarts_with(word, "ua-") && !ascii_iequals(word, "element-scoped") &&
                    !ascii_iequals(word, "property-scoped") &&
                    !ascii_iequals(word, "property-index-scoped")) {
                    return false;
                }
            }
            return any;
        };
        if (!args.empty()) {
            if (is_head(trim(args.front(), html_whitespace))) {
                const std::string_view text = trim(args.front(), html_whitespace);
                if (ascii_istarts_with(text, "fixed")) {
                    head = "fixed " + simplify_math(trim(text.substr(5), html_whitespace));
                } else {
                    const random_key sharing = random_options(text, property, position);
                    for (const std::string & word :
                         {sharing.name, std::string{sharing.element_scoped ? "element-scoped" : ""},
                          sharing.ua}) {
                        if (word.empty()) { continue; }
                        if (!head.empty()) { head += ' '; }
                        head += word;
                    }
                }
                args.erase(args.begin());
            } else {
                // No head at all: `auto`, spelled out.
                head = "element-scoped ua-" + std::string{property} + '-' +
                       std::to_string(position + 1);
            }
        }
        out += name;
        out += head;
        for (const std::string_view arg : args) {
            if (!out.ends_with('(')) { out += ", "; }
            out += bound(trim(arg, html_whitespace));
        }
        out += ')';
        // The bounds were not walked, so the random() functions nested in
        // them are counted here to keep step with the evaluator.
        ++position;
        for (std::size_t nested = inner.find(name); nested != std::string_view::npos;
             nested = inner.find(name, nested + name.size())) {
            if (nested == 0 || !is_name(inner[nested - 1])) { ++position; }
        }
    }
    return out;
}

// The specified value of a whole `calc-size(<basis>, <calculation>)`, or
// nullopt when it is invalid for a property whose keywords are `keywords`
// (space-separated).
std::optional<std::string> calc_size_text(std::string_view value, std::string_view keywords) {
    constexpr std::string_view name = "calc-size(";
    const std::string_view whole = trim(value, html_whitespace);
    if (!ascii_iequals(whole.substr(0, name.size()), name)) { return std::nullopt; }
    const function_span span = span_of(whole, 0, name);
    if (span.end != whole.size()) { return std::nullopt; }
    const std::string_view inner =
        whole.substr(name.size(), whole.size() - name.size() - (span.closed ? 1 : 0));
    const std::vector<std::string_view> args = top_level_arguments(inner);
    if (args.size() != 2) { return std::nullopt; }
    const std::string_view basis = trim(args[0], html_whitespace);
    const std::string_view calculation = trim(args[1], html_whitespace);
    if (basis.empty() || calculation.empty()) { return std::nullopt; }

    // The basis: `any`, an intrinsic size keyword the property also takes,
    // another calc-size(), or a <length-percentage>.
    std::string out{name};
    bool any = false;
    const token_stream ts = tokenize(basis);
    const bool one_token = ts.tokens.size() == 2; // The token and eof.
    if (one_token && ts.tokens.front().type == token_type::ident) {
        const std::string word = ascii_lower_copy(basis);
        const auto listed = [&](std::string_view list) {
            std::size_t i = 0;
            while (i <= list.size()) {
                const std::size_t end = std::min(list.find(' ', i), list.size());
                if (list.substr(i, end - i) == word) { return true; }
                i = end + 1;
            }
            return false;
        };
        // An intrinsic size keyword the property takes: `none` is max-width's
        // keyword but no size, and `auto` is a size but not max-width's.
        any = word == "any";
        if (!any && !(listed("auto content min-content max-content fit-content stretch") &&
                      listed(keywords))) {
            return std::nullopt;
        }
        out += word;
    } else if (ascii_iequals(basis.substr(0, name.size()), name)) {
        const std::optional<std::string> nested = calc_size_text(basis, keywords);
        if (!nested) { return std::nullopt; }
        out += *nested;
    } else {
        const auto [outcome, sum] = evaluate_symbolic(basis);
        if (outcome == math_outcome::invalid) { return std::nullopt; }
        if (outcome == math_outcome::resolved) {
            if (sum.type() != numeric_type::length || !sum.simple()) { return std::nullopt; }
            const std::string text = serialize_symbolic(sum);
            out += text.empty() ? std::string{basis} : text;
        } else {
            out += simplify_math(basis);
        }
    }
    out += ", ";

    // The calculation: a <calc-sum> over `size`, a length, and no calc-size()
    // inside it. What has no answer here (`sign(size) * size`) keeps its text.
    for (std::size_t i = 0; i < calculation.size(); ++i) {
        if (ascii_iequals(calculation.substr(i, name.size()), name) &&
            (i == 0 || !is_name(calculation[i - 1]))) {
            return std::nullopt;
        }
    }
    const auto [outcome, sum] = evaluate_symbolic(calculation, !any);
    if (outcome == math_outcome::invalid) { return std::nullopt; }
    if (outcome == math_outcome::resolved) {
        if (sum.type() != numeric_type::length || !sum.simple()) { return std::nullopt; }
        const std::string text = serialize_symbolic(sum);
        out += text.empty() ? std::string{calculation} : text;
    } else {
        out += simplify_math(calculation);
    }
    return out + ')';
}

// Whether every math function in `value` parses, and whether one used as an
// argument of an angle-only function (rotate, skew, hue-rotate) is an angle.
bool math_syntax_ok(std::string_view value) {
    if (!angle_arguments_ok(value)) { return false; }
    // The bases do not matter to a syntax question (`1em` is well formed at any
    // font size), so this asks with the defaults rather than making every
    // caller invent a context it has no use for.
    const length_context ctx;
    std::size_t at = 0;
    while (at < value.size()) {
        // A math function with its own vocabulary is stepped over whole.
        // `calc-size(10px, sign(size) * size)` is a valid declaration whose
        // `size` keyword is meaningless anywhere else, so reading the `sign()`
        // inside it as ordinary arithmetic would report the whole value
        // invalid (WPT css-values/calc-size/interpolate-size-parsing).
        // Descending into a function is right for `transform: rotate(calc(...))`;
        // only knowing which functions redefine their contents tells the two
        // cases apart.
        if (ascii_iequals(value.substr(at, 10), "calc-size(") &&
            (at == 0 || !is_name(value[at - 1]))) {
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
        // Inside a relative colour the channel keywords are values
        // (`rgb(from blue r g calc(b + 150))`), and this evaluator has no
        // channels, so a math function there is left to the colour grammar
        // rather than rejected for an ident it cannot read (WPT random-serialize).
        if (!inside_relative_color(value, at) &&
            evaluate_math(body_of(value, at, name, span), ctx).outcome == math_outcome::invalid) {
            return false;
        }
        // Skip the whole function so a nested one is not checked twice: the
        // outer evaluation already read it.
        at = span.end;
    }
    return true;
}

} // namespace ctbrowser::style::css
