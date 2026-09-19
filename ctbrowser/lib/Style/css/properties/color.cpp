#include "color/internal.hpp"

namespace ctbrowser::style::css {

using namespace color_detail;

std::string serialize_color(std::string_view text) {
    const std::unique_ptr<parsed> tree = parse_text(trim(text, html_whitespace));
    return tree ? serialize_specified(*tree, false) : std::string{};
}

std::string computed_color(std::string_view specified, const color_context & ctx) {
    const std::unique_ptr<parsed> tree = parse_text(trim(specified, html_whitespace));
    if (!tree) { return {}; }
    const resolve_context rc{ctx.current_color, ctx.lengths, ctx.dark};
    const std::optional<resolved> r = resolve(*tree, rc, 0);
    if (!r) { return {}; }
    return serialize_computed(*r);
}

std::optional<srgb_color> resolve_color(std::string_view specified, const color_context & ctx) {
    const std::unique_ptr<parsed> tree = parse_text(trim(specified, html_whitespace));
    if (!tree) { return std::nullopt; }
    const resolve_context rc{ctx.current_color, ctx.lengths, ctx.dark};
    const std::optional<resolved> r = resolve(*tree, rc, 0);
    if (!r) { return std::nullopt; }
    const resolved srgb = convert(*r, space::srgb);
    return srgb_color{static_cast<float>(srgb.none[0] ? 0.0 : srgb.c[0]),
                      static_cast<float>(srgb.none[1] ? 0.0 : srgb.c[1]),
                      static_cast<float>(srgb.none[2] ? 0.0 : srgb.c[2]),
                      static_cast<float>(srgb.alpha_none ? 0.0 : srgb.alpha)};
}

std::optional<space_color> color_in_space(std::string_view specified, std::string_view space_name,
                                          const color_context & ctx) {
    const std::optional<space> target = interpolation_space(space_name);
    if (!target) { return std::nullopt; }
    const std::unique_ptr<parsed> tree = parse_text(trim(specified, html_whitespace));
    if (!tree) { return std::nullopt; }
    const resolve_context rc{ctx.current_color, ctx.lengths, ctx.dark};
    const std::optional<resolved> r = resolve(*tree, rc, 0);
    if (!r) { return std::nullopt; }
    const resolved in = convert(*r, *target);
    return space_color{in.c, in.none, in.alpha, in.alpha_none};
}

std::string color_from_space(const space_color & c, std::string_view space_name) {
    const std::optional<space> s = interpolation_space(space_name);
    if (!s) { return {}; }
    resolved r;
    r.cs = *s;
    r.c = c.c;
    r.none = c.none;
    r.alpha = c.alpha;
    r.alpha_none = c.alpha_none;
    return serialize_computed(r);
}

std::string sanitize_color(std::string_view value, bool display_p3, bool alpha) {
    // "Parsing value": a CSS <color> with no context, so `currentcolor` and
    // `inherit` are failures and opaque black. A missing component is nought.
    resolved c;
    c.legacy = false;
    if (const std::unique_ptr<parsed> tree = parse_text(trim(value, html_whitespace))) {
        if (const std::optional<resolved> r = resolve(*tree, resolve_context{}, 0)) { c = *r; }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (c.none[i]) { c.c[i] = 0.0; }
    }
    if (c.alpha_none) { c.alpha = 0.0; }
    if (!alpha) { c.alpha = 1.0; }
    c.none = {};
    c.alpha_none = false;
    if (display_p3) { return modern_text(convert(c, space::display_p3)); }
    // Limited sRGB: eight bits per component, the alpha included.
    resolved srgb = convert(c, space::srgb);
    const auto byte = [](double v) {
        return static_cast<int>(
            std::floor(std::round(std::min(255.0, std::max(0.0, v)) * 1e6) / 1e6 + 0.5));
    };
    std::array<int, 4> bytes{byte(srgb.c[0] * 255.0), byte(srgb.c[1] * 255.0),
                             byte(srgb.c[2] * 255.0), byte(srgb.alpha * 255.0)};
    if (!alpha) {
        std::string out = "#";
        for (std::size_t i = 0; i < 3; ++i) {
            out += "0123456789abcdef"[bytes[i] >> 4];
            out += "0123456789abcdef"[bytes[i] & 15];
        }
        return out;
    }
    for (std::size_t i = 0; i < 3; ++i) { srgb.c[i] = bytes[i] / 255.0; }
    srgb.alpha = bytes[3] / 255.0;
    return modern_text(srgb);
}

} // namespace ctbrowser::style::css
