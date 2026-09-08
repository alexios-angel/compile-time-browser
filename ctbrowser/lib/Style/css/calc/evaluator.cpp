// calc() - the term algebra, the non-linear functions of CSS Values 4
// §10.4-§10.8, the recursive-descent evaluator over the token stream, and its
// entry points: evaluate_math, evaluate_calc and the symbolic evaluation a
// specified value uses.
//
// One of five files carved out of a 1,810-line css/calc.cpp on 2026-09-08. The
// public surface is include/ctbrowser/style/css/calc.hpp and did not change;
// the helpers more than one of these files needs are declared in internal.hpp
// beside this, with external linkage in ctbrowser::style::css::detail.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// A term whose whole magnitude is in ONE slot, so a non-linear function can be
// applied to it. `10px` and `10%` both qualify; `calc(10px + 10%)` does not,
// because `abs()` of it depends on a containing block nobody has yet.
[[nodiscard]] bool is_scalar(const term & t) noexcept {
    // A SYMBOLIC SUM IS NEVER ONE. `min(1em, 1px)` cannot be ordered before a
    // font size exists, exactly as `min(10px, 5%)` cannot be ordered before a
    // containing block does, and the two undecidable cases answer the same way.
    if (!t.symbols.empty()) { return false; }
    return !t.has_percent || t.value == 0.0;
}
[[nodiscard]] double scalar_of(const term & t) noexcept {
    return t.has_percent ? t.percent : t.value;
}
[[nodiscard]] term with_scalar(const term & like, double v) {
    term out = like;
    (like.has_percent ? out.percent : out.value) = v;
    return out;
}

[[nodiscard]] std::optional<term> add(const term & a, const term & b, bool subtract) {
    const double sign = subtract ? -1.0 : 1.0;
    // Two different families cannot be added. This is the check that makes
    // `calc(100% - 12)` invalid, which is what Chrome does with it, and it now
    // also catches `calc(1s + 1px)`.
    if (a.type != b.type) { return std::nullopt; }
    term out;
    out.type = a.type;
    out.value = a.value + sign * b.value;
    out.percent = a.percent + sign * b.percent;
    out.has_percent = a.has_percent || b.has_percent;
    out.symbols = a.symbols;
    for (const auto & [unit, coefficient] : b.symbols) {
        add_symbol(out, unit, sign * coefficient);
    }
    return out;
}

[[nodiscard]] std::optional<term> multiply(const term & a, const term & b) {
    // At most one operand may be dimensioned: `2px * 3px` is an area, and calc
    // has no property that takes one.
    if (!a.is_number() && !b.is_number()) { return std::nullopt; }
    const term & dim = a.is_number() ? b : a;
    const double scale = a.is_number() ? a.value : b.value;
    term out = dim;
    // AN ABSENT COMPONENT IS NOT A ZERO ONE. `10%` carries its magnitude in
    // `percent` and a zero `value`, and that zero means "there is no length in
    // this term" rather than "the length is nought". IEEE makes `0 * infinity` a
    // NaN, so scaling a bare percentage by an infinity invented a NaN LENGTH
    // beside the right answer and `calc(1% * infinity)` printed as `calc(NaN *
    // 1px)` where it is `calc(infinity * 1%)`. `calc(0px * infinity)` IS a NaN
    // and still is: there the zero is a length the author wrote.
    //
    // A SYMBOLIC TERM IS THE SAME CASE: `1em` carries its magnitude in `symbols`
    // and leaves `value` at nought, and there is no length there either.
    const bool no_plain_part = (dim.has_percent || !dim.symbols.empty()) && dim.value == 0.0;
    out.value = no_plain_part ? 0.0 : dim.value * scale;
    out.percent = dim.has_percent ? dim.percent * scale : 0.0;
    for (auto & [unit, coefficient] : out.symbols) { coefficient *= scale; }
    return out;
}

[[nodiscard]] std::optional<term> divide(const term & a, const term & b) {
    // The divisor must be a NUMBER - `calc(20 / 0.75rem)` is invalid and
    // `css/css-values/calc-unit-analysis` says so by name. It may be ZERO,
    // though, and that was a defect: CSS Values 4 §10.9 says the result is an
    // infinity or a NaN, not a syntax error, so `calc(100px / 0)` is
    // `calc(infinity * 1px)` and `calc(0 / 0)` is `calc(NaN)`. IEEE division
    // produces both, with the right sign for a negative zero, so there is
    // nothing here to special-case.
    if (!b.is_number()) { return std::nullopt; }
    term out = a;
    // The same absent-component rule multiply() carries, and for the same
    // reason: `calc(1% / 0)` is `calc(infinity * 1%)` and not a NaN length.
    const bool no_plain_part = (a.has_percent || !a.symbols.empty()) && a.value == 0.0;
    out.value = no_plain_part ? 0.0 : a.value / b.value;
    out.percent = a.has_percent ? a.percent / b.value : 0.0;
    for (auto & [unit, coefficient] : out.symbols) { coefficient /= b.value; }
    return out;
}

// --- the non-linear functions, CSS Values 4 §10.4-§10.8 ------------------

enum class round_to : std::uint8_t {
    nearest,
    up,
    down,
    to_zero
};

// §10.5's whole table, infinities included, and the infinities are not a corner:
// `round-mod-rem-computed` spends forty of its assertions on them because they
// are how a page asks "is this value finite" without a script.
[[nodiscard]] double round_one(round_to how, double a, double b) {
    if (b == 0.0 || std::isnan(a) || std::isnan(b)) { return std::nan(""); }
    if (std::isinf(a)) { return std::isinf(b) ? std::nan("") : a; }
    if (std::isinf(b)) {
        // A finite A against an infinite step: the answer is a zero or an
        // infinity, and WHICH is decided by the strategy and A's sign - a
        // negative zero being negative, which is why std::signbit is asked
        // rather than `a < 0`.
        const bool negative = std::signbit(a);
        switch (how) {
        case round_to::up:
            return a > 0.0 ? std::numeric_limits<double>::infinity() : (negative ? -0.0 : 0.0);
        case round_to::down:
            return a < 0.0 ? -std::numeric_limits<double>::infinity() : (negative ? -0.0 : 0.0);
        default: return negative ? -0.0 : 0.0;
        }
    }
    const double n = a / b;
    double stepped = 0.0;
    switch (how) {
    case round_to::up: stepped = std::ceil(n); break;
    case round_to::down: stepped = std::floor(n); break;
    case round_to::to_zero: stepped = std::trunc(n); break;
    // "If two multiples are equally near, use the one above" - so a half rounds
    // towards +infinity, which floor(n + 0.5) is and std::round(n) is NOT:
    // std::round takes -2.5 to -3 and CSS takes it to -2.
    case round_to::nearest: stepped = std::floor(n + 0.5); break;
    }
    return stepped * b;
}

// §10.6. `mod` takes the sign of the DIVISOR and `rem` the sign of the dividend,
// which is the whole difference between them and the reason both exist.
[[nodiscard]] double mod_one(double a, double b) {
    if (b == 0.0 || std::isinf(a) || std::isnan(a) || std::isnan(b)) { return std::nan(""); }
    if (std::isinf(b)) {
        // An infinite divisor leaves A alone if they point the same way and is
        // meaningless if they do not. An oppositely-signed ZERO counts as
        // opposite, so `mod(-0, infinity)` is NaN.
        return std::signbit(a) == std::signbit(b) ? a : std::nan("");
    }
    return a - b * std::floor(a / b);
}

[[nodiscard]] double rem_one(double a, double b) {
    if (b == 0.0 || std::isinf(a) || std::isnan(a) || std::isnan(b)) { return std::nan(""); }
    if (std::isinf(b)) { return a; }
    return std::fmod(a, b);
}

// CSS Values 5 §progress: how far `a` lies from `b` to `c`, as a <number>.
//
// AN EMPTY RANGE IS NEITHER AN ERROR NOR A NaN. `progress(1rad, 1rad, 1rad)` is
// nought over nought, which IEEE calls NaN and the specification calls no
// progress at all. The unclamped form keeps the numerator's SIGN, so
// `progress(no-clamp 2rad, 1rad, 1rad)` is an infinity and
// `progress(no-clamp 0rad, 1rad, 1rad)` its negative; the clamped form is nought
// whichever way it points, because there is no range to be anywhere in.
// `progress-serialize` asserts all six of those.
[[nodiscard]] double progress_one(bool clamped, double a, double b, double c) {
    if (c == b) {
        if (clamped || a == b) { return 0.0; }
        return a > b ? std::numeric_limits<double>::infinity()
                     : -std::numeric_limits<double>::infinity();
    }
    const double how_far = (a - b) / (c - b);
    if (!clamped || std::isnan(how_far)) { return how_far; }
    return std::min(std::max(how_far, 0.0), 1.0);
}

// WHAT A DIMENSION IS MEASURED AGAINST. Two answers, and the second is what a
// SPECIFIED value needs: there are no bases yet when one is written, so `1em`
// and `1cqw` are terms in their own right rather than numbers of pixels.
enum class basis : std::uint8_t {
    // `ctx` supplies a font size and a viewport and every length becomes pixels.
    // This is the computed-value evaluation and the only one that ever answers
    // with a `calc_result`.
    against_context,
    // Nothing is supplied and nothing is guessed. `calc(10px + 1vmin + 10%)` is
    // three terms, and printing them in §10.13's order is the whole answer.
    symbolic,
};

// A recursive-descent parser over the token stream, one instance per expression.
// `ok_` latches false on the first error so every level can stop checking.
class evaluator {
public:
    evaluator(const token_stream & tokens, const length_context & ctx,
              basis measure = basis::against_context)
        : t_(tokens), ctx_(ctx), basis_(measure) {}

    [[nodiscard]] math_answer run() {
        const std::optional<term> value = settle();
        if (!value) {
            return math_answer{outcome_, {}};
        }
        calc_result out;
        // A NUMBER IS AN ANSWER. `calc()` of a bare number used to be reported as
        // no answer at all, which the cascade read as an invalid declaration and
        // threw away - so `opacity: calc(2 / 4)` and `rgb(calc(0), calc(255),
        // calc(0))` produced nothing. CSS Values 3 §8.1 says a math function may
        // resolve to a <number>; whether the PROPERTY accepts one is a separate
        // question, and math_context is where it is asked.
        out.type = value->type;
        out.is_number = value->is_number();
        out.px = value->value;
        out.percent = value->percent;
        out.has_percent = value->has_percent;
        return math_answer{math_outcome::resolved, out};
    }

    // The SYMBOLIC evaluation's answer, which is the term itself: a
    // `calc_result` carries one magnitude and a percentage and cannot hold
    // `calc(10% + 10px + 1vmin)`.
    [[nodiscard]] std::pair<math_outcome, term> run_symbolic() {
        const std::optional<term> value = settle();
        if (!value) {
            return {outcome_, term{}};
        }
        return {math_outcome::resolved, *value};
    }

private:
    // The whole expression, parsed and ruled on. `nullopt` leaves the reason in
    // `outcome_`, which is what both entry points above report.
    [[nodiscard]] std::optional<term> settle() {
        const std::optional<term> value = sum();
        skip_whitespace();
        // UNRESOLVED WINS OVER INVALID. A comparison that could not be decided
        // here stopped the parse the same way an error does, so the latch has to
        // be read before the missing value is: `min(10px, 5%)` is a valid
        // declaration and reporting it as a syntax error would delete it.
        if (unresolved_) {
            outcome_ = math_outcome::unresolved;
            return std::nullopt;
        }
        // A trailing token means the expression did not consume its input -
        // `calc(1px 2px)` - which is an error and not a partial answer.
        if (!ok_ || !value || !at_end()) {
            outcome_ = math_outcome::invalid;
            return std::nullopt;
        }
        // A PERCENTAGE HAS TO BE A PERCENTAGE OF SOMETHING. CSS Values 4 §10.11
        // calls it the calculation context, and the only one this engine ever
        // supplies is a length: no property resolves a percentage into an angle,
        // a time, a frequency or a resolution, so a percentage in an expression
        // that answers with one has nothing to be measured against and the
        // expression is a syntax error rather than a value waiting for layout.
        // `animation-duration: calc(sign(50%) * 1s)` and
        // `transform: rotate(calc(sign(50%) * 1deg))` are two of the twelve
        // `percentage-without-context` writes, and every one of them folded to a
        // number here by reading the percentage's own digits as its magnitude.
        //
        // A <number> ANSWER IS NOT COVERED and must not be: `progress(1%, (10% -
        // 10%), 100%)` is `calc(0.01)` and `calc(1px * pow(tan(atan2(50%, 1px)),
        // 1))` is a valid width, because there the percentages sit in a length
        // context that the property does supply.
        if (saw_percent_ && value->type != numeric_type::number &&
            value->type != numeric_type::length) {
            outcome_ = math_outcome::invalid;
            return std::nullopt;
        }
        outcome_ = math_outcome::resolved;
        return value;
    }

    [[nodiscard]] const css_token & peek() const noexcept { return t_.tokens[at_]; }
    [[nodiscard]] bool at_end() const noexcept { return peek().type == token_type::eof; }
    void skip_whitespace() noexcept {
        while (peek().type == token_type::whitespace) { ++at_; }
    }
    [[nodiscard]] bool is_delim(char c) const noexcept {
        return peek().type == token_type::delim && t_.text_of(peek()) == std::string_view{&c, 1};
    }
    // EOF CLOSES EVERY OPEN FUNCTION. CSS Syntax 3 §5.4.9 says so, and the corpus
    // relies on it: `calc(min(1em, 21px) * 2` with no closing paren appears four
    // times in `minmax-length-computed` alone. Refusing it deleted the
    // declaration, which is the one answer a browser does not give.
    [[nodiscard]] bool at_close() const noexcept {
        return peek().type == token_type::close_paren || at_end();
    }
    void take_close() noexcept {
        if (peek().type == token_type::close_paren) { ++at_; }
    }

    // sum := product (('+' | '-') product)*
    //
    // CSS requires whitespace around `+` and `-` inside a calc, and this gets it
    // for free by not looking for a sign: the tokenizer folds `-12px` into one
    // dimension token, so `calc(100% -12px)` presents two lengths with no
    // operator between them and fails at the trailing-token check. That is
    // Chrome's answer for it too.
    [[nodiscard]] std::optional<term> sum() {
        std::optional<term> left = product();
        if (!left) { return std::nullopt; }
        for (;;) {
            skip_whitespace();
            const bool plus = is_delim('+');
            const bool minus = is_delim('-');
            if (!plus && !minus) { return left; }
            ++at_;
            const std::optional<term> right = product();
            if (!right) { return fail(); }
            left = add(*left, *right, minus);
            if (!left) { return fail(); }
        }
    }

    // product := term (('*' | '/') term)*
    [[nodiscard]] std::optional<term> product() {
        std::optional<term> left = single();
        if (!left) { return std::nullopt; }
        for (;;) {
            skip_whitespace();
            const bool times = is_delim('*');
            const bool over = is_delim('/');
            if (!times && !over) { return left; }
            ++at_;
            const std::optional<term> right = single();
            if (!right) { return fail(); }
            left = times ? multiply(*left, *right) : divide(*left, *right);
            if (!left) { return fail(); }
        }
    }

    // term := <number> | <dimension> | <percentage> | <constant>
    //       | '(' sum ')' | <math-function>
    [[nodiscard]] std::optional<term> single() {
        skip_whitespace();
        const css_token & tok = peek();
        switch (tok.type) {
        case token_type::number: {
            ++at_;
            term out;
            out.value = tok.number;
            return out;
        }
        case token_type::percentage: {
            ++at_;
            saw_percent_ = true;
            term out;
            // A percentage has no type of its own until the property says what it
            // is a percentage OF, and every property this engine resolves one
            // for takes a length. So it travels as a length carrying an
            // unresolved part, which is exactly what `calc(100% - 12px)` needs.
            out.type = numeric_type::length;
            out.percent = tok.number;
            out.has_percent = true;
            return out;
        }
        case token_type::dimension: {
            const std::string_view unit = t_.unit_of(tok);
            const std::optional<term> one = basis_ == basis::symbolic
                                                ? symbolic_term(tok.number, unit)
                                                : canonical_term(tok.number, unit, ctx_);
            if (!one) {
                // A unit the specification names and this engine has no basis
                // for - `1lh`, `1cqw` - is UNRESOLVED, not invalid. A typo is
                // invalid.
                return is_known_unit(unit) ? unresolvable() : fail();
            }
            ++at_;
            return one;
        }
        case token_type::ident: return constant();
        case token_type::open_paren: return nested();
        case token_type::function: return math_function();
        default: return fail();
        }
    }

    // THE NUMERIC CONSTANTS, CSS Values 4 §10.9. `infinity` and `NaN` are not
    // <number-token>s and could not be, which is why they are keywords and why
    // they may appear ONLY inside a math function: `opacity: infinity` is a
    // syntax error and `opacity: calc(infinity)` is a value.
    [[nodiscard]] std::optional<term> constant() {
        const std::string_view name = t_.text_of(peek());
        term out;
        if (ascii_iequals(name, "e")) {
            out.value = std::numbers::e;
        } else if (ascii_iequals(name, "pi")) {
            out.value = std::numbers::pi;
        } else if (ascii_iequals(name, "infinity")) {
            out.value = std::numeric_limits<double>::infinity();
        } else if (ascii_iequals(name, "-infinity")) {
            out.value = -std::numeric_limits<double>::infinity();
        } else if (ascii_iequals(name, "nan")) {
            out.value = std::nan("");
        } else {
            return fail();
        }
        ++at_;
        return out;
    }

    // The body of a `(` or `calc(`, up to its matching close paren.
    [[nodiscard]] std::optional<term> nested() {
        ++at_; // the opener
        const std::optional<term> inner = sum();
        if (!inner) { return std::nullopt; }
        skip_whitespace();
        if (!at_close()) { return fail(); }
        take_close();
        return inner;
    }

    enum class compare : std::uint8_t {
        smallest,
        largest
    };

    // A comma-separated argument list, the function token already consumed and
    // the close paren consumed here. An arity outside [min, max] is a syntax
    // error rather than an undecidable comparison: `round(nearest, 1px)` has no
    // step and `round(nearest, 1px, 1px, 1px)` has two, and the corpus lists
    // both as invalid.
    [[nodiscard]] std::optional<std::vector<term>> arguments(std::size_t min, std::size_t max) {
        std::vector<term> args;
        for (;;) {
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
        if (!at_close()) { return no_args(); }
        take_close();
        if (args.size() < min || args.size() > max) { return no_args(); }
        return args;
    }

    // Every argument the same family, and every one of them a single magnitude of
    // the same shape. THE TWO TESTS HAVE DIFFERENT ANSWERS and that is the point:
    // `min(1px, 2)` compares a length with a number and is a TYPE ERROR, while
    // `min(10px, 5%)` compares two lengths whose relative size depends on a
    // containing block and is merely UNDECIDABLE here. One deletes the
    // declaration; the other keeps it verbatim, which CSS Values 4 §10.11 says is
    // its computed value.
    [[nodiscard]] bool uniform(const std::vector<term> & args) {
        for (const term & one : args) {
            if (one.type != args.front().type) {
                ok_ = false;
                return false;
            }
        }
        for (const term & one : args) {
            if (!is_scalar(one) || one.has_percent != args.front().has_percent) {
                unresolved_ = true;
                return false;
            }
        }
        // ...AND A PERCENTAGE CANNOT BE COMPARED WITH ANYTHING, including another
        // percentage. `min(1%, 2%)` looks decidable and is not: a percentage
        // resolves against a basis that MAY BE NEGATIVE, and then 2% is the
        // smaller. `minmax-percentage-serialize` is explicit about it - it asks
        // for `calc(min(1%, 2%) + max(3%, 4%) + 10%)` back with both functions
        // still in it. One argument is not a comparison and is unaffected, which
        // is what keeps `min(1%)` simplifying to `calc(1%)`.
        if (args.size() > 1 && args.front().has_percent) {
            unresolved_ = true;
            return false;
        }
        return true;
    }

    [[nodiscard]] std::optional<term> math_function() {
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
                1, 2, [](double a, double b) { return std::log(a) / std::log(b); },
                std::numbers::e);
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
        // ANY OTHER FUNCTION IS UNRESOLVED, NOT INVALID, and the difference is
        // measured: `calc(1px * sibling-index())`, `calc(0.5s * sibling-count())`
        // and `calc(inherit(--x) + 1px)` are 34 `test_valid_value` assertions
        // across `css/css-values/tree-counting/` and they are valid CSS this file
        // simply cannot evaluate. Calling them errors would delete every one of
        // those declarations - the exact failure mode the third outcome exists to
        // avoid. `attr()` and `calc-size()` land here for the same reason.
        return unresolvable();
    }

    // min( sum [, sum]* ) | max( sum [, sum]* )
    //
    // CSS Values 4 §10.3. Two things make this more than a fold over `sum()`:
    //
    // EVERY ARGUMENT MUST BE THE SAME TYPE. `min(1px, 2)` compares a length with
    // a number and has no meaning, exactly as `1px + 2` has none.
    //
    // A MIXED PERCENTAGE MAKES THE COMPARISON UNDECIDABLE HERE. `min(10px, 5%)`
    // is 10px on a 200px containing block and 5% of it on a 100px one - there is
    // no answer until layout, and §10.11 says so: the computed value of a math
    // function whose percentages did not resolve is the function itself. That is
    // `unresolved`, NOT an error, and the difference is a declaration kept versus
    // a declaration deleted. `min(10%, 20%)` is decidable and is not affected.
    [[nodiscard]] std::optional<term> comparison(compare kind) {
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
            best = kind == compare::smallest ? std::min(best, v) : std::max(best, v);
        }
        return with_scalar(args->front(), best);
    }

    // clamp( [<calc-sum> | none], <calc-sum>, [<calc-sum> | none] )
    //
    // §10.3, and it has its own function rather than a third case of the one
    // above because of `none`: EITHER BOUND MAY BE ABSENT, and an absent one is
    // not a missing argument but an unbounded side. `clamp(none, 33px, 30px)` is
    // `min(33px, 30px)` and is 30px, which is what `clamp-length-serialize` asks
    // for six times; before this it was a syntax error and the declaration went.
    //
    // An absent bound is spelled as the infinity it means, which keeps the NaN
    // rule and the low-beats-high rule below in one place each.
    [[nodiscard]] std::optional<term> clamping() {
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
        return with_scalar(*middle, std::max(bound[0], std::min(bound[1], bound[2])));
    }

    // round( <rounding-strategy>?, A, B )
    [[nodiscard]] std::optional<term> rounding() {
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
        const std::optional<std::vector<term>> args = arguments(2, 2);
        if (!args) { return std::nullopt; }
        if (!uniform(*args)) { return std::nullopt; }
        return with_scalar(args->front(),
                           round_one(how, scalar_of((*args)[0]), scalar_of((*args)[1])));
    }

    [[nodiscard]] std::optional<term> stepped(bool truncating) {
        ++at_;
        const std::optional<std::vector<term>> args = arguments(2, 2);
        if (!args) { return std::nullopt; }
        if (!uniform(*args)) { return std::nullopt; }
        const double a = scalar_of((*args)[0]);
        const double b = scalar_of((*args)[1]);
        return with_scalar(args->front(), truncating ? rem_one(a, b) : mod_one(a, b));
    }

    // §10.7. `abs()` keeps its argument's type and `sign()` throws it away: the
    // sign of a length is a NUMBER, which is what makes `calc(1px * sign(1em -
    // 10px))` the idiom the corpus tests it with.
    [[nodiscard]] std::optional<term> sign_or_abs(bool want_sign) {
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

    // progress( [no-clamp]? A, B, C ), CSS Values 5 §progress. Three arguments
    // of ONE type and a <number> out - the fraction of the way A lies from B to
    // C - with the keyword, when it is there, sitting before the first argument
    // and taking no comma of its own.
    [[nodiscard]] std::optional<term> progress_of() {
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
        for (const term & one : *args) {
            if (one.type != args->front().type || one.has_percent != args->front().has_percent) {
                return fail();
            }
        }
        for (const term & one : *args) {
            if (!is_scalar(one)) { return unresolvable(); }
        }
        term out;
        out.value = progress_one(clamped, scalar_of((*args)[0]), scalar_of((*args)[1]),
                                 scalar_of((*args)[2]));
        return out;
    }

    [[nodiscard]] std::optional<term> hypot_of() {
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

    // The functions whose arguments and answer are all <number>. `fallback` is
    // the second argument's default, which only `log()` has one of.
    template <typename Fn>
    [[nodiscard]] std::optional<term> numeric(std::size_t min, std::size_t max, Fn && fn,
                                              double fallback = 0.0) {
        ++at_;
        const std::optional<std::vector<term>> args = arguments(min, max);
        if (!args) { return std::nullopt; }
        for (const term & one : *args) {
            if (!one.is_number()) { return fail(); }
        }
        const double b = args->size() > 1 ? (*args)[1].value : fallback;
        term out;
        out.value = fn(args->front().value, b);
        return out;
    }

    // sin/cos/tan take an <angle> OR a <number> read as radians, and answer a
    // <number>. `sin(30deg + 1.0471967rad)` mixing the two in one argument is
    // the corpus's case and it works because the sum is already in degrees.
    template <typename Fn> [[nodiscard]] std::optional<term> trig(Fn && fn) {
        ++at_;
        const std::optional<std::vector<term>> args = arguments(1, 1);
        if (!args) { return std::nullopt; }
        const term & one = args->front();
        if (one.has_percent) { return unresolvable(); }
        double radians = 0.0;
        if (one.is_number()) {
            radians = one.value;
        } else if (one.type == numeric_type::angle) {
            radians = one.value * std::numbers::pi / 180.0;
        } else {
            return fail();
        }
        term out;
        out.value = fn(radians);
        return out;
    }

    // ...and the inverses go the other way: <number> in, <angle> out, in the
    // canonical degrees.
    template <typename Fn>
    [[nodiscard]] std::optional<term> inverse_trig(std::size_t arity, Fn && fn) {
        ++at_;
        const std::optional<std::vector<term>> args = arguments(arity, arity);
        if (!args) { return std::nullopt; }
        // atan2() takes two of ANY one family - two lengths are as meaningful as
        // two numbers, because only their ratio matters.
        if (arity == 2) {
            if (!uniform(*args)) { return std::nullopt; }
        } else if (!args->front().is_number()) {
            return fail();
        }
        const double a = arity == 2 ? scalar_of((*args)[0]) : args->front().value;
        const double b = arity == 2 ? scalar_of((*args)[1]) : 0.0;
        term out;
        out.type = numeric_type::angle;
        out.value = fn(a, b) * 180.0 / std::numbers::pi;
        return out;
    }

    [[nodiscard]] std::optional<term> fail() {
        ok_ = false;
        return std::nullopt;
    }

    // `fail()` for the one caller whose return type is a list rather than a term.
    [[nodiscard]] std::optional<std::vector<term>> no_args() {
        ok_ = false;
        return std::nullopt;
    }

    // Well formed, and without an answer here. It stops the parse like an error
    // does - there is nothing to carry upwards - but `run()` reads this latch
    // first, so the caller is told to keep the text rather than to drop it.
    [[nodiscard]] std::optional<term> unresolvable() {
        unresolved_ = true;
        return std::nullopt;
    }

    const token_stream & t_;
    const length_context & ctx_;
    basis basis_ = basis::against_context;
    math_outcome outcome_ = math_outcome::invalid;
    std::size_t at_ = 0;
    bool ok_ = true;
    bool unresolved_ = false;
    // Whether a <percentage-token> was read ANYWHERE in the expression, which is
    // not the same question as whether the ANSWER carries one: `sign(50%)` is a
    // plain number and has nothing left to resolve, but the percentage was still
    // written and still had to mean something.
    bool saw_percent_ = false;
};

} // namespace

namespace detail {

// One expression with NO bases at all - which is what a specified value is
// written against - and its answer as a term rather than as a `calc_result`,
// because a sum of units that could not be added has no single magnitude.
[[nodiscard]] std::pair<math_outcome, term> evaluate_symbolic(std::string_view expression) {
    const token_stream tokens = tokenize(expression);
    const length_context none; // deliberately unused: nothing is measured here
    evaluator run{tokens, none, basis::symbolic};
    return run.run_symbolic();
}

} // namespace detail

math_answer evaluate_math(std::string_view expression, const length_context & ctx) {
    const token_stream tokens = tokenize(expression);
    evaluator run{tokens, ctx};
    return run.run();
}

std::optional<calc_result> evaluate_calc(std::string_view expression, const length_context & ctx) {
    const math_answer answer = evaluate_math(expression, ctx);
    if (answer.outcome != math_outcome::resolved || answer.value.is_number) { return std::nullopt; }
    return answer.value;
}

} // namespace ctbrowser::style::css
