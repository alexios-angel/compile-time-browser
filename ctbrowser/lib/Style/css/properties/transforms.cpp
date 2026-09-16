// CSS Transforms 2's individual transform properties and the origins:
//
//   rotate      none | <angle> | [ x | y | z | <number>{3} ] && <angle>
//   scale       none | [ <number> | <percentage> ]{1,3}
//   translate   none | <length-percentage> [ <length-percentage> <length>? ]?
//   transform-origin, perspective-origin
//               [ <lp> | left | center | right | top | bottom ]
//               | [ <lp> | left | center | right ] [ <lp> | top | center | bottom ] <length>?
//               | [ [ center | left | right ] && [ center | top | bottom ] ] <length>?
//
// Serialised as CSSOM writes them: an axis vector that is a multiple of x, y
// or z becomes the keyword (a negative one negating the angle), a scale
// drops a trailing `1` and a `y` equal to `x`, a translate drops a trailing
// `0px`, and the origins put the horizontal keyword first. Computed: angles
// in degrees, percentages of a scale as numbers, lengths in pixels, and an
// origin resolved against the box it was given.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

struct transform_context {
    const length_context * lengths = nullptr; // computed when set
};

[[nodiscard]] std::string px_text_of(float px) {
    return serialize_calc(calc_result{px, 0.0, false, false, numeric_type::length});
}

[[nodiscard]] std::vector<std::size_t> significant(const token_stream & ts) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i + 1 < ts.tokens.size(); ++i) {
        if (ts.tokens[i].type != token_type::whitespace) { out.push_back(i); }
    }
    return out;
}

// One component: the text of a single token or of a function to its close,
// and the index past it.
struct piece {
    std::string_view text;
    std::size_t next = 0;
    const css_token * token = nullptr;
};

[[nodiscard]] std::optional<piece> take(const token_stream & ts,
                                        const std::vector<std::size_t> & at, std::size_t k) {
    if (k >= at.size()) { return std::nullopt; }
    const css_token & t = ts.tokens[at[k]];
    if (t.text >= ts.source_length) { return std::nullopt; }
    std::size_t end = k + 1;
    if (t.type == token_type::function) {
        int depth = 0;
        for (std::size_t j = at[k]; j < ts.tokens.size(); ++j) {
            const token_type type = ts.tokens[j].type;
            if (type == token_type::function || type == token_type::open_paren) { ++depth; }
            if (type == token_type::close_paren && --depth == 0) {
                end = k;
                while (end < at.size() && at[end] <= j) { ++end; }
                break;
            }
        }
    }
    const css_token & last = ts.tokens[at[end - 1]];
    if (last.text >= ts.source_length) { return std::nullopt; }
    return piece{std::string_view{ts.pool}.substr(t.text, last.text + last.length - t.text), end,
                 &t};
}

enum class want : std::uint8_t {
    number,            // a <number> or a <percentage> read as one (scale)
    angle,             // an <angle>
    length,            // a <length>
    length_percentage, // a <length-percentage>
};

// One typed value, canonical: a specified value keeps its unit, a computed
// one is folded. `nullopt` when the piece is not of that type.
[[nodiscard]] std::optional<std::string> typed(const piece & p, want kind,
                                               const transform_context & ctx) {
    const css_token & t = *p.token;
    const bool computed = ctx.lengths != nullptr;
    const length_context none;
    const length_context & bases = computed ? *ctx.lengths : none;
    if (t.type == token_type::number) {
        if (kind == want::number) { return serialize_number(t.number); }
        if (t.number == 0 && kind != want::angle) { return "0px"; }
        if (t.number == 0 && kind == want::angle) { return "0deg"; }
        return std::nullopt;
    }
    if (t.type == token_type::percentage) {
        if (kind == want::number) { return serialize_number(t.number / 100.0); }
        if (kind == want::length_percentage) { return serialize_number(t.number) + "%"; }
        return std::nullopt;
    }
    if (t.type == token_type::dimension) {
        if (kind == want::number) { return std::nullopt; }
        const math_answer typed_answer = evaluate_math(p.text, bases);
        if (typed_answer.outcome == math_outcome::invalid) { return std::nullopt; }
        if (typed_answer.outcome == math_outcome::resolved) {
            const bool angle = typed_answer.value.type == numeric_type::angle;
            const bool length = typed_answer.value.type == numeric_type::length;
            if ((kind == want::angle) != angle || (kind != want::angle && !length)) {
                return std::nullopt;
            }
            if (computed) { return serialize_calc(typed_answer.value); }
        } else if (kind == want::angle) {
            return std::nullopt;
        }
        return serialize_number(t.number) +
               ascii_lower_copy(p.text.substr(p.text.size() - t.unit_length));
    }
    if (t.type != token_type::function || !may_have_math(p.text)) { return std::nullopt; }
    const math_answer answer = evaluate_math(p.text, bases);
    if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
    // An expression with no answer yet is still typed (CSS Values 4 §10.2):
    // `calc(1s * sibling-index())` is a time and no rotation.
    std::optional<calc_result> typed_only;
    if (answer.outcome != math_outcome::resolved) { typed_only = math_type_of(p.text); }
    if (answer.outcome == math_outcome::resolved || typed_only) {
        const bool folded = computed && answer.outcome == math_outcome::resolved;
        const calc_result & v = typed_only ? *typed_only : answer.value;
        const bool percent_only = v.has_percent && v.px == 0.0;
        switch (kind) {
        case want::number:
            if (!v.is_number && !percent_only) { return std::nullopt; }
            if (folded) { return serialize_number(percent_only ? v.percent / 100.0 : v.px); }
            break;
        case want::angle:
            if (v.type != numeric_type::angle || v.is_number) { return std::nullopt; }
            if (folded) { return serialize_calc(v); }
            break;
        case want::length:
            if (v.is_number || v.type != numeric_type::length || v.has_percent) {
                return std::nullopt;
            }
            if (folded) { return serialize_calc(v); }
            break;
        case want::length_percentage:
            if (v.is_number || v.type != numeric_type::length) { return std::nullopt; }
            if (folded) { return serialize_calc(v); }
            break;
        }
    }
    return simplify_math(p.text);
}

[[nodiscard]] std::optional<std::string> rotate(const token_stream & ts,
                                                const std::vector<std::size_t> & at,
                                                const transform_context & ctx) {
    if (at.size() == 1 && ts.tokens[at[0]].type == token_type::ident &&
        ascii_iequals(ts.text_of(ts.tokens[at[0]]), "none")) {
        return "none";
    }
    std::string angle, axis;
    std::vector<double> vec;
    std::vector<std::string> vec_text;
    for (std::size_t k = 0; k < at.size();) {
        const std::optional<piece> p = take(ts, at, k);
        if (!p) { return std::nullopt; }
        if (p->token->type == token_type::ident) {
            const std::string word = ascii_lower_copy(p->text);
            if (!has_keyword("x y z", word) || !axis.empty() || !vec.empty()) {
                return std::nullopt;
            }
            axis = word;
        } else if (p->token->type == token_type::number) {
            if (!axis.empty() || vec.size() == 3) { return std::nullopt; }
            vec.push_back(p->token->number);
            vec_text.push_back(serialize_number(p->token->number));
        } else {
            if (!angle.empty()) { return std::nullopt; }
            const std::optional<std::string> a = typed(*p, want::angle, ctx);
            if (!a) { return std::nullopt; }
            angle = *a;
        }
        k = p->next;
    }
    if (angle.empty() || (!vec.empty() && vec.size() != 3)) { return std::nullopt; }
    // A vector along one axis is that axis; along z is no axis at all.
    if (vec.size() == 3) {
        const int nonzero = (vec[0] != 0) + (vec[1] != 0) + (vec[2] != 0);
        if (nonzero == 1) {
            const std::size_t which = vec[0] != 0 ? 0 : (vec[1] != 0 ? 1 : 2);
            if (vec[which] < 0) {
                // The angle is negated, spelled with its sign in front.
                angle = angle.front() == '-' ? angle.substr(1) : "-" + angle;
            }
            axis = which == 0 ? "x" : (which == 1 ? "y" : "z");
        } else {
            return vec_text[0] + " " + vec_text[1] + " " + vec_text[2] + " " + angle;
        }
    }
    if (axis == "z" || axis.empty()) { return angle; }
    return axis + " " + angle;
}

[[nodiscard]] std::optional<std::string> scale(const token_stream & ts,
                                               const std::vector<std::size_t> & at,
                                               const transform_context & ctx) {
    if (at.size() == 1 && ts.tokens[at[0]].type == token_type::ident &&
        ascii_iequals(ts.text_of(ts.tokens[at[0]]), "none")) {
        return "none";
    }
    std::vector<std::string> parts;
    for (std::size_t k = 0; k < at.size();) {
        const std::optional<piece> p = take(ts, at, k);
        if (!p || parts.size() == 3) { return std::nullopt; }
        const std::optional<std::string> n = typed(*p, want::number, ctx);
        if (!n) { return std::nullopt; }
        parts.push_back(*n);
        k = p->next;
    }
    if (parts.empty()) { return std::nullopt; }
    if (parts.size() == 3 && parts[2] == "1") { parts.pop_back(); }
    if (parts.size() == 2 && parts[1] == parts[0]) { parts.pop_back(); }
    std::string out;
    for (const std::string & one : parts) { out += (out.empty() ? "" : " ") + one; }
    return out;
}

[[nodiscard]] std::optional<std::string> translate(const token_stream & ts,
                                                   const std::vector<std::size_t> & at,
                                                   const transform_context & ctx) {
    if (at.size() == 1 && ts.tokens[at[0]].type == token_type::ident &&
        ascii_iequals(ts.text_of(ts.tokens[at[0]]), "none")) {
        return "none";
    }
    std::vector<std::string> parts;
    for (std::size_t k = 0; k < at.size();) {
        const std::optional<piece> p = take(ts, at, k);
        if (!p || parts.size() == 3) { return std::nullopt; }
        const std::optional<std::string> v =
            typed(*p, parts.size() == 2 ? want::length : want::length_percentage, ctx);
        if (!v) { return std::nullopt; }
        parts.push_back(*v);
        k = p->next;
    }
    if (parts.empty()) { return std::nullopt; }
    if (parts.size() == 3 && parts[2] == "0px") { parts.pop_back(); }
    if (parts.size() == 2 && parts[1] == "0px") { parts.pop_back(); }
    std::string out;
    for (const std::string & one : parts) { out += (out.empty() ? "" : " ") + one; }
    return out;
}

// The origin: horizontal, vertical and an optional depth, with the keywords
// placed on their axes.
struct origin {
    std::string x, y, z;
};

[[nodiscard]] std::optional<origin> read_origin(const token_stream & ts,
                                                const std::vector<std::size_t> & at,
                                                const transform_context & ctx) {
    std::vector<std::string> values; // the first one or two components
    std::vector<int> axes;           // 0 horizontal keyword, 1 vertical, 2 center, 3 length
    std::string depth;
    for (std::size_t k = 0; k < at.size();) {
        const std::optional<piece> p = take(ts, at, k);
        if (!p) { return std::nullopt; }
        if (p->token->type == token_type::ident) {
            const std::string word = ascii_lower_copy(p->text);
            if (values.size() >= 2) { return std::nullopt; }
            if (word == "left" || word == "right") {
                axes.push_back(0);
            } else if (word == "top" || word == "bottom") {
                axes.push_back(1);
            } else if (word == "center") {
                axes.push_back(2);
            } else {
                return std::nullopt;
            }
            values.push_back(word);
        } else {
            if (values.size() == 2) {
                if (!depth.empty()) { return std::nullopt; }
                const std::optional<std::string> z = typed(*p, want::length, ctx);
                if (!z) { return std::nullopt; }
                depth = *z;
            } else {
                const std::optional<std::string> v = typed(*p, want::length_percentage, ctx);
                if (!v) { return std::nullopt; }
                values.push_back(*v);
                axes.push_back(3);
            }
        }
        k = p->next;
    }
    if (values.empty()) { return std::nullopt; }
    origin out;
    if (values.size() == 1) {
        // Written as the one value it was, so `bottom` reads back as `bottom`.
        if (!depth.empty()) { return std::nullopt; }
        return origin{values[0], "", ""};
    }
    // Two components: in order, or swapped when they are keywords on the
    // other axes.
    const bool in_order = (axes[0] == 0 || axes[0] == 2 || axes[0] == 3) &&
                          (axes[1] == 1 || axes[1] == 2 || axes[1] == 3);
    const bool swapped = (axes[0] == 1 || axes[0] == 2) && (axes[1] == 0 || axes[1] == 2) &&
                         axes[0] != 3 && axes[1] != 3;
    if (in_order) {
        out.x = values[0];
        out.y = values[1];
    } else if (swapped) {
        out.x = values[1];
        out.y = values[0];
    } else {
        return std::nullopt;
    }
    out.z = depth;
    return out;
}

} // namespace

namespace detail {

bool match_transform_property(std::string_view property, const token_stream & ts,
                              const scan & found, std::string & out) {
    (void)found;
    const std::vector<std::size_t> at = significant(ts);
    std::optional<std::string> answer;
    if (ascii_iequals(property, "rotate")) {
        answer = rotate(ts, at, {});
    } else if (ascii_iequals(property, "scale")) {
        answer = scale(ts, at, {});
    } else if (ascii_iequals(property, "translate")) {
        answer = translate(ts, at, {});
    } else if (ascii_iequals(property, "transform-origin")) {
        const std::optional<origin> o = read_origin(ts, at, {});
        if (o) {
            if (o->y.empty()) {
                answer = o->x;
            } else {
                answer = o->x + " " + o->y + (o->z.empty() ? "" : " " + o->z);
            }
        }
    } else {
        return false;
    }
    // A random() spells its sharing key with the property's name.
    if (answer && answer->find("random(") != std::string::npos) {
        answer = canonical_random(*answer, property);
    }
    out = answer.value_or(std::string{});
    return true;
}

} // namespace detail

std::string computed_transform_property(std::string_view property, std::string_view specified,
                                        const color_context & ctx, float box_width,
                                        float box_height) {
    const length_context fallback;
    transform_context tc;
    tc.lengths = ctx.lengths != nullptr ? ctx.lengths : &fallback;
    const token_stream ts = tokenize(trim(specified, html_whitespace));
    const std::vector<std::size_t> at = significant(ts);
    if (ascii_iequals(property, "rotate")) { return rotate(ts, at, tc).value_or(std::string{}); }
    if (ascii_iequals(property, "scale")) { return scale(ts, at, tc).value_or(std::string{}); }
    if (ascii_iequals(property, "translate")) {
        return translate(ts, at, tc).value_or(std::string{});
    }
    if (!ascii_iequals(property, "transform-origin") &&
        !ascii_iequals(property, "perspective-origin")) {
        return {};
    }
    // perspective-origin is a full <position> (CSS Values 5): read through
    // computed_position, whose halves resolve below like an origin's.
    std::optional<origin> o;
    if (ascii_iequals(property, "perspective-origin")) {
        std::string source{trim(specified, html_whitespace)};
        const token_stream pts = tokenize(source);
        std::string folded;
        for (const css_token & t : pts.tokens) {
            if (t.type == token_type::eof) { break; }
            if (t.type == token_type::dimension) {
                const std::optional<float> px = length_text_to_px(pts.text_of(t), *tc.lengths);
                if (!px) { return {}; }
                folded += px_text_of(*px);
                continue;
            }
            folded += pts.text_of(t);
        }
        if (may_have_math(folded)) { folded = fold_math(folded, *tc.lengths).text; }
        const std::string position = computed_position(folded, "horizontal-tb", "ltr");
        const std::vector<std::string_view> halves = split_top_level(position, " ");
        if (halves.size() != 2) { return {}; }
        o = origin{std::string{halves[0]}, std::string{halves[1]}, ""};
    } else {
        o = read_origin(ts, at, tc);
    }
    if (!o) { return {}; }
    // Each axis against its box edge: a keyword is 0%, 50% or 100%, a
    // percentage or a calc() with one resolves against the width or height.
    const auto resolve = [&](std::string value, bool vertical, bool lone_word) -> std::string {
        const float basis = vertical ? box_height : box_width;
        if (value == "left" || value == "top") { return "0px"; }
        if (value == "right" || value == "bottom") { return px_text_of(basis); }
        if (value == "center") { return px_text_of(basis / 2); }
        (void)lone_word;
        if (value.ends_with('%') && value.find('(') == std::string::npos) {
            const double pct = std::strtod(value.c_str(), nullptr);
            return px_text_of(static_cast<float>(pct / 100.0 * basis));
        }
        if (may_have_math(value)) {
            length_context with_basis = *tc.lengths;
            with_basis.percent_basis = basis;
            const math_answer answer = evaluate_math(value, with_basis);
            if (answer.outcome == math_outcome::resolved && !answer.value.has_percent) {
                return serialize_calc(answer.value);
            }
        }
        return value;
    };
    if (o->y.empty()) {
        // One component: a vertical keyword is the y axis, anything else x.
        const bool vertical = o->x == "top" || o->x == "bottom";
        return vertical ? px_text_of(box_width / 2) + " " + resolve(o->x, true, true)
                        : resolve(o->x, false, true) + " " + px_text_of(box_height / 2);
    }
    std::string out = resolve(o->x, false, false) + " " + resolve(o->y, true, false);
    if (!o->z.empty()) { out += " " + o->z; }
    return out;
}

} // namespace ctbrowser::style::css
