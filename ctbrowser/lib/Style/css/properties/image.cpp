// `<image>` lists, CSS Images 3 and 4: the gradients, `image()`,
// `cross-fade()` and `light-dark()`, with a `url()` or an unknown image
// function kept as written. What `background-image`, `mask-image`,
// `border-image-source` and `list-style-image` read back as, specified and
// computed.
//
// A GRADIENT IS THE ONLY GRAMMAR HERE WITH A CANONICAL ORDER: the direction
// or shape first, then `at <position>`, then the colour interpolation method,
// then the stops - each stop its colour first and its positions after, the
// hints between. The defaults are dropped: `to bottom`, `ellipse`,
// `farthest-corner`, `from 0deg`, and the interpolation method that CSS Images
// 4 §3.4.3 would have chosen anyway (`srgb` over legacy colours, `oklab` over
// anything else). The COMPUTED value is the same walk with the colours
// resolved, the lengths in pixels, the angles in degrees and a centre
// position dropped.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

struct image_context {
    const length_context * lengths = nullptr; // computed when set; specified otherwise
    std::string_view current_color;
};

// The source text of the tokens [begin, end), or nothing when one was
// rebuilt from an escape and has no source span.
[[nodiscard]] std::optional<std::string_view> slice(const token_stream & ts, std::size_t begin,
                                                    std::size_t end) {
    if (begin >= end) { return std::string_view{}; }
    const css_token & first = ts.tokens[begin];
    const css_token & last = ts.tokens[end - 1];
    if (first.text >= ts.source_length || last.text >= ts.source_length) { return std::nullopt; }
    return std::string_view{ts.pool}.substr(first.text, last.text + last.length - first.text);
}

// The index one past the block a function or paren at `open` starts.
[[nodiscard]] std::size_t matching_close(const token_stream & ts, std::size_t open) noexcept {
    int depth = 0;
    for (std::size_t i = open; i < ts.tokens.size(); ++i) {
        const token_type type = ts.tokens[i].type;
        if (type == token_type::eof) { return i; }
        if (type == token_type::function || type == token_type::open_paren ||
            type == token_type::open_square || type == token_type::open_curly) {
            ++depth;
        } else if (type == token_type::close_paren || type == token_type::close_square ||
                   type == token_type::close_curly) {
            if (--depth == 0) { return i + 1; }
        }
    }
    return ts.tokens.size() - 1;
}

// The significant (non-whitespace) token indices of [begin, end), split at
// the top-level commas.
[[nodiscard]] std::vector<std::vector<std::size_t>> arguments(const token_stream & ts,
                                                              std::size_t begin, std::size_t end) {
    std::vector<std::vector<std::size_t>> out(1);
    int depth = 0;
    for (std::size_t i = begin; i < end; ++i) {
        const css_token & t = ts.tokens[i];
        if (t.type == token_type::whitespace || t.type == token_type::eof) { continue; }
        if (t.type == token_type::function || t.type == token_type::open_paren ||
            t.type == token_type::open_square || t.type == token_type::open_curly) {
            ++depth;
        } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                   t.type == token_type::close_curly) {
            --depth;
        }
        if (depth == 0 && t.type == token_type::comma) {
            out.emplace_back();
            continue;
        }
        out.back().push_back(i);
    }
    return out;
}

// One value in canonical text: a length, percentage, angle or math
// function, absolutised when computing. `nullopt` for text that is not one.
enum class quantity : std::uint8_t {
    length_percentage,
    angle_percentage,
    length, // a non-negative one, for a radial size
};

[[nodiscard]] std::optional<std::string> canonical_quantity(std::string_view text, quantity kind,
                                                            const image_context & ctx) {
    const token_stream ts = tokenize(text);
    if (ts.tokens.size() == 2) {
        const css_token & t = ts.tokens.front();
        if (t.type == token_type::percentage) {
            if (kind == quantity::length) { return std::nullopt; }
            return serialize_number(t.number) + "%";
        }
        if (t.type == token_type::number && t.number == 0 && kind != quantity::angle_percentage) {
            return "0px";
        }
        if (t.type == token_type::dimension) {
            const std::string unit = ascii_lower_copy(ts.unit_of(t));
            const math_answer typed =
                evaluate_math(text, ctx.lengths != nullptr ? *ctx.lengths : length_context{});
            if (typed.outcome == math_outcome::invalid) { return std::nullopt; }
            if (typed.outcome == math_outcome::resolved) {
                const bool angle = typed.value.type == numeric_type::angle;
                if (angle != (kind == quantity::angle_percentage)) { return std::nullopt; }
                if (!angle && typed.value.type != numeric_type::length) { return std::nullopt; }
                if (kind == quantity::length && typed.value.px < 0) { return std::nullopt; }
                if (ctx.lengths != nullptr) { return serialize_calc(typed.value); }
            }
            // A specified value keeps the author's unit.
            return serialize_number(t.number) + unit;
        }
        return std::nullopt;
    }
    if (ts.tokens.size() < 2 || ts.tokens.front().type != token_type::function) {
        return std::nullopt;
    }
    if (!may_have_math(text)) { return std::nullopt; }
    // A math function: typed by what it answers, folded when computing.
    const length_context none;
    const math_answer answer = evaluate_math(text, ctx.lengths != nullptr ? *ctx.lengths : none);
    if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
    if (answer.outcome == math_outcome::resolved) {
        const bool angle = answer.value.type == numeric_type::angle;
        const bool percent_only = answer.value.has_percent && answer.value.px == 0.0;
        if (kind == quantity::angle_percentage ? (!angle && !percent_only)
                                               : (angle || answer.value.is_number)) {
            return std::nullopt;
        }
        if (kind == quantity::length && (answer.value.has_percent || answer.value.px < 0)) {
            return std::nullopt;
        }
        if (ctx.lengths != nullptr) {
            calc_result v = answer.value;
            if (kind == quantity::length && v.px < 0) { v.px = 0; }
            return serialize_calc(v);
        }
    }
    return simplify_math(text);
}

[[nodiscard]] std::string canonical_angle(std::string_view text, const image_context & ctx,
                                          bool * ok) {
    const token_stream ts = tokenize(text);
    if (ts.tokens.size() == 2 && ts.tokens.front().type == token_type::dimension) {
        const css_token & t = ts.tokens.front();
        const std::string unit = ascii_lower_copy(ts.unit_of(t));
        if (!ascii_iequals_any(unit, {"deg", "grad", "rad", "turn"})) {
            *ok = false;
            return {};
        }
        if (ctx.lengths == nullptr) { return serialize_number(t.number) + unit; }
        return canonical_dimension_text(text, *ctx.lengths).value_or(std::string{text});
    }
    if (ts.tokens.size() == 2 && ts.tokens.front().type == token_type::number &&
        ts.tokens.front().number == 0) {
        return "0deg";
    }
    const length_context none;
    const math_answer answer = evaluate_math(text, ctx.lengths != nullptr ? *ctx.lengths : none);
    if (answer.outcome == math_outcome::invalid ||
        (answer.outcome == math_outcome::resolved && answer.value.type != numeric_type::angle)) {
        *ok = false;
        return {};
    }
    if (answer.outcome == math_outcome::resolved && ctx.lengths != nullptr) {
        return serialize_calc(answer.value);
    }
    return simplify_math(text);
}

// `<position>` as CSS Values 5 writes it: the keywords in canonical order
// when specified, percentages and pixels when computed. Empty for a
// position this cannot read; `centered` says whether it is the centre.
[[nodiscard]] std::string canonical_position(std::string_view text, const image_context & ctx,
                                             bool & centered) {
    centered = false;
    std::string source{text};
    if (ctx.lengths != nullptr) {
        // Every length in pixels first, so `1lh` resolves against the box.
        const token_stream ts = tokenize(text);
        source.clear();
        for (const css_token & t : ts.tokens) {
            if (t.type == token_type::eof) { break; }
            if (t.type == token_type::dimension) {
                const std::optional<float> px = length_text_to_px(ts.text_of(t), *ctx.lengths);
                if (!px) { return {}; }
                source += serialize_calc(calc_result{*px, 0.0, false, false, numeric_type::length});
                continue;
            }
            source += ts.text_of(t);
        }
        if (may_have_math(source)) { source = fold_math(source, *ctx.lengths).text; }
        const std::string computed = computed_position(source, "horizontal-tb", "ltr");
        centered = computed == "50% 50%";
        return computed;
    }
    const token_stream ts = tokenize(source);
    const scan found = scan_tokens(ts);
    std::string out;
    if (match_position(ts, found, out)) {
        centered = out == "center center";
        return out;
    }
    // A math function inside a position keeps the author's bytes, as
    // object-position does.
    for (const std::size_t i : found.significant) {
        if (ts.tokens[i].type == token_type::function) {
            return normalize_value_tokens(ts, source);
        }
    }
    return {};
}

// Is this colour text one of the legacy sRGB forms - which decide a
// gradient's default interpolation space (CSS Images 4 §3.4.3)?
[[nodiscard]] bool legacy_color_syntax(std::string_view text) {
    const std::string_view t = trim(text, html_whitespace);
    if (t.empty()) { return false; }
    if (t.front() == '#') { return true; }
    const std::size_t open = t.find('(');
    if (open == std::string_view::npos) { return true; } // a keyword
    return ascii_iequals_any(t.substr(0, open), {"rgb", "rgba", "hsl", "hsla", "hwb"});
}

// `in <colorspace> [ <hue-method> hue ]?`, read from significant tokens at
// `at`; answers the canonical text and advances `at`. Three answers: no
// `in` here, a well-formed method, or an `in` with a malformed method -
// which condemns the gradient rather than leaving the tokens to the stops.
enum class method_read : std::uint8_t {
    absent,
    read,
    malformed
};

[[nodiscard]] method_read read_interpolation(const token_stream & ts,
                                             const std::vector<std::size_t> & arg, std::size_t & at,
                                             std::string & space, std::string & hue_method) {
    if (at >= arg.size() || ts.tokens[arg[at]].type != token_type::ident ||
        !ascii_iequals(ts.text_of(ts.tokens[arg[at]]), "in")) {
        return method_read::absent;
    }
    ++at;
    if (at >= arg.size() || ts.tokens[arg[at]].type != token_type::ident) {
        return method_read::malformed;
    }
    space = ascii_lower_copy(ts.text_of(ts.tokens[arg[at]]));
    static constexpr std::string_view spaces[] = {
        "srgb",    "srgb-linear",  "display-p3", "display-p3-linear",
        "a98-rgb", "prophoto-rgb", "rec2020",    "xyz",
        "xyz-d50", "xyz-d65",      "lab",        "oklab",
        "hsl",     "hwb",          "lch",        "oklch"};
    if (!ascii_iequals_any(space, spaces)) { return method_read::malformed; }
    if (space == "xyz") { space = "xyz-d65"; }
    ++at;
    const bool polar = space == "hsl" || space == "hwb" || space == "lch" || space == "oklch";
    if (at < arg.size() && ts.tokens[arg[at]].type == token_type::ident) {
        const std::string word = ascii_lower_copy(ts.text_of(ts.tokens[arg[at]]));
        if (ascii_iequals_any(word, {"shorter", "longer", "increasing", "decreasing"})) {
            if (!polar) { return method_read::malformed; }
            ++at;
            if (at >= arg.size() || ts.tokens[arg[at]].type != token_type::ident ||
                !ascii_iequals(ts.text_of(ts.tokens[arg[at]]), "hue")) {
                return method_read::malformed;
            }
            ++at;
            if (word != "shorter") { hue_method = word; }
        } else if (word == "hue") {
            return method_read::malformed;
        }
    }
    return method_read::read;
}

// A colour at significant index `at` of `arg`: one token, or a function to
// its close. Answers the token index past it, or `at` when there is none.
[[nodiscard]] std::size_t colour_extent(const token_stream & ts,
                                        const std::vector<std::size_t> & arg, std::size_t at) {
    if (at >= arg.size()) { return at; }
    const css_token & t = ts.tokens[arg[at]];
    if (t.type == token_type::ident || t.type == token_type::hash) { return at + 1; }
    if (t.type == token_type::function) {
        const std::size_t close = matching_close(ts, arg[at]);
        std::size_t k = at;
        while (k < arg.size() && arg[k] < close) { ++k; }
        return k;
    }
    return at;
}

struct gradient_reader {
    const token_stream & ts;
    const image_context & ctx;

    // One past the last significant token of a position that starts at
    // `from`: the argument's end, or the `in` of an interpolation method.
    [[nodiscard]] std::size_t position_end(const std::vector<std::size_t> & arg,
                                           std::size_t from) const {
        for (std::size_t k = from; k < arg.size(); ++k) {
            const css_token & t = ts.tokens[arg[k]];
            if (t.type == token_type::ident && ascii_iequals(ts.text_of(t), "in")) { return k; }
        }
        return arg.size();
    }

    // The colour stops and hints: `arg` lists after the first (or all of
    // them when the first is a stop). Appends the canonical text.
    [[nodiscard]] bool stops(const std::vector<std::vector<std::size_t>> & args, std::size_t from,
                             bool angular, std::string & out, bool & all_legacy) {
        if (from >= args.size()) { return false; }
        const quantity kind = angular ? quantity::angle_percentage : quantity::length_percentage;
        bool previous_hint = true; // a hint may not open the list
        bool first_piece = true;
        const auto separator = [&]() -> std::string_view {
            if (first_piece) {
                first_piece = false;
                return "";
            }
            return ", ";
        };
        for (std::size_t n = from; n < args.size(); ++n) {
            const std::vector<std::size_t> & arg = args[n];
            if (arg.empty()) { return false; }
            std::string colour;
            std::vector<std::string> positions;
            std::size_t at = 0;
            // The colour, wherever it sits.
            const auto read_colour = [&]() {
                const std::size_t end = colour_extent(ts, arg, at);
                if (end == at) { return false; }
                const std::optional<std::string_view> text = slice(ts, arg[at], arg[end - 1] + 1);
                if (!text) { return false; }
                const std::string serialized =
                    ctx.lengths != nullptr
                        ? computed_color(*text, color_context{ctx.current_color, ctx.lengths})
                        : serialize_color(*text);
                if (serialized.empty()) { return false; }
                if (!legacy_color_syntax(*text)) { all_legacy = false; }
                colour = serialized;
                at = end;
                return true;
            };
            const auto read_position = [&]() {
                if (at >= arg.size()) { return false; }
                const css_token & t = ts.tokens[arg[at]];
                std::size_t end = at + 1;
                if (t.type == token_type::function) {
                    const std::size_t close = matching_close(ts, arg[at]);
                    end = at;
                    while (end < arg.size() && arg[end] < close) { ++end; }
                } else if (t.type != token_type::dimension && t.type != token_type::percentage &&
                           t.type != token_type::number) {
                    return false;
                }
                const std::optional<std::string_view> text = slice(ts, arg[at], arg[end - 1] + 1);
                if (!text) { return false; }
                const std::optional<std::string> canonical = canonical_quantity(*text, kind, ctx);
                if (!canonical) { return false; }
                positions.push_back(*canonical);
                at = end;
                return true;
            };
            if (!read_colour()) {
                // `<position> <color>`, or a hint on its own.
                if (!read_position()) { return false; }
                if (at == arg.size()) {
                    if (previous_hint || n + 1 == args.size()) { return false; }
                    previous_hint = true;
                    out += std::string{separator()} + positions.front();
                    continue;
                }
                if (!read_colour()) { return false; }
            } else {
                while (positions.size() < 2 && read_position()) {}
            }
            if (at != arg.size()) { return false; }
            previous_hint = false;
            out += std::string{separator()} + colour;
            for (const std::string & p : positions) { out += " " + p; }
        }
        return !previous_hint;
    }

    [[nodiscard]] std::optional<std::string> linear(
        const std::vector<std::vector<std::size_t>> & args, std::string_view name) {
        std::string head;
        std::string space, hue;
        std::size_t from = 1;
        const std::vector<std::size_t> & first = args.front();
        std::size_t at = 0;
        bool have_direction = false;
        bool have_method = false;
        const auto read_direction = [&]() -> bool {
            if (at >= first.size() || have_direction) { return false; }
            const css_token & t = ts.tokens[first[at]];
            if (t.type == token_type::ident && ascii_iequals(ts.text_of(t), "to")) {
                std::string h, v;
                for (std::size_t k = at + 1; k < first.size() && k < at + 3; ++k) {
                    const css_token & w = ts.tokens[first[k]];
                    if (w.type != token_type::ident) { break; }
                    const std::string word = ascii_lower_copy(ts.text_of(w));
                    if ((word == "left" || word == "right") && h.empty()) {
                        h = word;
                    } else if ((word == "top" || word == "bottom") && v.empty()) {
                        v = word;
                    } else {
                        break;
                    }
                }
                if (h.empty() && v.empty()) { return false; }
                at += 1 + (h.empty() ? 0 : 1) + (v.empty() ? 0 : 1);
                if (!(h.empty() && v == "bottom")) {
                    head = "to " + h + (h.empty() || v.empty() ? "" : " ") + v;
                }
                have_direction = true;
                return true;
            }
            if (t.type == token_type::dimension || t.type == token_type::function ||
                (t.type == token_type::number && t.number == 0)) {
                std::size_t end = at + 1;
                if (t.type == token_type::function) {
                    const std::size_t close = matching_close(ts, first[at]);
                    end = at;
                    while (end < first.size() && first[end] < close) { ++end; }
                }
                const std::optional<std::string_view> text =
                    slice(ts, first[at], first[end - 1] + 1);
                if (!text) { return false; }
                bool ok = true;
                head = canonical_angle(*text, ctx, &ok);
                if (!ok) { return false; }
                at = end;
                have_direction = true;
                return true;
            }
            return false;
        };
        // `[ <direction> || <method> ]?` - either order, each at most once.
        for (int round = 0; round < 2; ++round) {
            if (!have_method) {
                const method_read method = read_interpolation(ts, first, at, space, hue);
                if (method == method_read::malformed) { return std::nullopt; }
                if (method == method_read::read) {
                    have_method = true;
                    continue;
                }
            }
            if (read_direction()) { continue; }
            break;
        }
        if (at != 0 && at != first.size()) { return std::nullopt; }
        if (at == 0) { from = 0; }
        std::string body;
        bool all_legacy = true;
        if (!stops(args, from, false, body, all_legacy)) { return std::nullopt; }
        return assemble(name, head, space, hue, all_legacy, body);
    }

    [[nodiscard]] std::optional<std::string> radial(
        const std::vector<std::vector<std::size_t>> & args, std::string_view name) {
        const std::vector<std::size_t> & first = args.front();
        std::size_t at = 0;
        std::string shape, extent, position, space, hue;
        std::vector<std::string> sizes;
        bool centered = false;
        bool have_method = false, have_geometry = false;
        const auto read_geometry = [&]() -> bool {
            bool any = false;
            for (;;) {
                if (at >= first.size()) { break; }
                const css_token & t = ts.tokens[first[at]];
                if (t.type == token_type::ident) {
                    const std::string word = ascii_lower_copy(ts.text_of(t));
                    if ((word == "circle" || word == "ellipse") && shape.empty() &&
                        !have_geometry) {
                        shape = word;
                        ++at;
                        any = true;
                        continue;
                    }
                    if (ascii_iequals_any(word, {"closest-corner", "closest-side",
                                                 "farthest-corner", "farthest-side"}) &&
                        extent.empty() && sizes.empty() && !have_geometry) {
                        extent = word;
                        ++at;
                        any = true;
                        continue;
                    }
                    if (word == "at" && position.empty()) {
                        // The position runs to the end of the argument, or to
                        // the interpolation method's `in`.
                        const std::size_t end = position_end(first, at + 1);
                        if (at + 1 >= end) { return false; }
                        const std::optional<std::string_view> text =
                            slice(ts, first[at + 1], first[end - 1] + 1);
                        if (!text) { return false; }
                        position = canonical_position(*text, ctx, centered);
                        if (position.empty()) { return false; }
                        at = end;
                        any = true;
                        continue;
                    }
                    break;
                }
                if ((t.type == token_type::dimension || t.type == token_type::percentage ||
                     t.type == token_type::function || t.type == token_type::number) &&
                    extent.empty() && sizes.size() < 2 && !have_geometry) {
                    std::size_t end = at + 1;
                    if (t.type == token_type::function) {
                        const std::size_t close = matching_close(ts, first[at]);
                        end = at;
                        while (end < first.size() && first[end] < close) { ++end; }
                    }
                    const std::optional<std::string_view> text =
                        slice(ts, first[at], first[end - 1] + 1);
                    if (!text) { return false; }
                    std::optional<std::string> one =
                        canonical_quantity(*text, quantity::length_percentage, ctx);
                    if (!one) { return false; }
                    // A radial size may not be negative: a literal is a
                    // syntax error, a computed calc() clamps to nought.
                    if (one->front() == '-') {
                        if (ctx.lengths == nullptr || t.type != token_type::function) {
                            return false;
                        }
                        *one = "0px";
                    }
                    sizes.push_back(*one);
                    at = end;
                    any = true;
                    continue;
                }
                break;
            }
            if (any) { have_geometry = true; }
            return any;
        };
        for (int round = 0; round < 2; ++round) {
            if (!have_method) {
                const method_read method = read_interpolation(ts, first, at, space, hue);
                if (method == method_read::malformed) { return std::nullopt; }
                if (method == method_read::read) {
                    have_method = true;
                    continue;
                }
            }
            if (read_geometry()) { continue; }
            break;
        }
        // A circle takes one length and no percentage; an ellipse two.
        if (sizes.size() == 1 && (sizes.front().back() == '%' || shape == "ellipse")) {
            return std::nullopt;
        }
        if (sizes.size() == 2 && shape == "circle") { return std::nullopt; }
        std::size_t from = 1;
        if (at == 0) {
            from = 0;
        } else if (at != first.size()) {
            return std::nullopt;
        }
        std::string head;
        if (sizes.empty()) {
            if (shape == "circle") { head = "circle"; }
            if (!extent.empty() && extent != "farthest-corner") {
                head += (head.empty() ? "" : " ") + extent;
            }
        } else {
            head = sizes.front();
            if (sizes.size() == 2) { head += " " + sizes[1]; }
        }
        if (!position.empty() && !(ctx.lengths != nullptr && centered)) {
            head += (head.empty() ? "at " : " at ") + position;
        }
        std::string body;
        bool all_legacy = true;
        if (!stops(args, from, false, body, all_legacy)) { return std::nullopt; }
        return assemble(name, head, space, hue, all_legacy, body);
    }

    [[nodiscard]] std::optional<std::string> conic(
        const std::vector<std::vector<std::size_t>> & args, std::string_view name) {
        const std::vector<std::size_t> & first = args.front();
        std::size_t at = 0;
        std::string angle, position, space, hue;
        bool centered = false;
        bool have_method = false, have_geometry = false;
        const auto read_geometry = [&]() -> bool {
            bool any = false;
            if (at < first.size() && ts.tokens[first[at]].type == token_type::ident &&
                ascii_iequals(ts.text_of(ts.tokens[first[at]]), "from") && angle.empty() &&
                !have_geometry) {
                if (at + 1 >= first.size()) { return false; }
                const css_token & t = ts.tokens[first[at + 1]];
                std::size_t end = at + 2;
                if (t.type == token_type::function) {
                    const std::size_t close = matching_close(ts, first[at + 1]);
                    end = at + 1;
                    while (end < first.size() && first[end] < close) { ++end; }
                }
                const std::optional<std::string_view> text =
                    slice(ts, first[at + 1], first[end - 1] + 1);
                if (!text) { return false; }
                bool ok = true;
                angle = canonical_angle(*text, ctx, &ok);
                if (!ok) { return false; }
                at = end;
                any = true;
            }
            if (at < first.size() && ts.tokens[first[at]].type == token_type::ident &&
                ascii_iequals(ts.text_of(ts.tokens[first[at]]), "at") && position.empty()) {
                const std::size_t end = position_end(first, at + 1);
                if (at + 1 >= end) { return false; }
                const std::optional<std::string_view> text =
                    slice(ts, first[at + 1], first[end - 1] + 1);
                if (!text) { return false; }
                position = canonical_position(*text, ctx, centered);
                if (position.empty()) { return false; }
                at = end;
                any = true;
            }
            if (any) { have_geometry = true; }
            return any;
        };
        for (int round = 0; round < 2; ++round) {
            if (!have_method) {
                const method_read method = read_interpolation(ts, first, at, space, hue);
                if (method == method_read::malformed) { return std::nullopt; }
                if (method == method_read::read) {
                    have_method = true;
                    continue;
                }
            }
            if (read_geometry()) { continue; }
            break;
        }
        std::size_t from = 1;
        if (at == 0) {
            from = 0;
        } else if (at != first.size()) {
            return std::nullopt;
        }
        std::string head;
        if (!angle.empty() && !(ctx.lengths != nullptr && angle == "0deg")) {
            head = "from " + angle;
        }
        if (!position.empty() && !(ctx.lengths != nullptr && centered)) {
            head += (head.empty() ? "at " : " at ") + position;
        }
        std::string body;
        bool all_legacy = true;
        if (!stops(args, from, true, body, all_legacy)) { return std::nullopt; }
        return assemble(name, head, space, hue, all_legacy, body);
    }

    [[nodiscard]] static std::string assemble(std::string_view name, std::string head,
                                              const std::string & space, const std::string & hue,
                                              bool all_legacy, const std::string & body) {
        const std::string_view default_space = all_legacy ? "srgb" : "oklab";
        if (!space.empty() && (space != default_space || !hue.empty())) {
            head += (head.empty() ? "in " : " in ") + space;
            if (!hue.empty()) { head += " " + hue + " hue"; }
        }
        std::string out{name};
        out += '(';
        if (!head.empty()) { out += head + ", "; }
        return out + body + ")";
    }
};

// One `<image>` from the significant tokens `item`; `nullopt` when it is not
// one this file models (the caller keeps the author's bytes), and an empty
// string when it is one and is invalid.
[[nodiscard]] std::optional<std::string> one_image(const token_stream & ts,
                                                   const std::vector<std::size_t> & item,
                                                   const image_context & ctx, int depth);

[[nodiscard]] std::optional<std::string> image_list(const token_stream & ts,
                                                    const std::vector<std::size_t> & item,
                                                    const image_context & ctx, int depth);

std::optional<std::string> one_image(const token_stream & ts, const std::vector<std::size_t> & item,
                                     const image_context & ctx, int depth) {
    if (item.empty() || depth > 16) { return std::string{}; }
    const css_token & first = ts.tokens[item.front()];
    if (first.type == token_type::ident && item.size() == 1) {
        if (ascii_iequals(ts.text_of(first), "none")) { return "none"; }
        // A keyword is no image; in cross-fade() it may be a colour, at the
        // top of a list it is a syntax error.
        return depth == 0 ? std::string{} : std::optional<std::string>{};
    }
    if (first.type != token_type::function) { return std::nullopt; }
    const std::size_t close = matching_close(ts, item.front());
    if (item.back() + 1 != close && !(close == ts.tokens.size() - 1 && item.back() < close)) {
        return std::string{}; // something after the function
    }
    const std::string name = ascii_lower_copy(ts.text_of(first));
    const std::string_view fn = std::string_view{name}.substr(0, name.size() - 1);
    const std::vector<std::vector<std::size_t>> args =
        arguments(ts, item.front() + 1,
                  close - (ts.tokens[close - 1].type == token_type::close_paren ? 1 : 0));
    gradient_reader reader{ts, ctx};
    if (fn == "linear-gradient" || fn == "repeating-linear-gradient") {
        return reader.linear(args, fn).value_or(std::string{});
    }
    if (fn == "radial-gradient" || fn == "repeating-radial-gradient") {
        return reader.radial(args, fn).value_or(std::string{});
    }
    if (fn == "conic-gradient" || fn == "repeating-conic-gradient") {
        return reader.conic(args, fn).value_or(std::string{});
    }
    if (fn == "image") {
        // image( <color> ) is the one form modelled; a url() inside keeps its bytes.
        if (args.size() != 1 || args.front().empty()) { return std::string{}; }
        const std::optional<std::string_view> text =
            slice(ts, args.front().front(), args.front().back() + 1);
        if (!text) { return std::nullopt; }
        if (ascii_istarts_with(trim(*text, html_whitespace), "url(") ||
            ts.tokens[args.front().front()].type == token_type::url ||
            ts.tokens[args.front().front()].type == token_type::string) {
            return std::string{};
        }
        const std::string colour =
            ctx.lengths != nullptr
                ? computed_color(*text, color_context{ctx.current_color, ctx.lengths})
                : serialize_color(*text);
        if (colour.empty()) { return std::string{}; }
        return "image(" + colour + ")";
    }
    if (fn == "light-dark") {
        if (args.size() != 2) { return std::string{}; }
        std::string out = "light-dark(";
        for (std::size_t i = 0; i < 2; ++i) {
            const std::optional<std::string> one = image_list(ts, args[i], ctx, depth + 1);
            if (!one) { return std::nullopt; }
            if (one->empty()) { return std::string{}; }
            if (ctx.lengths != nullptr) { return *one; }
            out += (i == 0 ? "" : ", ") + *one;
        }
        return out + ")";
    }
    if (fn == "cross-fade") {
        // cross-fade( [ <percentage>? && [ <image> | <color> ] ]# ), written
        // image first.
        std::string out = "cross-fade(";
        for (std::size_t i = 0; i < args.size(); ++i) {
            std::vector<std::size_t> arg = args[i];
            if (arg.empty()) { return std::string{}; }
            std::string percent;
            const auto take_percent = [&](std::size_t k) {
                const css_token & t = ts.tokens[arg[k]];
                if (t.type != token_type::percentage) { return false; }
                if (t.number < 0 || t.number > 100) { return false; }
                percent = serialize_number(t.number) + "%";
                arg.erase(arg.begin() + static_cast<std::ptrdiff_t>(k));
                return true;
            };
            if (arg.size() > 1 && ts.tokens[arg.front()].type == token_type::percentage) {
                if (!take_percent(0)) { return std::string{}; }
            } else if (arg.size() > 1 && ts.tokens[arg.back()].type == token_type::percentage) {
                if (!take_percent(arg.size() - 1)) { return std::string{}; }
            }
            if (arg.empty()) { return std::string{}; }
            for (const std::size_t k : arg) {
                if (ts.tokens[k].type == token_type::percentage) { return std::string{}; }
            }
            std::optional<std::string> one = one_image(ts, arg, ctx, depth + 1);
            if (one && one->empty()) { return std::string{}; }
            if (!one) {
                const std::optional<std::string_view> text = slice(ts, arg.front(), arg.back() + 1);
                if (!text) { return std::nullopt; }
                std::string colour =
                    ctx.lengths != nullptr
                        ? computed_color(*text, color_context{ctx.current_color, ctx.lengths})
                        : serialize_color(*text);
                if (colour.empty()) {
                    // A url() or an image function kept as written.
                    colour =
                        normalize_value_tokens(ts, *text, nullptr, arg.front(), arg.back() + 1);
                }
                one = colour;
            }
            out += (i == 0 ? "" : ", ") + *one;
            if (!percent.empty()) { out += " " + percent; }
        }
        return out + ")";
    }
    return std::nullopt;
}

std::optional<std::string> image_list(const token_stream & ts,
                                      const std::vector<std::size_t> & item,
                                      const image_context & ctx, int depth) {
    return one_image(ts, item, ctx, depth);
}

[[nodiscard]] std::optional<std::string> image_list_text(std::string_view text,
                                                         const image_context & ctx) {
    const token_stream ts = tokenize(text);
    const scan found = scan_tokens(ts);
    if (found.malformed || found.significant.empty()) { return std::nullopt; }
    const std::vector<std::vector<std::size_t>> items = arguments(ts, 0, ts.tokens.size() - 1);
    std::string out;
    bool any = false;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::vector<std::size_t> & item = items[i];
        if (item.empty()) { return std::string{}; }
        std::optional<std::string> one = one_image(ts, item, ctx, 0);
        if (one && one->empty()) { return std::string{}; }
        if (one) { any = true; }
        if (!one) {
            bool bad_url = false;
            one = normalize_value_tokens(ts, text, &bad_url, item.front(), item.back() + 1);
            if (bad_url) { return std::string{}; }
        }
        out += (i == 0 ? "" : ", ") + *one;
    }
    if (!any) { return std::nullopt; }
    return out;
}

} // namespace

namespace detail {

bool match_image_list(const token_stream & ts, const scan & found, std::string & out) {
    (void)found;
    const std::optional<std::string_view> whole = slice(ts, 0, ts.tokens.size() - 1);
    if (!whole) { return false; }
    const std::optional<std::string> answer = image_list_text(*whole, image_context{});
    if (!answer) { return false; }
    out = *answer;
    return true;
}

} // namespace detail

std::string computed_image(std::string_view specified, const color_context & ctx) {
    image_context ic;
    const length_context fallback;
    ic.lengths = ctx.lengths != nullptr ? ctx.lengths : &fallback;
    ic.current_color = ctx.current_color;
    return image_list_text(trim(specified, html_whitespace), ic).value_or(std::string{});
}

} // namespace ctbrowser::style::css
