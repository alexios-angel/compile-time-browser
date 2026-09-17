#include <ctbrowser/style/easing.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/token.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
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

namespace {

// --- numbers, lengths, percentages and their calc() mixes ---

struct numeric_pair {
    css::calc_result a, b;
};

// Both endpoints as one numeric type, or nothing: `10px` and `2s` do not pair,
// nor `auto` and anything.
[[nodiscard]] std::optional<numeric_pair> numeric_of(std::string_view from, std::string_view to,
                                                     const css::length_context & ctx) {
    const css::math_answer a = css::evaluate_math(from, ctx);
    const css::math_answer b = css::evaluate_math(to, ctx);
    if (a.outcome != css::math_outcome::resolved || b.outcome != css::math_outcome::resolved) {
        return std::nullopt;
    }
    numeric_pair out{a.value, b.value};
    // A unitless `0` is a <length> where one is wanted (CSS Values 4 §6.1):
    // `left: 0` transitions to `400px`.
    const auto zero_as_length = [](css::calc_result & zero, const css::calc_result & other) {
        if (zero.is_number && zero.px == 0 && !other.is_number &&
            other.type == css::numeric_type::length) {
            zero.is_number = false;
            zero.type = css::numeric_type::length;
        }
    };
    zero_as_length(out.a, out.b);
    zero_as_length(out.b, out.a);
    if (out.a.type != out.b.type || out.a.is_number != out.b.is_number) { return std::nullopt; }
    return out;
}

// (1 - p) * a + p * b, the specification's own formula, which is also the one
// that extrapolates sensibly: `p` is outside [0, 1] whenever the easing
// overshoots. A numeric endpoint goes through the arithmetic at 0 and 1 too:
// its value is the same and its text is the COMPUTED spelling -
// `random(300, 100)` is `300` at progress 1 and not the function
// (random-in-animations).
[[nodiscard]] css::calc_result mix(const css::calc_result & a, const css::calc_result & b,
                                   double p) {
    const auto lerp = [p](double x, double y) { return (1 - p) * x + p * y; };
    css::calc_result out;
    out.type = a.type;
    out.is_number = a.is_number;
    out.px = lerp(a.px, b.px);
    out.has_percent = a.has_percent || b.has_percent;
    out.percent = lerp(a.has_percent ? a.percent : 0.0, b.has_percent ? b.percent : 0.0);
    return out;
}

// CSS Values 4 §4.3: addition of two numerics of one type is the sum, term
// by term for a percentage mix. Accumulation is the same for every type here.
[[nodiscard]] css::calc_result sum(const css::calc_result & a, const css::calc_result & b) {
    css::calc_result out;
    out.type = a.type;
    out.is_number = a.is_number;
    out.px = a.px + b.px;
    out.has_percent = a.has_percent || b.has_percent;
    out.percent = (a.has_percent ? a.percent : 0.0) + (b.has_percent ? b.percent : 0.0);
    return out;
}

// The text of a numeric result, CLAMPED AS A COMPUTED VALUE IS: an infinity
// lands on the bound it overflowed and a NaN is zero (CSS Values 4 §10.10) -
// after the arithmetic rather than before, so `0px` to `calc(infinity * 1px)`
// is the bound at every progress past zero, which is what the corpus reads.
// The bound is the fold's (lib/Style/css/calc/fold.cpp). Then an <integer>
// rounds half up (§3.2), and the property's own range applies: the table's
// floor at zero - only when no percentage is left to resolve, since
// `calc(-50px + 40%)` cannot be judged before its basis exists - and
// font-weight's [1, 1000] (CSS Fonts 4 §2.2, random-in-animations).
// ponytail: the one property with a range that is not "non-negative"; give
// the table a range when a second one animates.
[[nodiscard]] std::string numeric_text(std::string_view property, css::calc_result out) {
    constexpr double bound = 33554432.0;
    const auto clamped = [](double v) {
        if (std::isnan(v)) { return 0.0; }
        return std::isinf(v) ? (v > 0 ? bound : -bound) : v;
    };
    out.px = clamped(out.px);
    out.percent = clamped(out.percent);
    const css::property_syntax * known = css::find_property(property);
    if (out.is_number && known != nullptr && known->kind == css::value_kind::integer) {
        out.px = std::floor(out.px + 0.5);
    }
    if (known != nullptr && known->nonnegative && out.px < 0 && !out.has_percent) { out.px = 0; }
    if (property == "font-weight") { out.px = std::clamp(out.px, 1.0, 1000.0); }
    return css::serialize_calc(out);
}

// --- colours ---

// A colour as sRGB with alpha, premultiplied: CSS Color 4 §17 interpolates
// legacy colours in sRGB with the channels weighted by alpha, so a transparent
// endpoint contributes no hue. Through style's resolver rather than paint's:
// paint holds a channel in eight bits, and an alpha of 0.5 read back as
// 128/255 - which put the midpoint of blue and half-transparent red at 0.753
// rather than 0.75.
struct premultiplied {
    double r, g, b, a;
};

[[nodiscard]] premultiplied premultiply(const css::srgb_color & c) {
    return {c.r * c.a, c.g * c.a, c.b * c.a, c.a};
}

// Back to `rgba()` text, clamped to the gamut as a computed colour is. The
// text goes through the same `rgb()` parser the computed-style serialiser
// reads, which rounds.
[[nodiscard]] std::string color_text(const premultiplied & c) {
    const double alpha = std::clamp(c.a, 0.0, 1.0);
    const auto channel = [alpha](double v) {
        return std::clamp(alpha == 0 ? 0.0 : v / alpha, 0.0, 1.0) * 255.0;
    };
    const auto number = [](double v) {
        char buffer[32];
        const auto [end, ec] =
            std::to_chars(buffer, buffer + sizeof buffer, v, std::chars_format::fixed, 4);
        return ec == std::errc{} ? std::string{buffer, end} : std::string{"0"};
    };
    return "rgba(" + number(channel(c.r)) + ", " + number(channel(c.g)) + ", " +
           number(channel(c.b)) + ", " + number(alpha) + ")";
}

[[nodiscard]] std::string lerp_color(const css::srgb_color & from, const css::srgb_color & to,
                                     double p) {
    const premultiplied a = premultiply(from);
    const premultiplied b = premultiply(to);
    const auto lerp = [p](double x, double y) { return (1 - p) * x + p * y; };
    return color_text({lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)});
}

// CSS Color 4 §12.4: colours add channel by channel, premultiplied, the alpha
// summed and clamped.
[[nodiscard]] std::string add_color(const css::srgb_color & x, const css::srgb_color & y) {
    const premultiplied a = premultiply(x);
    const premultiplied b = premultiply(y);
    return color_text({a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a});
}

// --- the computed shape of a list item ---

[[nodiscard]] bool is_shadow(std::string_view property) {
    return property == "box-shadow" || property == "text-shadow";
}

// A LIST WHOSE ADDITION IS CONCATENATION: a shadow list, a transform list and
// a filter list append (CSS Backgrounds 3 §7.2, CSS Transforms 1 §12, Filter
// Effects 1 §11); every other comma list adds item by item.
[[nodiscard]] bool appends(std::string_view property) {
    return is_shadow(property) || property == "transform" || property == "filter" ||
           property == "backdrop-filter";
}

// THE COMPUTED SHAPE OF A VALUE WHOSE GRAMMAR LETS THE AUTHOR REORDER OR OMIT:
// a shadow is `<color> <x> <y> <blur> <spread> inset?` with the colour first
// and the omitted lengths zero (CSS Backgrounds 3 §7.2, the order the
// computed-style serialiser prints), a corner radius is two lengths. Paired
// as written, `10px 30px orange` against `green 20px 20px 20px` has nothing
// to interpolate; in computed shape it has a colour and three lengths. A
// shadow list's `none` is the empty list.
// A LIST THAT REPEATS TO MATCH (CSS Values 4 §4.1, "repeatable list"): the
// background and mask layer lists pair the shorter with the longer by
// repeating it, to the least common multiple of the two lengths.
[[nodiscard]] bool repeatable(std::string_view property) {
    return property.starts_with("background-") || property.starts_with("mask-");
}

template <typename T> void repeat_to_match(std::vector<T> & a, std::vector<T> & b) {
    if (a.empty() || b.empty() || a.size() == b.size()) { return; }
    const std::size_t n = std::lcm(a.size(), b.size());
    for (std::size_t i = a.size(); i < n; ++i) { a.push_back(a[i % a.size()]); }
    for (std::size_t i = b.size(); i < n; ++i) { b.push_back(b[i % b.size()]); }
}

[[nodiscard]] std::string computed_shape(std::string_view property, std::string_view text,
                                         const css::length_context & ctx) {
    if (repeatable(property)) {
        // Keywords as percentages, a size's second `auto` written, so
        // `left 20px top 20px` and `20px 20px` are one shape.
        css::color_context cc;
        cc.lengths = &ctx;
        const std::string computed = css::computed_background_list(property, text, cc);
        return computed.empty() ? std::string{text} : computed;
    }
    if (is_shadow(property)) {
        if (ascii_iequals(text, "none")) { return {}; }
        const bool box = property == "box-shadow";
        std::string out;
        for (const std::string_view shadow : split_top_level(text, ",")) {
            std::string colour;
            std::vector<std::string> lengths;
            bool inset = false;
            for (const std::string_view raw : split_top_level(shadow, html_whitespace)) {
                const std::string_view part = trim(raw, html_whitespace);
                if (part.empty()) { continue; }
                if (ascii_iequals(part, "inset")) {
                    inset = true;
                } else if (ascii_iequals(part, "currentcolor") || css::resolve_color(part, {})) {
                    colour = std::string{part};
                } else {
                    lengths.emplace_back(part);
                }
            }
            const std::size_t wanted = box ? 4 : 3;
            if (lengths.size() < 2 || lengths.size() > wanted) { return std::string{text}; }
            while (lengths.size() < wanted) { lengths.emplace_back("0px"); }
            if (!out.empty()) { out += ", "; }
            out += colour.empty() ? std::string{"currentcolor"} : colour;
            for (const std::string & len : lengths) { out += ' ' + len; }
            if (inset) { out += " inset"; }
        }
        return out.empty() ? std::string{text} : out;
    }
    if (property.starts_with("border-") && property.ends_with("-radius")) {
        const std::vector<std::string_view> parts = split_top_level(text, html_whitespace);
        if (parts.size() == 1) { return std::string{parts[0]} + ' ' + std::string{parts[0]}; }
    }
    // THE INDIVIDUAL TRANSFORM PROPERTIES (CSS Transforms 2 §7): `scale` is
    // three numbers, `none` is `1 1 1` and one value is both axes; `translate`
    // is three lengths, `none` is zero; `rotate: none` is `0deg`. The computed
    // serialiser drops the defaults again on the way out.
    if (property == "scale" || property == "translate") {
        const bool is_scale = property == "scale";
        std::vector<std::string> parts;
        if (!ascii_iequals(text, "none")) {
            for (const std::string_view part : split_top_level(text, html_whitespace)) {
                if (!part.empty()) { parts.emplace_back(part); }
            }
        }
        if (parts.size() > 3) { return std::string{text}; }
        if (is_scale && parts.size() == 1) { parts.push_back(parts[0]); }
        while (parts.size() < 3) { parts.emplace_back(is_scale ? "1" : "0px"); }
        return parts[0] + ' ' + parts[1] + ' ' + parts[2];
    }
    if (property == "rotate" && ascii_iequals(text, "none")) { return "0deg"; }
    return std::string{text};
}

// The shadow a shorter list is padded with: transparent, every length zero,
// inset when the shadow it pairs with is (CSS Backgrounds 3 §7.2).
[[nodiscard]] std::string blank_shadow(bool box, std::string_view like) {
    std::string out = box ? "rgba(0, 0, 0, 0) 0px 0px 0px 0px" : "rgba(0, 0, 0, 0) 0px 0px 0px";
    if (like.ends_with("inset")) { out += " inset"; }
    return out;
}

[[nodiscard]] std::vector<std::string_view> comma_items(std::string_view text) {
    std::vector<std::string_view> out;
    for (const std::string_view item : split_top_level(text, ",")) {
        const std::string_view trimmed = trim(item, html_whitespace);
        if (!trimmed.empty()) { out.push_back(trimmed); }
    }
    return out;
}

// --- transform lists (CSS Transforms 1 §12) ---

// One 2D transform function with its arguments as numbers: lengths in px,
// angles in degrees. `scale(2)` is `scale(2, 2)`, `translate(1px)` is
// `translate(1px, 0px)`, so two functions of one primitive always pair.
struct transform_fn {
    std::string name;
    std::vector<double> args;
    // A translate's percentage part per argument, kept in step with `args`
    // (the px part): `translate(12px, 70%)` keeps its percentages and reads
    // back as written, which is how the computed serialiser prints it.
    std::vector<double> pct;
};

[[nodiscard]] std::string_view primitive_of(std::string_view name) {
    if (name.starts_with("translate")) { return "translate"; }
    if (name.starts_with("scale")) { return "scale"; }
    return name;
}

// A 2D transform list, or nothing for `none`, or nullopt for one this does
// not model - a 3D function, a percentage, an `em` - which stays discrete.
// ponytail: 2D only, matching the computed-style serialiser; a 3D list
// needs matrix3d() there first.
[[nodiscard]] std::optional<std::vector<transform_fn>> parse_transforms(std::string_view text) {
    using css::token_type;
    std::vector<transform_fn> out;
    if (ascii_iequals(text, "none")) { return out; }
    const css::token_stream ts = css::tokenize(text);
    std::size_t at = 0;
    for (;;) {
        while (ts.tokens[at].type == token_type::whitespace) { ++at; }
        if (ts.tokens[at].type == token_type::eof) { break; }
        if (ts.tokens[at].type != token_type::function) { return std::nullopt; }
        const std::string_view raw = ts.text_of(ts.tokens[at]);
        transform_fn fn;
        fn.name = ascii_lower_copy(raw.substr(0, raw.size() - 1));
        ++at;
        std::vector<bool> is_length;
        for (;;) {
            while (ts.tokens[at].type == token_type::whitespace) { ++at; }
            const css::css_token & t = ts.tokens[at];
            if (t.type == token_type::eof) { return std::nullopt; }
            ++at;
            if (t.type == token_type::close_paren) { break; }
            if (t.type == token_type::comma) { continue; }
            fn.pct.push_back(0);
            if (t.type == token_type::number) {
                fn.args.push_back(t.number);
                is_length.push_back(false);
            } else if (t.type == token_type::percentage) {
                fn.args.push_back(0);
                fn.pct.back() = t.number;
                is_length.push_back(true);
            } else if (t.type == token_type::dimension) {
                const std::string unit = ascii_lower_copy(ts.unit_of(t));
                if (unit == "px") {
                    fn.args.push_back(t.number);
                    is_length.push_back(true);
                } else if (unit == "deg") {
                    fn.args.push_back(t.number);
                    is_length.push_back(false);
                } else if (unit == "rad") {
                    fn.args.push_back(t.number * 180.0 / std::numbers::pi);
                    is_length.push_back(false);
                } else if (unit == "grad") {
                    fn.args.push_back(t.number * 0.9);
                    is_length.push_back(false);
                } else if (unit == "turn") {
                    fn.args.push_back(t.number * 360.0);
                    is_length.push_back(false);
                } else {
                    return std::nullopt;
                }
            } else {
                return std::nullopt;
            }
        }
        const std::size_t n = fn.args.size();
        const bool translate = fn.name.starts_with("translate");
        for (std::size_t i = 0; i < n; ++i) {
            if (is_length[i] != translate && !(translate && fn.args[i] == 0.0)) {
                return std::nullopt;
            }
            if (!translate && fn.pct[i] != 0) { return std::nullopt; }
        }
        // Canonical two-argument forms, so the same primitive always pairs.
        const auto append = [&fn](double v) { fn.args.push_back(v), fn.pct.push_back(0); };
        const auto prepend = [&fn](double v) {
            fn.args.insert(fn.args.begin(), v), fn.pct.insert(fn.pct.begin(), 0);
        };
        if (fn.name == "translatex" && n == 1) {
            fn.name = "translate", append(0);
        } else if (fn.name == "translatey" && n == 1) {
            fn.name = "translate", prepend(0);
        } else if (fn.name == "translate" && n == 1) {
            append(0);
        } else if (fn.name == "scalex" && n == 1) {
            fn.name = "scale", append(1);
        } else if (fn.name == "scaley" && n == 1) {
            fn.name = "scale", prepend(1);
        } else if (fn.name == "scale" && n == 1) {
            append(fn.args[0]);
        } else if (fn.name == "skew" && n == 1) {
            append(0);
        }
        const std::size_t count = fn.args.size();
        const bool shaped =
            (fn.name == "matrix" && count == 6) || (fn.name == "translate" && count == 2) ||
            (fn.name == "scale" && count == 2) || (fn.name == "rotate" && count == 1) ||
            (fn.name == "skew" && count == 2) || (fn.name == "skewx" && count == 1) ||
            (fn.name == "skewy" && count == 1);
        if (!shaped) { return std::nullopt; }
        out.push_back(std::move(fn));
    }
    return out;
}

// The affine matrix as CSS writes it: x' = a*x + c*y + e, y' = b*x + d*y + f.
using matrix2d = std::array<double, 6>;

[[nodiscard]] matrix2d multiply(const matrix2d & m, const matrix2d & n) {
    return {m[0] * n[0] + m[2] * n[1],        m[1] * n[0] + m[3] * n[1],
            m[0] * n[2] + m[2] * n[3],        m[1] * n[2] + m[3] * n[3],
            m[0] * n[4] + m[2] * n[5] + m[4], m[1] * n[4] + m[3] * n[5] + m[5]};
}

[[nodiscard]] double radians(double deg) {
    return deg * std::numbers::pi / 180.0;
}

[[nodiscard]] matrix2d matrix_of(const transform_fn & fn) {
    const std::vector<double> & a = fn.args;
    if (fn.name == "matrix") { return {a[0], a[1], a[2], a[3], a[4], a[5]}; }
    if (fn.name == "translate") { return {1, 0, 0, 1, a[0], a[1]}; }
    if (fn.name == "scale") { return {a[0], 0, 0, a[1], 0, 0}; }
    if (fn.name == "rotate") {
        const double r = radians(a[0]);
        return {std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0};
    }
    if (fn.name == "skew") {
        return {1, std::tan(radians(a[1])), std::tan(radians(a[0])), 1, 0, 0};
    }
    if (fn.name == "skewx") { return {1, 0, std::tan(radians(a[0])), 1, 0, 0}; }
    return {1, std::tan(radians(a[0])), 0, 1, 0, 0}; // skewy
}

[[nodiscard]] matrix2d matrix_of(const std::vector<transform_fn> & list) {
    matrix2d m{1, 0, 0, 1, 0, 0};
    for (const transform_fn & fn : list) { m = multiply(m, matrix_of(fn)); }
    return m;
}

// §12.2, the 2D decomposition: translation, scale, rotation and the
// residual matrix, interpolated separately and recomposed.
struct decomposed2d {
    double tx, ty, sx, sy, angle, m11, m12, m21, m22;
};

[[nodiscard]] decomposed2d decompose(const matrix2d & m) {
    double row0x = m[0], row0y = m[1], row1x = m[2], row1y = m[3];
    decomposed2d d{m[4], m[5], 0, 0, 0, 1, 0, 0, 1};
    d.sx = std::hypot(row0x, row0y);
    if (d.sx != 0) { row0x /= d.sx, row0y /= d.sx; }
    double skew = row0x * row1x + row0y * row1y;
    row1x -= row0x * skew, row1y -= row0y * skew;
    d.sy = std::hypot(row1x, row1y);
    if (d.sy != 0) { row1x /= d.sy, row1y /= d.sy, skew /= d.sy; }
    if (row0x * row1y - row0y * row1x < 0) {
        d.sx = -d.sx;
        row0x = -row0x, row0y = -row0y;
    }
    d.angle = std::atan2(row0y, row0x) * 180.0 / std::numbers::pi;
    // The residual is the matrix with its rotation taken back out, so that
    // recomposing rotate(angle) * residual gives the rows back.
    const double sn = -row0y, cs = row0x;
    const double m11 = row0x, m12 = row0y, m21 = row1x, m22 = row1y;
    d.m11 = cs * m11 + sn * m21, d.m12 = cs * m12 + sn * m22;
    d.m21 = -sn * m11 + cs * m21, d.m22 = -sn * m12 + cs * m22;
    return d;
}

[[nodiscard]] matrix2d recompose(const decomposed2d & d) {
    matrix2d m{1, 0, 0, 1, d.tx, d.ty};
    const double r = radians(d.angle);
    m = multiply(m, {std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0});
    m = multiply(m, {d.m11, d.m12, d.m21, d.m22, 0, 0});
    return multiply(m, {d.sx, 0, 0, d.sy, 0, 0});
}

[[nodiscard]] std::string matrix_text(const matrix2d & m) {
    std::string out{"matrix("};
    for (std::size_t i = 0; i < 6; ++i) {
        if (i != 0) { out += ", "; }
        // Six decimals, which is where the computed serialiser rounds anyway.
        out += css::serialize_number(std::round(m[i] * 1e6) / 1e6);
    }
    return out + ')';
}

[[nodiscard]] std::string function_text(const transform_fn & fn) {
    std::string out = fn.name + '(';
    const bool translate = fn.name == "translate";
    const bool angles = fn.name == "rotate" || fn.name.starts_with("skew");
    for (std::size_t i = 0; i < fn.args.size(); ++i) {
        if (i != 0) { out += ", "; }
        if (translate) {
            css::calc_result length;
            length.px = fn.args[i];
            length.percent = fn.pct[i];
            length.has_percent = fn.pct[i] != 0;
            out += css::serialize_calc(length);
            continue;
        }
        out += css::serialize_number(fn.args[i]);
        if (angles) { out += "deg"; }
    }
    return out + ')';
}

[[nodiscard]] transform_fn identity_like(const transform_fn & fn) {
    transform_fn out{fn.name, {}, {}};
    if (fn.name == "matrix") {
        out.args = {1, 0, 0, 1, 0, 0};
    } else if (fn.name == "scale") {
        out.args = {1, 1};
    } else {
        out.args.assign(fn.args.size(), 0.0);
    }
    out.pct.assign(out.args.size(), 0.0);
    return out;
}

// §12: function by function while the lists match - the shorter padded
// with the identity of its partner - else the whole lists as matrices,
// decomposed. `none` on both sides stays `none`.
[[nodiscard]] std::optional<std::string> interpolate_transform(std::string_view from,
                                                               std::string_view to, double p) {
    std::optional<std::vector<transform_fn>> a = parse_transforms(from);
    std::optional<std::vector<transform_fn>> b = parse_transforms(to);
    if (!a || !b) { return std::nullopt; }
    if (a->empty() && b->empty()) { return "none"; }
    const auto lerp = [p](double x, double y) { return (1 - p) * x + p * y; };
    const std::size_t shorter = std::min(a->size(), b->size());
    bool matched = true;
    for (std::size_t i = 0; i < shorter && matched; ++i) {
        matched = primitive_of((*a)[i].name) == primitive_of((*b)[i].name);
    }
    if (matched) {
        for (std::size_t i = a->size(); i < b->size(); ++i) {
            a->push_back(identity_like((*b)[i]));
        }
        for (std::size_t i = b->size(); i < a->size(); ++i) {
            b->push_back(identity_like((*a)[i]));
        }
        std::string out;
        for (std::size_t i = 0; i < a->size(); ++i) {
            transform_fn fn = (*a)[i];
            for (std::size_t k = 0; k < fn.args.size(); ++k) {
                fn.args[k] = lerp((*a)[i].args[k], (*b)[i].args[k]);
                fn.pct[k] = lerp((*a)[i].pct[k], (*b)[i].pct[k]);
            }
            if (i != 0) { out += ' '; }
            out += function_text(fn);
        }
        return out;
    }
    // A percentage has no matrix until the box exists: such a pair that does
    // not match function by function stays discrete.
    for (const std::vector<transform_fn> * list : {&*a, &*b}) {
        for (const transform_fn & fn : *list) {
            if (std::ranges::any_of(fn.pct, [](double v) { return v != 0; })) {
                return std::nullopt;
            }
        }
    }
    decomposed2d x = decompose(matrix_of(*a));
    const decomposed2d y = decompose(matrix_of(*b));
    // The rotation goes the short way round, and a flip of scale on one
    // side is undone on the other (§12.2).
    if ((x.sx < 0 && y.sy < 0) || (x.sy < 0 && y.sx < 0)) {
        x.sx = -x.sx, x.sy = -x.sy;
        x.angle += x.angle < 0 ? 180 : -180;
    }
    if (std::fabs(x.angle - y.angle) > 180) {
        if (x.angle > y.angle) {
            x.angle -= 360;
        } else {
            x.angle += 360;
        }
    }
    const decomposed2d z{lerp(x.tx, y.tx),   lerp(x.ty, y.ty),       lerp(x.sx, y.sx),
                         lerp(x.sy, y.sy),   lerp(x.angle, y.angle), lerp(x.m11, y.m11),
                         lerp(x.m12, y.m12), lerp(x.m21, y.m21),     lerp(x.m22, y.m22)};
    return matrix_text(recompose(z));
}

// --- the pair ---

// CSS Values 4 §4.1: two values interpolate when they are one number, length
// or percentage each; two colours; or LISTS of the same shape -
// comma-separated, then space-separated - whose items pair off as one of
// those or as identical text (`inset`, `/`, `auto`). `border-width: 20px
// 40px`, `box-shadow: red 2px 2px`, `background-size: 10px 20%` are all that.
// When the pair does not, `interpolable` is false and the answer flips at the
// midpoint.
[[nodiscard]] std::string interpolate_pair(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx, bool & interpolable) {
    from = trim(from, html_whitespace);
    to = trim(to, html_whitespace);
    interpolable = true;
    if (property == "transform") {
        if (const std::optional<std::string> t = interpolate_transform(from, to, p)) { return *t; }
    }
    if (const auto a = css::resolve_color(from, {}), b = css::resolve_color(to, {}); a && b) {
        return lerp_color(*a, *b, p);
    }
    if (const std::optional<numeric_pair> n = numeric_of(from, to, ctx)) {
        return numeric_text(property, mix(n->a, n->b, p));
    }
    std::vector<std::string> lists_a;
    std::vector<std::string> lists_b;
    for (const std::string_view item : comma_items(from)) { lists_a.emplace_back(item); }
    for (const std::string_view item : comma_items(to)) { lists_b.emplace_back(item); }
    const auto discrete = [&] {
        interpolable = false;
        return std::string{p < 0.5 ? from : to};
    };
    if (repeatable(property)) { repeat_to_match(lists_a, lists_b); }
    const bool shadow = is_shadow(property);
    if (shadow) {
        // The shorter shadow list is padded at its end to the longer one's
        // length, each blank shadow inset when its partner is.
        while (lists_a.size() < lists_b.size()) {
            lists_a.push_back(blank_shadow(property == "box-shadow", lists_b[lists_a.size()]));
        }
        while (lists_b.size() < lists_a.size()) {
            lists_b.push_back(blank_shadow(property == "box-shadow", lists_a[lists_b.size()]));
        }
    }
    if (lists_a.size() != lists_b.size() || lists_a.empty()) { return discrete(); }
    std::string out;
    for (std::size_t i = 0; i < lists_a.size(); ++i) {
        const std::vector<std::string_view> items_a = split_top_level(lists_a[i], html_whitespace);
        const std::vector<std::string_view> items_b = split_top_level(lists_b[i], html_whitespace);
        if (items_a.size() != items_b.size() || items_a.empty()) { return discrete(); }
        // A single item on each side is the pair itself, already refused above.
        if (items_a.size() == 1 && lists_a.size() == 1) { return discrete(); }
        if (i != 0) { out += ", "; }
        std::size_t k_out = 0;
        for (std::size_t k = 0; k < items_a.size(); ++k) {
            const std::string_view x = trim(items_a[k], html_whitespace);
            const std::string_view y = trim(items_b[k], html_whitespace);
            if (x.empty() && y.empty()) { continue; }
            if (k_out++ != 0) { out += ' '; }
            if (x == y) {
                out += x;
                continue;
            }
            bool item_ok = true;
            std::string piece = interpolate_pair(property, x, y, p, ctx, item_ok);
            if (!item_ok) { return discrete(); }
            // A shadow's blur - the third length, in computed shape - cannot
            // go negative (CSS Backgrounds 3 §7.2): the extrapolated one is zero.
            if (shadow && k_out == 4 && piece.starts_with('-')) { piece = "0px"; }
            out += piece;
        }
    }
    return out;
}

// Web Animations 1 §4.5.1 over one item: numerics sum, colours sum, identical
// keywords keep, a list adds item by item, and anything else is `value`.
[[nodiscard]] std::string add_pair(std::string_view property, std::string_view underlying,
                                   std::string_view value, const css::length_context & ctx) {
    underlying = trim(underlying, html_whitespace);
    value = trim(value, html_whitespace);
    if (underlying.empty()) { return std::string{value}; }
    if (const auto a = css::resolve_color(underlying, {}), b = css::resolve_color(value, {});
        a && b) {
        return add_color(*a, *b);
    }
    if (const std::optional<numeric_pair> n = numeric_of(underlying, value, ctx)) {
        return numeric_text(property, sum(n->a, n->b));
    }
    std::vector<std::string_view> lists_a = comma_items(underlying);
    std::vector<std::string_view> lists_b = comma_items(value);
    if (repeatable(property)) { repeat_to_match(lists_a, lists_b); }
    if (lists_a.size() != lists_b.size() || lists_a.empty()) { return std::string{value}; }
    std::string out;
    for (std::size_t i = 0; i < lists_a.size(); ++i) {
        const std::vector<std::string_view> items_a = split_top_level(lists_a[i], html_whitespace);
        const std::vector<std::string_view> items_b = split_top_level(lists_b[i], html_whitespace);
        if (items_a.size() != items_b.size() || items_a.empty()) { return std::string{value}; }
        if (items_a.size() == 1 && lists_a.size() == 1) { return std::string{value}; }
        if (i != 0) { out += ", "; }
        std::size_t k_out = 0;
        for (std::size_t k = 0; k < items_a.size(); ++k) {
            const std::string_view x = trim(items_a[k], html_whitespace);
            const std::string_view y = trim(items_b[k], html_whitespace);
            if (x.empty() && y.empty()) { continue; }
            if (k_out++ != 0) { out += ' '; }
            if (x == y) {
                out += x;
                continue;
            }
            bool item_ok = true;
            (void)interpolate_pair(property, x, y, 0.5, ctx, item_ok);
            if (!item_ok) { return std::string{value}; }
            out += add_pair(property, x, y, ctx);
        }
    }
    return out;
}

} // namespace

[[nodiscard]] std::string interpolate_text(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx) {
    // Identical endpoints are that value at every progress - `none` to
    // `none` stays `none` rather than becoming its expanded shape.
    if (trim(from, html_whitespace) == trim(to, html_whitespace)) {
        return std::string{trim(from, html_whitespace)};
    }
    bool interpolable = true;
    return interpolate_pair(property, computed_shape(property, from, ctx),
                            computed_shape(property, to, ctx), p, ctx, interpolable);
}

[[nodiscard]] bool interpolable_text(std::string_view property, std::string_view from,
                                     std::string_view to) {
    bool interpolable = true;
    const css::length_context ctx;
    (void)interpolate_pair(property, computed_shape(property, from, ctx),
                           computed_shape(property, to, ctx), 0.5, ctx, interpolable);
    return interpolable;
}

[[nodiscard]] std::string with_currentcolor(std::string_view value, std::string_view color) {
    static constexpr std::string_view word = "currentcolor";
    std::string out;
    std::size_t i = 0;
    const auto boundary = [](char c) {
        return !(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_');
    };
    while (i < value.size()) {
        if (value.size() - i >= word.size() && ascii_iequals(value.substr(i, word.size()), word) &&
            (i == 0 || boundary(value[i - 1])) &&
            (i + word.size() == value.size() || boundary(value[i + word.size()]))) {
            out += color;
            i += word.size();
            continue;
        }
        out += value[i++];
    }
    return out;
}

[[nodiscard]] std::string composite_text(std::string_view property, std::string_view underlying,
                                         std::string_view value, composite_op op,
                                         const css::length_context & ctx) {
    if (op == composite_op::replace) { return std::string{value}; }
    const std::string base = computed_shape(property, trim(underlying, html_whitespace), ctx);
    const std::string added = computed_shape(property, trim(value, html_whitespace), ctx);
    if (appends(property)) {
        // The underlying list first, then the keyframe's; `none` on either
        // side is the empty list and contributes nothing.
        const bool base_none = base.empty() || ascii_iequals(base, "none");
        const bool added_none = added.empty() || ascii_iequals(added, "none");
        if (base_none) { return added; }
        if (added_none) { return base; }
        return base + (is_shadow(property) ? ", " : " ") + added;
    }
    if (property == "scale") {
        // CSS Transforms 2 §7: scales add by multiplying component by
        // component, and accumulate by summing each one's excess over 1.
        const std::vector<std::string_view> a = split_top_level(base, html_whitespace);
        const std::vector<std::string_view> b = split_top_level(added, html_whitespace);
        if (a.size() != 3 || b.size() != 3) { return added; }
        std::string out;
        for (std::size_t i = 0; i < 3; ++i) {
            const std::optional<numeric_pair> n = numeric_of(a[i], b[i], ctx);
            if (!n || !n->a.is_number) { return added; }
            css::calc_result product = n->a;
            product.px = op == composite_op::add ? n->a.px * n->b.px : n->a.px + n->b.px - 1;
            out += (i == 0 ? "" : " ") + css::serialize_calc(product);
        }
        return out;
    }
    return add_pair(property, base, added, ctx);
}

} // namespace ctbrowser::style
