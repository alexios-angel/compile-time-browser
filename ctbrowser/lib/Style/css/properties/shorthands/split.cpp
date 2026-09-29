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
        // A CSS-wide keyword is a whole value, never one component of one
        // (CSS Cascade 5 §7.3): `inset-block: inherit inherit` is invalid.
        if (parts.size() > 1 && is_wide_keyword(parts[i])) { return split::invalid; }
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
    out.assign(n, std::string{});
    std::vector<bool> given(n, false);
    bool none_seen = false;
    for (const std::string_view part : parts) {
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
// `flex: 1` is `1 1 0%` - the omitted basis is the zero every engine writes as
// `0%` (css-flexbox flex-valid, flex-computed, flex-shorthand) and the one the
// cascade has always expanded it to (properties/cascade.cpp), so `el.style.flex
// = "1"` lays out as the same rule would. cssom/flex-serialization's older
// `0 1 0px` is the one expectation this leaves unmet.
split split_flex(std::span<const std::string_view> parts, std::vector<std::string> & out) {
    const auto parse = [](std::string_view name,
                          std::string_view part) -> std::optional<std::string> {
        bool valid = false;
        std::string text = canonical(name, part, valid);
        return valid ? std::optional{std::move(text)} : std::nullopt;
    };
    auto values = detail::expand_flex<std::string>(
        parts, "0%", [parse](std::string_view part) { return parse("flex-grow", part); },
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
    out.resize(expansion_of("font")->longhands.size(), "normal"); // the variants it resets
    return split::ok;
}

// `font-variant`: `normal` and `none` alone, or keywords and functions each
// handed to the one longhand that takes it and each longhand's words checked
// together by its own grammar - which refuses `sub super` and `jis78 jis83`
// and puts the words in canonical order (CSS Fonts 4 §6.11). `none` is
// `font-variant-ligatures: none` with the rest `normal`.
split split_font_variant(const expansion & e, std::span<const std::string_view> parts,
                         std::vector<std::string> & out) {
    if (parts.empty()) { return split::invalid; }
    out.assign(e.longhands.size(), "normal");
    if (parts.size() == 1 && ascii_iequals_any(parts[0], {"normal", "none"})) {
        out[0] = ascii_lower_copy(parts[0]);
        return split::ok;
    }
    std::vector<std::string> words(e.longhands.size());
    for (const std::string_view part : parts) {
        if (ascii_iequals_any(part, {"normal", "none"}) || is_wide_keyword(part)) {
            return split::invalid;
        }
        std::size_t slot = 0;
        for (; slot < e.longhands.size(); ++slot) {
            bool valid = false;
            (void)canonical(e.longhands[slot], part, valid);
            if (valid) { break; }
        }
        if (slot == e.longhands.size()) { return split::invalid; }
        words[slot] += (words[slot].empty() ? "" : " ") + std::string{part};
    }
    for (std::size_t slot = 0; slot < words.size(); ++slot) {
        if (words[slot].empty()) { continue; }
        bool valid = false;
        out[slot] = canonical(e.longhands[slot], words[slot], valid);
        if (!valid) { return split::invalid; }
    }
    return split::ok;
}

// `line-clamp`: `none` - every longhand initial - or a maximum
// (`<integer [1,∞]> || auto`, written together) and a block ellipsis in
// either order, one at least, then `-webkit-legacy` for the legacy
// continuation (CSS Overflow 4 §5.1). The maximum defaults to `auto`, the
// ellipsis to `ellipsis`, and the continuation is `collapse`.
// `-webkit-line-clamp` is `none` or an integer alone, which continues the
// legacy way with the default ellipsis.
split split_line_clamp(const expansion & e, std::span<const std::string_view> parts,
                       std::vector<std::string> & out) {
    if (parts.size() == 1 && ascii_iequals(parts[0], "none")) {
        out = {"auto", "no-ellipsis", "normal"};
        return split::ok;
    }
    const auto integer = [](std::string_view part, std::string & text) {
        if (ascii_iequals_any(part, {"none", "auto"}) || is_wide_keyword(part)) { return false; }
        bool valid = false;
        text = canonical("max-lines", part, valid);
        return valid;
    };
    std::string number;
    if (e.syntax->name == "-webkit-line-clamp") {
        if (parts.size() != 1 || !integer(parts[0], number)) { return split::invalid; }
        out = {number, "ellipsis", "-webkit-legacy"};
        return split::ok;
    }
    std::span<const std::string_view> rest = parts;
    const bool legacy = !rest.empty() && ascii_iequals(rest.back(), "-webkit-legacy");
    if (legacy) { rest = rest.first(rest.size() - 1); }
    bool automatic = false;
    std::string ellipsis;
    int run = 0; // 0 before the maximum's words, 1 among them, 2 past them
    for (const std::string_view part : rest) {
        const bool is_auto = ascii_iequals(part, "auto");
        std::string text;
        if (is_auto || integer(part, text)) {
            if (run == 2 || (is_auto ? automatic : !number.empty())) { return split::invalid; }
            run = 1;
            if (is_auto) {
                automatic = true;
            } else {
                number = text;
            }
            continue;
        }
        if (run == 1) { run = 2; }
        bool valid = false;
        if (!ellipsis.empty() || is_wide_keyword(part)) { return split::invalid; }
        ellipsis = canonical("block-ellipsis", part, valid);
        if (!valid) { return split::invalid; }
    }
    if (number.empty() && !automatic && ellipsis.empty()) { return split::invalid; }
    out = {number.empty() ? std::string{"auto"}
           : automatic    ? number + " auto"
                          : number,
           ellipsis.empty() ? std::string{"ellipsis"} : ellipsis,
           legacy ? "-webkit-legacy" : "collapse"};
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

// `transition`: per comma-separated item, `[ none | <single-transition-property> ]
// || <time> || <easing-function> || <time> || <transition-behavior-value>`
// (CSS Transitions 2 §3.5) - the first time the duration, which cannot be
// negative, the second the delay - each omitted one its longhand's initial
// value. `none` names no property, so it may stand only in a list of one.
split split_transition(std::string_view value, std::vector<std::string> & out) {
    std::array<std::string, 5> lists;
    const std::vector<std::string_view> items = split_top_level(value, ",");
    for (const std::string_view item : items) {
        std::array<std::string, 5> v{"all", "0s", "ease", "0s", "normal"};
        std::array<bool, 5> given{};
        const std::vector<std::string_view> parts =
            split_top_level(trim(item, html_whitespace), " \t\n\r\f");
        if (parts.empty() || trim(item, html_whitespace).empty()) { return split::invalid; }
        for (const std::string_view part : parts) {
            const auto take = [&](std::size_t i, std::string text) {
                v[i] = std::move(text);
                given[i] = true;
            };
            const value_check time = check_declaration("transition-delay", part, false);
            if (time.valid && !is_wide_keyword(time.serialized)) {
                if (!given[1]) {
                    if (!check_declaration("transition-duration", part, false).valid) {
                        return split::invalid;
                    }
                    take(1, time.serialized);
                } else if (!given[3]) {
                    take(3, time.serialized);
                } else {
                    return split::invalid;
                }
                continue;
            }
            if (!given[4] && ascii_iequals_any(part, {"normal", "allow-discrete"})) {
                take(4, ascii_lower_copy(part));
                continue;
            }
            const value_check easing = check_declaration("transition-timing-function", part, false);
            if (!given[2] && easing.valid && !is_wide_keyword(easing.serialized)) {
                take(2, easing.serialized);
                continue;
            }
            if (given[0]) { return split::invalid; }
            if (ascii_iequals(part, "none")) {
                if (items.size() != 1) { return split::invalid; }
                take(0, "none");
                continue;
            }
            const value_check property = check_declaration("transition-property", part, false);
            if (!property.valid || is_wide_keyword(property.serialized) ||
                property.serialized.find(',') != std::string::npos) {
                return split::invalid;
            }
            take(0, property.serialized);
        }
        for (std::size_t i = 0; i < 5; ++i) { lists[i] += (lists[i].empty() ? "" : ", ") + v[i]; }
    }
    if (lists[0].empty()) { return split::invalid; }
    out.assign(lists.begin(), lists.end());
    return split::ok;
}

// `animation-range`: per item a start and an optional end, each `normal`,
// a <length-percentage>, or a <timeline-range-name> with an optional one. An
// omitted end is the start's range name at 100% when the start names one,
// and `normal` otherwise (Scroll-driven Animations 1 §4.2).
split split_range(std::string_view value, std::vector<std::string> & out) {
    std::string starts, ends;
    for (const std::string_view item : split_top_level(value, ",")) {
        std::vector<std::string_view> words;
        for (const std::string_view raw :
             split_top_level(trim(item, html_whitespace), " \t\n\r\f")) {
            if (!trim(raw, html_whitespace).empty()) {
                words.push_back(trim(raw, html_whitespace));
            }
        }
        if (words.empty()) { return split::invalid; }
        const auto named = [](std::string_view w) {
            const char c = w.front();
            return (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' ||
                    (c == '-' && w.size() > 1 &&
                     std::isdigit(static_cast<unsigned char>(w[1])) == 0 && w[1] != '.')) &&
                   w.find('(') == std::string_view::npos;
        };
        // One component from `at`: a name (and an offset after it), or an offset.
        const auto component = [&](std::size_t & at) {
            const std::size_t from = at;
            if (named(words[at]) && !ascii_iequals(words[at], "normal") && at + 1 < words.size() &&
                !named(words[at + 1])) {
                at += 2;
            } else {
                at += 1;
            }
            std::string text;
            for (std::size_t k = from; k < at; ++k) {
                text += (text.empty() ? "" : " ") + std::string{words[k]};
            }
            return text;
        };
        std::size_t at = 0;
        const std::string first = component(at);
        const value_check start = check_declaration("animation-range-start", first, false);
        if (!start.valid || is_wide_keyword(start.serialized)) { return split::invalid; }
        std::string end;
        if (at < words.size()) {
            const std::string second = component(at);
            const value_check checked = check_declaration("animation-range-end", second, false);
            if (!checked.valid || is_wide_keyword(checked.serialized) || at != words.size()) {
                return split::invalid;
            }
            end = checked.serialized;
        } else {
            end = named(words[0]) && !ascii_iequals(words[0], "normal") ? ascii_lower_copy(words[0])
                                                                        : std::string{"normal"};
        }
        starts += (starts.empty() ? "" : ", ") + start.serialized;
        ends += (ends.empty() ? "" : ", ") + end;
    }
    if (starts.empty()) { return split::invalid; }
    out = {starts, ends};
    return split::ok;
}

// Each radius axis has the same nonnegative <length-percentage> grammar as padding.
bool radius_components(std::string_view value, std::size_t maximum,
                       std::vector<std::string> & out) {
    const auto parts = split_top_level(value, html_whitespace);
    if (parts.empty() || parts.size() > maximum) { return false; }
    out.clear();
    for (const std::string_view part : parts) {
        const value_check checked = check_declaration("padding-top", part, false);
        if (!checked.valid || is_wide_keyword(checked.serialized)) { return false; }
        out.push_back(checked.serialized);
    }
    return true;
}

bool split_radius(std::string_view value, std::vector<std::string> & out) {
    const token_stream ts = tokenize(value);
    std::size_t slash = std::string_view::npos;
    int depth = 0;
    for (const css_token & token : ts.tokens) {
        if (token.type == token_type::function || token.type == token_type::open_paren) {
            ++depth;
        } else if (token.type == token_type::close_paren) {
            --depth;
        } else if (depth == 0 && token.type == token_type::delim && ts.text_of(token) == "/") {
            if (slash != std::string_view::npos) { return false; }
            slash = token.text;
        }
    }
    std::vector<std::string> horizontal, vertical;
    if (!radius_components(value.substr(0, slash), 4, horizontal)) { return false; }
    horizontal = expand_positional<std::string>(horizontal, 4);
    if (slash == std::string_view::npos) {
        out = std::move(horizontal);
        return true;
    }
    if (!radius_components(value.substr(slash + 1), 4, vertical)) { return false; }
    vertical = expand_positional<std::string>(vertical, 4);
    out = std::move(horizontal);
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] != vertical[i]) { out[i] += " " + vertical[i]; }
    }
    return true;
}

// Delimiters outside functions must remain separate even without authored whitespace.
[[nodiscard]] std::string space_slashes(std::string_view value) {
    const token_stream ts = tokenize(value);
    std::string spaced;
    std::size_t start = 0;
    int depth = 0;
    for (const css_token & token : ts.tokens) {
        if (token.type == token_type::function || token.type == token_type::open_paren) {
            ++depth;
        } else if (token.type == token_type::close_paren) {
            --depth;
        } else if (depth == 0 && token.type == token_type::delim && ts.text_of(token) == "/") {
            spaced += value.substr(start, token.text - start);
            spaced += " / ";
            start = token.text + token.length;
        }
    }
    spaced += value.substr(start);
    return spaced;
}

// Each || component is contiguous; a slash can follow only the slice or width.
bool split_border_image(std::string_view value, std::vector<std::string> & out) {
    const std::string spaced = space_slashes(value);
    const auto parts = split_top_level(spaced, html_whitespace);
    const auto names = longhands_of("border-image");
    out.clear();
    for (const std::string_view name : names) { out.emplace_back(initial_of(name)); }
    std::array<bool, 5> given{};
    std::size_t i = 0;
    const auto take = [&](std::size_t slot, std::size_t maximum) {
        if (given[slot]) { return false; }
        for (std::size_t count = std::min(maximum, parts.size() - i); count > 0; --count) {
            const std::string_view text{parts[i].data(),
                                        static_cast<std::size_t>(parts[i + count - 1].data() +
                                                                 parts[i + count - 1].size() -
                                                                 parts[i].data())};
            const value_check checked = check_declaration(names[slot], text, false);
            if (!checked.valid || is_wide_keyword(checked.serialized)) { continue; }
            out[slot] = checked.serialized;
            given[slot] = true;
            i += count;
            return true;
        }
        return false;
    };
    while (i < parts.size()) {
        if (take(0, 1) || take(4, 2)) { continue; }
        if (!take(1, 5)) { return false; }
        if (i == parts.size() || parts[i] != "/") { continue; }
        ++i;
        if (i == parts.size()) { return false; }
        if (parts[i] != "/" && !take(2, 4)) { return false; }
        if (i < parts.size() && parts[i] == "/") {
            ++i;
            if (!take(3, 4)) { return false; }
        }
    }
    return !parts.empty();
}

// CSS Backgrounds: each layer shares the existing longhand grammars.
bool split_background(std::string_view value, std::vector<std::string> & out) {
    const auto names = longhands_of("background");
    const auto layers = split_top_level(value, ",");
    if (layers.empty()) { return false; }
    // split_top_level discards empty fields; the token stream keeps layer boundaries.
    const token_stream ts = tokenize(value);
    int depth = 0;
    std::size_t commas = 0;
    for (const css_token & token : ts.tokens) {
        if (token.type == token_type::function || token.type == token_type::open_paren) { ++depth; }
        if (token.type == token_type::close_paren) { --depth; }
        if (depth == 0 && token.type == token_type::comma) { ++commas; }
    }
    if (commas + 1 != layers.size()) { return false; }
    out.assign(names.size(), {});
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        std::vector<std::string> values;
        for (const auto name : names) { values.emplace_back(initial_of(name)); }
        std::array<bool, 8> given{};
        const std::string spaced = space_slashes(layers[layer]);
        const auto parts = split_top_level(spaced, html_whitespace);
        if (parts.empty()) { return false; }
        std::size_t at = 0;
        const auto take = [&](std::size_t component, std::size_t maximum) {
            if (given[component]) { return false; }
            for (std::size_t count = std::min(maximum, parts.size() - at); count > 0; --count) {
                const std::string_view text{parts[at].data(),
                                            static_cast<std::size_t>(parts[at + count - 1].data() +
                                                                     parts[at + count - 1].size() -
                                                                     parts[at].data())};
                // A layer has one <image>, so use the existing scalar image grammar.
                const value_check checked = check_declaration(
                    component == 1 ? "border-image-source" : names[component], text, false);
                // Do not turn an unsupported image function into a computed image.
                if (!checked.valid || (component == 1 && checked.uses_unknown_function) ||
                    is_wide_keyword(checked.serialized)) {
                    continue;
                }
                values[component] = checked.serialized;
                given[component] = true;
                at += count;
                return true;
            }
            return false;
        };
        while (at < parts.size()) {
            const auto box = [](std::string_view part) {
                return ascii_iequals_any(
                    part, {"border-box", "padding-box", "content-box", "border-area", "text"});
            };
            if (box(parts[at])) {
                if (given[6]) { return false; }
                given[6] = given[7] = true;
                std::vector<std::string> boxes;
                bool area = false, text = false;
                while (at < parts.size() && box(parts[at])) {
                    const std::string word = ascii_lower_copy(parts[at++]);
                    if (word == "border-area") {
                        if (area) { return false; }
                        area = true;
                    } else if (word == "text") {
                        if (text) { return false; }
                        text = true;
                    } else {
                        boxes.push_back(word);
                    }
                }
                if (boxes.size() > (area || text ? 1u : 2u)) { return false; }
                values[6] = boxes.empty() ? "border-box" : boxes.front();
                values[7] = area   ? (text ? "border-area text" : "border-area")
                            : text ? "text"
                                   : boxes.back();
                continue;
            }
            if (take(1, 1) || take(4, 2) || take(5, 1) || take(0, 1)) { continue; }
            if (!take(2, 4)) { return false; }
            if (at < parts.size() && parts[at] == "/") {
                ++at;
                if (!take(3, 2)) { return false; }
            }
        }
        if (given[0] && layer + 1 != layers.size()) { return false; }
        out[0] = values[0];
        for (std::size_t i = 1; i < names.size(); ++i) {
            out[i] += (layer == 0 ? "" : ", ") + values[i];
        }
    }
    return true;
}

// `mask`, CSS Masking 1 §7.1: per comma-separated layer `<mask-reference> ||
// <position> [ / <bg-size> ]? || <repeat-style> || <geometry-box> || [
// <geometry-box> | no-clip ] || <compositing-operator> || <masking-mode>`,
// each part through its longhand's grammar. The boxes need not stand
// together: one sets the origin and the clip, a second - or `no-clip` - the
// clip alone.
bool split_mask(std::string_view value, std::vector<std::string> & out) {
    const auto names = longhands_of("mask");
    const auto layers = split_top_level(value, ",");
    if (layers.empty()) { return false; }
    const token_stream ts = tokenize(value);
    int depth = 0;
    std::size_t commas = 0;
    for (const css_token & token : ts.tokens) {
        if (token.type == token_type::function || token.type == token_type::open_paren) { ++depth; }
        if (token.type == token_type::close_paren) { --depth; }
        if (depth == 0 && token.type == token_type::comma) { ++commas; }
    }
    if (commas + 1 != layers.size()) { return false; }
    out.assign(names.size(), {});
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        std::vector<std::string> values;
        for (const auto name : names) { values.emplace_back(initial_of(name)); }
        std::array<bool, 8> given{};
        const std::string spaced = space_slashes(layers[layer]);
        const auto parts = split_top_level(spaced, html_whitespace);
        if (parts.empty()) { return false; }
        std::vector<std::string> boxes;
        std::size_t at = 0;
        const auto take = [&](std::size_t component, std::size_t maximum) {
            if (given[component]) { return false; }
            for (std::size_t count = std::min(maximum, parts.size() - at); count > 0; --count) {
                const std::string_view text{parts[at].data(),
                                            static_cast<std::size_t>(parts[at + count - 1].data() +
                                                                     parts[at + count - 1].size() -
                                                                     parts[at].data())};
                const value_check checked = check_declaration(
                    component == 0 ? "border-image-source" : names[component], text, false);
                if (!checked.valid || (component == 0 && checked.uses_unknown_function) ||
                    is_wide_keyword(checked.serialized)) {
                    continue;
                }
                values[component] = checked.serialized;
                given[component] = true;
                at += count;
                return true;
            }
            return false;
        };
        const auto keyword = [&](std::size_t component, std::string_view set) {
            if (given[component] || !has_keyword(set, ascii_lower_copy(parts[at]))) {
                return false;
            }
            values[component] = ascii_lower_copy(parts[at++]);
            given[component] = true;
            return true;
        };
        while (at < parts.size()) {
            if (ascii_iequals_any(parts[at], {"content-box", "padding-box", "border-box",
                                              "fill-box", "stroke-box", "view-box", "no-clip"})) {
                boxes.push_back(ascii_lower_copy(parts[at++]));
                continue;
            }
            if (keyword(6, "add subtract intersect exclude") ||
                keyword(7, "alpha luminance match-source") || take(0, 1) || take(3, 2)) {
                continue;
            }
            if (!take(1, 4)) { return false; }
            if (at < parts.size() && parts[at] == "/") {
                ++at;
                if (!take(2, 2)) { return false; }
            }
        }
        const auto clip_only = [](const std::string & box) { return box == "no-clip"; };
        if (boxes.size() > 2 || (boxes.size() == 2 && clip_only(boxes[0]) && clip_only(boxes[1]))) {
            return false;
        }
        if (boxes.size() == 1) {
            if (!clip_only(boxes[0])) { values[4] = boxes[0]; }
            values[5] = boxes[0];
        } else if (boxes.size() == 2) {
            const bool swapped = clip_only(boxes[0]);
            values[4] = boxes[swapped ? 1 : 0];
            values[5] = boxes[swapped ? 0 : 1];
        }
        // ...and mask-border, which the shorthand only resets: one value,
        // not one per layer (CSS Masking 1 §7.1).
        for (std::size_t i = 0; i < names.size(); ++i) {
            out[i] = i < 8 ? out[i] + (layer == 0 ? "" : ", ") + values[i] : values[i];
        }
    }
    return true;
}

// `position-try`, CSS Anchor Positioning 1 §5.4: an order keyword only when
// something follows it, and the rest the fallbacks' own grammar.
split split_position_try(std::string_view value, std::vector<std::string> & out) {
    const std::string_view text = trim(value, html_whitespace);
    const std::size_t space = text.find_first_of(" \t\n\r\f");
    std::string order = "normal";
    std::string_view rest = text;
    if (space != std::string_view::npos &&
        ascii_iequals_any(text.substr(0, space), {"normal", "most-width", "most-height",
                                                  "most-block-size", "most-inline-size"})) {
        order = ascii_lower_copy(text.substr(0, space));
        rest = trim(text.substr(space), html_whitespace);
    }
    const value_check fallbacks = check_declaration("position-try-fallbacks", rest, false);
    if (!fallbacks.valid || is_wide_keyword(fallbacks.serialized)) { return split::invalid; }
    out = {order, fallbacks.serialized};
    return split::ok;
}

// `font-synthesis`, CSS Fonts 4 §4.7: `none | [ weight || [ style |
// oblique-only ] || small-caps || position ]`, each word the `auto` - or,
// for `oblique-only`, the value - of its longhand and every longhand not
// named `none`.
split split_font_synthesis(std::string_view value, std::vector<std::string> & out) {
    out = {"none", "none", "none", "none"};
    std::vector<std::string_view> words;
    for (const std::string_view raw : split_top_level(value, html_whitespace)) {
        if (!trim(raw, html_whitespace).empty()) { words.push_back(trim(raw, html_whitespace)); }
    }
    if (words.empty()) { return split::invalid; }
    if (words.size() == 1 && ascii_iequals(words[0], "none")) { return split::ok; }
    std::array<bool, 4> seen{};
    for (const std::string_view word : words) {
        std::size_t slot = 4;
        std::string_view to = "auto";
        if (ascii_iequals(word, "weight")) {
            slot = 0;
        } else if (ascii_iequals(word, "style") || ascii_iequals(word, "oblique-only")) {
            slot = 1;
            if (ascii_iequals(word, "oblique-only")) { to = "oblique-only"; }
        } else if (ascii_iequals(word, "small-caps")) {
            slot = 2;
        } else if (ascii_iequals(word, "position")) {
            slot = 3;
        }
        if (slot == 4 || seen[slot]) { return split::invalid; }
        seen[slot] = true;
        out[slot] = std::string{to};
    }
    return split::ok;
}

// `text-spacing`, CSS Text 4 §8.1: `none` is space-all and no-autospace,
// `auto` both auto, and otherwise a text-spacing-trim keyword at one end
// with text-autospace's words - or nothing, its initial - beside it.
split split_text_spacing(std::string_view value, std::vector<std::string> & out) {
    std::vector<std::string_view> words;
    for (const std::string_view raw : split_top_level(value, html_whitespace)) {
        if (!trim(raw, html_whitespace).empty()) { words.push_back(trim(raw, html_whitespace)); }
    }
    if (words.size() == 1 && ascii_iequals(words[0], "none")) {
        out = {"space-all", "no-autospace"};
        return split::ok;
    }
    if (words.size() == 1 && ascii_iequals(words[0], "auto")) {
        out = {"auto", "auto"};
        return split::ok;
    }
    for (const std::size_t at : {std::size_t{0}, words.size() - 1}) {
        if (words.empty()) { break; }
        const value_check trim_part = check_declaration("text-spacing-trim", words[at], false);
        if (!trim_part.valid || is_wide_keyword(trim_part.serialized) ||
            ascii_iequals(trim_part.serialized, "auto")) {
            continue;
        }
        std::string rest;
        for (std::size_t i = 0; i < words.size(); ++i) {
            if (i != at) { rest += (rest.empty() ? "" : " ") + std::string{words[i]}; }
        }
        if (rest.empty()) {
            out = {trim_part.serialized, "normal"};
            return split::ok;
        }
        const value_check space = check_declaration("text-autospace", rest, false);
        if (!space.valid || is_wide_keyword(space.serialized) ||
            ascii_iequals(space.serialized, "auto")) {
            continue;
        }
        out = {trim_part.serialized, space.serialized};
        return split::ok;
    }
    // ...or text-autospace's words alone, the trim at its initial `normal`:
    // `no-autospace` (text-spacing-valid).
    if (const value_check space = check_declaration("text-autospace", value, false);
        !words.empty() && space.valid && !is_wide_keyword(space.serialized) &&
        !ascii_iequals(space.serialized, "auto")) {
        out = {"normal", space.serialized};
        return split::ok;
    }
    return split::invalid;
}

// A value's halves either side of its one top-level `/`, EMPTY ONES KEPT:
// `/ 10px` is a first half missing, not a value without a slash, which is
// what split_top_level - skipping empty fields - made of it. The value alone
// when there is no slash; nothing when there are two.
[[nodiscard]] std::vector<std::string_view> slash_halves(std::string_view value) {
    const token_stream ts = tokenize(value);
    std::size_t slash = std::string_view::npos;
    int depth = 0;
    for (const css_token & token : ts.tokens) {
        if (token.type == token_type::function || token.type == token_type::open_paren) {
            ++depth;
        } else if (token.type == token_type::close_paren) {
            --depth;
        } else if (depth == 0 && token.type == token_type::delim && ts.text_of(token) == "/") {
            if (slash != std::string_view::npos) { return {}; }
            slash = token.text;
        }
    }
    if (slash == std::string_view::npos) { return {value}; }
    return {value.substr(0, slash), value.substr(slash + 1)};
}

// `container`: the name, then `/` and the type, each checked as its own
// longhand (CSS Conditional 5 §4.3).
split split_slash_pair(const expansion & e, std::string_view value,
                       std::vector<std::string> & out) {
    const std::vector<std::string_view> halves = slash_halves(value);
    if (halves.empty()) { return split::invalid; }
    out.clear();
    for (std::size_t i = 0; i < 2; ++i) {
        if (i >= halves.size()) {
            out.emplace_back(initial_of(e.longhands[i]));
            continue;
        }
        const std::string_view half = trim(halves[i], html_whitespace);
        if (half.empty()) { return split::invalid; }
        bool valid = false;
        std::string text = canonical(e.longhands[i], half, valid);
        if (!valid) { return split::invalid; }
        out.push_back(std::move(text));
    }
    return split::ok;
}

// `same`: the whole value on every longhand, each through its own grammar.
split split_same(const expansion & e, std::string_view value, std::vector<std::string> & out) {
    out.clear();
    for (const std::string_view longhand : e.longhands) {
        bool valid = false;
        out.push_back(canonical(longhand, value, valid));
        if (!valid) { return split::invalid; }
    }
    return split::ok;
}

// `column-rule-inset`: one or two caps, then `/` and one or two junctions;
// without the slash the junctions are the caps (CSS Gap Decorations 1).
split split_rule_inset(const expansion & e, std::string_view value,
                       std::vector<std::string> & out) {
    const std::vector<std::string_view> halves = slash_halves(value);
    if (halves.empty()) { return split::invalid; }
    out.clear();
    for (std::size_t half = 0; half < 2; ++half) {
        const std::string_view text =
            trim(halves[std::min(half, halves.size() - 1)], html_whitespace);
        std::vector<std::string> ends;
        for (const std::string_view part : split_top_level(text, html_whitespace)) {
            if (trim(part, html_whitespace).empty()) { continue; }
            bool valid = false;
            ends.push_back(canonical(e.longhands[half * 2], part, valid));
            if (!valid) { return split::invalid; }
        }
        if (ends.empty() || ends.size() > 2) { return split::invalid; }
        out.push_back(ends.front());
        out.push_back(ends.back());
    }
    return split::ok;
}

// `rule-*`: the value split by the column property of the same name, and
// the same values given to the rows.
split split_axes(const expansion & e, std::string_view value, std::vector<std::string> & out) {
    const std::string column = "column-" + std::string{e.syntax->name};
    if (const expansion * sub = expansion_of(column)) {
        std::vector<std::string> half;
        const split result = split_value(*sub, value, half);
        if (result != split::ok) { return result; }
        out = half;
        out.insert(out.end(), half.begin(), half.end());
        return split::ok;
    }
    return split_same(e, value, out);
}

// `text-decoration`: `<line> || <thickness> || <style> || <color>`, where the
// line is `underline overline ...` - several words, written together. The
// run of line keywords is taken whole, so `overline blue underline` is two
// lines and invalid (CSS Text Decoration 4 §2.6).
split split_text_decoration(const expansion & e, std::span<const std::string_view> parts,
                            std::vector<std::string> & out) {
    static constexpr std::string_view line_words =
        "none underline overline line-through blink spelling-error grammar-error";
    static constexpr std::string_view styles = "solid double dotted dashed wavy";
    out.assign(e.longhands.size(), std::string{});
    for (std::size_t i = 0; i < parts.size();) {
        std::size_t slot = e.longhands.size();
        std::string_view text = parts[i];
        std::size_t next = i + 1;
        if (has_keyword(line_words, parts[i])) {
            slot = 0;
            while (next < parts.size() && has_keyword(line_words, parts[next])) { ++next; }
            text = std::string_view{parts[i].data(), static_cast<std::size_t>(
                                                         parts[next - 1].data() +
                                                         parts[next - 1].size() - parts[i].data())};
        } else if (has_keyword(styles, parts[i])) {
            slot = 2;
        } else {
            bool valid = false;
            (void)canonical(e.longhands[1], parts[i], valid);
            slot = valid ? 1 : 3;
        }
        if (!out[slot].empty()) { return split::invalid; }
        bool valid = false;
        out[slot] = canonical(e.longhands[slot], text, valid);
        if (!valid || out[slot].empty()) { return split::invalid; }
        i = next;
    }
    for (std::size_t slot = 0; slot < out.size(); ++slot) {
        if (out[slot].empty()) { out[slot] = std::string{initial_of(e.longhands[slot])}; }
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
    case shape::radius: return split_radius(value, out) ? split::ok : split::invalid;
    case shape::background: return split_background(value, out) ? split::ok : split::invalid;
    case shape::mask: return split_mask(value, out) ? split::ok : split::invalid;
    case shape::position_try: return split_position_try(value, out);
    case shape::text_spacing: return split_text_spacing(value, out);
    case shape::font_synthesis: return split_font_synthesis(value, out);
    case shape::border_image: return split_border_image(value, out) ? split::ok : split::invalid;
    case shape::place: {
        std::string align, justify;
        if (!split_place(e.syntax->name, value, align, justify)) { return split::invalid; }
        out = {std::move(align), std::move(justify)};
        return split::ok;
    }
    case shape::grid_lines:
        return split_grid_lines(e.syntax->name, value, out) ? split::ok : split::invalid;
    case shape::grid_template: return split_grid_template(value, out) ? split::ok : split::invalid;
    case shape::grid: return split_grid(value, out) ? split::ok : split::invalid;
    case shape::slash_pair: return split_slash_pair(e, value, out);
    case shape::bar: return split_bar(e, parts, out);
    case shape::gap_rule: return split_gap_rule(value, out) ? split::ok : split::invalid;
    case shape::same: return split_same(e, value, out);
    case shape::rule_inset: return split_rule_inset(e, value, out);
    case shape::axes: return split_axes(e, value, out);
    case shape::text_decoration: return split_text_decoration(e, parts, out);
    case shape::columns: return split_columns(value, out) ? split::ok : split::invalid;
    case shape::flex: return split_flex(parts, out);
    case shape::border: return split_border(parts, out);
    case shape::border_axis: return split_border_axis(e, parts, out);
    case shape::white_space: return split_white_space(parts, out);
    case shape::animation: return split_animation(value, out);
    case shape::transition: return split_transition(value, out);
    case shape::range: return split_range(value, out);
    case shape::font: return split_font(parts, out);
    case shape::font_variant: return split_font_variant(e, parts, out);
    case shape::line_clamp: return split_line_clamp(e, parts, out);
    case shape::all:
    case shape::whole: break;
    }
    return split::invalid;
}

} // namespace ctbrowser::style::css::shorthand_detail
