#include "internal.hpp"

namespace ctbrowser::style::css::shorthand_detail {

// --- folding longhand values back into one shorthand value --------------------

[[nodiscard]] std::string join(std::span<const std::string> parts) {
    std::string out;
    for (const std::string & part : parts) {
        if (!out.empty()) { out += ' '; }
        out += part;
    }
    return out;
}

// The shortest `margin` spelling of four sides.
[[nodiscard]] std::string fold_sides(std::span<const std::string> v) {
    std::vector<std::string> parts(v.begin(), v.end());
    if (parts.size() == 4 && parts[3] == parts[1]) { parts.pop_back(); }
    if (parts.size() == 3 && parts[2] == parts[0]) { parts.pop_back(); }
    if (parts.size() == 2 && parts[1] == parts[0]) { parts.pop_back(); }
    return join(parts);
}

// `font`, folded: the keywords that are not `normal`, the size, ` / ` and
// the line-height when it is not `normal`, then the family - CSS Fonts 4
// §3.1's canonical form, which getComputedStyle reads back too, so the
// COMPUTED spellings of the four are folded as well: a weight of `400` is
// the initial, a stretch is one of the nine percentages the shorthand can
// only spell as its keyword, and one it cannot spell makes the whole
// unrepresentable.
[[nodiscard]] std::string fold_font(std::span<const std::string> v) {
    static constexpr std::pair<std::string_view, std::string_view> stretches[] = {
        {"50%", "ultra-condensed"},  {"62.5%", "extra-condensed"}, {"75%", "condensed"},
        {"87.5%", "semi-condensed"}, {"112.5%", "semi-expanded"},  {"125%", "expanded"},
        {"150%", "extra-expanded"},  {"200%", "ultra-expanded"}};
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

// `white-space`, folded: the keyword the longhands spell, else the parts that
// are not initial (`preserve-breaks nowrap`), else `normal`.
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

// `animation`, folded: per item, the components that are not their defaults in the
// canonical order - a delay carries the duration before it, so the one time
// is not read back as the other - and `none` for an item with nothing; ""
// when the lists disagree in length or the reset longhands are not initial.
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

[[nodiscard]] std::string fold_bar(const expansion & e, std::span<const std::string> v) {
    std::vector<std::string> parts;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (ascii_iequals(v[i], initial_of(e.longhands[i]))) { continue; }
        parts.push_back(v[i]);
    }
    if (parts.empty()) { return std::string{e.syntax->all_initial}; }
    return join(parts);
}

// "Serialize a CSS value" of a shorthand's longhands: "" when they cannot be
// one value - a CSS-wide keyword on some but not all, or `border` sides that
// differ.
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
    case shape::pair:
    case shape::place: return v[0] == v[1] ? v[0] : join(v);
    case shape::grid_lines: return fold_grid_lines(v);
    case shape::slash_pair:
        return ascii_iequals(v[1], initial_of(e.longhands[1])) ? v[0] : v[0] + " / " + v[1];
    case shape::bar: return fold_bar(e, v);
    case shape::columns: {
        // column-wrap is reset-only, so its non-initial values cannot be spelled here.
        if (!ascii_iequals(v[3], "auto")) { return {}; }
        std::string text = fold_bar(e, v.first(2));
        if (!ascii_iequals(v[2], "auto")) { text += " / " + v[2]; }
        return text;
    }
    case shape::flex: return join(v);
    case shape::font: return fold_font(v);
    case shape::white_space: return fold_white_space(v);
    case shape::animation: return fold_animation(v);
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
        const expansion * start = expansion_of(e.longhands[0].substr(0, e.longhands[0].size() - 6));
        return fold_bar(*start, v.subspan(0, 3));
    }
    }
    return {};
}

} // namespace ctbrowser::style::css::shorthand_detail
