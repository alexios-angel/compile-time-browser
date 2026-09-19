#include "internal.hpp"

namespace ctbrowser::style::css::color_detail {

// A channel calc() against the context, the relative colour's keywords
// substituted by their numbers first. NaN is nought (CSS Values 4 §10.9's
// censoring at the top level) and an infinity is left for the clamp.
[[nodiscard]] std::optional<double> evaluate_channel(const channel & c, space cs, int slot,
                                                     bool bytes, const resolved * origin,
                                                     const resolve_context & ctx) {
    std::string text = c.raw_calc;
    if (origin != nullptr) {
        const token_stream ts = tokenize(c.raw_calc);
        const std::array<std::string_view, 3> keywords = channel_keywords(origin->cs);
        text.clear();
        for (const css_token & t : ts.tokens) {
            if (t.type == token_type::eof) { break; }
            if (t.type == token_type::ident) {
                const std::string_view word = ts.text_of(t);
                bool replaced = false;
                for (std::size_t i = 0; i < 3; ++i) {
                    if (ascii_iequals(word, keywords[i])) {
                        text += channel_text(origin->none[i] ? 0.0 : origin->c[i]);
                        replaced = true;
                    }
                }
                if (!replaced && ascii_iequals(word, "alpha")) {
                    text += channel_text(origin->alpha_none ? 0.0 : origin->alpha);
                    replaced = true;
                }
                if (replaced) { continue; }
            }
            text += ts.text_of(t);
        }
    }
    const length_context fallback;
    const math_answer answer =
        evaluate_math(text, ctx.lengths != nullptr ? *ctx.lengths : fallback);
    if (answer.outcome != math_outcome::resolved) { return std::nullopt; }
    double v = answer.value.px;
    if (answer.value.has_percent && answer.value.px == 0.0) {
        v = answer.value.percent * percent_reference(cs, slot, bytes) / 100.0;
    }
    if (std::isnan(v)) { v = 0.0; }
    return v;
}

[[nodiscard]] std::optional<channel_answer> channel_value(const channel & c, space cs, int slot,
                                                          bool bytes, const resolved * origin,
                                                          const resolve_context & ctx) {
    channel_answer out;
    switch (c.k) {
    case channel::kind::none: out.none = true; return out;
    case channel::kind::keyword: {
        if (origin == nullptr) { return std::nullopt; }
        if (c.text == "alpha") {
            out.value = origin->alpha;
            out.none = origin->alpha_none;
            return out;
        }
        const std::array<std::string_view, 3> keywords = channel_keywords(origin->cs);
        for (std::size_t i = 0; i < 3; ++i) {
            if (c.text == keywords[i]) {
                out.value = origin->c[i];
                out.none = origin->none[i];
                return out;
            }
        }
        return std::nullopt;
    }
    case channel::kind::calc: {
        const std::optional<double> v = evaluate_channel(c, cs, slot, bytes, origin, ctx);
        if (!v) { return std::nullopt; }
        out.value = *v;
        return out;
    }
    default: out.value = literal_value(c, cs, slot, bytes); return out;
    }
}

// A resolved colour's channels in the units a relative colour's keywords
// read: rgb() in 0..255, everything else as the space keeps it.
[[nodiscard]] resolved as_keywords(resolved r, bool bytes) noexcept {
    if (r.cs == space::srgb && bytes) {
        for (double & v : r.c) { v *= 255.0; }
    }
    return r;
}

[[nodiscard]] std::optional<resolved> resolve_absolute(const parsed & p,
                                                       const resolve_context & ctx, int depth) {
    resolved origin;
    const bool relative = p.k == parsed::kind::relative;
    if (relative) {
        std::optional<resolved> from = resolve(*p.origin, ctx, depth + 1);
        if (!from) { return std::nullopt; }
        origin = as_keywords(convert(*from, p.cs), p.bytes);
    }
    resolved out;
    out.cs = p.cs;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const std::optional<channel_answer> a = channel_value(
            p.ch[slot], p.cs, static_cast<int>(slot), p.bytes, relative ? &origin : nullptr, ctx);
        if (!a) { return std::nullopt; }
        out.none[slot] = a->none;
        out.c[slot] =
            a->none ? 0.0
                    : clamp_literal(a->value, p.cs, static_cast<int>(slot), !relative, p.bytes);
    }
    if (p.alpha) {
        const std::optional<channel_answer> a =
            channel_value(*p.alpha, p.cs, 3, p.bytes, relative ? &origin : nullptr, ctx);
        if (!a) { return std::nullopt; }
        out.alpha_none = a->none;
        out.alpha = a->none ? 0.0 : clamp_literal(a->value, p.cs, 3, true, p.bytes);
    } else if (relative) {
        // CSS Color 5 §4.2: an omitted alpha is the origin's, not opaque.
        out.alpha_none = origin.alpha_none;
        out.alpha = origin.alpha_none ? 0.0 : clamp_literal(origin.alpha, p.cs, 3, true, p.bytes);
    }
    if (p.bytes) {
        for (double & v : out.c) { v /= 255.0; }
    }
    // An absolute rgb()/hsl()/hwb() with every channel present is a legacy
    // colour - serialised as `rgb()` - and STAYS IN ITS OWN SPACE, so that
    // `hsl(from hsl(180 0 50%) h s l)` converts nothing and keeps its hue.
    const bool legacy_function = p.fn == "rgb" || p.fn == "hsl" || p.fn == "hwb";
    out.legacy = legacy_function && !relative && !out.any_none();
    return out;
}

// §12: the interpolation of two colours in `space`, with their weights.
[[nodiscard]] resolved interpolate(resolved a, resolved b, space cs, std::string_view hue_method,
                                   double p1, double p2, double alpha_multiplier) {
    // A missing component takes the other colour's value; missing on both
    // sides stays missing.
    for (std::size_t i = 0; i < 3; ++i) {
        if (a.none[i] && !b.none[i]) {
            a.c[i] = b.c[i];
            a.none[i] = false;
        }
        if (b.none[i] && !a.none[i]) {
            b.c[i] = a.c[i];
            b.none[i] = false;
        }
    }
    if (a.alpha_none && !b.alpha_none) {
        a.alpha = b.alpha;
        a.alpha_none = false;
    }
    if (b.alpha_none && !a.alpha_none) {
        b.alpha = a.alpha;
        b.alpha_none = false;
    }
    const int hue = hue_slot(cs);
    // Premultiplied, hue excepted.
    const auto premultiply = [&](resolved & r) {
        if (r.alpha_none) { return; }
        for (std::size_t i = 0; i < 3; ++i) {
            if (static_cast<int>(i) != hue && !r.none[i]) { r.c[i] *= r.alpha; }
        }
    };
    premultiply(a);
    premultiply(b);
    if (hue >= 0 && !a.none[static_cast<std::size_t>(hue)]) {
        double & h1 = a.c[static_cast<std::size_t>(hue)];
        double & h2 = b.c[static_cast<std::size_t>(hue)];
        h1 = normalize_hue(h1);
        h2 = normalize_hue(h2);
        const double d = h2 - h1;
        if (hue_method == "longer") {
            if (0 < d && d < 180) {
                h1 += 360;
            } else if (-180 < d && d <= 0) {
                h2 += 360;
            }
        } else if (hue_method == "increasing") {
            if (h2 < h1) { h2 += 360; }
        } else if (hue_method == "decreasing") {
            if (h1 < h2) { h1 += 360; }
        } else {
            if (d > 180) {
                h1 += 360;
            } else if (d < -180) {
                h2 += 360;
            }
        }
    }
    resolved out;
    out.cs = cs;
    for (std::size_t i = 0; i < 3; ++i) {
        out.none[i] = a.none[i] && b.none[i];
        out.c[i] = out.none[i] ? 0.0 : a.c[i] * p1 + b.c[i] * p2;
    }
    out.alpha_none = a.alpha_none && b.alpha_none;
    out.alpha = out.alpha_none ? 0.0 : a.alpha * p1 + b.alpha * p2;
    if (!out.alpha_none && out.alpha != 0.0) {
        for (std::size_t i = 0; i < 3; ++i) {
            if (static_cast<int>(i) != hue && !out.none[i]) { out.c[i] /= out.alpha; }
        }
    }
    if (hue >= 0) {
        out.c[static_cast<std::size_t>(hue)] = normalize_hue(out.c[static_cast<std::size_t>(hue)]);
    }
    if (!out.alpha_none) { out.alpha *= alpha_multiplier; }
    return out;
}

[[nodiscard]] std::optional<resolved> resolve_mix(const parsed & p, const resolve_context & ctx,
                                                  int depth) {
    std::vector<resolved> colors;
    std::vector<std::optional<double>> weights;
    for (const mix_item & item : p.items) {
        std::optional<resolved> one = resolve(*item.color, ctx, depth + 1);
        if (!one) { return std::nullopt; }
        colors.push_back(convert(*one, p.mix_space));
        if (!item.weight) {
            weights.emplace_back(std::nullopt);
            continue;
        }
        const std::optional<channel_answer> w =
            channel_value(*item.weight, space::srgb, 3, false, nullptr, ctx);
        if (!w || w->none) { return std::nullopt; }
        weights.emplace_back(std::min(1.0, std::max(0.0, w->value)));
    }
    // CSS Values 5's normalisation of mix percentages, forced: the omitted
    // weights share what the written ones left, the total is scaled to one
    // when it is not nought, and what is short of one comes off the alpha.
    double specified = 0.0;
    std::size_t omitted = 0;
    for (const std::optional<double> & w : weights) {
        if (w) {
            specified += *w;
        } else {
            ++omitted;
        }
    }
    specified = std::min(1.0, specified);
    double total = 0.0;
    for (std::optional<double> & w : weights) {
        if (!w) { w = (1.0 - specified) / static_cast<double>(omitted); }
        total += *w;
    }
    const double alpha_multiplier = total < 1.0 ? total : 1.0;
    if (total > 0.0) {
        for (std::optional<double> & w : weights) { *w /= total; }
    }
    // CSS Color 5 §3.3: the items mixed pairwise in order, each result
    // carrying the combined weight, so a polar space's "shorter" is decided
    // one step at a time.
    resolved out = colors.front();
    double weight = *weights.front();
    for (std::size_t i = 1; i < colors.size(); ++i) {
        const double combined = weight + *weights[i];
        const double progress = combined > 0.0 ? *weights[i] / combined : 0.5;
        out = interpolate(out, colors[i], p.mix_space, p.hue_method, 1.0 - progress, progress, 1.0);
        weight = combined;
    }
    out.legacy = false;
    if (!out.alpha_none) { out.alpha *= alpha_multiplier; }
    return out;
}

std::optional<resolved> resolve(const parsed & p, const resolve_context & ctx, int depth) {
    if (depth > 32) { return std::nullopt; }
    switch (p.k) {
    case parsed::kind::keyword: {
        resolved out;
        out.legacy = true;
        if (p.keyword == "transparent") {
            out.alpha = 0.0;
            return out;
        }
        if (p.keyword == "currentcolor") {
            if (ctx.current_color.empty()) { return std::nullopt; }
            const std::unique_ptr<parsed> current = parse_text(ctx.current_color);
            if (!current || current->k == parsed::kind::keyword) { return std::nullopt; }
            resolve_context without = ctx;
            without.current_color = {};
            return resolve(*current, without, depth + 1);
        }
        const named * hit = find_named(named_colors, p.keyword);
        if (hit == nullptr && ctx.dark) { hit = find_named(dark_system_colors, p.keyword); }
        if (hit == nullptr) { hit = find_named(system_colors, p.keyword); }
        if (hit == nullptr) { return std::nullopt; }
        out.c = {((hit->rgb >> 16) & 0xFF) / 255.0, ((hit->rgb >> 8) & 0xFF) / 255.0,
                 (hit->rgb & 0xFF) / 255.0};
        return out;
    }
    case parsed::kind::hex: return p.fixed;
    case parsed::kind::absolute:
    case parsed::kind::relative: return resolve_absolute(p, ctx, depth);
    case parsed::kind::mix: return resolve_mix(p, ctx, depth);
    case parsed::kind::light_dark: return resolve(*p.items[ctx.dark ? 1 : 0].color, ctx, depth + 1);
    case parsed::kind::alpha_fn: {
        std::optional<resolved> origin = resolve(*p.origin, ctx, depth + 1);
        if (!origin) { return std::nullopt; }
        const std::optional<channel_answer> a =
            channel_value(*p.alpha, origin->cs, 3, false, &*origin, ctx);
        if (!a) { return std::nullopt; }
        origin->alpha_none = a->none;
        origin->alpha = a->none ? 0.0 : clamp_literal(a->value, origin->cs, 3, true, false);
        origin->legacy = false;
        return origin;
    }
    case parsed::kind::contrast: {
        const std::optional<resolved> origin = resolve(*p.origin, ctx, depth + 1);
        if (!origin) { return std::nullopt; }
        // WCAG's relative luminance, against the sRGB the colour maps to.
        const vec3 lin = mul(xyz_to_lin_srgb, to_xyz(origin->cs, origin->c));
        const double luminance = 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2];
        resolved out;
        out.legacy = true;
        const double v = (luminance + 0.05) / 0.05 > 1.05 / (luminance + 0.05) ? 0.0 : 1.0;
        out.c = {v, v, v};
        return out;
    }
    case parsed::kind::layers: return std::nullopt;
    }
    return std::nullopt;
}

// --- the computed serialisation ----------------------------------------------

[[nodiscard]] std::string modern_text(const resolved & r) {
    std::string out;
    const bool functional = r.cs == space::hsl || r.cs == space::hwb || r.cs == space::lab ||
                            r.cs == space::lch || r.cs == space::oklab || r.cs == space::oklch;
    out = functional ? std::string{space_name(r.cs)} + "("
                     : "color(" + std::string{space_name(r.cs)} + " ";
    for (std::size_t i = 0; i < 3; ++i) {
        if (i != 0) { out += ' '; }
        if (r.none[i]) {
            out += "none";
            continue;
        }
        // A hue a hair under 360 prints as 360, which re-parses as 0: print 0.
        std::string text = channel_text(r.c[i]);
        if (static_cast<int>(i) == hue_slot(r.cs) && text == "360") { text = "0"; }
        out += text;
        // A computed hsl()/hwb() writes its percentages as percentages.
        if ((r.cs == space::hsl || r.cs == space::hwb) && i != 0) { out += '%'; }
    }
    if (r.alpha_none) {
        out += " / none";
    } else if (r.alpha < 1.0) {
        out += " / " + channel_text(r.alpha);
    }
    return out + ")";
}

[[nodiscard]] std::string serialize_computed(const resolved & r) {
    if (r.legacy && !r.any_none()) { return legacy_text(convert(r, space::srgb)); }
    // A computed hsl or hwb with nothing missing is the sRGB colour it names.
    if ((r.cs == space::hsl || r.cs == space::hwb) && !r.any_none()) {
        return modern_text(convert(r, space::srgb));
    }
    return modern_text(r);
}

} // namespace ctbrowser::style::css::color_detail
