// CSS Gap Decorations 1's rule lists, parsed, split, folded and computed -
// no painting:
//
//   column-rule-width, row-rule-width    [ <line-width> | <repeat> ]#
//   column-rule-style, row-rule-style    the same over <line-style>
//   column-rule-color, row-rule-color    the same over <color>
//   column-rule, row-rule                the same over <gap-rule> =
//                                        <line-width> || <line-style> || <line-color>,
//                                        split item by item into the three
//
// where <repeat> is `repeat( <integer [1,∞]> | auto , <value># )` and a list
// holds at most one `repeat(auto, ...)`. The shorthand's three lists keep the
// shorthand's shape - `repeat(4, blue, 5px red)` is widths `repeat(4, medium,
// 5px)` - and fold back only when all three still have it.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// One comma-separated item: a plain value, or a repeat() of several.
struct gap_item {
    std::string count; // empty for a plain value; `auto` or the integer as written
    std::vector<std::string> values;
};
using gap_list = std::vector<gap_item>;

constexpr std::string_view line_styles =
    "none hidden dotted dashed solid double groove ridge inset outset";

// The list, each value through `one`; nullopt when it is not one.
template <typename One>
[[nodiscard]] std::optional<gap_list> parse_list(std::string_view text, One && one) {
    static const property_syntax count_syntax{"", value_kind::integer, "", "", false, true};
    gap_list out;
    bool saw_auto = false;
    for (const std::string_view raw : split_top_level(text, ",")) {
        const std::string_view item = trim(raw, html_whitespace);
        if (item.empty()) { return std::nullopt; }
        gap_item parsed;
        if (ascii_istarts_with(item, "repeat(") && item.back() == ')') {
            const std::vector<std::string_view> args =
                split_top_level(item.substr(7, item.size() - 8), ",");
            if (args.size() < 2) { return std::nullopt; }
            const std::string_view count = trim(args[0], html_whitespace);
            if (ascii_iequals(count, "auto")) {
                if (saw_auto) { return std::nullopt; }
                saw_auto = true;
                parsed.count = "auto";
            } else if (!typed_component(count, count_syntax, parsed.count) || parsed.count == "0") {
                return std::nullopt;
            }
            for (std::size_t i = 1; i < args.size(); ++i) {
                std::optional<std::string> value = one(trim(args[i], html_whitespace));
                if (!value) { return std::nullopt; }
                parsed.values.push_back(std::move(*value));
            }
        } else {
            std::optional<std::string> value = one(item);
            if (!value) { return std::nullopt; }
            parsed.values.push_back(std::move(*value));
        }
        out.push_back(std::move(parsed));
    }
    if (out.empty()) { return std::nullopt; }
    return out;
}

[[nodiscard]] std::string serialize_list(const gap_list & list) {
    std::string out;
    for (const gap_item & item : list) {
        if (!out.empty()) { out += ", "; }
        if (item.count.empty()) {
            out += item.values.front();
            continue;
        }
        out += "repeat(" + item.count;
        for (const std::string & value : item.values) { out += ", " + value; }
        out += ')';
    }
    return out;
}

// `<line-width>`: a non-negative length or one of the three keywords.
[[nodiscard]] std::optional<std::string> line_width(std::string_view part) {
    static const property_syntax syntax{"",  value_kind::length, "thin medium thick", "", false,
                                        true};
    std::string out;
    if (!typed_component(part, syntax, out)) { return std::nullopt; }
    return out;
}

[[nodiscard]] std::optional<std::string> line_style(std::string_view part) {
    if (words_of(part).size() != 1 || !has_keyword(line_styles, part)) { return std::nullopt; }
    return ascii_lower_copy(part);
}

// A `<color>`, by the grammar every colour property has.
[[nodiscard]] std::optional<std::string> line_color(std::string_view part) {
    const value_check checked = check_declaration("text-decoration-color", part, false);
    if (!checked.valid || is_wide_keyword(checked.serialized)) { return std::nullopt; }
    return checked.serialized;
}

// `<gap-rule>`: each of the three at most once, in any order; the omitted
// ones are their longhands' initial values.
[[nodiscard]] std::optional<std::array<std::string, 3>> gap_rule(std::string_view text) {
    std::array<std::string, 3> out;
    const std::vector<std::string_view> parts = words_of(text);
    if (parts.empty() || parts.size() > 3) { return std::nullopt; }
    for (const std::string_view part : parts) {
        if (std::optional<std::string> s = line_style(part); s && out[1].empty()) {
            out[1] = std::move(*s);
        } else if (std::optional<std::string> w = line_width(part); w && out[0].empty()) {
            out[0] = std::move(*w);
        } else if (std::optional<std::string> c = line_color(part); c && out[2].empty()) {
            out[2] = std::move(*c);
        } else {
            return std::nullopt;
        }
    }
    if (out[0].empty()) { out[0] = "medium"; }
    if (out[1].empty()) { out[1] = "none"; }
    if (out[2].empty()) { out[2] = "currentcolor"; }
    return out;
}

// One gap rule written back: the parts that are not initial, `medium` when
// none is.
[[nodiscard]] std::string gap_rule_text(std::string_view width, std::string_view style,
                                        std::string_view color) {
    std::string out;
    const auto add = [&out](std::string_view part) {
        if (!out.empty()) { out += ' '; }
        out += part;
    };
    if (!ascii_iequals(width, "medium")) { add(width); }
    if (!ascii_iequals(style, "none")) { add(style); }
    if (!ascii_iequals(color, "currentcolor")) { add(color); }
    return out.empty() ? std::string{"medium"} : out;
}

[[nodiscard]] std::optional<std::string> as_written(std::string_view part) {
    return std::string{part};
}

} // namespace

namespace detail {

bool match_gap_list(std::string_view property, std::string_view text, std::string & out) {
    std::optional<gap_list> list;
    if (ascii_iequals_any(property, {"column-rule-width", "row-rule-width"})) {
        list = parse_list(text, line_width);
    } else if (ascii_iequals_any(property, {"column-rule-style", "row-rule-style"})) {
        list = parse_list(text, line_style);
    } else if (ascii_iequals_any(property, {"column-rule-color", "row-rule-color"})) {
        list = parse_list(text, line_color);
    } else {
        return false;
    }
    out = list ? serialize_list(*list) : std::string{};
    return true;
}

bool split_gap_rule(std::string_view value, std::vector<std::string> & out) {
    std::array<gap_list, 3> lists;
    std::array<std::string, 3> item_parts;
    const std::optional<gap_list> rules = parse_list(value, [](std::string_view part) {
        const std::optional<std::array<std::string, 3>> rule = gap_rule(part);
        if (!rule) { return std::optional<std::string>{}; }
        // Carried through the list as one string, split again below: the
        // three are joined by a byte no value can contain.
        return std::optional<std::string>{(*rule)[0] + '\x1f' + (*rule)[1] + '\x1f' + (*rule)[2]};
    });
    if (!rules) { return false; }
    for (const gap_item & item : *rules) {
        for (gap_list & list : lists) {
            list.push_back(gap_item{item.count, {}});
        }
        for (const std::string & joined : item.values) {
            const std::size_t a = joined.find('\x1f');
            const std::size_t b = joined.find('\x1f', a + 1);
            lists[0].back().values.push_back(joined.substr(0, a));
            lists[1].back().values.push_back(joined.substr(a + 1, b - a - 1));
            lists[2].back().values.push_back(joined.substr(b + 1));
        }
    }
    out = {serialize_list(lists[0]), serialize_list(lists[1]), serialize_list(lists[2])};
    return true;
}

std::string fold_gap_rule(std::span<const std::string> v) {
    if (v.size() != 3) { return {}; }
    std::array<gap_list, 3> lists;
    for (std::size_t i = 0; i < 3; ++i) {
        std::optional<gap_list> list = parse_list(v[i], as_written);
        if (!list) { return {}; }
        lists[i] = std::move(*list);
    }
    // The three must have one shape: the same items, the same repeat counts,
    // the same number of values in each.
    gap_list folded;
    for (std::size_t k = 0; k < lists[0].size(); ++k) {
        for (std::size_t i = 1; i < 3; ++i) {
            if (lists[i].size() != lists[0].size() || lists[i][k].count != lists[0][k].count ||
                lists[i][k].values.size() != lists[0][k].values.size()) {
                return {};
            }
        }
        gap_item item{lists[0][k].count, {}};
        for (std::size_t n = 0; n < lists[0][k].values.size(); ++n) {
            item.values.push_back(
                gap_rule_text(lists[0][k].values[n], lists[1][k].values[n], lists[2][k].values[n]));
        }
        folded.push_back(std::move(item));
    }
    return serialize_list(folded);
}

} // namespace detail

std::string computed_gap_list(std::string_view property, std::string_view text,
                              const color_context & ctx) {
    const bool width = property.ends_with("-width");
    const bool color = property.ends_with("-color");
    const length_context lengths = ctx.lengths != nullptr ? *ctx.lengths : length_context{};
    std::optional<gap_list> list = parse_list(text, as_written);
    if (!list || (!width && !color)) { return {}; }
    for (gap_item & item : *list) {
        // A repeat count computes to its integer: `repeat(calc(5 + 3), ...)`
        // is `repeat(8, ...)`.
        if (!item.count.empty() && item.count != "auto") {
            const math_answer n = evaluate_math(item.count, lengths);
            if (n.outcome == math_outcome::resolved) {
                item.count = serialize_number(std::max(1.0, std::round(n.value.px)));
            }
        }
        for (std::string & value : item.values) {
            if (color) {
                if (std::string computed = computed_color(value, ctx); !computed.empty()) {
                    value = std::move(computed);
                }
                continue;
            }
            // A line width in pixels: the keywords are CSS Backgrounds 3
            // §4.3's 1px, 3px and 5px, and a length is clamped at zero.
            double px = 3;
            if (ascii_iequals(value, "thin")) {
                px = 1;
            } else if (ascii_iequals(value, "thick")) {
                px = 5;
            } else if (!ascii_iequals(value, "medium")) {
                const std::optional<float> resolved = length_text_to_px(value, lengths);
                if (!resolved) { continue; }
                px = std::max(0.0, static_cast<double>(*resolved));
            }
            value = serialize_calc(calc_result{px, 0.0, false, false, numeric_type::length});
        }
    }
    return serialize_list(*list);
}

} // namespace ctbrowser::style::css
