// CSS Box Alignment 3: the six longhands and the three `place-*` shorthands.
//
//   align-content    normal | <baseline-position> | <content-distribution>
//                    | <overflow-position>? <content-position>
//   justify-content  ...the same, and `left | right`
//   align-self       auto | normal | stretch | <baseline-position>
//                    | <overflow-position>? <self-position>
//   justify-self     ...the same, and `left | right`
//   align-items      normal | stretch | <baseline-position>
//                    | <overflow-position>? <self-position>
//   justify-items    ...the same, `left | right`, and `legacy` alone or with
//                    one of `left | right | center`
//
// Serialised as CSSOM writes it: `first baseline` is `baseline`, `legacy`
// comes before its keyword, the overflow keyword before its position.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

constexpr std::string_view content_positions = "center start end flex-start flex-end";
constexpr std::string_view self_positions =
    "center start end self-start self-end flex-start flex-end anchor-center";
constexpr std::string_view distributions = "space-between space-around space-evenly stretch";

enum class axis : std::uint8_t {
    content,
    self_,
    items,
};

// The words of the value from `at`, matched against `property`'s grammar;
// answers the canonical text and advances `at` past what it read, or fails.
[[nodiscard]] bool read_alignment(std::span<const std::string> words, std::size_t & at, axis kind,
                                  bool justify, std::string & out) {
    if (at >= words.size()) { return false; }
    const std::string_view w = words[at];
    const auto lone = [&](std::string_view set) {
        if (!has_keyword(set, w)) { return false; }
        out = std::string{w};
        ++at;
        return true;
    };
    if (w == "normal" || (kind == axis::self_ && w == "auto") ||
        (kind != axis::content && w == "stretch")) {
        return lone("normal auto stretch");
    }
    // `[ first | last ]? baseline`
    if (w == "first" || w == "last") {
        if (kind == axis::content && justify) { return false; }
        if (at + 1 >= words.size() || words[at + 1] != "baseline") { return false; }
        out = w == "last" ? "last baseline" : "baseline";
        at += 2;
        return true;
    }
    if (w == "baseline") { return !(kind == axis::content && justify) && lone("baseline"); }
    if (kind == axis::content && lone(distributions)) { return true; }
    // `legacy`, alone or beside `left | right | center`, justify-items only.
    if (kind == axis::items && justify && w == "legacy") {
        ++at;
        if (at < words.size() && has_keyword("left right center", words[at])) {
            out = "legacy " + words[at];
            ++at;
        } else {
            out = "legacy";
        }
        return true;
    }
    if (kind == axis::items && justify && has_keyword("left right center", w) &&
        at + 1 < words.size() && words[at + 1] == "legacy") {
        out = "legacy " + std::string{w};
        at += 2;
        return true;
    }
    const std::string_view positions = kind == axis::content ? content_positions : self_positions;
    // `anchor-center` is a self-alignment of the box itself, not of its items.
    if (kind == axis::items && w == "anchor-center") { return false; }
    std::string prefix;
    if (w == "safe" || w == "unsafe") {
        prefix = std::string{w} + " ";
        ++at;
        if (at >= words.size()) { return false; }
    }
    const std::string_view p = words[at];
    if (has_keyword(positions, p) || (justify && (p == "left" || p == "right"))) {
        out = prefix + std::string{p};
        ++at;
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<std::vector<std::string>> words_of(const token_stream & ts,
                                                               const scan & found) {
    std::vector<std::string> words;
    for (const std::size_t i : found.significant) {
        if (ts.tokens[i].type != token_type::ident) { return std::nullopt; }
        words.push_back(ascii_lower_copy(ts.text_of(ts.tokens[i])));
    }
    return words;
}

[[nodiscard]] std::optional<std::pair<axis, bool>> axis_of(std::string_view property) {
    static constexpr std::pair<std::string_view, std::pair<axis, bool>> table[] = {
        {"align-content", {axis::content, false}}, {"justify-content", {axis::content, true}},
        {"align-self", {axis::self_, false}},      {"justify-self", {axis::self_, true}},
        {"align-items", {axis::items, false}},     {"justify-items", {axis::items, true}},
    };
    for (const auto & [name, a] : table) {
        if (ascii_iequals(name, property)) { return a; }
    }
    return std::nullopt;
}

} // namespace

namespace detail {

bool match_alignment(std::string_view property, const token_stream & ts, const scan & found,
                     std::string & out) {
    const std::optional<std::pair<axis, bool>> a = axis_of(property);
    if (!a) { return false; }
    const std::optional<std::vector<std::string>> words = words_of(ts, found);
    if (!words) { return false; }
    std::size_t at = 0;
    if (!read_alignment(*words, at, a->first, a->second, out)) { return false; }
    return at == words->size();
}

bool split_place(std::string_view shorthand, std::string_view value, std::string & align,
                 std::string & justify) {
    const std::string_view suffix = shorthand.substr(6); // after `place-`
    const axis kind = suffix == "content" ? axis::content
                      : suffix == "self"  ? axis::self_
                                          : axis::items;
    const token_stream ts = tokenize(value);
    const scan found = scan_tokens(ts);
    const std::optional<std::vector<std::string>> words = words_of(ts, found);
    if (!words || words->empty()) { return false; }
    std::size_t at = 0;
    if (!read_alignment(*words, at, kind, false, align)) { return false; }
    if (at == words->size()) {
        // One value sets both, except that a baseline alignment has no
        // justify-content spelling and takes `start` there.
        justify = kind == axis::content && align.ends_with("baseline") ? "start" : align;
        // `legacy` is justify-items' alone; alone it is `normal` on the
        // align side and cannot come from here at all.
        return true;
    }
    if (!read_alignment(*words, at, kind, true, justify)) { return false; }
    return at == words->size();
}

} // namespace detail

} // namespace ctbrowser::style::css
