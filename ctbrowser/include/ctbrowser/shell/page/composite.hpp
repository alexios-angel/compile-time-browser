#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

#include <ctbrowser/paint/paint.hpp>

// `globalCompositeOperation` - how a new pixel combines with the one under it.
//
// Ignoring it was a silent wrong answer with a real victim: p5.js builds a
// tinted image out of FIVE composited draws - luminosity, color, multiply,
// destination-in - and with every mode treated as source-over the `multiply`
// fillRect covers the whole canvas and the `destination-in` that would restore
// the alpha channel does nothing. A tinted sprite came out as a solid rectangle
// of the tint colour. Found by comparing a page against Chrome pixel by pixel.
//
// It is also all of `blendMode()`, which p5 exposes as fourteen names that are
// these operators under different spellings, and it is how any page draws a
// glow, a mask or a knockout.
//
// The maths is the W3C Compositing and Blending Level 1 formula and not an
// approximation of it:
//
//     Cs' = (1 - ab) x Cs + ab x B(Cb, Cs)      the blend, weighted by backdrop
//     co  = as x Fa x Cs' + ab x Fb x Cb        premultiplied result
//     ao  = as x Fa + ab x Fb
//
// with (Fa, Fb) chosen by the Porter-Duff operator. Every mode in the spec is
// one blend function B and one pair of coefficients, so there is no per-mode
// special case anywhere below - which is the point: a table of formulas is
// checkable against the spec line by line, and a pile of cases is not.

namespace ctbrowser::shell {

enum class composite : std::uint8_t {
    // Porter-Duff. source_over is the default and the only one most pages use.
    source_over,
    source_in,
    source_out,
    source_atop,
    destination_over,
    destination_in,
    destination_out,
    destination_atop,
    lighter,
    copy,
    exclusive_or,
    // Separable blend modes: one formula per channel, over source-over.
    multiply,
    screen,
    overlay,
    darken,
    lighten,
    color_dodge,
    color_burn,
    hard_light,
    soft_light,
    difference,
    exclusion,
    // Non-separable: these act on the colour as a whole rather than per channel,
    // which is why they cannot be folded into the table above.
    hue,
    saturation,
    color_blend,
    luminosity,
};

// The spec's own names, so the property round-trips exactly what a page set.
[[nodiscard]] inline composite composite_from_name(std::string_view name) {
    struct entry {
        std::string_view name;
        composite mode;
    };
    static constexpr entry table[] = {
        {"source-over", composite::source_over},
        {"source-in", composite::source_in},
        {"source-out", composite::source_out},
        {"source-atop", composite::source_atop},
        {"destination-over", composite::destination_over},
        {"destination-in", composite::destination_in},
        {"destination-out", composite::destination_out},
        {"destination-atop", composite::destination_atop},
        {"lighter", composite::lighter},
        {"copy", composite::copy},
        {"xor", composite::exclusive_or},
        {"multiply", composite::multiply},
        {"screen", composite::screen},
        {"overlay", composite::overlay},
        {"darken", composite::darken},
        {"lighten", composite::lighten},
        {"color-dodge", composite::color_dodge},
        {"color-burn", composite::color_burn},
        {"hard-light", composite::hard_light},
        {"soft-light", composite::soft_light},
        {"difference", composite::difference},
        {"exclusion", composite::exclusion},
        {"hue", composite::hue},
        {"saturation", composite::saturation},
        {"color", composite::color_blend},
        {"luminosity", composite::luminosity},
    };
    for (const entry & known : table) {
        if (known.name == name) { return known.mode; }
    }
    // An UNKNOWN value leaves the operator alone, per spec - assigning rubbish to
    // globalCompositeOperation is not an error and must not silently change how
    // the next shape is drawn.
    return composite::source_over;
}

// WHETHER A PIXEL THE SOURCE NEVER TOUCHED HAS TO CHANGE.
//
// Five operators clear the rest of the canvas: put as = 0 in the formula above
// and ao comes out 0 for source-in, source-out, destination-in,
// destination-atop and copy. So `destination-in` with a small shape does not
// mask that shape - it throws away everything outside it, which is exactly what
// a page uses it for.
//
// canvas_context handles this by snapshotting the backdrop and clearing the
// surface before such a draw: an untouched pixel is then transparent, which is
// the right answer, and a touched one composites against the snapshot.
[[nodiscard]] inline constexpr bool clears_untouched(composite mode) noexcept {
    return mode == composite::source_in || mode == composite::source_out ||
           mode == composite::destination_in || mode == composite::destination_atop ||
           mode == composite::copy;
}

namespace detail {

// --- the separable blend functions, per channel, on 0..1 ------------------

// Not constexpr: soft-light takes a square root, and std::sqrt is not.
[[nodiscard]] inline float blend_channel(composite mode, float cb, float cs) {
    switch (mode) {
    case composite::multiply: return cb * cs;
    case composite::screen: return cb + cs - cb * cs;
    case composite::overlay:
        // Hard-light with the arguments swapped, which is what the spec says it
        // is - writing it that way keeps the two from drifting apart.
        return blend_channel(composite::hard_light, cs, cb);
    case composite::darken: return std::min(cb, cs);
    case composite::lighten: return std::max(cb, cs);
    case composite::color_dodge:
        if (cb <= 0.0f) { return 0.0f; }
        if (cs >= 1.0f) { return 1.0f; }
        return std::min(1.0f, cb / (1.0f - cs));
    case composite::color_burn:
        if (cb >= 1.0f) { return 1.0f; }
        if (cs <= 0.0f) { return 0.0f; }
        return 1.0f - std::min(1.0f, (1.0f - cb) / cs);
    case composite::hard_light:
        return cs <= 0.5f ? blend_channel(composite::multiply, cb, 2.0f * cs)
                          : blend_channel(composite::screen, cb, 2.0f * cs - 1.0f);
    case composite::soft_light: {
        if (cs <= 0.5f) { return cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb); }
        const float d =
            cb <= 0.25f ? ((16.0f * cb - 12.0f) * cb + 4.0f) * cb : std::sqrt(std::max(0.0f, cb));
        return cb + (2.0f * cs - 1.0f) * (d - cb);
    }
    case composite::difference: return std::abs(cb - cs);
    case composite::exclusion: return cb + cs - 2.0f * cb * cs;
    // Everything else does not blend: the source colour goes through unchanged
    // and the Porter-Duff coefficients do all the work.
    default: return cs;
    }
}

// --- the non-separable ones ------------------------------------------------
//
// These are defined on the colour as a whole, in the spec's own terms: Lum, Sat,
// ClipColor, SetLum, SetSat. Written out rather than approximated, because
// "roughly luminosity" is the kind of thing that looks fine and is wrong.

struct rgb {
    float r = 0;
    float g = 0;
    float b = 0;
};

[[nodiscard]] inline constexpr float luminance(rgb c) noexcept {
    return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b;
}

[[nodiscard]] inline rgb clip_color(rgb c) {
    const float lum = luminance(c);
    const float low = std::min({c.r, c.g, c.b});
    const float high = std::max({c.r, c.g, c.b});
    if (low < 0.0f) {
        const float span = lum - low;
        if (span > 0.0f) {
            c.r = lum + (c.r - lum) * lum / span;
            c.g = lum + (c.g - lum) * lum / span;
            c.b = lum + (c.b - lum) * lum / span;
        }
    }
    if (high > 1.0f) {
        const float span = high - lum;
        if (span > 0.0f) {
            c.r = lum + (c.r - lum) * (1.0f - lum) / span;
            c.g = lum + (c.g - lum) * (1.0f - lum) / span;
            c.b = lum + (c.b - lum) * (1.0f - lum) / span;
        }
    }
    return c;
}

[[nodiscard]] inline rgb set_luminance(rgb c, float want) {
    const float shift = want - luminance(c);
    return clip_color(rgb{c.r + shift, c.g + shift, c.b + shift});
}

[[nodiscard]] inline constexpr float saturation_of(rgb c) noexcept {
    return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b});
}

[[nodiscard]] inline rgb set_saturation(rgb c, float want) {
    const float low = std::min({c.r, c.g, c.b});
    const float high = std::max({c.r, c.g, c.b});
    if (high <= low) { return rgb{0, 0, 0}; } // no colour to stretch
    const auto scale = [&](float v) { return (v - low) / (high - low) * want; };
    return rgb{scale(c.r), scale(c.g), scale(c.b)};
}

[[nodiscard]] inline rgb blend_non_separable(composite mode, rgb cb, rgb cs) {
    switch (mode) {
    case composite::hue: return set_luminance(set_saturation(cs, saturation_of(cb)), luminance(cb));
    case composite::saturation:
        return set_luminance(set_saturation(cb, saturation_of(cs)), luminance(cb));
    case composite::color_blend: return set_luminance(cs, luminance(cb));
    case composite::luminosity: return set_luminance(cb, luminance(cs));
    default: return cs;
    }
}

[[nodiscard]] inline constexpr bool is_non_separable(composite mode) noexcept {
    return mode == composite::hue || mode == composite::saturation ||
           mode == composite::color_blend || mode == composite::luminosity;
}

// The Porter-Duff coefficients. Every blend mode composites with source-over's
// pair, which is what makes `multiply` a blend and `source-in` an operator.
struct coefficients {
    float fa = 1;
    float fb = 0;
};

[[nodiscard]] inline constexpr coefficients duff(composite mode, float ab, float as) noexcept {
    switch (mode) {
    case composite::source_in: return {ab, 0};
    case composite::source_out: return {1 - ab, 0};
    case composite::source_atop: return {ab, 1 - as};
    case composite::destination_over: return {1 - ab, 1};
    case composite::destination_in: return {0, as};
    case composite::destination_out: return {0, 1 - as};
    case composite::destination_atop: return {1 - ab, as};
    case composite::lighter: return {1, 1};
    case composite::copy: return {1, 0};
    case composite::exclusive_or: return {1 - ab, 1 - as};
    // source-over, and every blend mode.
    default: return {1, 1 - as};
    }
}

} // namespace detail

// One source pixel over one backdrop pixel. Both 0xAARRGGBB and NOT
// premultiplied, which is what the rest of this engine stores - the formula
// wants premultiplied, so this multiplies in and divides back out.
[[nodiscard]] inline std::uint32_t composite_pixel(composite mode, std::uint32_t backdrop,
                                                   std::uint32_t source) {
    // The overwhelmingly common case, and the one that must not pay for any of
    // the rest: an opaque source over anything, source-over.
    if (mode == composite::source_over && (source >> 24) == 0xFF) { return source; }

    static constexpr float inv = 1.0f / 255.0f;
    const float ab = static_cast<float>((backdrop >> 24) & 0xFF) * inv;
    const float as = static_cast<float>((source >> 24) & 0xFF) * inv;
    const detail::rgb cb{static_cast<float>((backdrop >> 16) & 0xFF) * inv,
                         static_cast<float>((backdrop >> 8) & 0xFF) * inv,
                         static_cast<float>(backdrop & 0xFF) * inv};
    const detail::rgb cs{static_cast<float>((source >> 16) & 0xFF) * inv,
                         static_cast<float>((source >> 8) & 0xFF) * inv,
                         static_cast<float>(source & 0xFF) * inv};

    // Cs' - the blended colour, weighted by how opaque the backdrop is. With no
    // backdrop there is nothing to blend WITH, so the source passes through:
    // that is why `multiply` onto a transparent canvas draws the source rather
    // than black.
    detail::rgb blended = cs;
    if (detail::is_non_separable(mode)) {
        blended = detail::blend_non_separable(mode, cb, cs);
    } else {
        blended.r = detail::blend_channel(mode, cb.r, cs.r);
        blended.g = detail::blend_channel(mode, cb.g, cs.g);
        blended.b = detail::blend_channel(mode, cb.b, cs.b);
    }
    blended.r = (1 - ab) * cs.r + ab * blended.r;
    blended.g = (1 - ab) * cs.g + ab * blended.g;
    blended.b = (1 - ab) * cs.b + ab * blended.b;

    const detail::coefficients k = detail::duff(mode, ab, as);
    const float out_alpha = std::clamp(as * k.fa + ab * k.fb, 0.0f, 1.0f);
    if (out_alpha <= 0.0f) { return 0; } // fully transparent has no colour

    const auto channel = [&](float source_channel, float backdrop_channel) {
        // Premultiplied out, then divided back by the result's alpha - which is
        // the step that is easy to leave out and shows up as a colour that goes
        // pale wherever it is semi-transparent.
        const float premultiplied = as * k.fa * source_channel + ab * k.fb * backdrop_channel;
        return static_cast<std::uint32_t>(
            std::clamp(premultiplied / out_alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    const auto alpha_byte = static_cast<std::uint32_t>(out_alpha * 255.0f + 0.5f);
    return (alpha_byte << 24) | (channel(blended.r, cb.r) << 16) | (channel(blended.g, cb.g) << 8) |
           channel(blended.b, cb.b);
}

} // namespace ctbrowser::shell
