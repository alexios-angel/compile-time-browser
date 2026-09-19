#include "internal.hpp"

namespace ctbrowser::style::css::color_detail {

// --- the specified serialisation ---------------------------------------------

// A literal channel in the function's own unit: a percentage becomes the
// number it stands for, an angle its degrees.
[[nodiscard]] double literal_value(const channel & c, space cs, int slot, bool bytes) noexcept {
    switch (c.k) {
    case channel::kind::percent: return c.value * percent_reference(cs, slot, bytes) / 100.0;
    case channel::kind::calc:
        if (c.resolved_kind == channel::kind::percent) {
            return c.resolved_value * percent_reference(cs, slot, bytes) / 100.0;
        }
        return c.resolved_value;
    default: return c.value;
    }
}

// The specification's parse-time clamps: rgb() channels to the byte, hsl()
// saturation and every chroma to nought and above, lightness to its range,
// alpha to [0, 1].
[[nodiscard]] double clamp_literal(double v, space cs, int slot, bool absolute,
                                   bool bytes) noexcept {
    if (std::isnan(v)) { v = 0; }
    if (slot == 3) { return std::min(1.0, std::max(0.0, v)); }
    switch (cs) {
    case space::srgb: return absolute && bytes ? std::min(255.0, std::max(0.0, v)) : v;
    case space::hsl:
        if (slot == 1 && absolute) { return std::max(0.0, v); }
        return slot == 0 ? normalize_hue(v) : v;
    case space::hwb: return slot == 0 ? normalize_hue(v) : v;
    case space::lab:
    case space::lch:
        if (slot == 0) { return std::min(100.0, std::max(0.0, v)); }
        if (cs == space::lch && slot == 1) { return std::max(0.0, v); }
        if (cs == space::lch && slot == 2) { return normalize_hue(v); }
        return v;
    case space::oklab:
    case space::oklch:
        if (slot == 0) { return std::min(1.0, std::max(0.0, v)); }
        if (cs == space::oklch && slot == 1) { return std::max(0.0, v); }
        if (cs == space::oklch && slot == 2) { return normalize_hue(v); }
        return v;
    default: return v;
    }
}

[[nodiscard]] bool channel_settled(const channel & c) noexcept {
    return c.k == channel::kind::number || c.k == channel::kind::percent ||
           c.k == channel::kind::angle || (c.k == channel::kind::calc && c.resolvable);
}

// Every channel known at parse time, `none` counted as known only when
// `none_ok`: the legacy serialisation resolves `rgb(none none none)` as
// black but keeps `hsl(120 none 50%)` in the modern form.
[[nodiscard]] bool all_settled(const parsed & p, bool none_ok) noexcept {
    for (const channel & c : p.ch) {
        if (c.k == channel::kind::none ? !none_ok : !channel_settled(c)) { return false; }
    }
    if (p.alpha && (p.alpha->k == channel::kind::none ? !none_ok : !channel_settled(*p.alpha))) {
        return false;
    }
    return true;
}

// An absolute rgb()/hsl()/hwb() with every channel settled, as sRGB.
[[nodiscard]] resolved settled_legacy(const parsed & p) {
    vec3 c{};
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const channel & ch = p.ch[slot];
        c[slot] = ch.k == channel::kind::none
                      ? 0.0
                      : clamp_literal(literal_value(ch, p.cs, static_cast<int>(slot), true), p.cs,
                                      static_cast<int>(slot), true, true);
    }
    resolved out;
    out.legacy = true;
    if (p.cs == space::srgb) {
        for (std::size_t i = 0; i < 3; ++i) { out.c[i] = c[i] / 255.0; }
    } else if (p.cs == space::hsl) {
        out.c = hsl_to_rgb(c[0], c[1], c[2]);
    } else {
        out.c = hwb_to_rgb(c[0], c[1], c[2]);
    }
    if (p.alpha) {
        out.alpha =
            p.alpha->k == channel::kind::none
                ? 0.0
                : clamp_literal(literal_value(*p.alpha, p.cs, 3, true), p.cs, 3, true, true);
    }
    return out;
}

// An absolute hsl()/hwb() an `rgb()` serialisation cannot stand in for:
// its hue is powerless as written (§4.4.1), or its lightness is at either
// end, so that converting the rgb() back makes the hue MISSING where the
// author's colour kept it.
[[nodiscard]] bool origin_loses_in_rgb(const parsed & p) {
    if (p.cs != space::hsl && p.cs != space::hwb) { return false; }
    vec3 c{};
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const channel & ch = p.ch[slot];
        c[slot] = ch.k == channel::kind::none
                      ? 0.0
                      : clamp_literal(literal_value(ch, p.cs, static_cast<int>(slot), true), p.cs,
                                      static_cast<int>(slot), true, true);
    }
    if (p.cs == space::hwb) { return c[1] + c[2] >= 99.999; }
    return c[1] <= hue_epsilon(space::hsl) || c[2] <= 0.0 || c[2] >= 100.0;
}

[[nodiscard]] std::string legacy_text(const resolved & r) {
    std::string out = byte_text(r.c[0] * 255.0) + ", " + byte_text(r.c[1] * 255.0) + ", " +
                      byte_text(r.c[2] * 255.0);
    const double alpha = r.alpha_none ? 0.0 : r.alpha;
    if (alpha >= 1.0) { return "rgb(" + out + ")"; }
    return "rgba(" + out + ", " + legacy_alpha_text(alpha) + ")";
}

// One channel of an absolute colour in the modern syntax.
[[nodiscard]] std::string modern_channel_text(const channel & c, space cs, int slot,
                                              bool resolve_calc, bool bytes) {
    switch (c.k) {
    case channel::kind::none: return "none";
    case channel::kind::keyword: return c.text;
    case channel::kind::calc:
        if (resolve_calc && c.resolvable) {
            return channel_text(
                clamp_literal(literal_value(c, cs, slot, bytes), cs, slot, true, bytes));
        }
        return c.text;
    default:
        return channel_text(
            clamp_literal(literal_value(c, cs, slot, bytes), cs, slot, true, bytes));
    }
}

// A relative colour's channel: kept as written, numbers canonical.
[[nodiscard]] std::string relative_channel_text(const channel & c) {
    switch (c.k) {
    case channel::kind::none: return "none";
    case channel::kind::keyword: return c.text;
    case channel::kind::calc: return c.text;
    case channel::kind::percent: return channel_text(c.value) + "%";
    case channel::kind::angle: return channel_text(c.value) + "deg";
    default: return channel_text(c.value);
    }
}

[[nodiscard]] std::string absolute_specified(const parsed & p, bool as_origin) {
    const bool srgb_function = p.cs == space::srgb || p.cs == space::hsl || p.cs == space::hwb;
    const bool legacy_function = p.fn == "rgb" || p.fn == "hsl" || p.fn == "hwb";
    // §15.2: a legacy-syntax colour with every channel known is `rgb()`, and
    // a top-level rgb() with a missing channel is one with that channel at
    // nought.
    //
    // AN ORIGIN IS THE EXCEPTION, on purpose. The cascade is string-typed:
    // the computed value of `hsl(from hsl(120 none 50%) h s l)` is computed
    // from this serialisation, so an origin serialised as `rgb(128, 128,
    // 128)` has lost its missing saturation, its hsl space (a hue that is
    // powerless in hsl is missing after a conversion FROM rgb, and kept
    // when there is none), and its exact channels. Chromium prints the
    // rgb() here because it keeps the parsed tree beside the text; this
    // engine keeps the modern form when the rgb() would lose something -
    // a missing channel, a powerless hue, a lightness at either end - which
    // is what the computed side (color-computed-none, -powerless,
    // -relative-color, -color-mix) measures, at the cost of the seventy-odd
    // specified serialisations that expect the rgb() form.
    if (legacy_function && srgb_function && all_settled(p, p.fn == "rgb" && !as_origin) &&
        !(as_origin && origin_loses_in_rgb(p))) {
        return legacy_text(settled_legacy(p));
    }
    std::string out = p.fn + "(";
    if (p.fn == "color") { out += std::string{space_name(p.cs)} + " "; }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (slot != 0) { out += ' '; }
        out +=
            modern_channel_text(p.ch[slot], p.cs, static_cast<int>(slot), legacy_function, p.bytes);
    }
    if (p.alpha) {
        const channel & a = *p.alpha;
        const bool one = channel_settled(a) && clamp_literal(literal_value(a, p.cs, 3, p.bytes),
                                                             p.cs, 3, true, p.bytes) >= 1.0;
        if (!(one && (a.k != channel::kind::calc || legacy_function))) {
            out += " / " + modern_channel_text(a, p.cs, 3, legacy_function, p.bytes);
        }
    }
    return out + ")";
}

[[nodiscard]] std::string relative_specified(const parsed & p) {
    std::string out = p.fn + "(from " + serialize_specified(*p.origin, true) + " ";
    if (p.fn == "color") { out += std::string{space_name(p.cs)} + " "; }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (slot != 0) { out += ' '; }
        out += relative_channel_text(p.ch[slot]);
    }
    if (p.alpha) { out += " / " + relative_channel_text(*p.alpha); }
    return out + ")";
}

[[nodiscard]] std::string mix_specified(const parsed & p) {
    std::string out = "color-mix(";
    if (p.mix_space != space::oklab || !p.hue_method.empty()) {
        out += "in " + std::string{space_name(p.mix_space)};
        if (!p.hue_method.empty()) { out += " " + p.hue_method + " hue"; }
        out += ", ";
    }
    // The weights: the ones the author left out are derived from the ones
    // written (CSS Values 5's normalisation, the remainder shared equally),
    // and when every weight is the even share they are all omitted - so
    // `red 50%, blue 50%` is `red, blue` and `red 50%, green, blue` is
    // `red 50%, green 25%, blue 25%`. A calc() weight keeps every item as
    // written.
    std::vector<std::optional<double>> weights;
    bool unresolved = false;
    std::size_t omitted = 0;
    double specified = 0.0;
    for (const mix_item & item : p.items) {
        if (!item.weight) {
            weights.emplace_back(std::nullopt);
            ++omitted;
        } else if (item.weight->k == channel::kind::percent) {
            weights.emplace_back(item.weight->value);
        } else {
            unresolved = true;
            weights.emplace_back(std::nullopt);
        }
        if (weights.back()) { specified += *weights.back(); }
    }
    if (!unresolved && omitted < p.items.size()) {
        for (std::optional<double> & w : weights) {
            if (!w) { w = (100.0 - std::min(100.0, specified)) / static_cast<double>(omitted); }
        }
        const double share = 100.0 / static_cast<double>(p.items.size());
        if (std::ranges::all_of(weights, [&](const auto & w) { return *w == share; })) {
            for (std::optional<double> & w : weights) { w.reset(); }
        }
    }
    for (std::size_t i = 0; i < p.items.size(); ++i) {
        if (i != 0) { out += ", "; }
        out += serialize_specified(*p.items[i].color, true);
        if (unresolved) {
            if (p.items[i].weight) { out += " " + relative_channel_text(*p.items[i].weight); }
        } else if (weights[i]) {
            out += " " + channel_text(*weights[i]) + "%";
        }
    }
    return out + ")";
}

std::string serialize_specified(const parsed & p, bool as_origin) {
    switch (p.k) {
    case parsed::kind::keyword: return p.keyword;
    case parsed::kind::hex: return legacy_text(p.fixed);
    case parsed::kind::absolute: return absolute_specified(p, as_origin);
    case parsed::kind::relative: return relative_specified(p);
    case parsed::kind::mix: return mix_specified(p);
    case parsed::kind::light_dark:
        return "light-dark(" + serialize_specified(*p.items[0].color, false) + ", " +
               serialize_specified(*p.items[1].color, false) + ")";
    case parsed::kind::alpha_fn:
        return "alpha(from " + serialize_specified(*p.origin, true) + " / " +
               relative_channel_text(*p.alpha) + ")";
    case parsed::kind::contrast:
        return "contrast-color(" + serialize_specified(*p.origin, false) + ")";
    case parsed::kind::layers: {
        std::string out = "color-layers(";
        if (!p.blend_mode.empty()) { out += p.blend_mode + ", "; }
        for (std::size_t i = 0; i < p.items.size(); ++i) {
            if (i != 0) { out += ", "; }
            out += serialize_specified(*p.items[i].color, false);
        }
        return out + ")";
    }
    }
    return {};
}

} // namespace ctbrowser::style::css::color_detail
