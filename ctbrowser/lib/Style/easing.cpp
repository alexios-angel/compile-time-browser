#include <ctbrowser/style/easing.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/token.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <system_error>
#include <vector>

namespace ctbrowser::style {
namespace {

[[nodiscard]] std::vector<std::string_view> split_arguments(std::string_view body) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= body.size(); ++i) {
        if (i == body.size() || body[i] == ',') {
            out.push_back(trim(body.substr(start, i - start), html_whitespace));
            start = i + 1;
        }
    }
    return out;
}

[[nodiscard]] std::optional<double> number_of(std::string_view text) {
    double n = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (ec != std::errc{} || end != text.data() + text.size() || !std::isfinite(n)) {
        return std::nullopt;
    }
    return n;
}

[[nodiscard]] std::optional<int> step_count(std::string_view text) {
    const css::token_stream tokens = css::tokenize(text);
    if (tokens.tokens.size() != 2) { return std::nullopt; } // one token and EOF
    const css::css_token & token = tokens.tokens.front();
    if (token.type != css::token_type::number || (token.flags & css::flag_integer) == 0 ||
        tokens.text_of(token) != text) {
        return std::nullopt;
    }
    std::string_view digits = tokens.text_of(token);
    if (digits.starts_with('+')) { digits.remove_prefix(1); }
    // The tokenizer owns integer syntax. Read its spelling rather than its
    // double value so even a very long positive integer clamps before the
    // conversion to the supported count range.
    int count = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), count);
    if (ec == std::errc::result_out_of_range && !digits.starts_with('-')) {
        count = std::numeric_limits<int>::max();
    } else if (ec != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return count > 0 ? std::optional{count} : std::nullopt;
}

} // namespace

[[nodiscard]] double easing::operator()(double input, bool before) const {
    switch (shape) {
    case kind::linear: return input;
    case kind::bezier: return bezier(input);
    case kind::steps: return step(input, before);
    }
    return input;
}

[[nodiscard]] double easing::bezier(double input) const {
    if (input < 0) {
        if (x1 > 0) { return input * y1 / x1; }
        if (x1 == 0 && y1 == 0 && x2 > 0) { return input * y2 / x2; }
        return 0;
    }
    if (input > 1) {
        if (x2 < 1) { return 1 + (input - 1) * (y2 - 1) / (x2 - 1); }
        if (x2 == 1 && y2 == 1 && x1 < 1) { return 1 + (input - 1) * (y1 - 1) / (x1 - 1); }
        return 1;
    }
    // Solve x(t) = input for t by bisection - the curve's x is monotone
    // because 0 <= x1, x2 <= 1 - then read y(t).
    const auto at = [](double p1, double p2, double t) {
        const double u = 1 - t;
        return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
    };
    double lo = 0, hi = 1, t = input;
    for (int i = 0; i < 60; ++i) {
        t = (lo + hi) / 2;
        const double x = at(x1, x2, t);
        if (std::fabs(x - input) < 1e-12) { break; }
        (x < input ? lo : hi) = t;
    }
    return at(y1, y2, t);
}

[[nodiscard]] double easing::step(double input, bool before) const {
    const double n = steps;
    double current = std::floor(input * n);
    if (position == jump::start || position == jump::both) { current += 1; }
    if (before && input * n == std::floor(input * n)) { current -= 1; }
    if (input >= 0 && current < 0) { current = 0; }
    const double jumps = position == jump::none ? n - 1 : position == jump::both ? n + 1 : n;
    if (input <= 1 && current > jumps) { current = jumps; }
    return current / jumps;
}

// `nullopt` for text that is not an easing, which is a TypeError at every
// place the specification takes one.
[[nodiscard]] std::optional<easing> parse_easing(std::string_view text) {
    const std::string lowered = ascii_lower_copy(trim(text, html_whitespace));
    easing out;
    const auto bezier = [&](double a, double b, double c, double d) {
        out.shape = easing::kind::bezier;
        out.x1 = a;
        out.y1 = b;
        out.x2 = c;
        out.y2 = d;
        return out;
    };
    if (lowered == "linear") { return out; }
    if (lowered == "ease") { return bezier(0.25, 0.1, 0.25, 1); }
    if (lowered == "ease-in") { return bezier(0.42, 0, 1, 1); }
    if (lowered == "ease-out") { return bezier(0, 0, 0.58, 1); }
    if (lowered == "ease-in-out") { return bezier(0.42, 0, 0.58, 1); }
    if (lowered == "step-start") {
        out.shape = easing::kind::steps;
        out.position = easing::jump::start;
        return out;
    }
    if (lowered == "step-end") {
        out.shape = easing::kind::steps;
        return out;
    }
    if (!lowered.ends_with(')')) { return std::nullopt; }
    if (lowered.starts_with("cubic-bezier(")) {
        const auto args =
            split_arguments(std::string_view{lowered}.substr(13, lowered.size() - 14));
        if (args.size() != 4) { return std::nullopt; }
        double n[4];
        for (std::size_t i = 0; i < 4; ++i) {
            const std::optional<double> v = number_of(args[i]);
            if (!v) { return std::nullopt; }
            n[i] = *v;
        }
        if (n[0] < 0 || n[0] > 1 || n[2] < 0 || n[2] > 1) { return std::nullopt; }
        return bezier(n[0], n[1], n[2], n[3]);
    }
    if (lowered.starts_with("steps(")) {
        const auto args = split_arguments(std::string_view{lowered}.substr(6, lowered.size() - 7));
        if (args.empty() || args.size() > 2) { return std::nullopt; }
        const std::optional<int> count = step_count(args[0]);
        if (!count) { return std::nullopt; }
        out.shape = easing::kind::steps;
        out.steps = *count;
        if (args.size() == 2) {
            const std::string_view p = args[1];
            if (p == "start" || p == "jump-start") {
                out.position = easing::jump::start;
            } else if (p == "end" || p == "jump-end") {
                out.position = easing::jump::end;
            } else if (p == "jump-none") {
                if (out.steps < 2) { return std::nullopt; }
                out.position = easing::jump::none;
            } else if (p == "jump-both") {
                out.position = easing::jump::both;
            } else {
                return std::nullopt;
            }
        }
        return out;
    }
    return std::nullopt;
}

// --- interpolation ------------------------------------------------------------

// (1 - p) * from + p * to, the specification's own formula, which is also the
// one that extrapolates sensibly: `p` is outside [0, 1] whenever the easing
// overshoots, and a `p` of 0 or 1 hands back the endpoint's own text so its
// computed value is exactly the declared one.
[[nodiscard]] std::string interpolate_text(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx) {
    const auto lerp = [p](double a, double b) { return (1 - p) * a + p * b; };
    const css::math_answer a = css::evaluate_math(from, ctx);
    const css::math_answer b = css::evaluate_math(to, ctx);
    const bool numeric = a.outcome == css::math_outcome::resolved &&
                         b.outcome == css::math_outcome::resolved && a.value.type == b.value.type &&
                         a.value.is_number == b.value.is_number;
    // An endpoint's own text at 0 and 1 when it is not arithmetic, so a
    // keyword's computed value is exactly the declared one. A numeric endpoint
    // goes through the interpolation like every other progress: its value is
    // the same and its text is the COMPUTED spelling - `random(300, 100)` is
    // `300` at progress 1 and not the function (random-in-animations) - and
    // an infinity is clamped below like every value on the way there
    // (calc-interpolation).
    // ponytail: colours, transforms and lists flip at the midpoint; add a
    // colour lerp beside this when a test reads an animated colour.
    if (!numeric) { return std::string{p < 0.5 ? from : to}; }
    css::calc_result out;
    out.type = a.value.type;
    out.is_number = a.value.is_number;
    out.px = lerp(a.value.px, b.value.px);
    out.has_percent = a.value.has_percent || b.value.has_percent;
    out.percent = lerp(a.value.has_percent ? a.value.percent : 0.0,
                       b.value.has_percent ? b.value.percent : 0.0);
    // CLAMPED AS A COMPUTED VALUE IS: an infinity lands on the bound it
    // overflowed and a NaN is zero (CSS Values 4 §10.10), after the
    // interpolation rather than before - `0px` to `calc(infinity * 1px)`
    // is the bound at every progress past zero, which is what the corpus
    // reads. The bound is the fold's (lib/Style/css/calc/fold.cpp).
    constexpr double bound = 33554432.0;
    const auto clamped = [](double v) {
        if (std::isnan(v)) { return 0.0; }
        return std::isinf(v) ? (v > 0 ? bound : -bound) : v;
    };
    out.px = clamped(out.px);
    out.percent = clamped(out.percent);
    // CSS Values 4 §3.2: an interpolated <integer> rounds half up.
    const css::property_syntax * known = css::find_property(property);
    if (out.is_number && known != nullptr && known->kind == css::value_kind::integer) {
        out.px = std::floor(out.px + 0.5);
    }
    // ...AND IS CLAMPED TO THE PROPERTY'S RANGE, as a computed value is
    // (CSS Values 4 §10.10): the table's floor at zero, and font-weight's own
    // [1, 1000] (CSS Fonts 4 §2.2, random-in-animations).
    // ponytail: the one property with a range that is not "non-negative";
    // give the table a range when a second one animates.
    if (known != nullptr && known->nonnegative && out.px < 0) { out.px = 0; }
    if (property == "font-weight") { out.px = std::clamp(out.px, 1.0, 1000.0); }
    return css::serialize_calc(out);
}

} // namespace ctbrowser::style
