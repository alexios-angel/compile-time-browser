//===- ctbrowser/lib/Style/easing/numeric.cpp - Numeric values ------------===//
//
// Value helpers for CSS interpolation and composition: pairing two numeric
// endpoints, mixing and summing them, colour interpolation in sRGB or Oklab,
// and rewriting list and shorthand values into a computed shape whose parts
// can be paired item by item.
//
//===----------------------------------------------------------------------===//
#include "internal.hpp"

namespace ctbrowser::style::easing_detail {

// The comma-separated arguments of a function body, each trimmed. Brackets
// are not tracked: this is for easing functions, whose arguments are numbers.
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

// `text` as a finite number, with nothing else around it.
[[nodiscard]] std::optional<double> number_of(std::string_view text) {
    double n = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (ec != std::errc{} || end != text.data() + text.size() || !std::isfinite(n)) {
        return std::nullopt;
    }
    return n;
}

// The step count of `steps(<integer>, ...)`: a positive integer, clamped to
// INT_MAX when larger, or nullopt.
[[nodiscard]] std::optional<int> step_count(std::string_view text) {
    const css::token_stream tokens = css::tokenize(text);
    if (tokens.tokens.size() != 2) { return std::nullopt; } // One token and EOF.
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
// its value is the same and its text is the computed spelling -
// `random(300, 100)` is `300` at progress 1 and not the function
// (WPT css/css-values/random-in-animations).
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

// The text of a numeric result, clamped the way a computed value is.
// Infinity becomes ±33554432 (2^25, the same bound calc folding uses in
// lib/Style/css/calc/fold.cpp) and NaN becomes 0 (CSS Values 4 §10.10).
// Clamping happens after interpolation, so `0px` to `calc(infinity * 1px)` is
// the bound at every progress past zero, as the WPT tests expect. Then an
// <integer> rounds half up, non-negative properties are floored at 0 (1 for
// integers) unless the value mixes a length with a percentage, which needs
// its basis to clamp, and font-weight is clamped to [1, 1000] (CSS Fonts 4
// §2.2).
// ponytail: font-weight is the only property with a range other than
// non-negative; add a range to the property table when a second one animates.
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
    if (known != nullptr && known->nonnegative) {
        if (!out.has_percent) {
            out.px = std::max(out.px, known->kind == css::value_kind::integer ? 1.0 : 0.0);
        } else if (out.px == 0) {
            out.percent = std::max(out.percent, 0.0);
        }
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

// A legacy colour - named, hex, rgb(), hsl(), hwb() - interpolates in sRGB;
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

// sRGB to Oklab: linearize each channel, then the two matrices of CSS Color 4
// §17.4 with a cube root between them.
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

// Interpolates two colours in Oklab with premultiplied alpha, as `oklab()` text.
[[nodiscard]] std::string lerp_oklab(const css::srgb_color & from, const css::srgb_color & to,
                                     double p) {
    const oklab x = oklab_of(from), y = oklab_of(to);
    const auto lerp = [p](double a, double b) { return (1 - p) * a + p * b; };
    const double alpha = std::clamp(lerp(from.a, to.a), 0.0, 1.0);
    const auto channel = [&](double a, double b) {
        const double premultiplied = lerp(a * from.a, b * to.a);
        const double v = alpha == 0 ? 0.0 : premultiplied / alpha;
        return std::fabs(v) < 5e-7 ? 0.0 : v; // No `-0` for a grey's chroma.
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

// A list whose addition is concatenation: a shadow list, a transform list and
// a filter list append (CSS Backgrounds 3 §7.2, CSS Transforms 1 §12, Filter
// Effects 1 §11); every other comma list adds item by item.
[[nodiscard]] bool appends(std::string_view property) {
    return is_shadow(property) || property == "transform" || property == "filter" ||
           property == "backdrop-filter";
}

// A list that repeats to match (CSS Values 4 §4.1, "repeatable list"): the
// background and mask layer lists pair the shorter with the longer by
// repeating it, to the least common multiple of the two lengths.
[[nodiscard]] bool repeatable(std::string_view property) {
    return property.starts_with("background-") || property.starts_with("mask-");
}

// The border-image and mask-border properties that take one to four sides.
[[nodiscard]] bool image_sides(std::string_view property) {
    return property == "border-image-outset" || property == "border-image-width" ||
           property == "border-image-slice" || property == "mask-border-outset" ||
           property == "mask-border-width" || property == "mask-border-slice";
}

// The computed shape of a value whose grammar lets the author reorder or omit
// parts. A shadow becomes `<color> <x> <y> <blur> <spread> inset?` with the
// colour first and omitted lengths zero (CSS Backgrounds 3 §7.2, the order the
// computed-style serializer prints); a corner radius becomes two lengths.
// Written as-is, `10px 30px orange` and `green 20px 20px 20px` cannot be
// paired; in this shape they can. A shadow list's `none` is the empty list.
[[nodiscard]] std::string computed_shape(std::string_view property, std::string_view text,
                                         const css::length_context & ctx) {
    // A border spacing is a pair, the vertical the horizontal when omitted, and
    // it interpolates as one: `10px` to `20px` passes `15px 15px`
    // (WPT border-spacing-interpolation).
    if (property == "border-spacing") {
        const std::vector<std::string_view> parts = split_top_level(text, html_whitespace);
        std::vector<std::string_view> words;
        for (const std::string_view part : parts) {
            if (!trim(part, html_whitespace).empty()) {
                words.push_back(trim(part, html_whitespace));
            }
        }
        if (words.size() == 1) { return std::string{words[0]} + " " + std::string{words[0]}; }
        return std::string{text};
    }
    // A font width's keyword is its percentage, which is what interpolates:
    // `normal` to `ultra-expanded` is 100% to 200% (WPT font-stretch-interpolation).
    if (property == "font-width" || property == "font-stretch") {
        if (const std::optional<std::string_view> percentage = css::font_width_percentage(text)) {
            return std::string{*percentage};
        }
    }
    if (image_sides(property)) {
        const css::value_check checked = css::check_declaration(property, text, false);
        if (!checked.valid) { return std::string{text}; }
        auto parts = split_top_level(checked.serialized, html_whitespace);
        const bool fill = !parts.empty() && parts.back() == "fill";
        if (fill) { parts.pop_back(); }
        if (parts.empty() || parts.size() > 4) { return std::string{text}; }
        // CSS Backgrounds 3 §6: omitted sides follow the margin pattern.
        if (parts.size() == 1) { parts.push_back(parts[0]); }
        if (parts.size() == 2) { parts.push_back(parts[0]); }
        if (parts.size() == 3) { parts.push_back(parts[1]); }
        return std::string{parts[0]} + ' ' + std::string{parts[1]} + ' ' + std::string{parts[2]} +
               ' ' + std::string{parts[3]} + (fill ? " fill" : "");
    }
    if (property == "clip-path" || property == "shape-outside" || property == "offset-path" ||
        property == "object-view-box") {
        std::string computed = css::computed_basic_shape(property, text, ctx);
        return computed.empty() ? std::string{text} : computed;
    }
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
    // The individual transform properties (CSS Transforms 2 §7): `scale` is
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
    // A transform's lengths are absolute once computed (CSS Transforms 1
    // §7): `translateZ(2em)` is 32px at a 16px font, which is what pairs
    // with `translateZ(3em)` (WPT transform-interpolation-004). A percentage
    // stays one; it needs the reference box.
    if (property == "transform") {
        const css::token_stream ts = css::tokenize(text);
        std::string out;
        for (const css::css_token & t : ts.tokens) {
            if (t.type == css::token_type::eof) { break; }
            const std::string_view piece = ts.text_of(t);
            if (t.type == css::token_type::dimension && !ascii_iequals(ts.unit_of(t), "px")) {
                if (const std::optional<float> px = css::length_text_to_px(piece, ctx)) {
                    out += css::serialize_number(*px) + "px";
                    continue;
                }
            }
            out += piece;
        }
        return out;
    }
    // `normal` spacing is none at all (CSS Text 4 §7.1-7.2): `word-spacing:
    // initial` pairs with `20px` (WPT word-spacing-interpolation).
    if ((property == "word-spacing" || property == "letter-spacing") &&
        ascii_iequals(text, "normal")) {
        return "0px";
    }
    // A <position>'s keywords are the percentages they name (CSS Values 5
    // §position): `left top` pairs with `50% 50%`, and perspective-origin's
    // four-value `top 20% left 40%` adds to `left 60% top 80%`
    // (WPT perspective-origin-composition).
    if (property == "object-position" || property == "offset-anchor" ||
        property == "offset-position" || property == "perspective-origin") {
        if (std::string computed = css::computed_position(text, "", ""); !computed.empty()) {
            return computed;
        }
    }
    // ...and so are transform-origin's, horizontal first whichever order they
    // were written in, and the z offset always written, `0px` when omitted
    // (CSS Transforms 1 §6): `top left` pairs with `100% 100%`
    // (WPT transform-origin-interpolation), and `40px 60px` adds to `160px
    // 140px 40px` (WPT transform-origin-composition). serialized_shape drops
    // the zero again.
    if (property == "transform-origin") {
        std::vector<std::string_view> words;
        for (const std::string_view raw : split_top_level(text, html_whitespace)) {
            if (!trim(raw, html_whitespace).empty()) {
                words.push_back(trim(raw, html_whitespace));
            }
        }
        const auto keyword = [](std::string_view w) {
            return ascii_iequals_any(w, {"left", "right", "top", "bottom", "center"});
        };
        if (words.size() == 2 && std::ranges::none_of(words, keyword)) {
            return std::string{words[0]} + ' ' + std::string{words[1]} + " 0px";
        }
        if (!words.empty() && words.size() <= 3 && std::ranges::any_of(words, keyword)) {
            const auto vertical = [](std::string_view w) {
                return ascii_iequals_any(w, {"top", "bottom"});
            };
            const auto percent = [](std::string_view w) {
                if (ascii_iequals_any(w, {"left", "top"})) { return std::string{"0%"}; }
                if (ascii_iequals_any(w, {"right", "bottom"})) { return std::string{"100%"}; }
                if (ascii_iequals(w, "center")) { return std::string{"50%"}; }
                return std::string{w};
            };
            std::string x = "50%", y = "50%";
            if (words.size() == 1) {
                (vertical(words[0]) ? y : x) = percent(words[0]);
            } else {
                const bool swapped =
                    vertical(words[0]) || ascii_iequals_any(words[1], {"left", "right"});
                x = percent(words[swapped ? 1 : 0]);
                y = percent(words[swapped ? 0 : 1]);
            }
            return x + ' ' + y + ' ' + (words.size() == 3 ? std::string{words[2]} : "0px");
        }
    }
    // An <absolute-size> is the length it names (CSS Fonts 4 §2.5's table
    // at the 16px medium): `large` adds to `100px` as 18px
    // (WPT font-size-composition).
    if (property == "font-size") {
        static constexpr std::pair<std::string_view, std::string_view> sizes[] = {
            {"xx-small", "9px"}, {"x-small", "10px"}, {"small", "13px"},    {"medium", "16px"},
            {"large", "18px"},   {"x-large", "24px"}, {"xx-large", "32px"}, {"xxx-large", "48px"}};
        for (const auto & [word, length] : sizes) {
            if (ascii_iequals(text, word)) { return std::string{length}; }
        }
    }
    // zoom animates by its computed number (CSS Viewport 1 §5): `100%` to
    // `300%` is 1 to 3, and `normal` is 1 (WPT zoom-interpolation).
    if (property == "zoom") {
        if (std::string computed = css::computed_zoom(text, ctx); !computed.empty()) {
            return computed;
        }
    }
    // offset-rotate's keywords are angles too: `reverse` is `auto 180deg`, so
    // it pairs with `auto 100deg` (WPT offset-rotate-interpolation).
    if (property == "offset-rotate") {
        if (std::string computed = css::computed_offset_rotate(text, false); !computed.empty()) {
            return computed;
        }
    }
    // font-variation-settings pairs axis by axis, whatever order either side
    // wrote them in: each list sorted by tag, a repeated tag's last value
    // winning (WPT font-variation-settings-interpolation).
    if (property == "font-variation-settings" && !ascii_iequals(text, "normal")) {
        std::vector<std::pair<std::string, std::string>> axes;
        for (const std::string_view raw : split_top_level(text, ",")) {
            const std::string_view item = trim(raw, html_whitespace);
            const std::size_t space = item.find_last_of(" \t\n\r\f");
            if (space == std::string_view::npos) { return std::string{text}; }
            std::string tag{trim(item.substr(0, space), html_whitespace)};
            if (tag.size() < 2) { return std::string{text}; }
            tag = tag.substr(1, tag.size() - 2);
            std::erase_if(axes, [&tag](const auto & axis) { return axis.first == tag; });
            axes.emplace_back(std::move(tag), std::string{item.substr(space + 1)});
        }
        std::ranges::stable_sort(axes, {}, &std::pair<std::string, std::string>::first);
        std::string out;
        for (const auto & [tag, value] : axes) {
            out += (out.empty() ? "\"" : ", \"") + tag + "\" " + value;
        }
        return out;
    }
    // `normal` is `oblique 0deg` and a bare `oblique` is `oblique 14deg` (CSS
    // Fonts 4 §2.4), so either pairs with an oblique angle.
    if (property == "font-style") {
        if (ascii_iequals(text, "normal")) { return "oblique 0deg"; }
        if (ascii_iequals(text, "oblique")) { return "oblique 14deg"; }
    }
    // A line width keyword is its length (CSS Backgrounds 3 §4.2), so
    // `border-left-width: initial` - `medium` - pairs with `23px`.
    if (property.ends_with("-width")) {
        if (ascii_iequals(text, "thin")) { return "1px"; }
        if (ascii_iequals(text, "medium")) { return "3px"; }
        if (ascii_iequals(text, "thick")) { return "5px"; }
    }
    return std::string{text};
}

// Undoes the expansions computed_shape made that the computed serializer
// would not print: a repeated radius, sides the margin pattern implies, and a
// zero transform-origin z offset.
[[nodiscard]] std::string serialized_shape(std::string_view property, std::string text) {
    if (image_sides(property) || property == "hyphenate-limit-chars") {
        const css::value_check checked = css::check_declaration(property, text, false);
        if (checked.valid) { return checked.serialized; }
    }
    if (property.starts_with("border-") && property.ends_with("-radius")) {
        const auto halves = split_top_level(text, html_whitespace);
        if (halves.size() == 2 && halves[0] == halves[1]) { return std::string{halves[0]}; }
    }
    // A zero z offset is left out of a transform-origin, as it was written.
    if (property == "transform-origin" && text.ends_with(" 0px") &&
        split_top_level(text, html_whitespace).size() == 3) {
        text.resize(text.size() - 4);
    }
    return text;
}

// The shadow a shorter list is padded with: transparent, every length zero,
// inset when the shadow it pairs with is (CSS Backgrounds 3 §7.2).
[[nodiscard]] std::string blank_shadow(bool box, std::string_view like) {
    std::string out = box ? "rgba(0, 0, 0, 0) 0px 0px 0px 0px" : "rgba(0, 0, 0, 0) 0px 0px 0px";
    if (like.ends_with("inset")) { out += " inset"; }
    return out;
}

// The non-empty top-level items of a comma list, each trimmed.
[[nodiscard]] std::vector<std::string_view> comma_items(std::string_view text) {
    std::vector<std::string_view> out;
    for (const std::string_view item : split_top_level(text, ",")) {
        const std::string_view trimmed = trim(item, html_whitespace);
        if (!trimmed.empty()) { out.push_back(trimmed); }
    }
    return out;
}

} // namespace ctbrowser::style::easing_detail
