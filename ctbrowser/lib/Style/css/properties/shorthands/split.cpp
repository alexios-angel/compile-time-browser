#include "internal.hpp"

namespace ctbrowser::style::css::shorthand_detail {

[[nodiscard]] std::string canonical(std::string_view longhand, std::string_view part,
                                    bool & valid) {
    const value_check checked = check_declaration(longhand, part, false);
    valid = checked.valid;
    return checked.serialized;
}

// `sides` and `pair`: positional, each part through its longhand's grammar.
split split_positional(const expansion & e, std::span<const std::string_view> parts,
                       std::vector<std::string> & out) {
    const std::size_t n = e.longhands.size();
    if (parts.empty() || parts.size() > n) { return split::invalid; }
    std::vector<std::string> given;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        bool valid = false;
        given.push_back(canonical(e.longhands[i], parts[i], valid));
        if (!valid) { return split::invalid; }
    }
    out = detail::expand_positional<std::string>(given, n);
    return split::ok;
}

// `list-style-image` is freeform and would take anything, so it is held to
// the two shapes an image can have; the type takes the rest.
[[nodiscard]] bool looks_like_image(std::string_view part) {
    return ascii_iequals(part, "none") || ascii_istarts_with(part, "url(") ||
           ascii_istarts_with(part, "image(") || ascii_istarts_with(part, "image-set(") ||
           part.find("gradient(") != std::string_view::npos;
}

// `bar`: each part goes to the first longhand not yet given that accepts it,
// TYPED longhands first - `outline-color` is freeform and would otherwise
// claim `2px` before `outline-width` saw it. A longhand nothing named gets its
// initial value.
split split_bar(const expansion & e, std::span<const std::string_view> parts,
                std::vector<std::string> & out) {
    const std::size_t n = e.longhands.size();
    if (parts.empty() || parts.size() > n) { return split::invalid; }
    // `border-image`'s slice, width and outset are each a list of up to four,
    // so only its one-part form - `none`, or an image - is divided here.
    if (e.syntax->name == "border-image" && parts.size() > 1) { return split::whole; }
    out.assign(n, std::string{});
    std::vector<bool> given(n, false);
    bool none_seen = false;
    for (const std::string_view part : parts) {
        // `border-image`'s `slice / width / outset` is not a list of parts.
        if (part == "/") { return split::whole; }
        std::size_t chosen = n;
        for (const bool typed_pass : {true, false}) {
            for (std::size_t i = 0; i < n && chosen == n; ++i) {
                if (given[i]) { continue; }
                const property_syntax * p = find_property(e.longhands[i]);
                const bool typed = p != nullptr && p->kind != value_kind::freeform;
                if (typed != typed_pass) { continue; }
                if (e.longhands[i] == "list-style-image" && !looks_like_image(part)) { continue; }
                bool valid = false;
                std::string text = canonical(e.longhands[i], part, valid);
                if (!valid) { continue; }
                out[i] = std::move(text);
                given[i] = true;
                chosen = i;
            }
        }
        if (chosen == n) { return split::invalid; }
        if (ascii_iequals(part, "none")) { none_seen = true; }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (given[i]) { continue; }
        // CSS Lists 3 §3.4: a lone `none` in `list-style` is both the image and
        // the type, whichever of the two was not otherwise given.
        if (none_seen && e.longhands[i] == "list-style-type") {
            out[i] = "none";
            continue;
        }
        out[i] = std::string{initial_of(e.longhands[i])};
    }
    return split::ok;
}

// `flex`, whose omitted parts are NOT the longhands' initial values:
// `flex: 1` is `1 1 0`, Flexbox 1 §7.1.1 - the omitted basis is the length 0
// (`0px` serialised), not `0%`: cssom/flex-serialization asks for `0 1 0px`.
split split_flex(std::span<const std::string_view> parts, std::vector<std::string> & out) {
    const auto parse = [](std::string_view name,
                          std::string_view part) -> std::optional<std::string> {
        bool valid = false;
        std::string text = canonical(name, part, valid);
        return valid ? std::optional{std::move(text)} : std::nullopt;
    };
    auto values = detail::expand_flex<std::string>(
        parts, "0px", [parse](std::string_view part) { return parse("flex-grow", part); },
        [parse](std::string_view part) { return parse("flex-basis", part); });
    if (!values) { return split::invalid; }
    out = {(*values)[0], (*values)[1], (*values)[2]};
    return split::ok;
}

// `font`: `[ <style> || <variant> || <weight> || <stretch> ]? <size> [ / <line-height> ]?
// <family>#`, CSS Fonts 4 §3.1. The keywords before the size are told apart by
// name, because three of the four longhands are freeform and would take
// anything; a system font (`caption`, `menu`) stays whole.
split split_font(std::span<const std::string_view> parts, std::vector<std::string> & out) {
    if (parts.empty()) { return split::invalid; }
    static constexpr std::string_view styles = "italic oblique";
    static constexpr std::string_view variants = "small-caps";
    static constexpr std::string_view weights = "bold bolder lighter";
    static constexpr std::string_view stretches =
        "ultra-condensed extra-condensed condensed semi-condensed semi-expanded expanded "
        "extra-expanded ultra-expanded";
    static constexpr std::string_view system =
        "caption icon menu message-box small-caption status-bar";
    if (parts.size() == 1 && has_keyword(system, parts[0])) { return split::whole; }
    std::string style = "normal", variant = "normal", weight = "normal", stretch = "normal";
    std::size_t i = 0;
    for (; i < parts.size(); ++i) {
        const std::string word = ascii_lower_copy(parts[i]);
        bool number = false;
        std::string text;
        if (word == "normal") { continue; }
        if (has_keyword(styles, word)) {
            style = word;
        } else if (has_keyword(variants, word)) {
            variant = word;
        } else if (has_keyword(weights, word) ||
                   (number = check_declaration("font-weight", word, false).valid)) {
            weight = number ? check_declaration("font-weight", word, false).serialized : word;
        } else if (has_keyword(stretches, word)) {
            stretch = word;
        } else {
            break;
        }
    }
    if (i >= parts.size()) { return split::invalid; }
    // The size, with `/ line-height` glued to it, glued to the next part, or
    // standing alone between the two.
    std::string_view size_part = parts[i++];
    std::string_view height_part;
    if (const std::size_t slash = size_part.find('/'); slash != std::string_view::npos) {
        height_part = size_part.substr(slash + 1);
        size_part = size_part.substr(0, slash);
        if (height_part.empty() && i < parts.size()) { height_part = parts[i++]; }
    } else if (i < parts.size() && parts[i].front() == '/') {
        height_part = parts[i++].substr(1);
        if (height_part.empty() && i < parts.size()) { height_part = parts[i++]; }
    }
    const value_check size = check_declaration("font-size", size_part, false);
    if (!size.valid) { return split::invalid; }
    std::string height = "normal";
    if (!height_part.empty()) {
        const value_check checked = check_declaration("line-height", height_part, false);
        if (!checked.valid) { return split::invalid; }
        height = checked.serialized;
    }
    if (i >= parts.size()) { return split::invalid; }
    std::string family;
    for (; i < parts.size(); ++i) {
        if (!family.empty()) { family += ' '; }
        family += parts[i];
    }
    const value_check families = check_declaration("font-family", family, false);
    if (!families.valid) { return split::invalid; }
    out = {style, variant, weight, stretch, size.serialized, height, families.serialized};
    return split::ok;
}

// `border`: width || style || color, then every side, then border-image reset.
split split_border(std::span<const std::string_view> parts, std::vector<std::string> & out) {
    const expansion * top = expansion_of("border-top");
    std::vector<std::string> one;
    const split result = split_bar(*top, parts, one);
    if (result != split::ok) { return result; }
    out.clear();
    for (const std::string & component : one) {
        for (int side = 0; side < 4; ++side) { out.push_back(component); }
    }
    const expansion * border = expansion_of("border");
    for (std::size_t i = 12; i < border->longhands.size(); ++i) {
        out.push_back(std::string{initial_of(border->longhands[i])});
    }
    return split::ok;
}

// `border-block` / `border-inline`: width || style || color on the axis's
// start side, copied to its end side.
split split_border_axis(const expansion & e, std::span<const std::string_view> parts,
                        std::vector<std::string> & out) {
    const expansion * start = expansion_of(e.longhands[0].substr(0, e.longhands[0].size() - 6));
    std::vector<std::string> one;
    const split result = split_bar(*start, parts, one);
    if (result != split::ok) { return result; }
    out = one;
    out.insert(out.end(), one.begin(), one.end());
    return split::ok;
}

// `white-space`: a keyword of the table, or the three longhands in any order,
// each at most once; the trim's `discard-*` words serialise in grammar order.
split split_white_space(std::span<const std::string_view> parts, std::vector<std::string> & out) {
    if (parts.empty()) { return split::invalid; }
    if (parts.size() == 1) {
        for (const auto & [keyword, collapse, mode] : white_space_keywords) {
            if (ascii_iequals(parts[0], keyword)) {
                out = {std::string{collapse}, std::string{mode}, "none"};
                return split::ok;
            }
        }
    }
    static constexpr std::string_view trims = "discard-before discard-after discard-inner";
    const std::string_view collapses = find_property("white-space-collapse")->keywords;
    std::string collapse, mode, trim;
    std::vector<std::string> discards;
    for (const std::string_view part : parts) {
        const std::string word = ascii_lower_copy(part);
        const bool discard = std::find(discards.begin(), discards.end(), word) != discards.end();
        if (collapse.empty() && has_keyword(collapses, word)) {
            collapse = word;
        } else if (mode.empty() && (word == "wrap" || word == "nowrap")) {
            mode = word;
        } else if (trim.empty() && discards.empty() && word == "none") {
            trim = word;
        } else if (trim.empty() && !discard && has_keyword(trims, word)) {
            discards.push_back(word);
        } else {
            return split::invalid;
        }
    }
    for (const std::string_view canonical : split_top_level(trims, " ")) {
        if (std::find(discards.begin(), discards.end(), canonical) == discards.end()) { continue; }
        trim += (trim.empty() ? "" : " ") + std::string{canonical};
    }
    out = {collapse.empty() ? "collapse" : collapse, mode.empty() ? "wrap" : mode,
           trim.empty() ? "none" : trim};
    return split::ok;
}

[[nodiscard]] bool animation_default(std::size_t i, std::string_view text) {
    return ascii_iequals(text, animation_items[i].second) || (i == 0 && ascii_iequals(text, "0s"));
}

// An easing function with its arguments respaced: `cubic-bezier( 0, -2, 1, 3 )`
// is `cubic-bezier(0, -2, 1, 3)`. A keyword is itself.
[[nodiscard]] std::string canonical_easing(std::string text) {
    const std::size_t open = text.find('(');
    if (open == std::string::npos || text.back() != ')') { return text; }
    std::string out = ascii_lower_copy(text.substr(0, open + 1));
    const std::string_view inner{text.data() + open + 1, text.size() - open - 2};
    bool first = true;
    for (const std::string_view argument : split_top_level(inner, ",")) {
        out += (first ? "" : ", ") + std::string{trim(argument, html_whitespace)};
        first = false;
    }
    return out + ")";
}

// `animation`: one `<single-animation>` per comma, each component to the
// first longhand of its kind not yet given - the times in order, the
// keywords of the typed longhands before the name takes what is left.
split split_animation(std::string_view value, std::vector<std::string> & out) {
    std::array<std::string, 8> lists;
    for (const std::string_view item : split_top_level(value, ",")) {
        std::array<std::string, 8> v;
        std::array<bool, 8> given{};
        for (std::size_t i = 0; i < 8; ++i) { v[i] = animation_items[i].second; }
        const std::vector<std::string_view> parts =
            split_top_level(trim(item, html_whitespace), " \t\n\r\f");
        if (parts.empty()) { return split::invalid; }
        for (const std::string_view part : parts) {
            const auto take = [&](std::size_t i, std::string text) {
                v[i] = std::move(text);
                given[i] = true;
            };
            const value_check time = check_declaration("animation-delay", part, false);
            if (time.valid && !is_wide_keyword(time.serialized)) {
                if (!given[0]) {
                    // The first <time> is the duration, which a negative
                    // one cannot be: `animation: -1s -2s` is refused.
                    if (!check_declaration("animation-duration", part, false).valid) {
                        return split::invalid;
                    }
                    take(0, time.serialized);
                } else if (!given[2]) {
                    take(2, time.serialized);
                } else {
                    return split::invalid;
                }
                continue;
            }
            if (!given[1] && parse_easing(part)) {
                take(1,
                     canonical_easing(
                         check_declaration("animation-timing-function", part, false).serialized));
                continue;
            }
            const value_check count = check_declaration("animation-iteration-count", part, false);
            if (!given[3] && count.valid && !is_wide_keyword(count.serialized)) {
                take(3, count.serialized);
                continue;
            }
            bool keyword = false;
            for (const std::size_t i : {std::size_t{4}, std::size_t{5}, std::size_t{6}}) {
                if (given[i] ||
                    !has_keyword(find_property(animation_items[i].first)->keywords, part)) {
                    continue;
                }
                take(i, ascii_lower_copy(part));
                keyword = true;
                break;
            }
            if (keyword) { continue; }
            const value_check name = check_declaration("animation-name", part, false);
            if (given[7] || !name.valid || is_wide_keyword(name.serialized) ||
                name.serialized.find(',') != std::string::npos) {
                return split::invalid;
            }
            take(7, name.serialized);
        }
        for (std::size_t i = 0; i < 8; ++i) { lists[i] += (lists[i].empty() ? "" : ", ") + v[i]; }
    }
    if (lists[0].empty()) { return split::invalid; }
    out.assign(lists.begin(), lists.end());
    for (std::size_t i = 8; i < 11; ++i) {
        out.emplace_back(initial_of(longhands_of("animation")[i]));
    }
    return split::ok;
}

// `container`: the name, then `/` and the type, each checked as its own
// longhand (CSS Conditional 5 §4.3).
split split_slash_pair(const expansion & e, std::string_view value,
                       std::vector<std::string> & out) {
    const std::vector<std::string_view> halves = split_top_level(value, "/");
    if (halves.empty() || halves.size() > 2) { return split::invalid; }
    out.clear();
    for (std::size_t i = 0; i < 2; ++i) {
        if (i >= halves.size()) {
            out.emplace_back(initial_of(e.longhands[i]));
            continue;
        }
        bool valid = false;
        std::string text = canonical(e.longhands[i], trim(halves[i], html_whitespace), valid);
        if (!valid) { return split::invalid; }
        out.push_back(std::move(text));
    }
    return split::ok;
}

split split_value(const expansion & e, std::string_view text, std::vector<std::string> & out) {
    const std::string_view value = trim(text, html_whitespace);
    if (is_wide_keyword(value)) {
        out.assign(e.longhands.size(), ascii_lower_copy(value));
        return split::ok;
    }
    if (e.syntax->kind == shape::all) { return split::invalid; }
    if (e.syntax->kind == shape::whole) { return split::whole; }
    const std::vector<std::string_view> parts = split_top_level(value, " \t\n\r\f");
    switch (e.syntax->kind) {
    case shape::sides:
    case shape::pair: return split_positional(e, parts, out);
    case shape::place: {
        std::string align, justify;
        if (!split_place(e.syntax->name, value, align, justify)) { return split::invalid; }
        out = {std::move(align), std::move(justify)};
        return split::ok;
    }
    case shape::grid_lines:
        return split_grid_lines(e.syntax->name, value, out) ? split::ok : split::invalid;
    case shape::slash_pair: return split_slash_pair(e, value, out);
    case shape::bar: return split_bar(e, parts, out);
    case shape::columns: return split_columns(value, out) ? split::ok : split::invalid;
    case shape::flex: return split_flex(parts, out);
    case shape::border: return split_border(parts, out);
    case shape::border_axis: return split_border_axis(e, parts, out);
    case shape::white_space: return split_white_space(parts, out);
    case shape::animation: return split_animation(value, out);
    case shape::font: return split_font(parts, out);
    case shape::all:
    case shape::whole: break;
    }
    return split::invalid;
}

} // namespace ctbrowser::style::css::shorthand_detail
