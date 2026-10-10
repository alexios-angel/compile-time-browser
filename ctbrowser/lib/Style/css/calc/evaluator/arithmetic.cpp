//===- arithmetic.cpp - calc() arithmetic -----------------------*- C++ -*-===//
//
// The arithmetic behind CSS math functions (CSS Values 4 §10, Values 5):
// adding, multiplying and dividing typed terms, signed-zero-aware min/max, and
// the per-value cores of round(), mod(), rem(), progress() and random().
//
//===----------------------------------------------------------------------===//
#include "internal.hpp"

namespace ctbrowser::style::css::evaluator_detail {

/// The names number_symbols_scope installed for the current evaluation.
thread_local std::span<const std::string_view> number_symbols;

/// Ordering with a signed zero. `min(0, -0)` is -0 and `max(-0, 0)` is 0: CSS
/// Values 4 §10.9 keeps the two zeros distinct and the WPT test signed-zero
/// reads them back through `1 / sign(...)`. std::min and std::max cannot tell
/// them apart (`-0 < 0` is false), so the comparison functions order through
/// this instead.
[[nodiscard]] bool less(double a, double b) noexcept {
    return a < b || (a == 0.0 && b == 0.0 && std::signbit(a) && !std::signbit(b));
}

[[nodiscard]] double smaller(double a, double b) noexcept {
    return less(b, a) ? b : a;
}

[[nodiscard]] double larger(double a, double b) noexcept {
    return less(a, b) ? b : a;
}

/// A term whose whole magnitude is in one slot, so a non-linear function can be
/// applied to it. `10px` and `10%` both qualify; `calc(10px + 10%)` does not,
/// because `abs()` of it depends on a containing block nobody has yet.
[[nodiscard]] bool is_scalar(const term & t) noexcept {
    // A symbolic sum is never one. `min(1em, 1px)` cannot be ordered before a
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

/// `a + b`, or `a - b` when `subtract`; nullopt when the dimensions differ.
[[nodiscard]] std::optional<term> add(const term & a, const term & b, bool subtract) {
    const double sign = subtract ? -1.0 : 1.0;
    // Terms of different dimensions cannot be added: `calc(100% - 12)` and
    // `calc(1s + 1px)` are invalid, as in Chrome.
    if (a.dims != b.dims) { return std::nullopt; }
    term out;
    out.dims = a.dims;
    out.value = a.value + sign * b.value;
    out.percent = a.percent + sign * b.percent;
    out.has_percent = a.has_percent || b.has_percent;
    out.symbols = a.symbols;
    for (const auto & [unit, coefficient] : b.symbols) {
        add_symbol(out, unit, sign * coefficient);
    }
    return out;
}

/// Whether a term's plain magnitude is absent rather than zero. `10%` carries
/// its magnitude in `percent` and `1em` in `symbols`, and the zero left in
/// `value` means "there is no length in this term", which matters because
/// IEEE makes `0 * infinity` a NaN, and `calc(1% * infinity)` is
/// `calc(infinity * 1%)`, not a NaN length beside the right answer. A `0px`
/// the author wrote is a length, and `calc(0px * infinity)` is a NaN.
[[nodiscard]] bool no_plain_part(const term & t) noexcept {
    return (t.has_percent || !t.symbols.empty()) && t.value == 0.0;
}

/// `dim` multiplied by a plain number, every component alike.
[[nodiscard]] term scaled(const term & dim, double scale) {
    term out = dim;
    out.value = no_plain_part(dim) ? 0.0 : dim.value * scale;
    out.percent = dim.has_percent ? dim.percent * scale : 0.0;
    for (auto & [unit, coefficient] : out.symbols) { coefficient *= scale; }
    return out;
}

/// Whether a symbol is a function rather than a unit (`sibling-index()` in a
/// specified value). function_only below asks whether a term is made only of
/// those.
[[nodiscard]] bool function_symbol(std::string_view key) noexcept {
    return key.ends_with("()") || key == "size" || is_number_symbol(key);
}

[[nodiscard]] bool function_only(const term & t) noexcept {
    return !t.symbols.empty() && t.value == 0.0 && !t.has_percent &&
           std::ranges::all_of(t.symbols, [](const auto & s) { return function_symbol(s.first); });
}

[[nodiscard]] arithmetic multiply(const term & a, const term & b) {
    // A number scales the other operand, which is the product every stylesheet
    // writes.
    if (a.is_number() && !a.has_percent && a.symbols.empty()) { return {scaled(b, a.value)}; }
    if (b.is_number() && !b.has_percent && b.symbols.empty()) { return {scaled(a, b.value)}; }
    // A function term times a dimension is a product with nothing to fold:
    // `1turn * sibling-count()` is the term `360deg * sibling-count()`, keyed
    // on both so the canonical spelling is one entry. The other side is a
    // plain magnitude in its canonical unit, or a unit term of its own.
    if (function_only(a) != function_only(b)) {
        const term & fn = function_only(a) ? a : b;
        const term & other = function_only(a) ? b : a;
        if (other.has_percent || std::ranges::any_of(other.symbols, [](const auto & s) {
                return function_symbol(s.first);
            })) {
            return {std::nullopt, true};
        }
        // The product has one symbol per pair, and it is function-only again,
        // so a chain of `* (1px + 1em + ...)` squares the count at every step.
        // Past the cap the expression is invalid rather than a heap exhausted.
        constexpr std::size_t max_symbols = 64;
        if (fn.symbols.size() * std::max<std::size_t>(other.symbols.size(), 1) > max_symbols) {
            return {};
        }
        term out;
        for (std::size_t i = 0; i < out.dims.size(); ++i) {
            out.dims[i] = static_cast<std::int8_t>(fn.dims[i] + other.dims[i]);
        }
        const std::string unit =
            other.symbols.empty() ? std::string{canonical_unit(other.type())} : std::string{};
        const double magnitude = other.symbols.empty() ? other.value : 0.0;
        for (const auto & [key, coefficient] : fn.symbols) {
            if (other.symbols.empty()) {
                add_symbol(out, unit.empty() ? key : unit + '*' + key, coefficient * magnitude);
            } else {
                for (const auto & [other_unit, other_coefficient] : other.symbols) {
                    add_symbol(out, other_unit + '*' + key, coefficient * other_coefficient);
                }
            }
        }
        return {out};
    }
    // Two dimensions multiply into a type of their own (the exponents add, CSS
    // Values 4 §10.2), and `2px * 3px` is an area on its way to being divided
    // back down, or a syntax error if it never is (`settle()` decides). A
    // symbolic term has no magnitude to multiply, and a percentage times a
    // percentage needs the basis twice over; both wait for the cascade.
    if (!a.symbols.empty() || !b.symbols.empty() || (a.has_percent && b.has_percent)) {
        return {std::nullopt, true};
    }
    term out;
    for (std::size_t i = 0; i < out.dims.size(); ++i) {
        out.dims[i] = static_cast<std::int8_t>(a.dims[i] + b.dims[i]);
    }
    out.value = (no_plain_part(a) || no_plain_part(b)) ? 0.0 : a.value * b.value;
    out.has_percent = a.has_percent || b.has_percent;
    out.percent = a.has_percent ? a.percent * b.value : (b.has_percent ? b.percent * a.value : 0.0);
    return {out};
}

[[nodiscard]] arithmetic divide(const term & a, const term & b) {
    // Dividing by a dimension, §10.2 again: the exponents subtract, so `110px /
    // 10px` is the number 11 and `10em / 1px` the number of pixels in ten ems.
    // A divisor carrying a percentage has no magnitude until layout, and a
    // symbolic one has none at all.
    if (!b.is_number() || b.has_percent) {
        // A percentage over a percentage is a number: the basis cancels, so
        // `calc(10% / 20%)` is 0.5 with nothing left to resolve (WPT
        // typed_arithmetic), provided each side is nothing but its
        // percentage, since `(10% + 1px) / 20%` needs the basis after all.
        if (a.has_percent && b.has_percent && no_plain_part(a) && no_plain_part(b) &&
            a.symbols.empty() && b.symbols.empty()) {
            term out;
            for (std::size_t i = 0; i < out.dims.size(); ++i) {
                out.dims[i] = static_cast<std::int8_t>(a.dims[i] - b.dims[i]);
            }
            out.value = a.percent / b.percent;
            return {out};
        }
        if (b.has_percent || !b.symbols.empty() || !a.symbols.empty()) {
            return {std::nullopt, true};
        }
        term out;
        for (std::size_t i = 0; i < out.dims.size(); ++i) {
            out.dims[i] = static_cast<std::int8_t>(a.dims[i] - b.dims[i]);
        }
        out.value = no_plain_part(a) ? 0.0 : a.value / b.value;
        out.has_percent = a.has_percent;
        out.percent = a.has_percent ? a.percent / b.value : 0.0;
        return {out};
    }
    // The divisor is a number and may be zero: CSS Values 4 §10.9 makes the
    // result an infinity or NaN, not a syntax error, so `calc(100px / 0)` is
    // `calc(infinity * 1px)` and `calc(0 / 0)` is `calc(NaN)`. IEEE division
    // already gives both, with the right sign for a negative zero.
    term out = a;
    // The same absent-component rule `scaled()` carries, and for the same
    // reason: `calc(1% / 0)` is `calc(infinity * 1%)` and not a NaN length.
    out.value = no_plain_part(a) ? 0.0 : a.value / b.value;
    out.percent = a.has_percent ? a.percent / b.value : 0.0;
    for (auto & [unit, coefficient] : out.symbols) { coefficient /= b.value; }
    return {out};
}

/// round(): §10.5's whole table, infinities included. The infinities are not a
/// corner case: the WPT test round-mod-rem-computed spends forty of its
/// assertions on them because they are how a page asks "is this value finite"
/// without a script.
[[nodiscard]] double round_one(round_to how, double a, double b) {
    if (b == 0.0 || std::isnan(a) || std::isnan(b)) { return std::nan(""); }
    if (std::isinf(a)) { return std::isinf(b) ? std::nan("") : a; }
    if (std::isinf(b)) {
        // A finite A against an infinite step: the answer is a zero or an
        // infinity, and which is decided by the strategy and A's sign (a
        // negative zero being negative), which is why std::signbit is asked
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
    // The step's sign is ignored: the multiples of -10 are the multiples of 10,
    // so `round(15px, -10px)` is 20px like `round(15px, 10px)`. Dividing by the
    // signed step would flip the half-way rule. The WPT test round-function
    // checks both spellings.
    const double step = std::fabs(b);
    const double n = a / step;
    double stepped = 0.0;
    switch (how) {
    case round_to::up: stepped = std::ceil(n); break;
    case round_to::down: stepped = std::floor(n); break;
    case round_to::to_zero: stepped = std::trunc(n); break;
    // "If two multiples are equally near, use the one above", so a half rounds
    // towards +infinity, which floor(n + 0.5) does and std::round(n) does not:
    // std::round takes -2.5 to -3 and CSS takes it to -2.
    case round_to::nearest: stepped = std::floor(n + 0.5); break;
    }
    return stepped * step;
}

/// mod(), §10.6. `mod` takes the sign of the divisor and `rem` the sign of the
/// dividend, which is the whole difference between them and the reason both
/// exist.
[[nodiscard]] double mod_one(double a, double b) {
    if (b == 0.0 || std::isinf(a) || std::isnan(a) || std::isnan(b)) { return std::nan(""); }
    if (std::isinf(b)) {
        // An infinite divisor leaves A alone if they point the same way and is
        // meaningless if they do not. An oppositely-signed zero counts as
        // opposite, so `mod(-0, infinity)` is NaN.
        return std::signbit(a) == std::signbit(b) ? a : std::nan("");
    }
    const double result = a - b * std::floor(a / b);
    // A zero result still takes the divisor's sign. `mod(1, -1)` is -0 and not
    // 0, which `1 / sign(mod(1, -1))` can tell apart; WPT signed-zero does.
    return result == 0.0 ? std::copysign(0.0, b) : result;
}

/// rem(), §10.6: the remainder takes the sign of the dividend, as std::fmod.
[[nodiscard]] double rem_one(double a, double b) {
    if (b == 0.0 || std::isinf(a) || std::isnan(a) || std::isnan(b)) { return std::nan(""); }
    if (std::isinf(b)) { return a; }
    return std::fmod(a, b);
}

/// CSS Values 5 §progress: how far `a` lies from `b` to `c`, as a <number>.
///
/// An empty range is neither an error nor a NaN. `progress(1rad, 1rad, 1rad)`
/// is zero over zero, which IEEE calls NaN and the specification calls no
/// progress at all. The unclamped form keeps the numerator's sign, so
/// `progress(no-clamp 2rad, 1rad, 1rad)` is an infinity and
/// `progress(no-clamp 0rad, 1rad, 1rad)` its negative; the clamped form is zero
/// whichever way it points, because there is no range to be anywhere in. The
/// WPT test progress-serialize asserts these cases.
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

/// CSS Values 5 §random: the value between A and B that the random base picks,
/// on a grid of `step` when there is one. Edge cases (all checked by the WPT
/// test random-computed): a NaN anywhere gives NaN, an infinite A gives A, an
/// infinite B gives NaN, B < A gives A, an infinite step gives A, and a step
/// that is not positive is ignored.
[[nodiscard]] double random_one(double base, double a, double b, std::optional<double> step) {
    if (std::isnan(a) || std::isnan(b) || (step && std::isnan(*step))) { return std::nan(""); }
    if (std::isinf(a)) { return a; }
    if (std::isinf(b)) { return std::nan(""); }
    if (b < a) { return a; }
    if (step && *step > 0.0) {
        if (std::isinf(*step)) { return a; }
        const double count = std::floor((b - a) / *step) + 1.0; // the multiples that fit
        const double pick = std::floor(base * count);
        return std::min(b, a + pick * *step);
    }
    return a + base * (b - a);
}

} // namespace ctbrowser::style::css::evaluator_detail
