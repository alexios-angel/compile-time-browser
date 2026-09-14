// Computed-value shorthand expansion. The cascade substitutes variables first;
// this module borrows that value while CSSOM owns its canonicalized longhands.
#include <ctbrowser/style/css/properties.hpp>

#include <ctbrowser/core/algorithms.hpp>

#include "expansion.hpp"

namespace ctbrowser::style::css {
namespace {

// A shorthand's parts, up to `limit`. The splitting itself is
// core/algorithms.hpp's, because paint needs the same rule with commas.
[[nodiscard]] std::vector<std::string_view> value_parts(std::string_view value, std::size_t limit) {
    std::vector<std::string_view> parts = split_top_level(value, " \t\n\r\f");
    if (parts.size() > limit) { parts.resize(limit); }
    return parts;
}

// Is this part of a `border` shorthand a STYLE keyword? The `border` grammar is
// `<width> || <style> || <color>` in ANY order, so its parts are classified by
// what they are rather than by where they sit - unlike the side lists, which are
// positional.
[[nodiscard]] bool is_border_style(std::string_view part) {
    return ascii_iequals_any(part, {"none", "hidden", "dotted", "dashed", "solid", "double",
                                    "groove", "ridge", "inset", "outset"});
}
// A `background` component that names a longhand OTHER than the colour or
// the image: repeat, attachment, position, size, clip/origin boxes.
[[nodiscard]] bool is_background_keyword(std::string_view part) {
    return ascii_iequals_any(
        part, {"none",       "repeat",      "repeat-x",    "repeat-y", "no-repeat", "space",
               "round",      "scroll",      "fixed",       "local",    "center",    "top",
               "bottom",     "left",        "right",       "cover",    "contain",   "auto",
               "border-box", "padding-box", "content-box", "text"});
}
[[nodiscard]] bool is_border_width(std::string_view part) {
    if (ascii_iequals(part, "thin") || ascii_iequals(part, "medium") ||
        ascii_iequals(part, "thick")) {
        return true;
    }
    return !part.empty() && (part.front() == '.' || part.front() == '-' || part.front() == '+' ||
                             (part.front() >= '0' && part.front() <= '9'));
}

// Names are shared with CSSOM; only the value storage/validation differs.
[[nodiscard]] std::vector<std::pair<std::string_view, std::string_view>> positional(
    std::string_view property, std::span<const std::string_view> parts) {
    const auto names = longhands_of(property);
    const auto values = detail::expand_positional<std::string_view>(parts, names.size());
    std::vector<std::pair<std::string_view, std::string_view>> out;
    for (std::size_t i = 0; i < values.size(); ++i) { out.emplace_back(names[i], values[i]); }
    return out;
}

} // namespace

std::vector<std::pair<std::string_view, std::string_view>> expand_cascaded_shorthand(
    std::string_view property, std::string_view value) {
    // A substituted border still has the cascade's permissive classification;
    // CSSOM validates/canonicalizes through its longhand grammars instead.
    constexpr std::array<std::string_view, 4> sides{"border-top", "border-right", "border-bottom",
                                                    "border-left"};
    if (property == "border" || std::ranges::find(sides, property) != sides.end()) {
        const auto parts = value_parts(value, 3);
        if (parts.empty()) { return {}; }
        std::string_view width, style, colour;
        for (const auto part : parts) {
            if (style.empty() && is_border_style(part)) {
                style = part;
            } else if (width.empty() && is_border_width(part)) {
                width = part;
            } else if (colour.empty()) {
                colour = part;
            }
        }
        const std::array components{width.empty() ? std::string_view{"medium"} : width,
                                    style.empty() ? std::string_view{"none"} : style,
                                    colour.empty() ? std::string_view{"currentcolor"} : colour};
        std::vector<std::pair<std::string_view, std::string_view>> out;
        const auto append = [&](std::string_view side) {
            const auto names = longhands_of(side);
            for (std::size_t i = 0; i < components.size(); ++i) {
                out.emplace_back(names[i], components[i]);
            }
        };
        if (property == "border") {
            // Keep uniform aliases first, followed by each side's width/style/color.
            out = {{"border-width", components[0]},
                   {"border-style", components[1]},
                   {"border-color", components[2]}};
            for (const auto side : sides) { append(side); }
        } else {
            append(property);
        }
        return out;
    }
    if (property == "border-width" || property == "border-style" || property == "border-color") {
        const auto parts = value_parts(value, 4);
        auto out = positional(property, parts);
        // Readers still consult the uniform first-component alias.
        if (!out.empty()) { out.emplace_back(property, parts.front()); }
        return out;
    }
    // `flex`, WHICH MUST BE EXPANDED RATHER THAN READ: `.col { flex: 1 0 0 }` and
    // a `.flex-grow-0` utility written after it has to win, so the longhands are
    // produced here and flex only ever sees those.
    //
    // THE SHORTHAND'S DEFAULTS ARE NOT THE LONGHANDS' INITIAL VALUES, which is
    // the part that is easy to get wrong: `flex-basis` initial is `auto`, but
    // `flex: 1` means `1 1 0%`. Flexbox 1 §7.1.1 is explicit that the omitted
    // components take these values and not the initial ones, because `flex: 1`
    // is meant to make an item flexible from nothing rather than from its
    // content.
    if (property == "flex") {
        const std::vector<std::string_view> parts = value_parts(value, 3);
        if (parts.empty()) { return {}; }
        const auto is_number = [](std::string_view part) {
            if (part.empty()) { return false; }
            std::size_t at = part.front() == '-' || part.front() == '+' ? 1 : 0;
            bool digits = false;
            for (; at < part.size(); ++at) {
                if (part[at] >= '0' && part[at] <= '9') {
                    digits = true;
                    continue;
                }
                if (part[at] == '.') { continue; }
                return false; // a unit or a `%`, so a width and not a number
            }
            return digits;
        };
        if (parts.size() == 1 && ascii_iequals(parts[0], "initial")) {
            return {{"flex-grow", "0"}, {"flex-shrink", "1"}, {"flex-basis", "auto"}};
        }
        auto components = detail::expand_flex<std::string_view>(
            parts, "0%",
            [is_number](std::string_view part) -> std::optional<std::string_view> {
                return is_number(part) ? std::optional{part} : std::nullopt;
            },
            [](std::string_view part) { return std::optional{part}; });
        if (!components) {
            // Keep the cascade's legacy acceptance of otherwise invalid multi-part
            // forms. CSSOM rejects these; tightening the cascade is a separate change.
            const bool second_is_shrink = is_number(parts[1]);
            components = std::array{parts[0], second_is_shrink ? parts[1] : std::string_view{"1"},
                                    parts.size() > 2   ? parts[2]
                                    : second_is_shrink ? std::string_view{"0%"}
                                                       : parts[1]};
        }
        const auto names = longhands_of(property);
        return {{names[0], (*components)[0]},
                {names[1], (*components)[1]},
                {names[2], (*components)[2]}};
    }

    // `border-radius`, whose four values go round the box CLOCKWISE FROM THE
    // TOP LEFT - not the top/right/bottom/left of the edge shorthands, because
    // these name corners rather than sides. A two-value form is the two
    // diagonals, which has no analogue at all in `margin`.
    //
    // The elliptical `a / b` form gives horizontal radii before the slash and
    // vertical after. Only the first group is kept, which makes every corner
    // circular; Bootstrap writes no elliptical radius, and half of one is a
    // better answer than dropping the declaration. Recorded as a known
    // difference in docs/plans/bootstrap.md.
    if (property == "border-radius") {
        std::string_view circular = value;
        if (const std::size_t slash = circular.find('/'); slash != std::string_view::npos) {
            circular = circular.substr(0, slash);
        }
        return positional(property, value_parts(circular, 4));
    }

    // `gap`, which is ROW then COLUMN - the opposite order to everything else
    // here, and the opposite order to how it reads. It is the one shorthand
    // whose two values are not left-to-right: `gap: 1rem 2rem` is a 1rem gap
    // BETWEEN ROWS and a 2rem one between columns, because the block axis
    // comes first in every Box Alignment shorthand. One value sets both.
    if (property == "gap") { return positional(property, value_parts(value, 2)); }

    // `overflow` is the two physical axes, X then Y. It has to be expanded
    // in the cascade rather than interpreted beside its longhands later:
    //
    //   overflow: hidden; overflow-x: visible
    //
    // leaves Y hidden, while the reverse source order lets the shorthand
    // replace both. Keeping all three declarations and OR-ing their values
    // loses that ordering and incorrectly creates a formatting context.
    if (property == "overflow") {
        const std::vector<std::string_view> parts = split_top_level(value, " \t\n\r\f");
        if (parts.empty() || parts.size() > 2) { return {}; }
        const auto valid = [](std::string_view part) {
            return ascii_iequals_any(part, {"visible", "hidden", "clip", "scroll", "auto",
                                            "overlay", "inherit", "initial", "unset", "revert"});
        };
        if (!valid(parts[0]) || (parts.size() == 2 && !valid(parts[1]))) { return {}; }
        // CSS-wide keywords apply to the whole shorthand and cannot be
        // paired with a second component. `put()` resolves each expanded
        // longhand against the parent/initial value afterwards.
        const auto is_css_wide = [](std::string_view part) {
            return ascii_iequals(part, "inherit") || ascii_iequals(part, "initial") ||
                   ascii_iequals(part, "unset") || ascii_iequals(part, "revert");
        };
        if (parts.size() == 2 && (is_css_wide(parts[0]) || is_css_wide(parts[1]))) { return {}; }
        return positional(property, parts);
    }
    // `list-style` is `<type> || <position> || <image>` in any order, and the
    // only part with a consumer is the type. `none` is ambiguous between the
    // type and the image and CSS says it sets whichever is not otherwise
    // given - which for a lone `none` is both, and the type is the one that
    // matters here.
    if (property == "list-style") {
        const std::vector<std::string_view> parts = value_parts(value, 3);
        if (parts.empty()) { return {}; }
        std::string_view type = "disc";
        std::string_view position = "outside";
        for (const std::string_view part : parts) {
            if (ascii_iequals(part, "inside") || ascii_iequals(part, "outside")) {
                position = part;
            } else if (!ascii_istarts_with(part, "url(")) {
                type = part;
            }
        }
        return {{"list-style-type", type}, {"list-style-position", position}};
    }
    // `background` is `<bg-layer>#? , <final-bg-layer>`, and the two parts
    // with a consumer are the COLOUR and the IMAGE. Every other component is
    // a keyword of some other longhand, a position or size (a number, a
    // percentage or the `/` between them), or the layer comma - so the
    // colour is whatever is left, and only the final layer may carry one.
    // An omitted colour is `transparent`: `background: url(x)` resets a
    // colour set elsewhere, as every shorthand resets what it does not name.
    if (property == "background") {
        const std::vector<std::string_view> parts = value_parts(value, 32);
        if (parts.empty()) { return {}; }
        std::string_view colour = "transparent";
        std::string_view image = "none";
        for (std::string_view part : parts) {
            // `red,` - a layer boundary glued to the part before it.
            const bool comma = !part.empty() && part.back() == ',';
            if (comma) { part.remove_suffix(1); }
            if (ascii_istarts_with(part, "url(") ||
                part.find("gradient(") != std::string_view::npos) {
                image = part;
            } else if (!part.empty() && part != "/" && !is_border_width(part) &&
                       !is_background_keyword(part)) {
                colour = part;
            }
            // The colour belongs to the LAST layer only.
            if (comma) { colour = "transparent"; }
        }
        return {{"background-color", colour}, {"background-image", image}};
    }
    if (property == "inset" || property == "margin" || property == "padding") {
        return positional(property, value_parts(value, 4));
    }
    return {};
}

} // namespace ctbrowser::style::css
