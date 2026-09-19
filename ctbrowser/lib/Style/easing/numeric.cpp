#include "internal.hpp"

namespace ctbrowser::style::easing_detail {

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
    // A mix keeps both terms at 0 and 1 too: `10%` to `20px` ends at
    // `calc(0% + 20px)`, which is what a computed length-percentage is.
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
// floor at zero (one for positive integers) - only when no percentage is left to resolve, since
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
    if (known != nullptr && known->nonnegative && !out.has_percent) {
        out.px = std::max(out.px, known->kind == css::value_kind::integer ? 1.0 : 0.0);
    }
    if (property == "font-weight") { out.px = std::clamp(out.px, 1.0, 1000.0); }
    return css::serialize_calc(out);
}

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

// A LEGACY COLOUR - named, hex, rgb(), hsl(), hwb() - interpolates in sRGB;
// a pair with a modern one in it interpolates in Oklab (CSS Color 4 §12.1)
// and reads back as `oklab()`.
[[nodiscard]] bool legacy_color(std::string_view text) {
    const std::string_view lowered_start = text.substr(0, std::min<std::size_t>(text.size(), 12));
    const std::string head = ascii_lower_copy(lowered_start);
    for (const std::string_view modern : {"color(", "lab(", "lch(", "oklab(", "oklch(",
                                          "color-mix(", "light-dark(", "device-cmyk("}) {
        if (head.starts_with(modern)) { return false; }
    }
    return true;
}

[[nodiscard]] oklab oklab_of(const css::srgb_color & c) {
    const auto linear = [](double v) {
        const double m = std::fabs(v);
        const double out = m <= 0.04045 ? m / 12.92 : std::pow((m + 0.055) / 1.055, 2.4);
        return v < 0 ? -out : out;
    };
    const double r = linear(c.r), g = linear(c.g), b = linear(c.b);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

[[nodiscard]] std::string lerp_oklab(const css::srgb_color & from, const css::srgb_color & to,
                                     double p) {
    const oklab x = oklab_of(from), y = oklab_of(to);
    const auto lerp = [p](double a, double b) { return (1 - p) * a + p * b; };
    const double alpha = std::clamp(lerp(from.a, to.a), 0.0, 1.0);
    const auto channel = [&](double a, double b) {
        const double premultiplied = lerp(a * from.a, b * to.a);
        const double v = alpha == 0 ? 0.0 : premultiplied / alpha;
        return std::fabs(v) < 5e-7 ? 0.0 : v; // no `-0` for a grey's chroma
    };
    std::string out = "oklab(" + css::serialize_number(std::clamp(channel(x.l, y.l), 0.0, 1.0)) +
                      " " + css::serialize_number(channel(x.a, y.a)) + " " +
                      css::serialize_number(channel(x.b, y.b));
    if (alpha < 1) { out += " / " + css::serialize_number(alpha); }
    return out + ')';
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
    // A line width keyword is its length (CSS Backgrounds 3 §4.2), so
    // `border-left-width: initial` - `medium` - pairs with `23px`.
    if (property.ends_with("-width")) {
        if (ascii_iequals(text, "thin")) { return "1px"; }
        if (ascii_iequals(text, "medium")) { return "3px"; }
        if (ascii_iequals(text, "thick")) { return "5px"; }
    }
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

} // namespace ctbrowser::style::easing_detail
