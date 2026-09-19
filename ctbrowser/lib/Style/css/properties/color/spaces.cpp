#include "internal.hpp"

namespace ctbrowser::style::css::color_detail {

[[nodiscard]] const named * find_named(std::span<const named> table, std::string_view word) {
    for (const named & one : table) {
        if (ascii_iequals(one.name, word)) { return &one; }
    }
    return nullptr;
}

[[nodiscard]] std::string_view space_name(space s) noexcept {
    switch (s) {
    case space::srgb: return "srgb";
    case space::hsl: return "hsl";
    case space::hwb: return "hwb";
    case space::lab: return "lab";
    case space::lch: return "lch";
    case space::oklab: return "oklab";
    case space::oklch: return "oklch";
    case space::srgb_linear: return "srgb-linear";
    case space::display_p3: return "display-p3";
    case space::display_p3_linear: return "display-p3-linear";
    case space::a98_rgb: return "a98-rgb";
    case space::prophoto_rgb: return "prophoto-rgb";
    case space::rec2020: return "rec2020";
    case space::xyz_d50: return "xyz-d50";
    case space::xyz_d65: return "xyz-d65";
    }
    return "srgb";
}

// The spaces `color()` and `color-mix(in ...)` name. `xyz` is `xyz-d65`.
[[nodiscard]] std::optional<space> predefined_space(std::string_view word) {
    static constexpr std::pair<std::string_view, space> table[] = {
        {"srgb", space::srgb},
        {"srgb-linear", space::srgb_linear},
        {"display-p3", space::display_p3},
        {"display-p3-linear", space::display_p3_linear},
        {"a98-rgb", space::a98_rgb},
        {"prophoto-rgb", space::prophoto_rgb},
        {"rec2020", space::rec2020},
        {"xyz", space::xyz_d65},
        {"xyz-d50", space::xyz_d50},
        {"xyz-d65", space::xyz_d65},
    };
    for (const auto & [name, s] : table) {
        if (ascii_iequals(name, word)) { return s; }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<space> interpolation_space(std::string_view word) {
    if (const std::optional<space> s = predefined_space(word)) { return s; }
    static constexpr std::pair<std::string_view, space> table[] = {
        {"hsl", space::hsl}, {"hwb", space::hwb},     {"lab", space::lab},
        {"lch", space::lch}, {"oklab", space::oklab}, {"oklch", space::oklch},
    };
    for (const auto & [name, s] : table) {
        if (ascii_iequals(name, word)) { return s; }
    }
    return std::nullopt;
}

[[nodiscard]] std::array<part, 3> parts_of(space s) noexcept {
    switch (s) {
    case space::hsl: return {part::hue, part::colorfulness, part::lightness};
    case space::hwb: return {part::hue, part::whiteness, part::blackness};
    case space::lab:
    case space::oklab: return {part::lightness, part::opponent_a, part::opponent_b};
    case space::lch:
    case space::oklch: return {part::lightness, part::colorfulness, part::hue};
    default: return {part::red, part::green, part::blue};
    }
}

// The slot holding `p` in `s`, or -1.
[[nodiscard]] int slot_of(space s, part p) noexcept {
    const std::array<part, 3> parts = parts_of(s);
    for (int i = 0; i < 3; ++i) {
        if (parts[static_cast<std::size_t>(i)] == p) { return i; }
    }
    return -1;
}

[[nodiscard]] int hue_slot(space s) noexcept {
    return slot_of(s, part::hue);
}

[[nodiscard]] int colorfulness_slot(space s) noexcept {
    return slot_of(s, part::colorfulness);
}

// The channel keywords a relative colour may use, per space, in slot order;
// `alpha` is always the fourth.
[[nodiscard]] std::array<std::string_view, 3> channel_keywords(space s) noexcept {
    switch (s) {
    case space::hsl: return {"h", "s", "l"};
    case space::hwb: return {"h", "w", "b"};
    case space::lab:
    case space::oklab: return {"l", "a", "b"};
    case space::lch:
    case space::oklch: return {"l", "c", "h"};
    case space::xyz_d50:
    case space::xyz_d65: return {"x", "y", "z"};
    default: return {"r", "g", "b"};
    }
}

// What 100% means in each slot (CSS Color 4 §4.1's reference ranges), and
// how a plain number in the channel maps to the space's own unit.
[[nodiscard]] double percent_reference(space s, int slot, bool bytes) noexcept {
    switch (s) {
    case space::srgb: return slot == 3 || !bytes ? 1.0 : 255.0; // rgb(): numbers are 0..255
    case space::hsl:
    case space::hwb: return slot == 3 ? 1.0 : 100.0;
    case space::lab: return slot == 0 ? 100.0 : (slot == 3 ? 1.0 : 125.0);
    case space::lch: return slot == 0 ? 100.0 : (slot == 3 ? 1.0 : 150.0);
    case space::oklab: return slot == 0 || slot == 3 ? 1.0 : 0.4;
    case space::oklch: return slot == 0 || slot == 3 ? 1.0 : 0.4;
    default: return 1.0;
    }
}

[[nodiscard]] vec3 mul(const mat3 & m, const vec3 & v) noexcept {
    return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
}

// The transfer functions, sign-preserving as the specification writes them.
[[nodiscard]] double lin_srgb(double v) noexcept {
    const double a = std::fabs(v);
    const double sign = v < 0 ? -1.0 : 1.0;
    return a <= 0.04045 ? v / 12.92 : sign * std::pow((a + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double gam_srgb(double v) noexcept {
    const double a = std::fabs(v);
    const double sign = v < 0 ? -1.0 : 1.0;
    return a > 0.0031308 ? sign * (1.055 * std::pow(a, 1.0 / 2.4) - 0.055) : 12.92 * v;
}

[[nodiscard]] double lin_prophoto(double v) noexcept {
    const double a = std::fabs(v);
    const double sign = v < 0 ? -1.0 : 1.0;
    return a <= 16.0 / 512 ? v / 16.0 : sign * std::pow(a, 1.8);
}

[[nodiscard]] double gam_prophoto(double v) noexcept {
    const double a = std::fabs(v);
    const double sign = v < 0 ? -1.0 : 1.0;
    return a >= 1.0 / 512 ? sign * std::pow(a, 1.0 / 1.8) : 16.0 * v;
}

[[nodiscard]] double lin_a98(double v) noexcept {
    const double sign = v < 0 ? -1.0 : 1.0;
    return sign * std::pow(std::fabs(v), 563.0 / 256);
}

[[nodiscard]] double gam_a98(double v) noexcept {
    const double sign = v < 0 ? -1.0 : 1.0;
    return sign * std::pow(std::fabs(v), 256.0 / 563);
}

// rec2020 is display-referred: BT.1886's pure 2.4 gamma, not BT.2020's
// camera curve (CSS Color 4 §10.8 since 2024; the corpus's rec2020 answers
// are computed with it).
[[nodiscard]] double lin_2020(double v) noexcept {
    const double sign = v < 0 ? -1.0 : 1.0;
    return sign * std::pow(std::fabs(v), 2.4);
}

[[nodiscard]] double gam_2020(double v) noexcept {
    const double sign = v < 0 ? -1.0 : 1.0;
    return sign * std::pow(std::fabs(v), 1.0 / 2.4);
}

[[nodiscard]] vec3 map3(const vec3 & v, double (*f)(double) noexcept) noexcept {
    return {f(v[0]), f(v[1]), f(v[2])};
}

[[nodiscard]] double normalize_hue(double h) noexcept {
    if (!std::isfinite(h)) { return 0.0; }
    h = std::fmod(h, 360.0);
    if (h < 0) { h += 360.0; }
    return h;
}

// §7.1 and §8: hsl and hwb to and from sRGB, the specification's own code.
[[nodiscard]] vec3 hsl_to_rgb(double hue, double sat, double light) noexcept {
    hue = normalize_hue(hue);
    sat /= 100.0;
    light /= 100.0;
    const auto f = [&](double n) {
        const double k = std::fmod(n + hue / 30.0, 12.0);
        const double a = sat * std::min(light, 1.0 - light);
        return light - a * std::max(-1.0, std::min({k - 3.0, 9.0 - k, 1.0}));
    };
    return {f(0), f(8), f(4)};
}

// The hue of an sRGB triple, the specification's rgbToHue: nought when the
// channels are equal, and no rotation for a negative saturation - that is
// hsl's own step below, and hwb does not take it.
[[nodiscard]] double rgb_to_hue(const vec3 & rgb) noexcept {
    const double red = rgb[0], green = rgb[1], blue = rgb[2];
    const double max = std::max({red, green, blue});
    const double min = std::min({red, green, blue});
    const double d = max - min;
    if (d == 0.0) { return 0.0; }
    double hue = 0.0;
    if (max == red) {
        hue = (green - blue) / d + (green < blue ? 6.0 : 0.0);
    } else if (max == green) {
        hue = (blue - red) / d + 2.0;
    } else {
        hue = (red - green) / d + 4.0;
    }
    hue *= 60.0;
    return hue >= 360.0 ? hue - 360.0 : hue;
}

[[nodiscard]] vec3 rgb_to_hsl(const vec3 & rgb) noexcept {
    const double max = std::max({rgb[0], rgb[1], rgb[2]});
    const double min = std::min({rgb[0], rgb[1], rgb[2]});
    double hue = rgb_to_hue(rgb), sat = 0.0;
    const double light = (min + max) / 2.0;
    if (max != min) {
        sat = (light == 0.0 || light == 1.0) ? 0.0 : (max - light) / std::min(light, 1.0 - light);
    }
    // A very out-of-gamut colour has a negative saturation: rotate the hue by
    // 180 and use the positive one (csswg-drafts/9222).
    if (sat < 0) {
        hue += 180.0;
        sat = std::fabs(sat);
    }
    if (hue >= 360.0) { hue -= 360.0; }
    return {hue, sat * 100.0, light * 100.0};
}

[[nodiscard]] vec3 hwb_to_rgb(double hue, double white, double black) noexcept {
    white /= 100.0;
    black /= 100.0;
    if (white + black >= 1.0) {
        const double gray = white / (white + black);
        return {gray, gray, gray};
    }
    vec3 rgb = hsl_to_rgb(hue, 100.0, 50.0);
    for (double & v : rgb) { v = v * (1.0 - white - black) + white; }
    return rgb;
}

[[nodiscard]] vec3 rgb_to_hwb(const vec3 & rgb) noexcept {
    const double white = std::min({rgb[0], rgb[1], rgb[2]});
    const double black = 1.0 - std::max({rgb[0], rgb[1], rgb[2]});
    return {rgb_to_hue(rgb), white * 100.0, black * 100.0};
}

// §9 and §10: Lab (D50) and OKLab, to and from XYZ D65.
[[nodiscard]] vec3 xyz_to_lab(const vec3 & xyz65) noexcept {
    constexpr double epsilon = 216.0 / 24389;
    constexpr double kappa = 24389.0 / 27;
    const vec3 xyz = mul(d65_to_d50, xyz65);
    vec3 f{};
    for (std::size_t i = 0; i < 3; ++i) {
        const double v = xyz[i] / d50_white[i];
        f[i] = v > epsilon ? std::cbrt(v) : (kappa * v + 16.0) / 116.0;
    }
    return {116.0 * f[1] - 16.0, 500.0 * (f[0] - f[1]), 200.0 * (f[1] - f[2])};
}

[[nodiscard]] vec3 lab_to_xyz(const vec3 & lab) noexcept {
    constexpr double epsilon = 216.0 / 24389;
    constexpr double kappa = 24389.0 / 27;
    vec3 f{};
    f[1] = (lab[0] + 16.0) / 116.0;
    f[0] = lab[1] / 500.0 + f[1];
    f[2] = f[1] - lab[2] / 200.0;
    const double f0 = f[0] * f[0] * f[0];
    const double f2 = f[2] * f[2] * f[2];
    vec3 xyz{f0 > epsilon ? f0 : (116.0 * f[0] - 16.0) / kappa,
             lab[0] > kappa * epsilon ? std::pow((lab[0] + 16.0) / 116.0, 3.0) : lab[0] / kappa,
             f2 > epsilon ? f2 : (116.0 * f[2] - 16.0) / kappa};
    for (std::size_t i = 0; i < 3; ++i) { xyz[i] *= d50_white[i]; }
    return mul(d50_to_d65, xyz);
}

[[nodiscard]] vec3 xyz_to_oklab(const vec3 & xyz) noexcept {
    const vec3 lms = mul(xyz_to_lms, xyz);
    return mul(lms_to_oklab, {std::cbrt(lms[0]), std::cbrt(lms[1]), std::cbrt(lms[2])});
}

[[nodiscard]] vec3 oklab_to_xyz(const vec3 & lab) noexcept {
    const vec3 lms = mul(oklab_to_lms, lab);
    return mul(lms_to_xyz,
               {lms[0] * lms[0] * lms[0], lms[1] * lms[1] * lms[1], lms[2] * lms[2] * lms[2]});
}

[[nodiscard]] vec3 lab_to_lch(const vec3 & lab) noexcept {
    const double hue = std::atan2(lab[2], lab[1]) * 180.0 / std::numbers::pi;
    return {lab[0], std::sqrt(lab[1] * lab[1] + lab[2] * lab[2]), normalize_hue(hue)};
}

[[nodiscard]] vec3 lch_to_lab(const vec3 & lch) noexcept {
    const double h = lch[2] * std::numbers::pi / 180.0;
    return {lch[0], lch[1] * std::cos(h), lch[1] * std::sin(h)};
}

// Every channel to XYZ D65, `none` as zero.
[[nodiscard]] vec3 to_xyz(space s, vec3 c) noexcept {
    switch (s) {
    case space::hsl: c = hsl_to_rgb(c[0], c[1], c[2]); [[fallthrough]];
    case space::srgb: return mul(lin_srgb_to_xyz, map3(c, lin_srgb));
    case space::hwb: return mul(lin_srgb_to_xyz, map3(hwb_to_rgb(c[0], c[1], c[2]), lin_srgb));
    case space::srgb_linear: return mul(lin_srgb_to_xyz, c);
    case space::display_p3: return mul(lin_p3_to_xyz, map3(c, lin_srgb));
    case space::display_p3_linear: return mul(lin_p3_to_xyz, c);
    case space::a98_rgb: return mul(lin_a98_to_xyz, map3(c, lin_a98));
    case space::prophoto_rgb:
        return mul(d50_to_d65, mul(lin_prophoto_to_xyz, map3(c, lin_prophoto)));
    case space::rec2020: return mul(lin_2020_to_xyz, map3(c, lin_2020));
    case space::xyz_d50: return mul(d50_to_d65, c);
    case space::xyz_d65: return c;
    case space::lab: return lab_to_xyz(c);
    case space::lch: return lab_to_xyz(lch_to_lab(c));
    case space::oklab: return oklab_to_xyz(c);
    case space::oklch: return oklab_to_xyz(lch_to_lab(c));
    }
    return c;
}

[[nodiscard]] vec3 from_xyz(space s, const vec3 & xyz) noexcept {
    switch (s) {
    case space::srgb: return map3(mul(xyz_to_lin_srgb, xyz), gam_srgb);
    case space::hsl: return rgb_to_hsl(map3(mul(xyz_to_lin_srgb, xyz), gam_srgb));
    case space::hwb: return rgb_to_hwb(map3(mul(xyz_to_lin_srgb, xyz), gam_srgb));
    case space::srgb_linear: return mul(xyz_to_lin_srgb, xyz);
    case space::display_p3: return map3(mul(xyz_to_lin_p3, xyz), gam_srgb);
    case space::display_p3_linear: return mul(xyz_to_lin_p3, xyz);
    case space::a98_rgb: return map3(mul(xyz_to_lin_a98, xyz), gam_a98);
    case space::prophoto_rgb:
        return map3(mul(xyz_to_lin_prophoto, mul(d65_to_d50, xyz)), gam_prophoto);
    case space::rec2020: return map3(mul(xyz_to_lin_2020, xyz), gam_2020);
    case space::xyz_d50: return mul(d65_to_d50, xyz);
    case space::xyz_d65: return xyz;
    case space::lab: return xyz_to_lab(xyz);
    case space::lch: return lab_to_lch(xyz_to_lab(xyz));
    case space::oklab: return xyz_to_oklab(xyz);
    case space::oklch: return lab_to_lch(xyz_to_oklab(xyz));
    }
    return xyz;
}

// §4.4.1's POWERLESS HUE: a colourfulness at or under the space's own epsilon
// - `hsl(180 0.001% 50%)` and `lch(20 0.0015 180)` are the corpus's
// boundaries - or an hwb whose white and black fill the whole colour. A
// missing colourfulness is nought here, as it is for every other purpose.
[[nodiscard]] double hue_epsilon(space s) noexcept {
    switch (s) {
    case space::hsl: return 0.001;
    case space::lch: return 0.0015;
    case space::oklch: return 0.000004;
    default: return 0.0;
    }
}

// A colour about to be converted, its powerless hue made missing (§4.4.1):
// the colourfulness that made it powerless goes to nought so the conversion
// does not amplify floating-point noise, and an hwb fills its white and
// black to 100. A colour written by hand never has this done to it -
// `hsl(180 0% 50%)` keeps its hue for as long as it stays hsl.
void settle_powerless(resolved & r) noexcept {
    const int h = hue_slot(r.cs);
    if (h < 0) { return; }
    if (r.cs == space::hwb) {
        const double w = r.none[1] ? 0.0 : r.c[1];
        const double b = r.none[2] ? 0.0 : r.c[2];
        if (w + b < 99.999) { return; }
        r.none[0] = true;
        r.c[0] = 0.0;
        if (w + b < 100.0) {
            if (!r.none[1] && !r.none[2]) {
                r.c[2] = 100.0 - w;
            } else if (!r.none[1]) {
                r.c[1] = 100.0;
            } else if (!r.none[2]) {
                r.c[2] = 100.0;
            }
        }
        return;
    }
    const auto c = static_cast<std::size_t>(colorfulness_slot(r.cs));
    const double chroma = r.none[c] ? 0.0 : r.c[c];
    if (chroma > hue_epsilon(r.cs)) { return; }
    r.none[static_cast<std::size_t>(h)] = true;
    r.c[static_cast<std::size_t>(h)] = 0.0;
    if (chroma > 0.0) { r.c[c] = 0.0; }
}

[[nodiscard]] bool srgb_family(space s) noexcept {
    return s == space::srgb || s == space::hsl || s == space::hwb;
}

// ONE COLOUR IN ANOTHER SPACE: §11.2's algorithm with §12.2's carrying
// forward of missing components. The source's powerless hue is made missing
// first; a missing component lands in the analogous slot of the target; and
// when every source component WITHOUT an analogue is missing, every target
// component without one is missing too - so `lab(50 none none)` is
// `lch(50 none none)`, `hwb(180 none none)` is `hsl(180 none none)`, and
// `rgb(none none none)` is missing everything wherever it goes. A hue the
// conversion produces powerless is missing as well. A colour is never
// converted to its own space, so a hand-written powerless hue stays.
[[nodiscard]] resolved convert(const resolved & from, space to) {
    if (from.cs == to) { return from; }
    resolved src = from;
    settle_powerless(src);
    vec3 c{};
    for (std::size_t i = 0; i < 3; ++i) { c[i] = src.none[i] ? 0.0 : src.c[i]; }
    resolved out;
    out.cs = to;
    out.alpha = from.alpha;
    out.alpha_none = from.alpha_none;
    // Within the sRGB family, and between a Lab and its own LCH, the
    // conversion is the specification's own arithmetic and not a trip through
    // XYZ, so `hsl(120 0% 50%)` comes back exactly 0.5 and rounds to 128
    // rather than 127, and `lab(50 10 0)` is `lch(50 10 0)` and not 360.
    if (srgb_family(src.cs) && srgb_family(to)) {
        vec3 rgb = c;
        if (src.cs == space::hsl) { rgb = hsl_to_rgb(c[0], c[1], c[2]); }
        if (src.cs == space::hwb) { rgb = hwb_to_rgb(c[0], c[1], c[2]); }
        out.c = to == space::srgb ? rgb : (to == space::hsl ? rgb_to_hsl(rgb) : rgb_to_hwb(rgb));
    } else if ((src.cs == space::lab && to == space::lch) ||
               (src.cs == space::oklab && to == space::oklch)) {
        out.c = lab_to_lch(c);
    } else if ((src.cs == space::lch && to == space::lab) ||
               (src.cs == space::oklch && to == space::oklab)) {
        out.c = lch_to_lab(c);
    } else {
        out.c = from_xyz(to, to_xyz(src.cs, c));
    }
    // The conversion's own powerless hue is judged on what it computed - a
    // carried-forward missing saturation does not make `hwb(180 none none)`
    // lose the hue it converts with - and the carried components are
    // re-inserted after (§12.2's order).
    settle_powerless(out);
    const std::array<part, 3> from_parts = parts_of(src.cs);
    const std::array<part, 3> to_parts = parts_of(to);
    bool rest_any = false, rest_all_missing = true;
    for (std::size_t i = 0; i < 3; ++i) {
        const int j = slot_of(to, from_parts[i]);
        if (j >= 0) {
            if (src.none[i]) { out.none[static_cast<std::size_t>(j)] = true; }
        } else {
            rest_any = true;
            rest_all_missing = rest_all_missing && src.none[i];
        }
    }
    if (rest_any && rest_all_missing) {
        for (std::size_t j = 0; j < 3; ++j) {
            if (slot_of(src.cs, to_parts[j]) < 0) { out.none[j] = true; }
        }
    }
    if (const int h = hue_slot(to); h >= 0) {
        out.c[static_cast<std::size_t>(h)] = normalize_hue(out.c[static_cast<std::size_t>(h)]);
    }
    return out;
}

// --- serialising numbers ----------------------------------------------------

// A channel value as every engine prints one: six significant digits, no
// exponent, trailing zeros dropped. `0.501961` for 128/255 and `73.3386` for
// 1.28rad.
[[nodiscard]] std::string channel_text(double v) {
    if (std::isnan(v)) { return "calc(NaN)"; }
    if (std::isinf(v)) { return v > 0 ? "calc(infinity)" : "calc(-infinity)"; }
    if (v == 0.0) { return "0"; }
    const double magnitude = std::fabs(v);
    int decimals = 0;
    if (magnitude < 1e6) {
        const int exponent = static_cast<int>(std::floor(std::log10(magnitude)));
        decimals = std::max(0, 5 - exponent);
    }
    const double scale = std::pow(10.0, decimals);
    const double rounded = std::round(v * scale) / scale;
    if (rounded == 0.0) { return "0"; }
    std::array<char, 64> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                       rounded, std::chars_format::fixed, decimals);
    if (written.ec != std::errc{}) { return "0"; }
    std::string out{buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())};
    if (out.find('.') != std::string::npos) {
        while (out.back() == '0') { out.pop_back(); }
        if (out.back() == '.') { out.pop_back(); }
    }
    if (out == "-0") { return "0"; }
    return out;
}

// A legacy sRGB channel: clamped to the byte and rounded, half up.
[[nodiscard]] std::string byte_text(double v) {
    if (std::isnan(v)) { v = 0; }
    // Snapped to a millionth first: `hwb(120 30% 50%)` is 127.49999999999997
    // of green in doubles and 128 in every browser.
    const double clamped = std::round(std::min(255.0, std::max(0.0, v)) * 1e6) / 1e6;
    return std::to_string(static_cast<int>(std::floor(clamped + 0.5)));
}

// CSSOM §6.7.2's <alphavalue> for an 8-bit alpha: the two-decimal fraction
// that rounds to the same byte if there is one, else three decimals.
[[nodiscard]] std::string legacy_alpha_text(double alpha) {
    if (std::isnan(alpha)) { alpha = 0; }
    alpha = std::min(1.0, std::max(0.0, alpha));
    const int byte = static_cast<int>(std::floor(alpha * 255.0 + 0.5));
    for (int i = 0; i <= 100; ++i) {
        if (static_cast<int>(std::floor(i * 255.0 / 100.0 + 0.5)) == byte) {
            return channel_text(i / 100.0);
        }
    }
    return channel_text(std::round(byte / 255.0 * 1000.0) / 1000.0);
}

} // namespace ctbrowser::style::css::color_detail
