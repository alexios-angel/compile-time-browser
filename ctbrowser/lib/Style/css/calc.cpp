#include <ctbrowser/style/css/calc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/token.hpp>

namespace ctbrowser::style::css {
namespace {

// One operand mid-expression. CSS Values 4 §10.2 gives calc a type algebra over
// SIX base types and this is the whole of it: a tag saying which family, a value
// already converted to that family's CANONICAL unit, and a percentage that could
// not be resolved here. Keeping the tag separate from "the value happens to be
// zero" is what makes `1px + 2` an error rather than 3px, and what makes
// `1s + 1deg` one too.
struct term {
    numeric_type type = numeric_type::number;
    double value = 0.0;
    double percent = 0.0;
    bool has_percent = false;
    // THE DIMENSIONS THAT COULD NOT BE ADDED TOGETHER, one entry per unit, in
    // the order they were first written.
    //
    // Empty in the ordinary evaluation, where every unit has a basis and every
    // length is a number of pixels. A SPECIFIED value has no bases at all, and
    // there `calc(1em + 1cap)` is two terms for good - which is not a failure to
    // simplify but the simplified form itself, CSS Values 4 §10.12. Sorted only
    // when it is printed, because §10.13's order is a serialisation rule and the
    // arithmetic does not care.
    std::vector<std::pair<std::string, double>> symbols;

    [[nodiscard]] bool is_number() const noexcept { return type == numeric_type::number; }
};

// `unit`'s coefficient in `into`, created at the end if it is not there yet.
// Linear because a sum has a handful of distinct units and the write order is
// what keeps a serialisation stable before it is sorted.
void add_symbol(term & into, std::string_view unit, double coefficient) {
    for (auto & [name, value] : into.symbols) {
        if (name == unit) {
            value += coefficient;
            return;
        }
    }
    into.symbols.emplace_back(unit, coefficient);
}

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

// --- the unit table ------------------------------------------------------
//
// EVERY UNIT THE SPECIFICATION NAMES, whether or not this engine can resolve it,
// because "I have no basis for `cqw`" and "`cqw` is not a unit" are different
// answers and only the second may delete a declaration. A known unit with no
// basis makes the expression `unresolved` - well formed, no answer here - which
// is the same third outcome `min(10px, 5%)` already used.
constexpr std::string_view known_units[] = {
    // <length>, CSS Values 4 §6
    "em", "rem", "ex", "rex", "ch", "rch", "cap", "rcap", "ic", "ric", "lh", "rlh", "vw", "vh",
    "vi", "vb", "vmin", "vmax", "svw", "svh", "svi", "svb", "svmin", "svmax", "lvw", "lvh", "lvi",
    "lvb", "lvmin", "lvmax", "dvw", "dvh", "dvi", "dvb", "dvmin", "dvmax", "cqw", "cqh", "cqi",
    "cqb", "cqmin", "cqmax", "cm", "mm", "q", "in", "pt", "pc", "px",
    // <angle>, <time>, <frequency>, <resolution>, <flex>
    "deg", "grad", "rad", "turn", "s", "ms", "hz", "khz", "dpi", "dpcm", "dppx", "x", "fr"};

[[nodiscard]] bool is_known_unit(std::string_view unit) noexcept {
    for (const std::string_view one : known_units) {
        if (ascii_iequals(one, unit)) { return true; }
    }
    return false;
}

// THE UNITS WHOSE VALUE IS THE SAME EVERYWHERE. An absolute length, an angle, a
// time, a frequency and a resolution all convert to their canonical unit by a
// constant; `em`, `vw`, `lh`, `cqw`, `fr` and `%` do not, and a SPECIFIED value
// is written before any of their bases exist.
[[nodiscard]] bool context_free_unit(std::string_view unit) noexcept {
    static constexpr std::string_view units[] = {"px",  "cm",   "mm",   "q",    "in", "pt", "pc",
                                                 "deg", "grad", "rad",  "turn", "s",  "ms", "hz",
                                                 "khz", "dpi",  "dpcm", "dppx", "x"};
    for (const std::string_view one : units) {
        if (ascii_iequals(one, unit)) { return true; }
    }
    return false;
}

// One dimension in its family's canonical unit. `nullopt` means this file has no
// basis for it; `is_known_unit` is what tells that from a typo.
[[nodiscard]] std::optional<term> canonical_term(double value, std::string_view unit,
                                                 const length_context & ctx) {
    term out;
    // The four families with a fixed conversion. CSS Values 4 §6.4-§6.7: the
    // canonical units are deg, s, Hz and dppx, and every member converts by a
    // constant, so there is nothing context-dependent about any of them.
    struct fixed {
        std::string_view unit;
        numeric_type type;
        double factor;
    };
    static constexpr fixed table[] = {
        {"deg", numeric_type::angle, 1.0},
        {"grad", numeric_type::angle, 0.9},
        {"rad", numeric_type::angle, 180.0 / std::numbers::pi},
        {"turn", numeric_type::angle, 360.0},
        {"s", numeric_type::time, 1.0},
        {"ms", numeric_type::time, 0.001},
        {"hz", numeric_type::frequency, 1.0},
        {"khz", numeric_type::frequency, 1000.0},
        {"dppx", numeric_type::resolution, 1.0},
        {"x", numeric_type::resolution, 1.0},
        {"dpi", numeric_type::resolution, 1.0 / 96.0},
        {"dpcm", numeric_type::resolution, 2.54 / 96.0},
        // `fr` converts to itself and to nothing else. It is here for its TYPE,
        // not for a basis: what a flex is worth is a grid track sizing question
        // and no expression can answer it, but `1fr + 1fr` is 2fr and `1px +
        // 1fr` is a type error, and both need the family named.
        {"fr", numeric_type::flex, 1.0},
    };
    for (const fixed & one : table) {
        if (ascii_iequals(one.unit, unit)) {
            out.type = one.type;
            out.value = value * one.factor;
            return out;
        }
    }
    // A length goes through the one function that owns the pixel bases, so there
    // is exactly one place `rem` and `vw` are defined - and, deliberately, at the
    // same `float` precision the rest of the cascade folds a plain `1cm` at.
    // `test_math_used` compares the computed value of `min(1cm)` against that of
    // `1cm`, so the two paths agreeing matters far more here than either being
    // exact; widening this one alone would break every such pair. It can become a
    // double the day `folded()` in `style/engine.hpp` folds through
    // `canonical_dimension_text`.
    if (const std::optional<float> px = unit_to_px(static_cast<float>(value), unit, ctx)) {
        out.type = numeric_type::length;
        out.value = *px;
        return out;
    }
    return std::nullopt;
}

// ONE DIMENSION WITH NO BASES AVAILABLE, which is what a SPECIFIED value is
// written against. A unit whose value is the same everywhere folds into its
// family's canonical unit exactly as it always did; one that needs a font size,
// a viewport or a container becomes a term of its OWN, keyed by the unit the
// author wrote. `nullopt` for a typo, which is a syntax error at any stage.
//
// `fr` is here rather than in `context_free_unit` because the two questions
// differ: a flex converts to nothing and never will, so `calc(1fr + 1fr)` may
// not be FOLDED against a basis - but it is still two terms of one unit and
// `calc(2fr)` is their sum.
[[nodiscard]] std::optional<term> symbolic_term(double value, std::string_view unit) {
    if (context_free_unit(unit)) {
        const std::optional<term> fixed = canonical_term(value, unit, length_context{});
        if (!fixed) { return std::nullopt; }
        term out;
        out.type = fixed->type;
        add_symbol(out, canonical_unit(out.type), fixed->value);
        return out;
    }
    if (!is_known_unit(unit)) { return std::nullopt; }
    term out;
    out.type = ascii_iequals(unit, "fr") ? numeric_type::flex : numeric_type::length;
    add_symbol(out, ascii_lower_copy(unit), value);
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
        if (!value) { return math_answer{outcome_, {}}; }
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
        if (!value) { return {outcome_, term{}}; }
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

// Trailing zeros off a double, so a folded `12px` is not `12.000000px`. CSS
// serialisation drops them and so does every engine's getComputedStyle.
[[nodiscard]] std::string format_number(double value) {
    if (value == std::floor(value) && std::fabs(value) < 1e9) {
        return std::to_string(static_cast<long long>(value));
    }
    std::string text = std::to_string(value);
    while (text.size() > 1 && text.back() == '0') { text.pop_back(); }
    if (!text.empty() && text.back() == '.') { text.pop_back(); }
    return text;
}

// One expression with NO bases at all - which is what a specified value is
// written against - and its answer as a term rather than as a `calc_result`,
// because a sum of units that could not be added has no single magnitude.
[[nodiscard]] std::pair<math_outcome, term> evaluate_symbolic(std::string_view expression) {
    const token_stream tokens = tokenize(expression);
    const length_context none; // deliberately unused: nothing is measured here
    evaluator run{tokens, none, basis::symbolic};
    return run.run_symbolic();
}

// THE INSIDE OF A SYMBOLIC calc(), in CSS Values 4 §10.13's order: the
// percentage first, then one term per unit sorted ASCII case-insensitively by
// unit name. `calc(10px + 1vmin + 10%)` is `calc(10% + 10px + 1vmin)` in every
// browser, and `calc-dimension-serialization-order` walks all forty-four
// relative units to say so - `px` among them, in its alphabetical place between
// `lvw` and `rcap` rather than first for being the canonical one.
//
// AN INFINITY OR A NaN MOVES ITS UNIT OUT TO A MULTIPLIER, exactly as
// `serialize_calc` does it and for the same reason: `NaNem` is not a token.
// It is only spellable when the whole sum is that one term - `NaN * 1em + 1px`
// has no canonical form and there is no case for one - so anything else comes
// back EMPTY, which means "print the author's bytes instead" and is never a
// reason to condemn a declaration. A plain number sitting beside the symbols is
// the other such shape, and this file does not build it.
[[nodiscard]] std::string serialize_symbolic(const term & value) {
    if (value.value != 0.0) { return {}; }
    const std::size_t parts = value.symbols.size() + (value.has_percent ? 1 : 0);
    const bool finite =
        std::isfinite(value.percent) && std::ranges::all_of(value.symbols, [](const auto & one) {
            return std::isfinite(one.second);
        });
    if (!finite) {
        if (parts != 1) { return {}; }
        const double lead = value.has_percent ? value.percent : value.symbols.front().second;
        const std::string unit = value.has_percent ? "%" : value.symbols.front().first;
        const std::string word = std::isnan(lead) ? "NaN" : (lead > 0 ? "infinity" : "-infinity");
        return word + " * 1" + unit;
    }
    std::vector<std::pair<std::string, double>> sorted = value.symbols;
    std::ranges::sort(sorted, [](const auto & a, const auto & b) { return a.first < b.first; });
    std::string out;
    // The sign is folded into the operator the way every engine prints it:
    // `calc(100% - 12px)`, never `calc(100% + -12px)`.
    const auto append = [&out](double n, std::string_view unit) {
        if (out.empty()) {
            out += format_number(n);
        } else {
            out += n < 0.0 ? " - " : " + ";
            out += format_number(n < 0.0 ? -n : n);
        }
        out += unit;
    };
    if (value.has_percent) { append(value.percent, "%"); }
    for (const auto & [unit, coefficient] : sorted) { append(coefficient, unit); }
    return out;
}

} // namespace

std::string_view canonical_unit(numeric_type type) noexcept {
    switch (type) {
    case numeric_type::number: return {};
    case numeric_type::length: return "px";
    case numeric_type::angle: return "deg";
    case numeric_type::time: return "s";
    case numeric_type::frequency: return "hz";
    case numeric_type::resolution: return "dppx";
    case numeric_type::flex: return "fr";
    }
    return {};
}

std::optional<float> unit_to_px(float value, std::string_view unit, const length_context & ctx) {
    if (unit.empty()) { return value; } // a plain number in a calc term
    if (ascii_iequals(unit, "px")) { return value; }
    if (ascii_iequals(unit, "em")) { return value * ctx.font_size; }
    if (ascii_iequals(unit, "rem")) { return value * ctx.root_font_size; }
    // ch and ex need font metrics the style engine deliberately cannot see -
    // layout/values.hpp is explicit that measurement is injected - so both take
    // CSS's own fallback of half an em. Bootstrap uses neither.
    if (ascii_iequals(unit, "ch") || ascii_iequals(unit, "ex")) {
        return value * ctx.font_size / 2;
    }
    if (ascii_iequals(unit, "vw")) { return value * ctx.viewport_width / 100.0f; }
    if (ascii_iequals(unit, "vh")) { return value * ctx.viewport_height / 100.0f; }
    if (ascii_iequals(unit, "vmin")) {
        return value * std::min(ctx.viewport_width, ctx.viewport_height) / 100.0f;
    }
    if (ascii_iequals(unit, "vmax")) {
        return value * std::max(ctx.viewport_width, ctx.viewport_height) / 100.0f;
    }
    // The absolute units, all defined against the CSS inch of 96px.
    if (ascii_iequals(unit, "in")) { return value * 96.0f; }
    if (ascii_iequals(unit, "cm")) { return value * 96.0f / 2.54f; }
    if (ascii_iequals(unit, "mm")) { return value * 96.0f / 25.4f; }
    if (ascii_iequals(unit, "q")) { return value * 96.0f / 101.6f; }
    if (ascii_iequals(unit, "pt")) { return value * 96.0f / 72.0f; }
    if (ascii_iequals(unit, "pc")) { return value * 16.0f; }
    return std::nullopt;
}

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
    return math_context::any;
}

namespace {

// The one token of a single-value length, or nullptr if the text is not exactly
// one value. `12px 4px` is a shorthand and answering with its first component
// would be a quiet wrong answer.
[[nodiscard]] const css_token * lone_value(const token_stream & tokens) {
    std::size_t at = 0;
    while (tokens.tokens[at].type == token_type::whitespace) { ++at; }
    std::size_t after = at + 1;
    while (tokens.tokens[after].type == token_type::whitespace) { ++after; }
    if (tokens.tokens[after].type != token_type::eof) { return nullptr; }
    return &tokens.tokens[at];
}

} // namespace

std::optional<float> dimension_text_to_px(std::string_view text, const length_context & ctx) {
    const token_stream tokens = tokenize(text);
    const css_token * tok = lone_value(tokens);
    if (tok == nullptr || tok->type != token_type::dimension) { return std::nullopt; }
    return unit_to_px(static_cast<float>(tok->number), tokens.unit_of(*tok), ctx);
}

std::optional<float> length_text_to_px(std::string_view text, const length_context & ctx) {
    // Tokenized rather than scanned, so `1.5e1px` and an escaped unit behave the
    // same here as they do everywhere else in the front end.
    const token_stream tokens = tokenize(text);
    const css_token * tok = lone_value(tokens);
    if (tok == nullptr) { return std::nullopt; }
    if (tok->type == token_type::number) { return static_cast<float>(tok->number); }
    if (tok->type != token_type::dimension) { return std::nullopt; }
    return unit_to_px(static_cast<float>(tok->number), tokens.unit_of(*tok), ctx);
}

std::string serialize_calc(const calc_result & value) {
    // A PERCENTAGE THAT IS THE WHOLE ANSWER prints as one, with no dimension
    // beside it; `50%` and not `calc(50% + 0px)`.
    const bool percent_only = value.has_percent && value.px == 0.0;
    const double lead = percent_only ? value.percent : value.px;
    // A NUMBER HAS NO UNIT. `opacity: calc(2 / 4)` is `0.5`, and appending `px`
    // to it would be a different kind of wrong from dropping it - a value layout
    // and the cascade would both happily misread.
    const std::string_view unit = percent_only ? "%" : canonical_unit(value.type);

    // AN INFINITY OR A NaN CANNOT BE WRITTEN AS A TOKEN, so CSS Values 4 §10.12
    // keeps the calc() around it and moves the unit out to a multiplier:
    // `calc(infinity)`, `calc(-infinity)`, `calc(NaN * 1px)`. Printing `infpx`
    // or `nan%` - which `std::to_string` would have done - is not a CSS value at
    // all, and a page reading it back gets something it cannot re-parse.
    if (!std::isfinite(lead)) {
        const std::string word = std::isnan(lead) ? "NaN" : (lead > 0 ? "infinity" : "-infinity");
        if (unit.empty()) { return "calc(" + word + ")"; }
        return "calc(" + word + " * 1" + std::string{unit} + ")";
    }
    if (!value.has_percent || percent_only) { return format_number(lead) + std::string{unit}; }
    // The two-term canonical form, with the sign folded into the operator the way
    // Chrome prints it: `calc(100% - 12px)`, never `calc(100% + -12px)`.
    const bool negative = value.px < 0;
    return "calc(" + format_number(value.percent) + "% " + (negative ? "- " : "+ ") +
           format_number(negative ? -value.px : value.px) +
           std::string{canonical_unit(value.type)} + ")";
}

std::optional<std::string> canonical_dimension_text(std::string_view text,
                                                    const length_context & ctx) {
    const token_stream tokens = tokenize(text);
    const css_token * tok = lone_value(tokens);
    if (tok == nullptr || tok->type != token_type::dimension) { return std::nullopt; }
    const math_answer answer = evaluate_math(text, ctx);
    if (answer.outcome != math_outcome::resolved) { return std::nullopt; }
    return serialize_calc(answer.value);
}

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

// A `(`-terminated function name AT `at`, or an empty view. The boundary test is
// the whole point: `-webkit-calc(` and a custom property called `--my-calc` both
// contain the five bytes of `calc(` and neither is one, and `minmax(100px, 1fr)`
// contains `max(` three bytes in.
[[nodiscard]] constexpr bool is_name_char(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' ||
           c == '_';
}

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

} // namespace

bool may_have_math(std::string_view value) noexcept {
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (!math_name_at(value, i).empty()) { return true; }
    }
    return false;
}

namespace {

// The end of the math function that starts at `at`, name included. Parentheses
// are matched with quoted runs skipped, so a `)` inside a string cannot end the
// expression early.
//
// AN UNTERMINATED FUNCTION RUNS TO THE END OF THE VALUE, which is CSS Syntax 3
// §5.4.9: EOF closes every open block. It is not a corner case here -
// `minmax-length-computed` writes `calc(min(1em, 21px) * 2` with no closing paren
// four times over, and refusing it deleted the declaration where a browser folds
// it to 40px.
struct function_span {
    std::size_t end = 0; // one past the last byte, the `)` included when there is one
    bool closed = false; // whether a matching `)` was actually found
};

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

[[nodiscard]] bool has_percentage(std::string_view text) {
    const token_stream ts = tokenize(text);
    for (const css_token & t : ts.tokens) {
        if (t.type == token_type::percentage) { return true; }
    }
    return false;
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
        if (ascii_iequals(name, "calc(") && is_lone_calc(trim(body, html_whitespace))) {
            out.append(simplify_math(trim(body, html_whitespace)));
            continue;
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
            out.append(serialize_calc(answer.value));
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
