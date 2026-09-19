#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace grammar_detail;

// --- the computed `<position>` -----------------------------------------------

namespace {

// One component of a position as the computed value reads it: a keyword, or an
// offset with its text - a percentage keeping its number so `right 30%` can fold
// to `70%` without re-parsing the string.
struct position_component {
    position_axis kind = position_axis::none;
    std::string text;
    bool is_percent = false;
    double number = 0;
};

// The components, functions kept whole. `position_axis_of` reads one token;
// a `calc()` is several, and its text is the slice of the source from the
// function token to its matching close paren.
[[nodiscard]] bool position_components(const token_stream & ts,
                                       std::vector<position_component> & out) {
    for (std::size_t i = 0; i < ts.tokens.size(); ++i) {
        const css_token & t = ts.tokens[i];
        if (t.type == token_type::eof) { break; }
        if (t.type == token_type::whitespace) { continue; }
        position_component one;
        if (t.type == token_type::function) {
            int depth = 1;
            std::size_t j = i + 1;
            for (; j < ts.tokens.size() && depth > 0; ++j) {
                const token_type type = ts.tokens[j].type;
                if (type == token_type::eof) { return false; }
                if (type == token_type::function || type == token_type::open_paren) { ++depth; }
                if (type == token_type::close_paren) { --depth; }
            }
            const css_token & last = ts.tokens[j - 1];
            // A rebuilt (escaped) token is not a slice of the source, and a
            // position with an escape in a calc() is not worth a second path.
            if (t.text >= ts.source_length || last.text >= ts.source_length) { return false; }
            one.kind = position_axis::offset;
            one.text = std::string_view{ts.pool}.substr(t.text, last.text + last.length - t.text);
            out.push_back(std::move(one));
            i = j - 1;
            continue;
        }
        one.kind = position_axis_of(ts, t, one.text);
        if (one.kind == position_axis::none) { return false; }
        one.is_percent = t.type == token_type::percentage;
        one.number = t.number;
        out.push_back(std::move(one));
    }
    return !out.empty();
}

enum class box_side : std::uint8_t {
    start,
    center,
    end
};

// The physical side a keyword names, the flow-relative ones folded through
// `flipped` - which is what the writing mode and direction reduce to for one
// axis.
[[nodiscard]] box_side physical_side(std::string_view word, bool flipped) {
    if (word == "center") { return box_side::center; }
    const bool is_end = word == "right" || word == "bottom" || word == "x-end" || word == "y-end";
    const bool flow = word.starts_with("x-") || word.starts_with("y-");
    return (is_end != (flow && flipped)) ? box_side::end : box_side::start;
}

// `<side> <offset>?` as a percentage or a length from the START edge. A
// percentage offset from the end folds; a length from the end is a calc(),
// which is how every browser writes `right 20px`.
[[nodiscard]] std::string computed_half(box_side side, const position_component * offset) {
    if (offset == nullptr) {
        return side == box_side::start ? "0%" : (side == box_side::center ? "50%" : "100%");
    }
    if (side == box_side::start) { return offset->text; }
    if (offset->is_percent) { return number_text(100.0 - offset->number) + "%"; }
    return "calc(100% - " + offset->text + ")";
}

} // namespace

std::string computed_position(std::string_view specified, std::string_view writing_mode,
                              std::string_view direction) {
    const token_stream ts = tokenize(specified);
    std::vector<position_component> parts;
    if (!position_components(ts, parts)) { return {}; }

    const std::string mode = ascii_lower_copy(trim(writing_mode, html_whitespace));
    const bool rtl = ascii_iequals(trim(direction, html_whitespace), "rtl");
    const bool vertical_rl = mode == "vertical-rl" || mode == "sideways-rl";
    const bool vertical_lr = mode == "vertical-lr";
    const bool sideways_lr = mode == "sideways-lr";
    const bool horizontal = !vertical_rl && !vertical_lr && !sideways_lr;
    // Where `x-start` and `y-start` fall. In a horizontal box the inline axis
    // follows `direction`; in a vertical one the block axis runs right-to-left
    // for `vertical-rl`, and the inline axis follows `direction` except in
    // `sideways-lr`, whose lines run bottom-to-top.
    const bool flip_x = horizontal ? rtl : vertical_rl;
    const bool flip_y = horizontal ? false : (sideways_lr ? !rtl : rtl);

    const auto fits = [&](const position_component & c, position_axis want) {
        return c.kind == want || c.kind == position_axis::center;
    };
    // One `<side> <offset>?` pair, resolved along one axis.
    const auto half = [&](const position_component & keyword, const position_component * offset,
                          bool flipped) -> std::string {
        if (keyword.kind == position_axis::offset) { return keyword.text; }
        return computed_half(physical_side(keyword.text, flipped), offset);
    };
    const position_component center{position_axis::center, "center", false, 0};

    if (parts.size() == 1) {
        if (parts[0].kind == position_axis::vertical) {
            return half(center, nullptr, false) + " " + half(parts[0], nullptr, flip_y);
        }
        return half(parts[0], nullptr, flip_x) + " " + half(center, nullptr, false);
    }
    if (parts.size() == 2) {
        if ((fits(parts[0], position_axis::horizontal) || parts[0].kind == position_axis::offset) &&
            (fits(parts[1], position_axis::vertical) || parts[1].kind == position_axis::offset)) {
            return half(parts[0], nullptr, flip_x) + " " + half(parts[1], nullptr, flip_y);
        }
        if (fits(parts[0], position_axis::vertical) && fits(parts[1], position_axis::horizontal)) {
            return half(parts[1], nullptr, flip_x) + " " + half(parts[0], nullptr, flip_y);
        }
        return {};
    }
    if (parts.size() != 4 || parts[1].kind != position_axis::offset ||
        parts[3].kind != position_axis::offset) {
        return {};
    }
    if (parts[0].kind == position_axis::horizontal && parts[2].kind == position_axis::vertical) {
        return half(parts[0], &parts[1], flip_x) + " " + half(parts[2], &parts[3], flip_y);
    }
    if (parts[0].kind == position_axis::vertical && parts[2].kind == position_axis::horizontal) {
        return half(parts[2], &parts[3], flip_x) + " " + half(parts[0], &parts[1], flip_y);
    }
    return {};
}

} // namespace ctbrowser::style::css
