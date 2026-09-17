#include <ctbrowser/style/easing.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/token.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
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
    if (a.outcome != css::math_outcome::resolved || b.outcome != css::math_outcome::resolved ||
        a.value.type != b.value.type || a.value.is_number != b.value.is_number) {
        return std::nullopt;
    }
    return numeric_pair{a.value, b.value};
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
[[nodiscard]] std::string computed_shape(std::string_view property, std::string_view text) {
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
    const std::vector<std::string_view> lists_a = comma_items(underlying);
    const std::vector<std::string_view> lists_b = comma_items(value);
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
    return interpolate_pair(property, computed_shape(property, from), computed_shape(property, to),
                            p, ctx, interpolable);
}

[[nodiscard]] bool interpolable_text(std::string_view property, std::string_view from,
                                     std::string_view to) {
    bool interpolable = true;
    const css::length_context ctx;
    (void)interpolate_pair(property, computed_shape(property, from), computed_shape(property, to),
                           0.5, ctx, interpolable);
    return interpolable;
}

[[nodiscard]] std::string composite_text(std::string_view property, std::string_view underlying,
                                         std::string_view value, composite_op op,
                                         const css::length_context & ctx) {
    if (op == composite_op::replace) { return std::string{value}; }
    const std::string base = computed_shape(property, trim(underlying, html_whitespace));
    const std::string added = computed_shape(property, trim(value, html_whitespace));
    if (appends(property)) {
        // The underlying list first, then the keyframe's; `none` on either
        // side is the empty list and contributes nothing.
        const bool base_none = base.empty() || ascii_iequals(base, "none");
        const bool added_none = added.empty() || ascii_iequals(added, "none");
        if (base_none) { return added; }
        if (added_none) { return base; }
        return base + ", " + added;
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
