#include "internal.hpp"

namespace ctbrowser::style::css::evaluator_detail {

[[nodiscard]] std::optional<term> evaluator::math_function() {
    const std::string_view name = t_.text_of(peek());
    const auto named = [&](std::string_view n) { return ascii_iequals(name, n); };
    // A NESTED CALC, which Bootstrap writes eight times over
    // (`calc(1em + .5rem + calc(var(x) * 2))`).
    if (named("calc(")) { return nested(); }
    if (named("min(")) { return comparison(compare::smallest); }
    if (named("max(")) { return comparison(compare::largest); }
    if (named("clamp(")) { return clamping(); }
    if (named("round(")) { return rounding(); }
    if (named("mod(")) { return stepped(false); }
    if (named("rem(")) { return stepped(true); }
    if (named("abs(")) { return sign_or_abs(false); }
    if (named("sign(")) { return sign_or_abs(true); }
    if (named("progress(")) { return progress_of(); }
    if (named("random(")) { return random_of(); }
    if (named("calc-mix(")) { return calc_mix(); }
    if (named("hypot(")) { return hypot_of(); }
    if (named("sqrt(")) {
        return numeric(1, 1, [](double a, double) { return std::sqrt(a); });
    }
    if (named("exp(")) {
        return numeric(1, 1, [](double a, double) { return std::exp(a); });
    }
    if (named("pow(")) {
        return numeric(2, 2, [](double a, double b) { return std::pow(a, b); });
    }
    // `log(A)` is the natural logarithm and `log(A, B)` is base B, which the
    // two-argument form spells as a ratio of two natural logs.
    if (named("log(")) {
        return numeric(
            1, 2, [](double a, double b) { return std::log(a) / std::log(b); }, std::numbers::e);
    }
    if (named("sin(")) {
        return trig([](double r) { return std::sin(r); });
    }
    if (named("cos(")) {
        return trig([](double r) { return std::cos(r); });
    }
    if (named("tan(")) {
        return trig([](double r) { return std::tan(r); });
    }
    if (named("asin(")) {
        return inverse_trig(1, [](double a, double) { return std::asin(a); });
    }
    if (named("acos(")) {
        return inverse_trig(1, [](double a, double) { return std::acos(a); });
    }
    if (named("atan(")) {
        return inverse_trig(1, [](double a, double) { return std::atan(a); });
    }
    if (named("atan2(")) {
        return inverse_trig(2, [](double a, double b) { return std::atan2(a, b); });
    }
    // THE TREE-COUNTING FUNCTIONS, CSS Values 5 §tree-counting: a <number>
    // that is a fact about the element rather than about the expression, so
    // the cascade supplies it through the context and a SPECIFIED value -
    // which has no element - keeps the function as written. Either takes no
    // argument at all; `sibling-index(1)` is a syntax error.
    if (named("sibling-index(") || named("sibling-count(")) {
        const bool index = named("sibling-index(");
        ++at_;
        skip_whitespace();
        if (!at_close()) { return fail(); }
        take_close();
        const std::uint32_t known = index ? ctx_.sibling_index : ctx_.sibling_count;
        // A SPECIFIED VALUE KEEPS THE FUNCTION AS A TERM OF ITS OWN, a
        // <number> no basis can supply, so `calc(1turn * sibling-count())`
        // simplifies to `calc(360deg * sibling-count())` around it
        // (calc-sibling-function-parsing, CSS Values 4 §10.12).
        if (basis_ == basis::symbolic) {
            term out;
            add_symbol(out, index ? "sibling-index()" : "sibling-count()", 1.0);
            return out;
        }
        if (known == 0) { return unresolvable(); }
        term out;
        out.value = known;
        return out;
    }
    // `calc-size()` IS A TOP-LEVEL FUNCTION ONLY, CSS Values 5 §calc-size:
    // it may not sit inside another math function, and the only way one
    // reaches this evaluator is nested - a top-level one is stepped over
    // whole by every scan (calc-size-parsing).
    if (named("calc-size(")) { return fail(); }
    // ANY OTHER FUNCTION IS UNRESOLVED, NOT INVALID, and the difference is
    // measured: `calc(inherit(--x) + 1px)` and `calc(attr(data-n px) * 2)`
    // are `test_valid_value` assertions and they are valid CSS this file
    // simply cannot evaluate. Calling them errors would delete every one of
    // those declarations - the exact failure mode the third outcome exists to
    // avoid. `attr()` and `calc-size()` land here for the same reason.
    return unresolvable();
}

[[nodiscard]] std::optional<term> evaluator::comparison(compare kind) {
    ++at_; // the function token, `(` included
    const std::optional<std::vector<term>> args = arguments(1, ~std::size_t{0});
    if (!args) { return std::nullopt; }
    // ONE ARGUMENT IS NOT A COMPARISON. `min(X)` is X whatever X is, so the
    // uniformity check below - which exists to say when two arguments cannot
    // be ORDERED - has nothing to ask. Running it anyway made `min(1% + 1px)`
    // undecidable, where `minmax-length-percent-serialize` wants
    // `calc(1% + 1px)`.
    if (args->size() == 1) { return args->front(); }
    if (!uniform(*args)) { return std::nullopt; }
    double best = scalar_of(args->front());
    for (const term & one : *args) {
        const double v = scalar_of(one);
        if (std::isnan(best) || std::isnan(v)) {
            best = std::nan("");
            break;
        }
        best = kind == compare::smallest ? smaller(best, v) : larger(best, v);
    }
    return with_scalar(args->front(), best);
}

[[nodiscard]] std::optional<term> evaluator::clamping() {
    ++at_; // the function token, `(` included
    constexpr double huge = std::numeric_limits<double>::infinity();
    std::vector<term> present; // for the type check, which `none` sits out of
    double bound[3] = {-huge, 0.0, huge};
    std::optional<term> middle;
    for (int i = 0; i < 3; ++i) {
        skip_whitespace();
        const bool may_be_none = i != 1;
        if (may_be_none && peek().type == token_type::ident &&
            ascii_iequals(t_.text_of(peek()), "none")) {
            ++at_;
        } else {
            const std::optional<term> one = sum();
            if (!one) { return std::nullopt; }
            present.push_back(*one);
            if (i == 1) { middle = one; }
            bound[i] = scalar_of(*one);
        }
        skip_whitespace();
        if (i == 2) { break; }
        if (peek().type != token_type::comma) { return fail(); }
        ++at_;
    }
    if (!at_close()) { return fail(); }
    take_close();
    if (!uniform(present)) { return std::nullopt; }
    // clamp(low, value, high) is max(low, min(value, high)) - and the spec's
    // order matters when low > high: the LOW bound wins, because the min is
    // taken first. A NaN anywhere poisons the result, which std::min and
    // std::max do NOT do on their own.
    if (std::isnan(bound[0]) || std::isnan(bound[1]) || std::isnan(bound[2])) {
        return with_scalar(*middle, std::nan(""));
    }
    return with_scalar(*middle, larger(bound[0], smaller(bound[1], bound[2])));
}

[[nodiscard]] std::optional<term> evaluator::rounding() {
    ++at_;
    round_to how = round_to::nearest;
    skip_whitespace();
    if (peek().type == token_type::ident) {
        const std::string_view word = t_.text_of(peek());
        bool named = true;
        if (ascii_iequals(word, "nearest")) {
            how = round_to::nearest;
        } else if (ascii_iequals(word, "up")) {
            how = round_to::up;
        } else if (ascii_iequals(word, "down")) {
            how = round_to::down;
        } else if (ascii_iequals(word, "to-zero")) {
            how = round_to::to_zero;
        } else {
            named = false; // `e`, `pi`, `infinity` - a constant, not a strategy
        }
        if (named) {
            ++at_;
            skip_whitespace();
            if (peek().type != token_type::comma) { return fail(); }
            ++at_;
        }
    }
    std::optional<std::vector<term>> args = arguments(1, 2);
    if (!args) { return std::nullopt; }
    // THE STEP DEFAULTS TO THE NUMBER 1, CSS Values 4 §10.5 - so `round(1.5)`
    // is 2 and `round(1.5px)` is a type error, a length rounded to a number.
    if (args->size() == 1) {
        term one;
        one.value = 1.0;
        args->push_back(one);
    }
    if (!uniform(*args)) { return std::nullopt; }
    return with_scalar(args->front(), round_one(how, scalar_of((*args)[0]), scalar_of((*args)[1])));
}

[[nodiscard]] std::optional<term> evaluator::stepped(bool truncating) {
    ++at_;
    const std::optional<std::vector<term>> args = arguments(2, 2);
    if (!args) { return std::nullopt; }
    if (!uniform(*args)) { return std::nullopt; }
    const double a = scalar_of((*args)[0]);
    const double b = scalar_of((*args)[1]);
    return with_scalar(args->front(), truncating ? rem_one(a, b) : mod_one(a, b));
}

[[nodiscard]] std::optional<term> evaluator::sign_or_abs(bool want_sign) {
    ++at_;
    const std::optional<std::vector<term>> args = arguments(1, 1);
    if (!args) { return std::nullopt; }
    if (!uniform(*args)) { return std::nullopt; }
    const double a = scalar_of(args->front());
    if (!want_sign) { return with_scalar(args->front(), std::fabs(a)); }
    term out;
    // A ZERO KEEPS ITS SIGN. `sign(-0px)` is -0 and not 0, which is
    // observable through `1 / sign(x)` - the corpus's own way of asking.
    out.value = std::isnan(a) ? a : (a > 0.0 ? 1.0 : (a < 0.0 ? -1.0 : a));
    return out;
}

[[nodiscard]] std::optional<term> evaluator::progress_of() {
    ++at_; // the function token, `(` included
    skip_whitespace();
    bool clamped = true;
    if (peek().type == token_type::ident && ascii_iequals(t_.text_of(peek()), "no-clamp")) {
        ++at_;
        clamped = false;
    }
    const std::optional<std::vector<term>> args = arguments(3, 3);
    if (!args) { return std::nullopt; }
    // ONE TYPE FOR ALL THREE, THE PERCENTAGE INCLUDED - and this is where it
    // parts company with `uniform()`. A RATIO of percentages is decidable
    // because the basis cancels, so `progress(1%, 0%, 100%)` is `calc(0.01)`
    // with nothing left to resolve; but `progress(5%, 0px, 10px)` does not
    // have three arguments that agree on what they measure, and that is a
    // type error rather than a comparison awaiting layout.
    // ...AND EACH IS A <number>, A <dimension> OR A <percentage>, which is
    // what the specification's "the argument calculations can resolve to
    // any" lists. Typed arithmetic makes `10px * 10px` a term with a
    // type, and the three would agree on it - but an area is not one of
    // the three and `progress-invalid` says the whole is a syntax error.
    for (const term & one : *args) {
        if (!one.simple()) { return fail(); }
        if (one.dims != args->front().dims || one.has_percent != args->front().has_percent) {
            return fail();
        }
    }
    for (const term & one : *args) {
        if (!is_scalar(one)) { return unresolvable(); }
    }
    term out;
    out.value =
        progress_one(clamped, scalar_of((*args)[0]), scalar_of((*args)[1]), scalar_of((*args)[2]));
    return out;
}

[[nodiscard]] std::optional<term> evaluator::random_of() {
    ++at_; // the function token, `(` included
    // ITS ORDINAL AMONG THE VALUE'S random() FUNCTIONS, in source order,
    // which is what `property-index-scoped` and the automatic key count
    // by: `a, random()` shares with `random(), random()`'s first and not
    // its second, and two inside one calc() are two (random-computed,
    // random-in-if). The caller's `random_index` is where this expression
    // starts counting; `randoms_` is how far it got.
    length_context keyed = ctx_;
    keyed.random_index = ctx_.random_index + randoms_++;
    // A SPECIFIED VALUE KEEPS ITS random(): the draw happens at
    // computed-value time and nowhere earlier (random-serialize).
    if (basis_ == basis::symbolic) { return unresolvable(); }
    // The options: everything before the first comma, read as tokens.
    // `[ [ auto | <dashed-ident> ] || <ua-ident> || element-scoped |
    // property-scoped | property-index-scoped ] | fixed <number>`: one
    // dashed name and one UA ident at most, each scoping word at most once,
    // `property-scoped` and `property-index-scoped` exclusive of each other
    // and of a UA ident, and `fixed` alone with its number in [0, 1]
    // (random-invalid, random-serialize).
    std::string options;
    bool fixed = false;
    double base = 0.0;
    int names = 0, element = 0, property = 0, index = 0, ua = 0;
    skip_whitespace();
    for (;;) {
        skip_whitespace();
        const css_token & tok = peek();
        if (tok.type == token_type::comma || at_close()) { break; }
        if (tok.type == token_type::ident) {
            const std::string_view word = t_.text_of(tok);
            if (ascii_iequals(word, "fixed")) {
                if (fixed || !options.empty()) { return fail(); }
                ++at_;
                skip_whitespace();
                // A literal outside [0, 1] is a syntax error; a computed
                // one is clamped (random-invalid, random-computed).
                if (peek().type == token_type::number &&
                    (peek().number < 0.0 || peek().number > 1.0)) {
                    return fail();
                }
                const std::optional<term> given = sum();
                if (!given || !given->is_number() || given->has_percent ||
                    !given->symbols.empty()) {
                    return fail();
                }
                fixed = true;
                // Clamped to [0, 1), so a base that overshoots picks the
                // top of the range without landing exactly on it.
                base = std::min(std::max(given->value, 0.0), 1.0 - 1e-9);
                continue;
            }
            const bool is_name = word.starts_with("--") || ascii_iequals(word, "auto");
            const bool is_ua = ascii_istarts_with(word, "ua-");
            const bool is_element = ascii_iequals(word, "element-scoped");
            const bool is_property = ascii_iequals(word, "property-scoped");
            const bool is_index = ascii_iequals(word, "property-index-scoped");
            if (is_name || is_ua || is_element || is_property || is_index) {
                if (fixed) { return fail(); }
                names += is_name ? 1 : 0;
                ua += is_ua ? 1 : 0;
                element += is_element ? 1 : 0;
                property += is_property ? 1 : 0;
                index += is_index ? 1 : 0;
                if (names > 1 || ua > 1 || element > 1 || property + index > 1 ||
                    (ua > 0 && property + index > 0)) {
                    return fail();
                }
                if (!options.empty()) { options += ' '; }
                options += word;
                ++at_;
                continue;
            }
            // `NaN`, `infinity`, `pi`: the first argument, with no options.
            if (!options.empty() || fixed) { return fail(); }
            break;
        }
        // A number or a dimension: the first argument, with no options.
        if (!options.empty() || fixed) { return fail(); }
        break;
    }
    if (!options.empty() || fixed) {
        skip_whitespace();
        if (peek().type != token_type::comma) { return fail(); }
        ++at_;
    }
    if (!fixed) { base = random_base(options, keyed); }
    // A, B, and the optional step - which may be spelled `by <step>`.
    std::vector<term> args;
    for (;;) {
        skip_whitespace();
        if (args.size() == 2 && peek().type == token_type::ident &&
            ascii_iequals(t_.text_of(peek()), "by")) {
            ++at_;
        }
        const std::optional<term> one = sum();
        if (!one) { return std::nullopt; }
        args.push_back(*one);
        skip_whitespace();
        if (peek().type == token_type::comma) {
            ++at_;
            continue;
        }
        break;
    }
    if (!at_close()) { return fail(); }
    take_close();
    if (args.size() < 2 || args.size() > 3) { return fail(); }
    if (!uniform(args)) { return std::nullopt; }
    const std::optional<double> step =
        args.size() == 3 ? std::optional<double>{scalar_of(args[2])} : std::nullopt;
    return with_scalar(args.front(),
                       random_one(base, scalar_of(args[0]), scalar_of(args[1]), step));
}

[[nodiscard]] std::optional<term> evaluator::calc_mix() {
    ++at_; // the function token, `(` included
    std::vector<term> values;
    std::vector<std::optional<double>> weights;
    for (;;) {
        const std::optional<term> one = sum();
        if (!one) { return std::nullopt; }
        values.push_back(*one);
        skip_whitespace();
        std::optional<double> weight;
        if (peek().type == token_type::percentage) {
            // A literal weight outside [0%, 100%] is a syntax error; only
            // a computed one is clamped (calc-mix-invalid).
            if (peek().number < 0.0 || peek().number > 100.0) { return fail(); }
            weight = peek().number;
            ++at_;
        } else if (peek().type == token_type::function) {
            // A weight written as a function keeps the specified value as
            // written, resolvable or not (calc-mix-serialize).
            if (basis_ == basis::symbolic) { return unresolvable(); }
            const std::optional<term> given = math_function();
            if (!given) { return std::nullopt; }
            // A weight is a bare percentage; one with a magnitude of its
            // own or a symbol in it is a type error or waits for the
            // cascade.
            if (!given->has_percent || given->value != 0.0 || !given->symbols.empty()) {
                return given->symbols.empty() ? fail() : unresolvable();
            }
            weight = given->percent;
        }
        if (weight) { weight = std::min(std::max(*weight, 0.0), 100.0); }
        weights.push_back(weight);
        skip_whitespace();
        if (peek().type == token_type::comma) {
            ++at_;
            continue;
        }
        break;
    }
    if (!at_close()) { return fail(); }
    take_close();
    for (const term & one : values) {
        if (one.dims != values.front().dims) { return fail(); }
    }
    double given = 0.0;
    double missing = 0.0;
    for (const std::optional<double> & w : weights) {
        if (w) {
            given += *w;
        } else {
            missing += 1.0;
        }
    }
    const double share = missing == 0.0 ? 0.0 : std::max(0.0, 100.0 - given) / missing;
    const double total = std::max(100.0, given);
    std::optional<term> out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double w = weights[i].value_or(share) / total;
        if (w == 0.0) { continue; }
        // A value with no magnitude yet - `3em`, `sibling-index()` - keeps
        // the specified value a calc-mix(); one weighing nothing does not.
        if (basis_ == basis::symbolic && !values[i].symbols.empty()) { return unresolvable(); }
        const term part = scaled(values[i], w);
        out = out ? add(*out, part, false) : std::optional<term>{part};
        if (!out) { return fail(); }
    }
    if (out) { return out; }
    // Every weight zero: nought, of the first value's kind - which in a
    // symbolic sum is a `0px` term of its own, so `calc(10% +
    // calc-mix(1px 0%, 3% 0%))` keeps its `0px`.
    term zero;
    zero.dims = values.front().dims;
    zero.has_percent = values.front().has_percent && values.front().symbols.empty();
    if (basis_ == basis::symbolic && !zero.has_percent && !zero.is_number()) {
        add_symbol(zero, canonical_unit(zero.type()), 0.0);
    }
    return zero;
}

[[nodiscard]] std::optional<term> evaluator::hypot_of() {
    ++at_;
    const std::optional<std::vector<term>> args = arguments(1, ~std::size_t{0});
    if (!args) { return std::nullopt; }
    if (!uniform(*args)) { return std::nullopt; }
    double total = 0.0;
    for (const term & one : *args) {
        const double v = scalar_of(one);
        total += v * v;
    }
    return with_scalar(args->front(), std::sqrt(total));
}

} // namespace ctbrowser::style::css::evaluator_detail
