//===- internal.hpp - Evaluator internals --------------------- -*- C++ -*-===//
//
// Private to lib/Style/css/calc/evaluator/: the term algebra (arithmetic.cpp),
// the non-linear functions of CSS Values 4 §10.4-§10.8 (functions.cpp), and the
// recursive-descent evaluator over the token stream that evaluate_math and the
// symbolic evaluation of a specified value both use.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../internal.hpp"

#include <ctbrowser/core/depth.hpp>

namespace ctbrowser::style::css::evaluator_detail {

using namespace detail;

/// Ordering helpers for min()/max() that treat -0 as less than +0, which
/// std::min/std::max do not (CSS Values 4 keeps the signed zeros distinct).
[[nodiscard]] bool less(double a, double b) noexcept;

[[nodiscard]] double smaller(double a, double b) noexcept;

[[nodiscard]] double larger(double a, double b) noexcept;

/// Whether a term's whole magnitude is in one slot (`10px` or `10%`, not
/// `calc(10px + 10%)` or a symbolic sum), so a non-linear function applies.
[[nodiscard]] bool is_scalar(const term & t) noexcept;

/// The magnitude of a scalar term: its percentage if it has one, else its value.
[[nodiscard]] double scalar_of(const term & t) noexcept;

/// A copy of `like` with its scalar magnitude replaced by `v`.
[[nodiscard]] term with_scalar(const term & like, double v);

/// `a + b`, or `a - b` when `subtract`; `nullopt` when the types differ.
[[nodiscard]] std::optional<term> add(const term & a, const term & b, bool subtract);

/// What a product or a quotient came to. There are three answers, as for the
/// evaluator itself: a term, a type error (no value, not unresolved), or an
/// operation that is well formed but has no magnitude here: `1em * 1em` before
/// a font size exists, `10% * 10%` before a containing block does.
struct arithmetic {
    std::optional<term> value;
    bool unresolved = false;
};

/// Whether a term's plain magnitude is absent rather than zero (it is all
/// percentage or symbols), so scaling it by infinity must not produce a NaN.
[[nodiscard]] bool no_plain_part(const term & t) noexcept;

/// `dim` multiplied by the number `scale`.
[[nodiscard]] term scaled(const term & dim, double scale);

/// Whether a symbol key names a function (`sibling-index()`, calc-size()'s
/// `size`, a colour channel) rather than a unit.
[[nodiscard]] bool function_symbol(std::string_view key) noexcept;

/// The idents the symbolic evaluator currently reads as <number> terms, set by
/// number_symbols_scope in ../internal.hpp.
extern thread_local std::span<const std::string_view> number_symbols;

/// Whether a term consists only of function symbols.
[[nodiscard]] bool function_only(const term & t) noexcept;

[[nodiscard]] arithmetic multiply(const term & a, const term & b);

[[nodiscard]] arithmetic divide(const term & a, const term & b);

// --- The non-linear functions, CSS Values 4 §10.4-§10.8 ----------------------

/// The <rounding-strategy> of round().
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

/// What a dimension is measured against. The second answer is what a specified
/// value needs: there are no bases yet when one is computed, so `1em` and
/// `1cqw` are terms in their own right rather than numbers of pixels.
enum class basis : std::uint8_t {
    /// `ctx` supplies a font size and a viewport, and every length becomes
    /// pixels. This is the computed-value evaluation and the only one that
    /// answers with a `calc_result`.
    against_context,
    /// Nothing is supplied and nothing is guessed. `calc(10px + 1vmin + 10%)`
    /// is three terms, and printing them in §10.13's order is the whole answer.
    symbolic,
};

/// A recursive-descent parser over the token stream, one instance per
/// expression. `ok_` latches false on the first error so every level can stop
/// checking.
class evaluator {
public:
    evaluator(const token_stream & tokens, const length_context & ctx,
              basis measure = basis::against_context, bool size_symbol = false)
        : t_(tokens), ctx_(ctx), basis_(measure), size_symbol_(size_symbol) {}

    /// Evaluates against the context, answering with a `calc_result`.
    [[nodiscard]] math_answer run() {
        const std::optional<term> value = settle();
        if (!value) { return math_answer{outcome_, {}, randoms_}; }
        calc_result out;
        // A number is an answer: CSS Values 3 §8.1 says a math function may
        // resolve to a <number>. Whether the property accepts one is a separate
        // question, asked through math_context.
        out.type = value->type();
        out.is_number = value->is_number();
        out.px = value->value;
        out.percent = value->percent;
        out.has_percent = value->has_percent;
        return math_answer{math_outcome::resolved, out, randoms_};
    }

    /// The symbolic evaluation's answer, which is the term itself: a
    /// `calc_result` carries one magnitude and a percentage and cannot hold
    /// `calc(10% + 10px + 1vmin)`.
    [[nodiscard]] std::pair<math_outcome, term> run_symbolic() {
        const std::optional<term> value = settle();
        if (!value) {
            return {outcome_, term{}};
        }
        return {math_outcome::resolved, *value};
    }

private:
    /// The whole expression, parsed and ruled on. `nullopt` leaves the reason in
    /// `outcome_`, which both entry points above report.
    [[nodiscard]] std::optional<term> settle() {
        const std::optional<term> value = sum();
        skip_whitespace();
        // Unresolved wins over invalid. A comparison that could not be decided
        // here stopped the parse the same way an error does, so the latch must
        // be read before the missing value is: `min(10px, 5%)` is a valid
        // declaration, and reporting it as a syntax error would delete it.
        if (unresolved_) {
            outcome_ = math_outcome::unresolved;
            return std::nullopt;
        }
        // A trailing token means the expression did not consume its input
        // (`calc(1px 2px)`), which is an error and not a partial answer.
        if (!ok_ || !value || !at_end()) {
            outcome_ = math_outcome::invalid;
            return std::nullopt;
        }
        // A type no property takes is a syntax error. Typed arithmetic allows
        // `2px * 1px` or `20 / 0.75rem` mid-expression, but an area or an
        // inverse length as the whole answer is invalid (WPT
        // calc-unit-analysis).
        if (!value->simple()) {
            outcome_ = math_outcome::invalid;
            return std::nullopt;
        }
        // A number that still carries a percentage (`10% / 1px`) is a ratio of
        // the basis to a length, and there is no basis here: well formed, but
        // not answerable until layout.
        if (value->is_number() && value->has_percent) {
            outcome_ = math_outcome::unresolved;
            return std::nullopt;
        }
        // A percentage needs a basis (CSS Values 4 §10.11's calculation
        // context), and the only one this engine supplies is a length. A
        // percentage in an expression that answers with an angle, time,
        // frequency or resolution (`calc(sign(50%) * 1s)`) is therefore a
        // syntax error (WPT percentage-without-context). A <number> answer is
        // allowed: in `progress(1%, (10% - 10%), 100%)` or
        // `calc(1px * pow(tan(atan2(50%, 1px)), 1))` the percentages sit in a
        // length context the property does supply.
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
    /// End of input closes every open function (CSS Syntax 3 §5.4.9), so
    /// `calc(min(1em, 21px) * 2` with no closing paren is still valid.
    [[nodiscard]] bool at_close() const noexcept {
        return peek().type == token_type::close_paren || at_end();
    }
    void take_close() noexcept {
        if (peek().type == token_type::close_paren) { ++at_; }
    }

    /// sum := product (('+' | '-') product)*
    ///
    /// CSS requires whitespace around `+` and `-` inside a calc, and this gets
    /// it for free by not looking for a sign: the tokenizer folds `-12px` into
    /// one dimension token, so `calc(100% -12px)` presents two lengths with no
    /// operator between them and fails at the trailing-token check, as in
    /// Chrome.
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

    /// product := term (('*' | '/') term)*
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

    /// term := <number> | <dimension> | <percentage> | <constant>
    ///       | '(' sum ')' | <math-function>
    ///
    /// The only place the evaluator recurses (through `(` and every math
    /// function), so the nesting cap is here: past max_depth the expression is
    /// a syntax error, not a stack overflow.
    static constexpr int max_depth = 128;

    [[nodiscard]] std::optional<term> single() {
        if (depth_ >= max_depth) { return fail(); }
        const depth_guard level{depth_};
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
            // With a basis it is a length, and every non-linear function that
            // could not be applied to `10%` applies to `7.5px`.
            if (basis_ == basis::against_context && ctx_.percent_basis) {
                out.set_type(numeric_type::length);
                out.value = tok.number / 100.0 * static_cast<double>(*ctx_.percent_basis);
                return out;
            }
            saw_percent_ = true;
            // A percentage has no type of its own until the property says what
            // it is a percentage of, and every property this engine resolves
            // one for takes a length. So it travels as a length carrying an
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
                // for (`1lh`, `1cqw`) is unresolved, not invalid. A typo is
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

    /// The numeric constants, CSS Values 4 §10.9. `infinity` and `NaN` are not
    /// <number-token>s and could not be, which is why they are keywords and may
    /// appear only inside a math function: `opacity: infinity` is a syntax
    /// error and `opacity: calc(infinity)` is a value.
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
            // `size` inside calc-size(): the basis, a <length> with no
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

    /// The body of a `(` or `calc(`, up to its matching close paren.
    [[nodiscard]] std::optional<term> nested() {
        ++at_; // The opener.
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

    /// A comma-separated argument list, the function token already consumed and
    /// the close paren consumed here. An arity outside [min, max] is a syntax
    /// error rather than an undecidable comparison: `round(nearest, 1px)` has
    /// no step and `round(nearest, 1px, 1px, 1px)` has two, and WPT lists both
    /// as invalid.
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

    /// Whether every argument has the same type and is a single magnitude of the
    /// same shape. The two tests fail differently: `min(1px, 2)` compares a
    /// length with a number and is a type error (`ok_` cleared), while
    /// `min(10px, 5%)` compares two lengths whose relative size depends on a
    /// containing block and is merely undecidable here (`unresolved_` set). The
    /// first deletes the declaration; the second keeps it verbatim, which CSS
    /// Values 4 §10.11 says is its computed value.
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
        // A percentage cannot be compared with anything, including another
        // percentage. `min(1%, 2%)` looks decidable and is not: a percentage
        // resolves against a basis that may be negative, and then 2% is the
        // smaller. WPT minmax-percentage-serialize expects
        // `calc(min(1%, 2%) + max(3%, 4%) + 10%)` back with both functions
        // still in it. One argument is not a comparison and is unaffected,
        // which keeps `min(1%)` simplifying to `calc(1%)`.
        if (args.size() > 1 && args.front().has_percent) {
            unresolved_ = true;
            return false;
        }
        return true;
    }

    [[nodiscard]] std::optional<term> math_function();

    /// min( sum [, sum]* ) | max( sum [, sum]* )
    ///
    /// CSS Values 4 §10.3. Two things make this more than a fold over `sum()`.
    ///
    /// Every argument must be the same type: `min(1px, 2)` compares a length
    /// with a number and has no meaning, exactly as `1px + 2` has none.
    ///
    /// A percentage makes the comparison undecidable here. `min(10px, 5%)` is
    /// 10px on a 200px containing block and 5px on a 100px one, and even
    /// `min(1%, 2%)` has no answer because the basis may be negative. CSS
    /// Values 4 §10.11 makes the computed value the function itself, so this is
    /// `unresolved` (the declaration is kept), not an error. A single argument
    /// is not a comparison and is returned unchanged.
    [[nodiscard]] std::optional<term> comparison(compare kind);

    /// clamp( [<calc-sum> | none], <calc-sum>, [<calc-sum> | none] )
    ///
    /// CSS Values 4 §10.3. It has its own function rather than a third case of
    /// comparison() because of `none`: either bound may be absent, which means
    /// an unbounded side, not a missing argument. `clamp(none, 33px, 30px)` is
    /// `min(33px, 30px)`, so 30px (WPT clamp-length-serialize).
    ///
    /// An absent bound is spelled as the infinity it means, which keeps the NaN
    /// rule and the low-beats-high rule in one place each.
    [[nodiscard]] std::optional<term> clamping();

    /// round( <rounding-strategy>?, A, B )
    [[nodiscard]] std::optional<term> rounding();

    /// mod( A, B ) when `truncating` is false, rem( A, B ) when it is true.
    [[nodiscard]] std::optional<term> stepped(bool truncating);

    /// abs() and sign(), CSS Values 4 §10.7. `abs()` keeps its argument's type
    /// and `sign()` drops it: the sign of a length is a number, which makes
    /// `calc(1px * sign(1em - 10px))` a common idiom.
    [[nodiscard]] std::optional<term> sign_or_abs(bool want_sign);

    /// progress( [no-clamp]? A, B, C ), CSS Values 5 §progress. Three arguments
    /// of one type and a <number> out (the fraction of the way A lies from B to
    /// C), with the keyword, when present, before the first argument and taking
    /// no comma of its own.
    [[nodiscard]] std::optional<term> progress_of();

    /// random( <random-value-sharing>? , A, B, [by]? step? ), CSS Values 5
    /// §random. The sharing options come first and end at the first comma:
    /// `fixed <number>` names the base outright; otherwise a `<dashed-ident>`,
    /// `element-scoped` and `property-index-scoped` say what the base is keyed
    /// on and `random_base` derives it. Then two values of one type, and an
    /// optional step of the same type.
    [[nodiscard]] std::optional<term> random_of();

    /// calc-mix( [ <calc-sum> <percentage>? ]# ), CSS Values 5 §calc-mix: a
    /// weighted sum, its weights normalized the way color-mix() normalizes
    /// them. Each weight is clamped to [0%, 100%]; the omitted ones share what
    /// is left below 100% equally; a total over 100% is scaled down to it and a
    /// total under is not scaled up. All weights zero gives the zero of the
    /// first value's type (WPT calc-mix-serialize, calc-mix-computed).
    ///
    /// A specified value folds only when everything in it has a magnitude.
    /// With a `3em`, a `sibling-index()` or a weight that is itself unresolved,
    /// §10.12 keeps the function (normalized, which simplify.cpp writes),
    /// because `calc-mix(10px 50%, 3em 50%)` is not `calc(5px + 1.5em)` to the
    /// page.
    [[nodiscard]] std::optional<term> calc_mix();

    /// hypot( A [, B]* ), CSS Values 4 §10.6.
    [[nodiscard]] std::optional<term> hypot_of();

    /// The functions whose arguments and answer are all <number>. `fallback` is
    /// the second argument's default, which only `log()` has.
    template <typename Fn>
    [[nodiscard]] std::optional<term> numeric(std::size_t min, std::size_t max, Fn && fn,
                                              double fallback = 0.0) {
        ++at_;
        const std::optional<std::vector<term>> args = arguments(min, max);
        if (!args) { return std::nullopt; }
        for (const term & one : *args) {
            if (!one.is_number()) { return fail(); }
            // A number still carrying a percentage (`pow(50% / 1px, 1)`) has
            // no magnitude until layout, and neither has a symbolic one:
            // `sqrt(sibling-index())` in a specified value is not `sqrt(0)`.
            if (one.has_percent || !one.symbols.empty()) { return unresolvable(); }
        }
        const double b = args->size() > 1 ? (*args)[1].value : fallback;
        term out;
        out.value = fn(args->front().value, b);
        return out;
    }

    /// sin/cos/tan take an <angle> or a <number> read as radians, and answer a
    /// <number>. `sin(30deg + 1.0471967rad)` mixes two angle units in one
    /// argument and works because the sum is already in degrees.
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

    /// asin/acos/atan/atan2 go the other way: <number> in, <angle> out, in the
    /// canonical degrees.
    template <typename Fn>
    [[nodiscard]] std::optional<term> inverse_trig(std::size_t arity, Fn && fn) {
        ++at_;
        const std::optional<std::vector<term>> args = arguments(arity, arity);
        if (!args) { return std::nullopt; }
        // atan2() takes two values of any one type: two lengths are as
        // meaningful as two numbers, because only their ratio matters.
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

    /// Records a syntax error and stops the parse.
    [[nodiscard]] std::optional<term> fail() {
        ok_ = false;
        return std::nullopt;
    }

    /// `fail()` for the one caller whose return type is a list rather than a term.
    [[nodiscard]] std::optional<std::vector<term>> no_args() {
        ok_ = false;
        return std::nullopt;
    }

    /// Well formed, but without an answer here. It stops the parse like an error
    /// does, but `settle()` checks this latch first, so the caller is told to
    /// keep the text rather than drop it.
    [[nodiscard]] std::optional<term> unresolvable() {
        unresolved_ = true;
        return std::nullopt;
    }

    const token_stream & t_;
    const length_context & ctx_;
    basis basis_ = basis::against_context;
    bool size_symbol_ = false; ///< `size` is a term: the calculation of a calc-size().
    math_outcome outcome_ = math_outcome::invalid;
    std::size_t at_ = 0;
    int depth_ = 0; ///< Nesting depth of single(), capped at max_depth.
    bool ok_ = true;
    bool unresolved_ = false;
    std::uint32_t randoms_ = 0; ///< The random() functions read so far.
    /// Whether a <percentage-token> was read anywhere in the expression, which
    /// is not the same as whether the answer carries one: `sign(50%)` is a
    /// plain number with nothing left to resolve, but the percentage was still
    /// written and still had to mean something.
    bool saw_percent_ = false;
};

} // namespace ctbrowser::style::css::evaluator_detail
