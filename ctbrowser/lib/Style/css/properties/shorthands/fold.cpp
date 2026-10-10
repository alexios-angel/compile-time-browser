//===- fold.cpp - Shorthand serialisation --------------------- -*- C++ -*-===//
//
// Serialising a shorthand: folding its longhands' values back into the
// shortest value the shorthand's grammar can spell, or "" when it cannot
// spell them (CSSOM "serialize a CSS value"). Each fold_* function takes the
// longhand values in the order the shorthand's table row lists them.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::style::css::shorthand_detail {

/// Joins `parts` with single spaces.
[[nodiscard]] std::string join(std::span<const std::string> parts) {
    std::string out;
    for (const std::string & part : parts) {
        if (!out.empty()) { out += ' '; }
        out += part;
    }
    return out;
}

/// The shortest `margin` spelling of four sides.
[[nodiscard]] std::string fold_sides(std::span<const std::string> v) {
    std::vector<std::string> parts(v.begin(), v.end());
    if (parts.size() == 4 && parts[3] == parts[1]) { parts.pop_back(); }
    if (parts.size() == 3 && parts[2] == parts[0]) { parts.pop_back(); }
    if (parts.size() == 2 && parts[1] == parts[0]) { parts.pop_back(); }
    return join(parts);
}

/// `border-radius`, folded: each corner's value is `h` or `h v`; the
/// horizontal and vertical radii fold like `margin`, joined by ` / ` when the
/// two differ.
[[nodiscard]] std::string fold_radius(std::span<const std::string> values) {
    std::vector<std::string> horizontal, vertical;
    for (const std::string & value : values) {
        const auto parts = split_top_level(value, html_whitespace);
        if (parts.empty() || parts.size() > 2) { return {}; }
        horizontal.emplace_back(parts.front());
        vertical.emplace_back(parts.back());
    }
    const std::string h = fold_sides(horizontal);
    const std::string v = fold_sides(vertical);
    return h == v ? h : h + " / " + v;
}

/// `border-image`, folded: source, slice, `/ width`, `/ outset`, repeat, each
/// left out at its initial value where the grammar allows; `none` when all are.
[[nodiscard]] std::string fold_border_image(std::span<const std::string> v) {
    std::string out = v[0] == "none" ? "" : v[0];
    const bool width = v[2] != "1", outset = v[3] != "0";
    if (v[1] != "100%" || width || outset) {
        out += (out.empty() ? "" : " ") + v[1];
        if (width || outset) {
            out += " /";
            if (width) { out += " " + v[2]; }
            if (outset) { out += " / " + v[3]; }
        }
    }
    if (v[4] != "stretch") { out += (out.empty() ? "" : " ") + v[4]; }
    return out.empty() ? "none" : out;
}

/// `background`, folded per layer. `v` is in longhands_of("background")
/// order: 0 color, 1 image, 2 position, 3 size, 4 repeat, 5 attachment,
/// 6 origin, 7 clip. The colour is written once, on the last layer.
[[nodiscard]] std::string fold_background(std::span<const std::string> v) {
    const auto names = longhands_of("background");
    std::array<std::vector<std::string_view>, 8> lists;
    for (std::size_t i = 1; i < lists.size(); ++i) {
        lists[i] = split_top_level(v[i], ",");
        if (lists[i].size() != lists[1].size()) { return {}; }
    }
    std::string out;
    for (std::size_t layer = 0; layer < lists[1].size(); ++layer) {
        std::vector<std::string> parts;
        const auto value = [&](std::size_t i) { return trim(lists[i][layer], html_whitespace); };
        const auto initial = [&](std::size_t i) { return value(i) == initial_of(names[i]); };
        if (!initial(1)) { parts.emplace_back(value(1)); }
        if (!initial(2) || !initial(3)) {
            std::string position{value(2)};
            if (split_top_level(position, html_whitespace).size() == 1) { position += " center"; }
            parts.push_back(std::move(position));
            if (!initial(3)) { parts.push_back("/ " + std::string{value(3)}); }
        }
        if (!initial(4)) { parts.emplace_back(value(4)); }
        if (!initial(5)) { parts.emplace_back(value(5)); }
        const bool special_clip = value(7) == "text" || value(7).starts_with("border-area");
        if (special_clip) {
            if (value(6) != "border-box") { parts.emplace_back(value(6)); }
            parts.emplace_back(value(7));
        } else if (!initial(6) || !initial(7)) {
            parts.emplace_back(value(6));
            if (value(6) != value(7)) { parts.emplace_back(value(7)); }
        }
        if (layer + 1 == lists[1].size() && v[0] != initial_of(names[0]) && v[0] != "transparent") {
            parts.push_back(v[0]);
        }
        out += (layer == 0 ? "" : ", ") + (parts.empty() ? std::string{"none"} : join(parts));
    }
    return out;
}

/// `mask`, folded per layer in its longhands' order with the initial values
/// left out: the position when it or the size is not initial, the size after
/// a `/`, and one box for an origin and clip that agree - none at all for
/// `border-box` - or `no-clip` alone after the initial origin; `none` for a
/// layer that is all initial (WPT mask-valid, mask-computed).
[[nodiscard]] std::string fold_mask(std::span<const std::string> v) {
    const auto names = longhands_of("mask");
    // Indices 8 and up are the mask-border longhands, which `mask` only
    // resets: any value but the initial one (or a computed `0px` for an
    // initial `0`) cannot be spelled.
    for (std::size_t i = 8; i < v.size(); ++i) {
        const bool zero = initial_of(names[i]) == "0" && v[i] == "0px";
        if (v[i] != initial_of(names[i]) && !zero) { return {}; }
    }
    std::array<std::vector<std::string_view>, 8> lists;
    for (std::size_t i = 0; i < lists.size(); ++i) {
        lists[i] = split_top_level(v[i], ",");
        if (lists[i].size() != lists[0].size()) { return {}; }
    }
    std::string out;
    for (std::size_t layer = 0; layer < lists[0].size(); ++layer) {
        std::vector<std::string> parts;
        const auto value = [&](std::size_t i) {
            return std::string{trim(lists[i][layer], html_whitespace)};
        };
        const auto initial = [&](std::size_t i) { return value(i) == initial_of(names[i]); };
        if (!initial(0)) { parts.push_back(value(0)); }
        if (!initial(1) || !initial(2)) {
            parts.push_back(value(1));
            if (!initial(2)) { parts.push_back("/ " + value(2)); }
        }
        if (!initial(3)) { parts.push_back(value(3)); }
        if (value(4) == value(5)) {
            if (!initial(4)) { parts.push_back(value(4)); }
        } else if (initial(4) && value(5) == "no-clip") {
            parts.emplace_back("no-clip");
        } else {
            parts.push_back(value(4));
            parts.push_back(value(5));
        }
        if (!initial(6)) { parts.push_back(value(6)); }
        if (!initial(7)) { parts.push_back(value(7)); }
        out += (layer == 0 ? "" : ", ") + (parts.empty() ? std::string{"none"} : join(parts));
    }
    return out;
}

/// `font`, folded: the keywords that are not `normal`, the size, ` / ` and
/// the line-height when it is not `normal`, then the family - CSS Fonts 4
/// §3.1's canonical form, which getComputedStyle reads back too, so the
/// computed spellings of the four are folded as well: a weight of `400` is
/// the initial, a stretch is one of the nine percentages the shorthand can
/// only spell as its keyword, and one it cannot spell makes the whole
/// unrepresentable.
///
/// Indices follow longhands_of("font"): 0 style, 1 variant-caps, 2 weight,
/// 3 stretch, 4 size, 5 line-height, 6 family, 7+ the variant longhands
/// `font` can only reset.
[[nodiscard]] std::string fold_font(std::span<const std::string> v) {
    static constexpr std::pair<std::string_view, std::string_view> stretches[] = {
        {"50%", "ultra-condensed"},  {"62.5%", "extra-condensed"}, {"75%", "condensed"},
        {"87.5%", "semi-condensed"}, {"112.5%", "semi-expanded"},  {"125%", "expanded"},
        {"150%", "extra-expanded"},  {"200%", "ultra-expanded"}};
    // The variant is CSS 2's: `small-caps` or nothing, and every variant
    // longhand `font` only resets must be at its initial value (WPT
    // font-shorthand-variant).
    if (!ascii_iequals_any(v[1], {"normal", "small-caps"})) { return {}; }
    for (std::size_t i = 7; i < v.size(); ++i) {
        if (!ascii_iequals(v[i], "normal")) { return {}; }
    }
    std::vector<std::string> parts;
    for (std::size_t i = 0; i < 4; ++i) {
        if (ascii_iequals(v[i], "normal")) { continue; }
        if (i == 2 && v[i] == "400") { continue; }
        if (i == 3) {
            if (v[i] == "100%") { continue; }
            bool keyword = false;
            for (const auto & [percentage, name] : stretches) {
                if (v[i] == percentage) {
                    parts.emplace_back(name);
                    keyword = true;
                }
            }
            if (keyword) { continue; }
            if (v[i].ends_with('%')) { return {}; }
        }
        parts.push_back(v[i]);
    }
    parts.push_back(v[4]);
    if (!ascii_iequals(v[5], "normal")) {
        parts.push_back("/");
        parts.push_back(v[5]);
    }
    if (v[6].empty()) { return {}; }
    parts.push_back(v[6]);
    return join(parts);
}

/// `font-variant`, folded: `normal` when every longhand is, `none` when only
/// the ligatures are `none`, else the longhands that are not `normal` in the
/// shorthand's order - and nothing when `none` sits beside another value,
/// which the grammar cannot say.
[[nodiscard]] std::string fold_font_variant(std::span<const std::string> v) {
    std::vector<std::string> parts;
    for (const std::string & one : v) {
        if (!ascii_iequals(one, "normal")) { parts.push_back(one); }
    }
    if (parts.empty()) { return "normal"; }
    if (ascii_iequals(v[0], "none") && parts.size() > 1) { return {}; }
    return join(parts);
}

/// `line-clamp`, folded: `none` for the initial three, else the maximum, the
/// ellipsis when it is not `ellipsis`, and `-webkit-legacy` for that
/// continuation - nothing for any other. `-webkit-line-clamp` spells only
/// `none` and a bare integer continued the legacy way.
[[nodiscard]] std::string fold_line_clamp(const expansion & e, std::span<const std::string> v) {
    if (ascii_iequals(v[0], "auto") && ascii_iequals(v[1], "no-ellipsis") &&
        ascii_iequals(v[2], "normal")) {
        return "none";
    }
    const bool legacy = ascii_iequals(v[2], "-webkit-legacy");
    if (e.syntax->name == "-webkit-line-clamp") {
        const bool integer =
            !ascii_iequals_any(v[0], {"auto", "none"}) && v[0].find(' ') == std::string::npos;
        return integer && legacy && ascii_iequals(v[1], "ellipsis") ? v[0] : std::string{};
    }
    if (!legacy && !ascii_iequals(v[2], "collapse")) { return {}; }
    std::string out = v[0];
    if (!ascii_iequals(v[1], "ellipsis")) { out += " " + v[1]; }
    if (legacy) { out += " -webkit-legacy"; }
    return out;
}

/// `white-space`, folded: the keyword the longhands spell, else the parts that
/// are not initial (`preserve-breaks nowrap`), else `normal`.
[[nodiscard]] std::string fold_white_space(std::span<const std::string> v) {
    if (ascii_iequals(v[2], "none")) {
        for (const auto & [keyword, collapse, mode] : white_space_keywords) {
            if (ascii_iequals(v[0], collapse) && ascii_iequals(v[1], mode)) {
                return std::string{keyword};
            }
        }
    }
    std::vector<std::string> parts;
    if (!ascii_iequals(v[0], "collapse")) { parts.push_back(v[0]); }
    if (!ascii_iequals(v[1], "wrap")) { parts.push_back(v[1]); }
    if (!ascii_iequals(v[2], "none")) { parts.push_back(v[2]); }
    return parts.empty() ? std::string{"normal"} : join(parts);
}

/// `animation-range`, folded: per item the start, and the end only when it is
/// not the one the start implies - its range name, or `normal` (or the
/// `100%` that means the same) after a start that names none.
[[nodiscard]] std::string fold_range(std::span<const std::string> v) {
    const std::vector<std::string_view> starts = split_top_level(v[0], ",");
    const std::vector<std::string_view> ends = split_top_level(v[1], ",");
    if (starts.size() != ends.size()) { return {}; }
    std::string out;
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const std::string_view start = trim(starts[i], html_whitespace);
        const std::string_view end = trim(ends[i], html_whitespace);
        const std::string_view first = start.substr(0, start.find(' '));
        const bool named =
            !first.empty() && std::isalpha(static_cast<unsigned char>(first.front())) != 0 &&
            first.find('(') == std::string_view::npos && !ascii_iequals(first, "normal");
        const bool implied =
            named ? ascii_iequals(end, first) : ascii_iequals(end, "normal") || end == "100%";
        out += (out.empty() ? "" : ", ") + std::string{start} +
               (implied ? "" : " " + std::string{end});
    }
    return out;
}

/// `transition`, folded: per item, the property then the components that are
/// not their defaults - a delay carries the duration before it - and the
/// property alone, `all` included, when nothing else is; "" when the lists
/// disagree in length.
[[nodiscard]] std::string fold_transition(std::span<const std::string> v) {
    std::array<std::vector<std::string_view>, 5> lists;
    for (std::size_t i = 0; i < 5; ++i) {
        for (const std::string_view item : split_top_level(v[i], ",")) {
            lists[i].push_back(trim(item, html_whitespace));
        }
        if (lists[i].size() != lists[0].size()) { return {}; }
    }
    std::string out;
    for (std::size_t item = 0; item < lists[0].size(); ++item) {
        const std::string_view property = lists[0][item];
        const std::string_view duration = lists[1][item];
        const std::string_view easing = lists[2][item];
        const std::string_view delay = lists[3][item];
        const std::string_view behavior = lists[4][item];
        std::vector<std::string_view> parts;
        const bool zero_delay = ascii_iequals(delay, "0s");
        if (!ascii_iequals(property, "all")) { parts.push_back(property); }
        if (!ascii_iequals(duration, "0s") || !zero_delay) { parts.push_back(duration); }
        if (!ascii_iequals(easing, "ease")) { parts.push_back(easing); }
        if (!zero_delay) { parts.push_back(delay); }
        if (!ascii_iequals(behavior, "normal")) { parts.push_back(behavior); }
        std::string one;
        for (const std::string_view part : parts) {
            one += (one.empty() ? "" : " ") + std::string{part};
        }
        out += (out.empty() ? "" : ", ") + (one.empty() ? std::string{"all"} : one);
    }
    return out;
}

/// `animation`, folded: per item, the components that are not their defaults in the
/// canonical order - a delay carries the duration before it, so the one time
/// is not read back as the other - and `none` for an item with nothing; ""
/// when the lists disagree in length or the reset longhands are not initial.
[[nodiscard]] std::string fold_animation(std::span<const std::string> v) {
    std::array<std::vector<std::string_view>, 8> lists;
    for (std::size_t i = 0; i < 8; ++i) {
        lists[i] = split_top_level(v[i], ",");
        if (lists[i].size() != lists[0].size()) { return {}; }
    }
    for (std::size_t i = 8; i < 11; ++i) {
        if (!ascii_iequals(v[i], initial_of(longhands_of("animation")[i]))) { return {}; }
    }
    std::string out;
    for (std::size_t item = 0; item < lists[0].size(); ++item) {
        std::vector<std::string> parts;
        for (std::size_t i = 0; i < 8; ++i) {
            const std::string_view text = trim(lists[i][item], html_whitespace);
            const bool delay_needs_duration =
                i == 0 && !animation_default(2, trim(lists[2][item], html_whitespace));
            if (animation_default(i, text) && !delay_needs_duration) { continue; }
            parts.emplace_back(text);
        }
        out += (out.empty() ? "" : ", ") + (parts.empty() ? std::string{"none"} : join(parts));
    }
    return out;
}

/// A shorthand whose parts can appear in any order (`border-top`,
/// `text-decoration`): the non-initial longhands joined by spaces, or the
/// row's all_initial spelling when every longhand is initial.
[[nodiscard]] std::string fold_bar(const expansion & e, std::span<const std::string> v) {
    std::vector<std::string> parts;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (ascii_iequals(v[i], initial_of(e.longhands[i]))) { continue; }
        parts.push_back(v[i]);
    }
    if (parts.empty()) { return std::string{e.syntax->all_initial}; }
    return join(parts);
}

/// "Serialize a CSS value" of a shorthand's longhands: "" when they cannot be
/// one value - a CSS-wide keyword on some but not all, or `border` sides that
/// differ.
[[nodiscard]] std::string fold(const expansion & e, std::span<const std::string> v) {
    bool all_wide = true, any_wide = false;
    for (const std::string & one : v) {
        const bool wide = is_wide_keyword(one) && ascii_iequals(one, v.front());
        all_wide = all_wide && wide;
        any_wide = any_wide || is_wide_keyword(one);
    }
    if (all_wide) { return v.front(); }
    if (any_wide) { return {}; }
    switch (e.syntax->kind) {
    case shape::all:
    case shape::whole: return {};
    case shape::sides: return fold_sides(v);
    case shape::radius: return fold_radius(v);
    case shape::border_image: return fold_border_image(v);
    case shape::background: return fold_background(v);
    case shape::mask: return fold_mask(v);
    case shape::font_synthesis: {
        // Each longhand at `auto` names its synthesis, `oblique-only` names
        // itself, and nothing named is `none`; any other value has no
        // shorthand spelling.
        static constexpr std::array<std::string_view, 4> names = {"weight", "style", "small-caps",
                                                                  "position"};
        std::string out;
        for (std::size_t i = 0; i < names.size() && i < v.size(); ++i) {
            std::string_view word;
            if (ascii_iequals(v[i], "auto")) {
                word = names[i];
            } else if (i == 1 && ascii_iequals(v[i], "oblique-only")) {
                word = "oblique-only";
            } else if (!ascii_iequals(v[i], "none")) {
                return {};
            }
            if (!word.empty()) { out += (out.empty() ? "" : " ") + std::string{word}; }
        }
        return out.empty() ? std::string{"none"} : out;
    }
    case shape::text_spacing:
        if (v[0] == "space-all" && v[1] == "no-autospace") { return "none"; }
        if (v[0] == "auto" && v[1] == "auto") { return "auto"; }
        if (v[0] == "auto" || v[1] == "auto") { return {}; }
        if (v[0] == "normal") { return v[1]; }
        return v[1] == "normal" ? v[0] : v[0] + " " + v[1];
    case shape::position_try: return ascii_iequals(v[0], "normal") ? v[1] : v[0] + " " + v[1];
    case shape::pair:
    case shape::place: return v[0] == v[1] ? v[0] : join(v);
    case shape::grid_lines: return fold_grid_lines(v);
    case shape::grid_template: return fold_grid_template(v);
    case shape::grid: return fold_grid(v);
    case shape::slash_pair:
        return ascii_iequals(v[1], initial_of(e.longhands[1])) ? v[0] : v[0] + " / " + v[1];
    case shape::bar:
    case shape::text_decoration: return fold_bar(e, v);
    case shape::gap_rule: return fold_gap_rule(v);
    case shape::same:
        for (const std::string & one : v) {
            if (one != v.front()) { return {}; }
        }
        return v.front();
    case shape::rule_inset:
        if (v[0] == v[1] && v[0] == v[2] && v[0] == v[3]) { return v[0]; }
        return v[0] + " " + v[1] + " / " + v[2] + " " + v[3];
    case shape::axes: {
        // Only when the rows say what the columns say.
        const std::size_t half = v.size() / 2;
        for (std::size_t i = 0; i < half; ++i) {
            if (v[i] != v[half + i]) { return {}; }
        }
        const expansion * column = expansion_of("column-" + std::string{e.syntax->name});
        return column != nullptr ? fold(*column, v.first(half)) : v.front();
    }
    case shape::columns: {
        // column-wrap is reset-only, so its non-initial values cannot be spelled here.
        if (!ascii_iequals(v[3], "auto")) { return {}; }
        std::string text = fold_bar(e, v.first(2));
        if (!ascii_iequals(v[2], "auto")) { text += " / " + v[2]; }
        return text;
    }
    case shape::flex: return join(v);
    case shape::font: return fold_font(v);
    case shape::font_variant: return fold_font_variant(v);
    case shape::line_clamp: return fold_line_clamp(e, v);
    case shape::white_space: return fold_white_space(v);
    case shape::animation: return fold_animation(v);
    case shape::transition: return fold_transition(v);
    case shape::range: return fold_range(v);
    case shape::border: {
        // Four equal sides per component, and border-image at its initial
        // values - CSS Backgrounds 3 §5.3: `border` resets it, so a block that
        // has it set to anything else cannot be spelled as `border`.
        std::vector<std::string> one;
        for (std::size_t c = 0; c < 3; ++c) {
            for (std::size_t side = 1; side < 4; ++side) {
                if (v[c * 4 + side] != v[c * 4]) { return {}; }
            }
            one.push_back(v[c * 4]);
        }
        for (std::size_t i = 12; i < v.size(); ++i) {
            if (!ascii_iequals(v[i], initial_of(e.longhands[i]))) { return {}; }
        }
        return fold_bar(*expansion_of("border-top"), one);
    }
    case shape::border_axis: {
        // The two sides equal, component by component.
        for (std::size_t c = 0; c < 3; ++c) {
            if (v[c] != v[c + 3]) { return {}; }
        }
        // Strip the "-width" suffix (6 characters) from the first longhand,
        // e.g. border-block-start-width -> border-block-start.
        const expansion * start = expansion_of(e.longhands[0].substr(0, e.longhands[0].size() - 6));
        return fold_bar(*start, v.subspan(0, 3));
    }
    }
    return {};
}

} // namespace ctbrowser::style::css::shorthand_detail
