#pragma once

// calc() - the term algebra, the non-linear functions of CSS Values 4
// §10.4-§10.8, the recursive-descent evaluator over the token stream, and its
// entry points: evaluate_math and the symbolic evaluation a specified value
// uses.

#include "../internal.hpp"

namespace ctbrowser::style::css::evaluator_detail {

using namespace detail;

[[nodiscard]] bool less(double a, double b) noexcept;

[[nodiscard]] double smaller(double a, double b) noexcept;

[[nodiscard]] double larger(double a, double b) noexcept;

[[nodiscard]] bool is_scalar(const term & t) noexcept;

[[nodiscard]] double scalar_of(const term & t) noexcept;

[[nodiscard]] term with_scalar(const term & like, double v);

[[nodiscard]] std::optional<term> add(const term & a, const term & b, bool subtract);

// What a product or a quotient came to. THREE ANSWERS, like the evaluator's
// own: a term, a type error, or an operation that is well formed and has no
// magnitude here - `1em * 1em` before a font size exists, `10% * 10%` before
// a containing block does.
struct arithmetic {
    std::optional<term> value;
    bool unresolved = false;
};

[[nodiscard]] bool no_plain_part(const term & t) noexcept;

[[nodiscard]] term scaled(const term & dim, double scale);

[[nodiscard]] bool function_symbol(std::string_view key) noexcept;

// The idents the symbolic evaluator currently reads as <number> terms - see
// number_symbols_scope in internal.hpp.
extern thread_local std::span<const std::string_view> number_symbols;

[[nodiscard]] bool function_only(const term & t) noexcept;

[[nodiscard]] arithmetic multiply(const term & a, const term & b);

[[nodiscard]] arithmetic divide(const term & a, const term & b);

// --- the non-linear functions, CSS Values 4 §10.4-§10.8 ------------------

enum class round_to : std::uint8_t {
    nearest,
    up,
    down,
    to_zero
};

[[nodiscard]] double round_one(round_to how, double a, double b);

[[nodiscard]] double mod_one(double a, double b);

[[nodiscard]] double rem_one(double a, double b);

[[nodiscard]] double progress_one(bool clamped, double a, double b, double c);

[[nodiscard]] double random_one(double base, double a, double b, std::optional<double> step);

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
              basis measure = basis::against_context, bool size_symbol = false)
        : t_(tokens), ctx_(ctx), basis_(measure), size_symbol_(size_symbol) {}

    [[nodiscard]] math_answer run() {
        const std::optional<term> value = settle();
        if (!value) { return math_answer{outcome_, {}, randoms_}; }
        calc_result out;
        // A NUMBER IS AN ANSWER. CSS Values 3 §8.1 says a math function may
        // resolve to a <number>; whether the PROPERTY accepts one is a separate
        // question, and math_context is where it is asked.
        out.type = value->type();
        out.is_number = value->is_number();
        out.px = value->value;
        out.percent = value->percent;
        out.has_percent = value->has_percent;
        return math_answer{math_outcome::resolved, out, randoms_};
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
        // A TYPE NO PROPERTY TAKES IS A SYNTAX ERROR. Typed arithmetic lets
        // `2px * 1px` and `20 / 0.75rem` exist mid-expression; as the WHOLE
        // answer an area and an inverse length are what
        // `calc-unit-analysis` lists as invalid, and still are.
        if (!value->simple()) {
            outcome_ = math_outcome::invalid;
            return std::nullopt;
        }
        // A NUMBER THAT STILL CARRIES A PERCENTAGE - `10% / 1px` - is a ratio
        // of the basis to a length, and there is no basis here. Well formed,
        // and not answerable until layout.
        if (value->is_number() && value->has_percent) {
            outcome_ = math_outcome::unresolved;
            return std::nullopt;
        }
        if (saw_percent_ && value->type() != numeric_type::number &&
            value->type() != numeric_type::length) {
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
            arithmetic result = times ? multiply(*left, *right) : divide(*left, *right);
            if (result.unresolved) { return unresolvable(); }
            if (!result.value) { return fail(); }
            left = std::move(result.value);
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
            term out;
            // WITH A BASIS IT IS A LENGTH, and every non-linear function that
            // could not be applied to `10%` applies to `7.5px`.
            if (basis_ == basis::against_context && ctx_.percent_basis) {
                out.set_type(numeric_type::length);
                out.value = tok.number / 100.0 * static_cast<double>(*ctx_.percent_basis);
                return out;
            }
            saw_percent_ = true;
            // A percentage has no type of its own until the property says what it
            // is a percentage OF, and every property this engine resolves one
            // for takes a length. So it travels as a length carrying an
            // unresolved part, which is exactly what `calc(100% - 12px)` needs.
            out.set_type(numeric_type::length);
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
        } else if (size_symbol_ && basis_ == basis::symbolic && ascii_iequals(name, "size")) {
            // `size` INSIDE calc-size(): the basis, a <length> with no
            // magnitude until layout (CSS Values 5 §calc-size).
            out.set_type(numeric_type::length);
            add_symbol(out, "size", 1.0);
        } else if (basis_ == basis::symbolic && ascii_iequals_any(name, number_symbols)) {
            // A relative colour's channel keyword: a <number> with no
            // magnitude until the origin colour is known.
            add_symbol(out, "$" + ascii_lower_copy(name), 1.0);
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
            if (one.dims != args.front().dims) {
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

    [[nodiscard]] std::optional<term> math_function();

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
    [[nodiscard]] std::optional<term> comparison(compare kind);

    // clamp( [<calc-sum> | none], <calc-sum>, [<calc-sum> | none] )
    //
    // §10.3, and it has its own function rather than a third case of the one
    // above because of `none`: EITHER BOUND MAY BE ABSENT, and an absent one is
    // not a missing argument but an unbounded side. `clamp(none, 33px, 30px)` is
    // `min(33px, 30px)` and is 30px, which is what `clamp-length-serialize` asks.
    //
    // An absent bound is spelled as the infinity it means, which keeps the NaN
    // rule and the low-beats-high rule below in one place each.
    [[nodiscard]] std::optional<term> clamping();

    // round( <rounding-strategy>?, A, B )
    [[nodiscard]] std::optional<term> rounding();

    [[nodiscard]] std::optional<term> stepped(bool truncating);

    // §10.7. `abs()` keeps its argument's type and `sign()` throws it away: the
    // sign of a length is a NUMBER, which is what makes `calc(1px * sign(1em -
    // 10px))` the idiom the corpus tests it with.
    [[nodiscard]] std::optional<term> sign_or_abs(bool want_sign);

    // progress( [no-clamp]? A, B, C ), CSS Values 5 §progress. Three arguments
    // of ONE type and a <number> out - the fraction of the way A lies from B to
    // C - with the keyword, when it is there, sitting before the first argument
    // and taking no comma of its own.
    [[nodiscard]] std::optional<term> progress_of();

    // random( <random-value-sharing>? , A, B, [by]? step? ), CSS Values 5
    // §random. The sharing options come first and end at the first comma:
    // `fixed <number>` names the base outright, otherwise a `<dashed-ident>`,
    // `element-scoped` and `property-index-scoped` say what the base is keyed
    // on and `random_base` derives it. Then two values of one type, and an
    // optional step of the same type.
    [[nodiscard]] std::optional<term> random_of();

    // calc-mix( [ <calc-sum> <percentage>? ]# ), CSS Values 5 §calc-mix: a
    // weighted sum, its weights normalised the way color-mix() normalises
    // them. Each weight is clamped to [0%, 100%]; the omitted ones share what
    // is left below 100% equally; a total over 100% is scaled down to it and a
    // total under is not scaled up. Weights all zero is the zero of the first
    // value's kind (calc-mix-serialize, calc-mix-computed).
    //
    // A SPECIFIED VALUE FOLDS ONLY WHEN EVERYTHING IN IT HAS A MAGNITUDE. With
    // a `3em`, a `sibling-index()` or a weight that is itself unresolved, §10.12
    // keeps the function - normalised, which simplify.cpp writes - because
    // `calc-mix(10px 50%, 3em 50%)` is not `calc(5px + 1.5em)` to the page.
    [[nodiscard]] std::optional<term> calc_mix();

    [[nodiscard]] std::optional<term> hypot_of();

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
            // A number still carrying a percentage - `pow(50% / 1px, 1)` - has
            // no magnitude until layout, and neither has a symbolic one:
            // `sqrt(sibling-index())` in a specified value is not `sqrt(0)`.
            if (one.has_percent || !one.symbols.empty()) { return unresolvable(); }
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
        if (one.has_percent || !one.symbols.empty()) { return unresolvable(); }
        double radians = 0.0;
        if (one.is_number()) {
            radians = one.value;
        } else if (one.simple() && one.type() == numeric_type::angle) {
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
        } else if (!args->front().symbols.empty()) {
            return unresolvable();
        }
        const double a = arity == 2 ? scalar_of((*args)[0]) : args->front().value;
        const double b = arity == 2 ? scalar_of((*args)[1]) : 0.0;
        term out;
        out.set_type(numeric_type::angle);
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
    bool size_symbol_ = false; // `size` is a term: the calculation of a calc-size()
    math_outcome outcome_ = math_outcome::invalid;
    std::size_t at_ = 0;
    bool ok_ = true;
    bool unresolved_ = false;
    std::uint32_t randoms_ = 0; // the random() functions read so far
    // Whether a <percentage-token> was read ANYWHERE in the expression, which is
    // not the same question as whether the ANSWER carries one: `sign(50%)` is a
    // plain number and has nothing left to resolve, but the percentage was still
    // written and still had to mean something.
    bool saw_percent_ = false;
};

} // namespace ctbrowser::style::css::evaluator_detail
