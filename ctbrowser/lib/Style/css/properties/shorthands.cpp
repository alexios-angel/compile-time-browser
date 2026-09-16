// The CSSOM declaration block - CSSOM §6.6 - and the shorthand table it needs:
// which longhands each shorthand expands to, how a value is split into them,
// and how they fold back for `cssText` and `getPropertyValue(shorthand)`.
//
// A SHORTHAND IS STORED AS ITS LONGHANDS, because that is what every CSSOM
// question is asked of: `length` counts longhands, `item(i)` names them,
// `removeProperty("margin")` removes four, and `cssText` reconstructs the
// shorthand only when all of them are there with one priority. The table below
// splits the shorthands whose grammar is positional or a `||` of typed parts;
// one it does not model - `background`, `font`, `grid` - or any value holding a
// `var()` is kept whole, exactly as every property was before this existed.

#include "expansion.hpp"
#include "internal.hpp"

#include <ctbrowser/style/css/parser.hpp>

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// HOW A SHORTHAND'S PARTS MAP ONTO ITS LONGHANDS.
enum class shape : std::uint8_t {
    sides,      // 1-4 values: top, right, bottom, left, in `margin`'s way
    pair,       // 1-2 values: start/end, x/y or row/column; one sets both
    place,      // `place-*`: an align value then a justify one, each of several words
    grid_lines, // grid-row / grid-column / grid-area: `/`-separated grid lines
    bar,        // `a || b || c`: each part goes to the longhand that takes it
    flex,       // Flexbox 1 §7.1.1's own defaults
    font,       // CSS Fonts 4 §3.1: the four keywords, the size, `/ line-height`, the family
    border,     // `bar` over width/style/color, applied to four sides, plus
                // border-image reset to its initial values
    all,        // every longhand; CSS-wide keywords only
    whole,      // a grammar this table does not split: only a CSS-wide keyword
                // reaches the longhands, and only one folds back
};

struct shorthand_syntax {
    std::string_view name;
    shape kind;
    std::string_view longhands; // space-separated, in canonical order
    // What a `bar` shorthand serialises as when every part is its initial
    // value: `border: none` and `border: medium` both mean "all initial", and
    // one spelling has to be picked.
    std::string_view all_initial;
};

constexpr std::string_view border_longhands =
    "border-top-width border-right-width border-bottom-width border-left-width "
    "border-top-style border-right-style border-bottom-style border-left-style "
    "border-top-color border-right-color border-bottom-color border-left-color "
    "border-image-source border-image-slice border-image-width border-image-outset "
    "border-image-repeat";

constexpr shorthand_syntax table[] = {
    {"all", shape::all, "", ""},
    {"border", shape::border, border_longhands, "none"},
    {"margin", shape::sides, "margin-top margin-right margin-bottom margin-left", ""},
    {"padding", shape::sides, "padding-top padding-right padding-bottom padding-left", ""},
    {"inset", shape::sides, "top right bottom left", ""},
    {"border-width", shape::sides,
     "border-top-width border-right-width border-bottom-width border-left-width", ""},
    {"border-style", shape::sides,
     "border-top-style border-right-style border-bottom-style border-left-style", ""},
    {"border-color", shape::sides,
     "border-top-color border-right-color border-bottom-color border-left-color", ""},
    {"border-image", shape::bar,
     "border-image-source border-image-slice border-image-width border-image-outset "
     "border-image-repeat",
     "none"},
    {"border-top", shape::bar, "border-top-width border-top-style border-top-color", "none"},
    {"border-right", shape::bar, "border-right-width border-right-style border-right-color",
     "none"},
    {"border-bottom", shape::bar, "border-bottom-width border-bottom-style border-bottom-color",
     "none"},
    {"border-left", shape::bar, "border-left-width border-left-style border-left-color", "none"},
    {"outline", shape::bar, "outline-color outline-style outline-width", "none"},
    {"flex", shape::flex, "flex-grow flex-shrink flex-basis", ""},
    {"flex-flow", shape::bar, "flex-direction flex-wrap", "row"},
    {"list-style", shape::bar, "list-style-position list-style-image list-style-type", "outside"},
    {"margin-inline", shape::pair, "margin-inline-start margin-inline-end", ""},
    {"margin-block", shape::pair, "margin-block-start margin-block-end", ""},
    {"padding-inline", shape::pair, "padding-inline-start padding-inline-end", ""},
    {"padding-block", shape::pair, "padding-block-start padding-block-end", ""},
    {"inset-inline", shape::pair, "inset-inline-start inset-inline-end", ""},
    {"inset-block", shape::pair, "inset-block-start inset-block-end", ""},
    {"overflow", shape::pair, "overflow-x overflow-y", ""},
    {"gap", shape::pair, "row-gap column-gap", ""},
    {"scroll-padding", shape::sides,
     "scroll-padding-top scroll-padding-right scroll-padding-bottom scroll-padding-left", ""},
    {"scroll-margin", shape::sides,
     "scroll-margin-top scroll-margin-right scroll-margin-bottom scroll-margin-left", ""},
    {"scroll-padding-block", shape::pair, "scroll-padding-block-start scroll-padding-block-end",
     ""},
    {"scroll-padding-inline", shape::pair, "scroll-padding-inline-start scroll-padding-inline-end",
     ""},
    {"scroll-margin-block", shape::pair, "scroll-margin-block-start scroll-margin-block-end", ""},
    {"scroll-margin-inline", shape::pair, "scroll-margin-inline-start scroll-margin-inline-end",
     ""},
    {"overscroll-behavior", shape::pair, "overscroll-behavior-x overscroll-behavior-y", ""},
    {"grid-gap", shape::pair, "row-gap column-gap", ""},
    {"columns", shape::bar, "column-width column-count", "auto"},
    {"column-rule", shape::bar, "column-rule-width column-rule-style column-rule-color", "medium"},
    {"text-emphasis", shape::bar, "text-emphasis-style text-emphasis-color", "none"},
    {"text-wrap", shape::bar, "text-wrap-mode text-wrap-style", "wrap"},
    {"grid-row", shape::grid_lines, "grid-row-start grid-row-end", ""},
    {"grid-column", shape::grid_lines, "grid-column-start grid-column-end", ""},
    {"grid-area", shape::grid_lines,
     "grid-row-start grid-column-start grid-row-end grid-column-end", ""},
    {"place-content", shape::place, "align-content justify-content", ""},
    {"place-items", shape::place, "align-items justify-items", ""},
    {"place-self", shape::place, "align-self justify-self", ""},
    {"font", shape::font,
     "font-style font-variant font-weight font-stretch font-size line-height font-family", ""},
    {"background", shape::whole,
     "background-color background-image background-position background-size "
     "background-repeat background-attachment background-origin background-clip",
     ""},
    {"text-decoration", shape::whole,
     "text-decoration-line text-decoration-style text-decoration-color", ""},
    {"transition", shape::whole,
     "transition-property transition-duration transition-timing-function transition-delay", ""},
    {"animation", shape::whole,
     "animation-name animation-duration animation-delay animation-iteration-count", ""},
    {"border-radius", shape::whole,
     "border-top-left-radius border-top-right-radius border-bottom-right-radius "
     "border-bottom-left-radius",
     ""},
};

struct expansion {
    const shorthand_syntax * syntax = nullptr;
    std::vector<std::string_view> longhands;
};

// The table with its longhand lists split once. `all` is every longhand the
// property table has except the two CSS Cascade 4 §3.1 leaves out.
const std::vector<expansion> & expansions() {
    static const std::vector<expansion> built = [] {
        std::vector<expansion> out;
        for (const shorthand_syntax & one : table) {
            expansion e{&one, {}};
            if (one.kind == shape::all) {
                for (const property_syntax & p : known_properties()) {
                    if (p.shorthand || p.name == "direction" || p.name == "unicode-bidi") {
                        continue;
                    }
                    e.longhands.push_back(p.name);
                }
            } else {
                e.longhands = split_top_level(one.longhands, " ");
            }
            out.push_back(std::move(e));
        }
        return out;
    }();
    return built;
}

[[nodiscard]] const expansion * expansion_of(std::string_view name) {
    for (const expansion & e : expansions()) {
        if (ascii_iequals(e.syntax->name, name)) { return &e; }
    }
    return nullptr;
}

// The shorthands a longhand belongs to, in PREFERRED ORDER: the one with the
// most longhands first, so `border` is tried before `border-width` before
// `border-top`, and `all` before everything.
[[nodiscard]] std::vector<const expansion *> shorthands_for(std::string_view longhand) {
    std::vector<const expansion *> out;
    for (const expansion & e : expansions()) {
        if (ascii_iequals_any(longhand, e.longhands)) { out.push_back(&e); }
    }
    std::stable_sort(out.begin(), out.end(), [](const expansion * a, const expansion * b) {
        return a->longhands.size() > b->longhands.size();
    });
    return out;
}

[[nodiscard]] std::string_view initial_of(std::string_view longhand) {
    const property_syntax * p = find_property(longhand);
    return p == nullptr ? std::string_view{} : p->initial;
}

// --- the logical property groups, CSS Logical 1 §1.1 ------------------------
//
// `margin-top` and `margin-inline-start` are one group with two MAPPING
// LOGICS, and a block that has one written between the other's shorthand parts
// cannot fold the shorthand without changing what wins. The group is the name
// with its side or axis removed; the logic is which of the three it was.
enum class mapping : std::uint8_t {
    none,
    physical,
    block,
    inline_
};

struct logical_group {
    std::string group;
    mapping logic = mapping::none;
};

[[nodiscard]] logical_group group_of(std::string_view name) {
    static constexpr std::pair<std::string_view, mapping> parts[] = {
        {"-inline-start", mapping::inline_}, {"-inline-end", mapping::inline_},
        {"-block-start", mapping::block},    {"-block-end", mapping::block},
        {"-top", mapping::physical},         {"-right", mapping::physical},
        {"-bottom", mapping::physical},      {"-left", mapping::physical},
    };
    for (const auto & [part, logic] : parts) {
        const std::size_t at = name.find(part);
        if (at == std::string_view::npos) { continue; }
        std::string group{name.substr(0, at)};
        group += name.substr(at + part.size());
        return {std::move(group), logic};
    }
    for (const std::string_view side : {"top", "right", "bottom", "left"}) {
        if (name == side) { return {"inset", mapping::physical}; }
    }
    return {};
}

// --- splitting a shorthand value into its longhands ---------------------------

enum class split : std::uint8_t {
    ok,
    whole,  // a value this table cannot divide: kept as one declaration
    invalid // refused, as the cascade would refuse it
};

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
    case shape::bar: return split_bar(e, parts, out);
    case shape::flex: return split_flex(parts, out);
    case shape::border: return split_border(parts, out);
    case shape::font: return split_font(parts, out);
    case shape::all:
    case shape::whole: break;
    }
    return split::invalid;
}

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
// the line-height when it is not `normal`, then the family.
[[nodiscard]] std::string fold_font(std::span<const std::string> v) {
    std::vector<std::string> parts;
    for (std::size_t i = 0; i < 4; ++i) {
        if (!ascii_iequals(v[i], "normal")) { parts.push_back(v[i]); }
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
    case shape::bar: return fold_bar(e, v);
    case shape::flex: return join(v);
    case shape::font: return fold_font(v);
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
    }
    return {};
}

// --- the block ----------------------------------------------------------------

[[nodiscard]] std::size_t index_of(const declaration_block & block, std::string_view name) {
    for (std::size_t i = 0; i < block.size(); ++i) {
        if (block[i].name == name) { return i; }
    }
    return block.size();
}

// CSSOM §6.6 "set a CSS declaration", by the algorithm the specification
// suggests: update in place, unless a declaration of the same logical group
// with a different mapping logic follows - then the new one must land after it.
bool set_one(declaration_block & block, std::string_view name, std::string value, bool important) {
    const std::size_t at = index_of(block, name);
    if (at < block.size()) {
        bool needs_append = false;
        const logical_group mine = group_of(name);
        if (mine.logic != mapping::none) {
            for (std::size_t i = at + 1; i < block.size() && !needs_append; ++i) {
                const logical_group other = group_of(block[i].name);
                needs_append = other.group == mine.group && other.logic != mine.logic;
            }
        }
        if (!needs_append) {
            if (block[at].value == value && block[at].important == important) { return false; }
            block[at].value = std::move(value);
            block[at].important = important;
            return true;
        }
        block.erase(block.begin() + static_cast<std::ptrdiff_t>(at));
    }
    block.push_back(declaration{std::string{name}, std::move(value), important});
    return true;
}

// The PARSED form of the same: a later declaration replaces an earlier one and
// takes its place at the end, unless the earlier was important and it is not -
// CSS Cascade 4 §6.1 within one block.
bool add_parsed(declaration_block & block, std::string_view name, std::string value,
                bool important) {
    const std::size_t at = index_of(block, name);
    if (at < block.size()) {
        if (block[at].important && !important) { return false; }
        block.erase(block.begin() + static_cast<std::ptrdiff_t>(at));
    }
    // ...and an important WHOLE shorthand already speaks for this longhand:
    // `background: red !important; background-color: green` keeps the red.
    if (!important) {
        for (const expansion & e : expansions()) {
            if (!ascii_iequals_any(name, e.longhands)) { continue; }
            const std::size_t whole = index_of(block, e.syntax->name);
            if (whole < block.size() && block[whole].important) { return false; }
        }
    }
    block.push_back(declaration{std::string{name}, std::move(value), important});
    return true;
}

// A whole shorthand entry replaces its longhands, and longhands replace a whole
// entry of their shorthand, so the two forms never both speak for one property.
bool erase_named(declaration_block & block, std::span<const std::string_view> names) {
    bool any = false;
    for (const std::string_view name : names) {
        const std::size_t at = index_of(block, name);
        if (at == block.size()) { continue; }
        block.erase(block.begin() + static_cast<std::ptrdiff_t>(at));
        any = true;
    }
    return any;
}

// One declaration into the block, from either path.
bool put(declaration_block & block, std::string_view name, std::string_view text, bool important,
         bool parsed) {
    const value_check checked = check_declaration(name, text, false);
    if (!checked.valid) { return false; }
    const auto add = parsed ? add_parsed : set_one;
    const expansion * e = name.starts_with("--") ? nullptr : expansion_of(name);
    if (e == nullptr) { return add(block, name, checked.serialized, important); }
    std::vector<std::string> values;
    // The SERIALISED shorthand is what is split, not the author's text: a
    // `random()` in it has had its key spelled against the shorthand's name -
    // `margin: random(property-index-scoped, ...)` is `ua-margin-1` on every
    // side, not `ua-margin-top-1` on one of them (random-computed).
    const split result =
        checked.substituted ? split::whole : split_value(*e, checked.serialized, values);
    if (result == split::invalid) { return false; }
    if (result == split::whole) {
        bool changed = erase_named(block, e->longhands);
        changed = add(block, name, checked.serialized, important) || changed;
        return changed;
    }
    bool changed = erase_named(block, std::array<std::string_view, 1>{name});
    // ...and every whole entry of another shorthand these longhands belong to:
    // `all: revert` after `font: 12px serif` speaks for `font` now.
    for (const expansion & other : expansions()) {
        if (&other == e || index_of(block, other.syntax->name) == block.size()) { continue; }
        for (const std::string_view longhand : other.longhands) {
            if (!ascii_iequals_any(longhand, e->longhands)) { continue; }
            changed =
                erase_named(block, std::array<std::string_view, 1>{other.syntax->name}) || changed;
            break;
        }
    }
    for (std::size_t i = 0; i < e->longhands.size(); ++i) {
        changed = add(block, e->longhands[i], values[i], important) || changed;
    }
    return changed;
}

} // namespace

std::span<const std::string_view> longhands_of(std::string_view shorthand) {
    const expansion * e = expansion_of(shorthand);
    return e == nullptr ? std::span<const std::string_view>{} : e->longhands;
}

bool set_declaration(declaration_block & block, std::string_view name, std::string_view text,
                     bool important) {
    if (trim(text, html_whitespace).empty()) {
        bool removed = false;
        (void)remove_declaration(block, name, removed);
        return removed;
    }
    return put(block, name, text, important, false);
}

void parse_declaration_block(declaration_block & block, std::string_view text,
                             bool (*allow)(std::string_view, std::string_view, const void *),
                             const void * ctx) {
    atom_table atoms;
    const stylesheet parsed = parse_declaration_list(text, atoms);
    for (const raw_declaration & d : parsed.declarations) {
        const std::string_view property = atoms.text(d.property);
        if (allow != nullptr && !allow(property, parsed.text_of(d), ctx)) { continue; }
        (void)put(block, property, parsed.text_of(d), d.important, true);
    }
}

std::string remove_declaration(declaration_block & block, std::string_view name, bool & removed) {
    std::string was = declaration_value(block, name);
    const std::span<const std::string_view> longhands = longhands_of(name);
    removed = erase_named(block, std::array<std::string_view, 1>{name});
    if (!longhands.empty()) { removed = erase_named(block, longhands) || removed; }
    return was;
}

std::string declaration_value(const declaration_block & block, std::string_view name) {
    if (const std::size_t at = index_of(block, name); at < block.size()) { return block[at].value; }
    const expansion * e = name.starts_with("--") ? nullptr : expansion_of(name);
    if (e == nullptr) { return {}; }
    std::vector<std::string> values;
    bool important = false;
    for (std::size_t i = 0; i < e->longhands.size(); ++i) {
        const std::size_t at = index_of(block, e->longhands[i]);
        if (at == block.size()) { return {}; }
        if (i == 0) { important = block[at].important; }
        if (block[at].important != important) { return {}; }
        values.push_back(block[at].value);
    }
    return fold(*e, values);
}

std::string declaration_priority(const declaration_block & block, std::string_view name) {
    if (const std::size_t at = index_of(block, name); at < block.size()) {
        return block[at].important ? "important" : "";
    }
    const std::span<const std::string_view> longhands = longhands_of(name);
    if (longhands.empty()) { return {}; }
    for (const std::string_view longhand : longhands) {
        const std::size_t at = index_of(block, longhand);
        if (at == block.size() || !block[at].important) { return {}; }
    }
    return "important";
}

std::string serialize_declaration_block(const declaration_block & block) {
    std::string out;
    std::vector<bool> done(block.size(), false);
    const auto emit = [&](std::string_view name, std::string_view value, bool important) {
        if (!out.empty()) { out += ' '; }
        // A custom property's name is an identifier the tokenizer decoded, so
        // `--a\;b` has to be written back escaped to survive a re-parse.
        out += name.starts_with("--") ? serialize_identifier(name) : std::string{name};
        out += ": ";
        out += value;
        if (important) { out += " !important"; }
        out += ';';
    };
    for (std::size_t i = 0; i < block.size(); ++i) {
        if (done[i]) { continue; }
        const declaration & d = block[i];
        bool folded = false;
        // "Serialize into a shorthand form": the first shorthand, in preferred
        // order, whose every longhand is here, unserialised, of one priority,
        // and not interleaved with the same logical group's other mapping.
        for (const expansion * e :
             d.name.starts_with("--") ? std::vector<const expansion *>{} : shorthands_for(d.name)) {
            std::vector<std::size_t> used;
            bool ok = true;
            for (const std::string_view longhand : e->longhands) {
                const std::size_t at = index_of(block, longhand);
                if (at == block.size() || done[at]) {
                    ok = false;
                    break;
                }
                used.push_back(at);
            }
            if (!ok) { continue; }
            const bool important = block[used.front()].important;
            for (const std::size_t at : used) { ok = ok && block[at].important == important; }
            if (!ok) { continue; }
            const auto [first, last] = std::minmax_element(used.begin(), used.end());
            for (std::size_t at = *first; at <= *last && ok; ++at) {
                if (std::find(used.begin(), used.end(), at) != used.end()) { continue; }
                const logical_group other = group_of(block[at].name);
                if (other.logic == mapping::none) { continue; }
                for (const std::size_t u : used) {
                    const logical_group mine = group_of(block[u].name);
                    if (mine.group == other.group && mine.logic != other.logic) { ok = false; }
                }
            }
            if (!ok) { continue; }
            std::vector<std::string> values;
            for (const std::size_t at : used) { values.push_back(block[at].value); }
            const std::string value = fold(*e, values);
            if (value.empty()) { continue; }
            for (const std::size_t at : used) { done[at] = true; }
            emit(e->syntax->name, value, important);
            folded = true;
            break;
        }
        if (folded) { continue; }
        done[i] = true;
        emit(d.name, d.value, d.important);
    }
    return out;
}

} // namespace ctbrowser::style::css
