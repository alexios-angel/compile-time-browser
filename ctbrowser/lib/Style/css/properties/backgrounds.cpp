// The comma-separated layer lists of CSS Backgrounds 4 and Masking 1 -
// one entry per background or mask image:
//
//   <repeat-style>#     background-repeat, mask-repeat
//   <bg-size>#          background-size, mask-size
//   <position>#         background-position, mask-position
//   background-position-x / -y and their [ keyword <lp>? ] forms
//   a keyword per layer  background-attachment, -clip, -origin,
//                        mask-mode, -composite, -clip, -origin
//
// `repeat no-repeat` is `repeat-x`, a pair of equals is one word, a size's
// second `auto` is dropped when specified and written when computed, and a
// keyword position computes to its percentage.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

struct layer_context {
    const length_context * lengths = nullptr; // computed when set
};

// The significant tokens of each comma-separated layer.
[[nodiscard]] std::vector<std::vector<std::size_t>> layers_of(const token_stream & ts,
                                                              const scan & found) {
    std::vector<std::vector<std::size_t>> out(1);
    int depth = 0;
    for (const std::size_t i : found.significant) {
        const token_type type = ts.tokens[i].type;
        if (type == token_type::function || type == token_type::open_paren) { ++depth; }
        if (type == token_type::close_paren) { --depth; }
        if (depth == 0 && type == token_type::comma) {
            out.emplace_back();
            continue;
        }
        out.back().push_back(i);
    }
    return out;
}

[[nodiscard]] std::optional<std::string_view> layer_text(const token_stream & ts,
                                                         const std::vector<std::size_t> & layer) {
    if (layer.empty()) { return std::nullopt; }
    const css_token & first = ts.tokens[layer.front()];
    const css_token & last = ts.tokens[layer.back()];
    if (first.text >= ts.source_length || last.text >= ts.source_length) { return std::nullopt; }
    return std::string_view{ts.pool}.substr(first.text, last.text + last.length - first.text);
}

[[nodiscard]] std::optional<std::vector<std::string>> words_of(
    const token_stream & ts, const std::vector<std::size_t> & layer) {
    std::vector<std::string> words;
    for (const std::size_t i : layer) {
        if (ts.tokens[i].type != token_type::ident) { return std::nullopt; }
        words.push_back(ascii_lower_copy(ts.text_of(ts.tokens[i])));
    }
    return words;
}

[[nodiscard]] std::optional<std::string> repeat_style(std::span<const std::string> words) {
    if (words.empty() || words.size() > 2) { return std::nullopt; }
    if (words.size() == 1) {
        if (words[0] == "repeat-x" || words[0] == "repeat-y" ||
            has_keyword("repeat space round no-repeat", words[0])) {
            return words[0];
        }
        return std::nullopt;
    }
    for (const std::string & w : words) {
        if (!has_keyword("repeat space round no-repeat", w)) { return std::nullopt; }
    }
    if (words[0] == "repeat" && words[1] == "no-repeat") { return "repeat-x"; }
    if (words[0] == "no-repeat" && words[1] == "repeat") { return "repeat-y"; }
    if (words[0] == words[1]) { return words[0]; }
    return words[0] + " " + words[1];
}

// One `<length-percentage>` or `auto`, canonical, folded when computing.
[[nodiscard]] std::optional<std::string> size_part(std::string_view text, const layer_context & ctx,
                                                   bool non_negative) {
    const token_stream ts = tokenize(text);
    if (ts.tokens.size() == 2) {
        const css_token & t = ts.tokens.front();
        if (t.type == token_type::ident && ascii_iequals(text, "auto")) { return "auto"; }
        if (t.type == token_type::number && t.number == 0) { return "0px"; }
        if (t.type == token_type::percentage) {
            if (non_negative && t.number < 0) { return std::nullopt; }
            return serialize_number(t.number) + "%";
        }
        if (t.type == token_type::dimension) {
            if (non_negative && t.number < 0) { return std::nullopt; }
            const length_context none;
            const math_answer typed =
                evaluate_math(text, ctx.lengths != nullptr ? *ctx.lengths : none);
            if (typed.outcome == math_outcome::invalid) { return std::nullopt; }
            if (typed.outcome == math_outcome::resolved) {
                if (typed.value.type != numeric_type::length) { return std::nullopt; }
                if (ctx.lengths != nullptr) { return serialize_calc(typed.value); }
            }
            return serialize_number(t.number) + ascii_lower_copy(ts.unit_of(t));
        }
        return std::nullopt;
    }
    if (ts.tokens.size() < 2 || ts.tokens.front().type != token_type::function ||
        !may_have_math(text)) {
        return std::nullopt;
    }
    const length_context none;
    const math_answer answer = evaluate_math(text, ctx.lengths != nullptr ? *ctx.lengths : none);
    if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
    if (answer.outcome == math_outcome::resolved) {
        if (answer.value.is_number || answer.value.type != numeric_type::length) {
            return std::nullopt;
        }
        if (ctx.lengths != nullptr) {
            calc_result v = answer.value;
            if (non_negative && !v.has_percent && v.px < 0) { v.px = 0; }
            return serialize_calc(v);
        }
    }
    return simplify_math(text);
}

// The space-separated pieces of a layer, a function kept whole.
[[nodiscard]] std::vector<std::string_view> pieces(std::string_view text) {
    return split_top_level(text, " \t\n\r\f");
}

[[nodiscard]] std::optional<std::string> bg_size(std::string_view text, const layer_context & ctx) {
    const std::vector<std::string_view> parts = pieces(text);
    if (parts.empty() || parts.size() > 2) { return std::nullopt; }
    if (parts.size() == 1 &&
        (ascii_iequals(parts[0], "cover") || ascii_iequals(parts[0], "contain"))) {
        return ascii_lower_copy(parts[0]);
    }
    std::vector<std::string> out;
    for (const std::string_view part : parts) {
        const std::optional<std::string> one = size_part(part, ctx, true);
        if (!one) { return std::nullopt; }
        out.push_back(*one);
    }
    // `1px` is `1px auto`; `auto auto` is `auto` (CSS Backgrounds 3 §3.9).
    if (out.size() == 1 && out[0] != "auto") { out.emplace_back("auto"); }
    if (out.size() == 2 && out[0] == "auto" && out[1] == "auto") { out.pop_back(); }
    return out.size() == 1 ? out[0] : out[0] + " " + out[1];
}

// background-position-x / -y: `center | [ <side>? <lp>? ]!` per layer.
[[nodiscard]] std::optional<std::string> axis_position(std::string_view text, bool vertical,
                                                       const layer_context & ctx) {
    const std::vector<std::string_view> parts = pieces(text);
    if (parts.empty() || parts.size() > 2) { return std::nullopt; }
    std::string side, offset;
    const std::string_view sides =
        vertical ? "top bottom y-start y-end" : "left right x-start x-end";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string word = ascii_lower_copy(parts[i]);
        if (i == 0 && word == "center") {
            if (parts.size() != 1) { return std::nullopt; }
            side = word;
            break;
        }
        if (i == 0 && has_keyword(sides, word)) {
            side = word;
            continue;
        }
        if (!offset.empty() || (i == 1 && side.empty())) { return std::nullopt; }
        const std::optional<std::string> one = size_part(parts[i], ctx, false);
        if (!one || *one == "auto") { return std::nullopt; }
        offset = *one;
    }
    if (side.empty() && offset.empty()) { return std::nullopt; }
    if (ctx.lengths == nullptr) {
        return side + (side.empty() || offset.empty() ? "" : " ") + offset;
    }
    // Computed: a keyword is a percentage, an offset from the far side is a
    // calc() unless it is a percentage.
    const bool far = side == "right" || side == "bottom" || side == "x-end" || side == "y-end";
    if (side == "center") { return "50%"; }
    if (offset.empty()) { return side.empty() ? std::nullopt : std::optional{far ? "100%" : "0%"}; }
    if (!far) { return offset; }
    if (offset.ends_with('%') && offset.find('(') == std::string::npos) {
        return serialize_number(100.0 - std::strtod(offset.c_str(), nullptr)) + "%";
    }
    if (offset.front() == '-') { return "calc(100% + " + offset.substr(1) + ")"; }
    return "calc(100% - " + offset + ")";
}

[[nodiscard]] std::optional<std::string> position_layer(std::string_view text,
                                                        const layer_context & ctx) {
    if (ctx.lengths != nullptr) {
        // Lengths to pixels first, then the resolved percentages.
        const token_stream ts = tokenize(text);
        std::string folded;
        for (const css_token & t : ts.tokens) {
            if (t.type == token_type::eof) { break; }
            if (t.type == token_type::dimension) {
                const std::optional<float> px = length_text_to_px(ts.text_of(t), *ctx.lengths);
                if (!px) { return std::nullopt; }
                folded += serialize_calc(calc_result{*px, 0.0, false, false, numeric_type::length});
                continue;
            }
            folded += ts.text_of(t);
        }
        if (may_have_math(folded)) { folded = fold_math(folded, *ctx.lengths).text; }
        const std::string computed = computed_position(folded, "horizontal-tb", "ltr");
        if (computed.empty()) { return std::nullopt; }
        return computed;
    }
    const token_stream ts = tokenize(text);
    const scan found = scan_tokens(ts);
    std::string out;
    if (match_position(ts, found, out)) { return out; }
    for (const std::size_t i : found.significant) {
        if (ts.tokens[i].type == token_type::function) {
            // A MATH FUNCTION IN A LAYER IS STILL SIMPLIFIED (CSS Values 4
            // §10.12). check_declaration simplifies the whole value before any
            // grammar sees it, and a layer list re-reads the AUTHOR'S text to
            // split it, so this is the one path where that would be lost:
            // `background-position: calc(2px + 3px)` reads back as `calc(5px)`,
            // which is calc-background-position-003 for six values at once.
            const std::string written = normalize_value_tokens(ts, text);
            return may_have_math(written) ? simplify_math(written) : written;
        }
    }
    return std::nullopt;
}

// `[ center | [ left | right ] <lp>? ] && [ center | [ top | bottom ] <lp>? ]`
// with exactly one offset, CSS Backgrounds 3 §3.6: `center right 7%` is
// `right 7% center`, `top 15px center` is `center top 15px`.
[[nodiscard]] std::optional<std::string> three_value_position(std::string_view text,
                                                              const layer_context & ctx) {
    const std::vector<std::string_view> parts = pieces(text);
    if (parts.size() != 3) { return std::nullopt; }
    struct half {
        std::string side;
        std::string offset;
    };
    std::vector<half> halves;
    std::size_t i = 0;
    while (i < parts.size()) {
        const std::string word = ascii_lower_copy(parts[i]);
        if (!has_keyword("left right top bottom center", word) || halves.size() == 2) {
            return std::nullopt;
        }
        halves.push_back({word, {}});
        ++i;
        if (i < parts.size() && word != "center") {
            const std::string maybe = ascii_lower_copy(parts[i]);
            if (!has_keyword("left right top bottom center", maybe)) {
                const std::optional<std::string> offset = size_part(parts[i], ctx, false);
                if (!offset || *offset == "auto") { return std::nullopt; }
                halves.back().offset = *offset;
                ++i;
            }
        }
    }
    if (halves.size() != 2) { return std::nullopt; }
    // The horizontal half first; `center` takes whichever axis is left.
    const auto horizontal = [](const half & one) {
        return one.side == "left" || one.side == "right";
    };
    const auto vertical = [](const half & one) {
        return one.side == "top" || one.side == "bottom";
    };
    half h = halves[0], v = halves[1];
    if (vertical(h) || horizontal(v)) { std::swap(h, v); }
    if (vertical(h) || horizontal(v)) { return std::nullopt; }
    if (h.offset.empty() == v.offset.empty()) { return std::nullopt; } // exactly one offset
    if (ctx.lengths != nullptr) {
        const auto resolve = [](const half & one) -> std::string {
            if (one.side == "center") { return "50%"; }
            const bool far = one.side == "right" || one.side == "bottom";
            if (one.offset.empty()) { return far ? "100%" : "0%"; }
            if (!far) { return one.offset; }
            if (one.offset.ends_with('%') && one.offset.find('(') == std::string::npos) {
                return serialize_number(100.0 - std::strtod(one.offset.c_str(), nullptr)) + "%";
            }
            if (one.offset.front() == '-') { return "calc(100% + " + one.offset.substr(1) + ")"; }
            return "calc(100% - " + one.offset + ")";
        };
        return resolve(h) + " " + resolve(v);
    }
    std::string out = h.side + (h.offset.empty() ? "" : " " + h.offset);
    out += " " + v.side + (v.offset.empty() ? "" : " " + v.offset);
    return out;
}

[[nodiscard]] std::string_view layer_keywords(std::string_view property) {
    if (ascii_iequals(property, "background-attachment")) { return "scroll fixed local"; }
    if (ascii_iequals(property, "background-clip")) {
        return "border-box padding-box content-box text border-area";
    }
    if (ascii_iequals(property, "background-origin")) {
        return "border-box padding-box content-box";
    }
    if (ascii_iequals(property, "background-blend-mode")) {
        return "normal multiply screen overlay darken lighten color-dodge color-burn "
               "hard-light soft-light difference exclusion hue saturation color luminosity";
    }
    if (ascii_iequals(property, "mask-mode")) { return "alpha luminance match-source"; }
    if (ascii_iequals(property, "mask-composite")) { return "add subtract intersect exclude"; }
    if (ascii_iequals(property, "mask-clip")) {
        return "content-box padding-box border-box fill-box stroke-box view-box no-clip";
    }
    if (ascii_iequals(property, "mask-origin")) {
        return "content-box padding-box border-box fill-box stroke-box view-box";
    }
    return {};
}

[[nodiscard]] std::optional<std::string> list(std::string_view property, const token_stream & ts,
                                              const scan & found, const layer_context & ctx) {
    std::string out;
    for (const std::vector<std::size_t> & layer : layers_of(ts, found)) {
        std::optional<std::string> one;
        if (ascii_iequals(property, "background-repeat") ||
            ascii_iequals(property, "mask-repeat")) {
            const std::optional<std::vector<std::string>> words = words_of(ts, layer);
            if (words) { one = repeat_style(*words); }
        } else if (ascii_iequals(property, "background-size") ||
                   ascii_iequals(property, "mask-size")) {
            const std::optional<std::string_view> text = layer_text(ts, layer);
            if (text) { one = bg_size(*text, ctx); }
        } else if (ascii_iequals(property, "background-position-x") ||
                   ascii_iequals(property, "background-position-y")) {
            const std::optional<std::string_view> text = layer_text(ts, layer);
            if (text) { one = axis_position(*text, property.back() == 'y', ctx); }
        } else if (ascii_iequals(property, "background-position") ||
                   ascii_iequals(property, "mask-position")) {
            const std::optional<std::string_view> text = layer_text(ts, layer);
            if (text) { one = position_layer(*text, ctx); }
            // Backgrounds 3 keeps the three-value form (`right 1rem center`)
            // that Values 5's <position> dropped: written horizontal first.
            if (!one && text && ascii_iequals(property, "background-position")) {
                one = three_value_position(*text, ctx);
            }
        } else {
            const std::string_view keywords = layer_keywords(property);
            const std::optional<std::vector<std::string>> words = words_of(ts, layer);
            if (words && !words->empty()) {
                // background-clip's `border-area text` is the one two-word layer.
                if (words->size() == 1 && has_keyword(keywords, words->front())) {
                    one = words->front();
                } else if (words->size() == 2 && ascii_iequals(property, "background-clip") &&
                           ((words->at(0) == "border-area" && words->at(1) == "text") ||
                            (words->at(0) == "text" && words->at(1) == "border-area"))) {
                    one = "border-area text";
                }
            }
        }
        if (!one) { return std::nullopt; }
        out += (out.empty() ? "" : ", ") + *one;
    }
    return out;
}

} // namespace

namespace detail {

bool match_background_list(std::string_view property, const token_stream & ts, const scan & found,
                           std::string & out) {
    if (!ascii_iequals_any(property,
                           {"background-repeat", "mask-repeat", "background-size", "mask-size",
                            "background-position-x", "background-position-y", "background-position",
                            "mask-position", "background-attachment", "background-clip",
                            "background-origin", "background-blend-mode", "mask-mode",
                            "mask-composite", "mask-clip", "mask-origin"})) {
        return false;
    }
    out = list(property, ts, found, {}).value_or(std::string{});
    return true;
}

} // namespace detail

std::string computed_background_list(std::string_view property, std::string_view specified,
                                     const color_context & ctx) {
    const length_context fallback;
    layer_context lc;
    lc.lengths = ctx.lengths != nullptr ? ctx.lengths : &fallback;
    const token_stream ts = tokenize(trim(specified, html_whitespace));
    const scan found = scan_tokens(ts);
    if (found.significant.empty()) { return {}; }
    return list(property, ts, found, lc).value_or(std::string{});
}

} // namespace ctbrowser::style::css
