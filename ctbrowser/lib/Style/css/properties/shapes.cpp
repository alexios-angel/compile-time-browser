// <basic-shape>, CSS Shapes 1 §3 - for clip-path and shape-outside: parsed,
// written in canonical form, computed, and blended for an animation:
//
//   circle( <radial-size>? [ at <position> ]? )
//   ellipse( [ <radial-size>{2} ]? [ at <position> ]? )
//   inset( <length-percentage>{1,4} [ round <'border-radius'> ]? )
//   xywh( <length-percentage>{2} <length-percentage [0,∞]>{2} [ round <'border-radius'> ]? )
//   rect( [ <length-percentage> | auto ]{4} [ round <'border-radius'> ]? )
//   polygon( [ <fill-rule> || round <length> ]? , [ <length-percentage>{2} ]# )
//   path( <fill-rule>? , <string> )
//
// beside the box it is drawn in: clip-path's <geometry-box> (`border-box`
// by default) or shape-outside's <shape-box> (`margin-box`). Nothing here
// draws; a shape() is kept as written.
//
// The COMPUTED shape has its lengths absolute, its position in percentages
// and calc()s from the top left, and xywh() and rect() as the inset() they
// mean (CSS Shapes 1 §3.2). Two computed shapes of one function blend
// component by component - the interpolation of §3.3 and the addition of
// Web Animations - and anything else is discrete.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

struct shape_parts {
    std::string function;            // lowercased name; empty when not a shape
    std::vector<std::string> args;   // radial sizes, inset sides, xywh / rect components
    std::string position;            // after `at`; empty when omitted
    std::vector<std::string> radii;  // `round`'s eight corners, or empty
    std::string fill_rule;           // `evenodd`, or empty for the default
    std::string round;               // polygon's `round <length>`, or empty
    std::vector<std::string> points; // polygon: x, y, x, y ...
    std::string path;                // path's string, serialised
    std::string box;                 // the box beside it, or empty for the default
};

constexpr std::string_view geometry_boxes =
    "border-box padding-box content-box margin-box fill-box stroke-box view-box";

[[nodiscard]] std::vector<std::string_view> words_of(std::string_view text) {
    std::vector<std::string_view> out;
    for (const std::string_view raw : split_top_level(text, html_whitespace)) {
        const std::string_view part = trim(raw, html_whitespace);
        if (!part.empty()) { out.push_back(part); }
    }
    return out;
}

[[nodiscard]] std::string joined(std::span<const std::string> parts, std::string_view by = " ") {
    std::string out;
    for (const std::string & part : parts) {
        if (!out.empty()) { out += by; }
        out += part;
    }
    return out;
}

[[nodiscard]] std::optional<std::string> length_percentage(std::string_view part,
                                                           bool nonnegative) {
    static const property_syntax signed_lp{"", value_kind::length_percentage, "", "", false, false};
    static const property_syntax unsigned_lp{"",  value_kind::length_percentage, "", "", false,
                                             true};
    std::string out;
    if (!typed_component(part, nonnegative ? unsigned_lp : signed_lp, out)) { return std::nullopt; }
    return out;
}

// `margin`'s four sides, from one to four values.
[[nodiscard]] std::vector<std::string> four(std::span<const std::string> v) {
    if (v.empty() || v.size() > 4) { return {}; }
    return {v[0], v.size() > 1 ? v[1] : v[0], v.size() > 2 ? v[2] : v[0],
            v.size() > 3 ? v[3] : (v.size() > 1 ? v[1] : v[0])};
}

[[nodiscard]] std::string fold_four(std::span<const std::string> v) {
    std::vector<std::string> parts(v.begin(), v.end());
    if (parts.size() == 4 && parts[3] == parts[1]) { parts.pop_back(); }
    if (parts.size() == 3 && parts[2] == parts[0]) { parts.pop_back(); }
    if (parts.size() == 2 && parts[1] == parts[0]) { parts.pop_back(); }
    return joined(parts);
}

// `round <'border-radius'>`: eight corners, the vertical four the
// horizontal when there is no slash.
[[nodiscard]] bool parse_radii(std::span<const std::string_view> words,
                               std::vector<std::string> & out) {
    std::vector<std::string> horizontal, vertical;
    bool slash = false;
    for (const std::string_view word : words) {
        if (word == "/") {
            if (slash) { return false; }
            slash = true;
            continue;
        }
        std::optional<std::string> one = length_percentage(word, true);
        if (!one) { return false; }
        (slash ? vertical : horizontal).push_back(std::move(*one));
    }
    if (slash && vertical.empty()) { return false; }
    std::vector<std::string> h = four(horizontal);
    std::vector<std::string> v = slash ? four(vertical) : h;
    if (h.empty() || v.empty()) { return false; }
    out = h;
    out.insert(out.end(), v.begin(), v.end());
    return true;
}

[[nodiscard]] std::string radii_text(std::span<const std::string> r) {
    if (r.size() != 8) { return {}; }
    const std::string h = fold_four(r.first(4));
    const std::string v = fold_four(r.subspan(4));
    return h == v ? h : h + " / " + v;
}

[[nodiscard]] bool zero(std::string_view text) {
    return text == "0px" || text == "0%" || text == "0";
}

[[nodiscard]] std::optional<std::string> canonical_position(std::string_view text) {
    const token_stream ts = tokenize(text);
    const scan found = scan_tokens(ts);
    std::string out;
    if (found.malformed || found.significant.empty() || !match_position(ts, found, out)) {
        return std::nullopt;
    }
    return out;
}

// The words of a circle() or an ellipse(): the radii, then `at` and a position.
[[nodiscard]] bool parse_radial(std::span<const std::string_view> words, std::size_t most,
                                shape_parts & out) {
    std::size_t at = 0;
    for (; at < words.size() && !ascii_iequals(words[at], "at"); ++at) {
        if (at == most) { return false; }
        if (ascii_iequals_any(words[at], {"closest-side", "farthest-side"})) {
            out.args.push_back(ascii_lower_copy(words[at]));
        } else if (std::optional<std::string> r = length_percentage(words[at], true)) {
            out.args.push_back(std::move(*r));
        } else {
            return false;
        }
    }
    if (!out.args.empty() && out.args.size() != most) { return false; }
    if (at < words.size()) {
        if (at + 1 == words.size()) { return false; }
        std::string rest;
        for (std::size_t i = at + 1; i < words.size(); ++i) {
            rest += (rest.empty() ? "" : " ") + std::string{words[i]};
        }
        std::optional<std::string> position = canonical_position(rest);
        if (!position) { return false; }
        out.position = std::move(*position);
    }
    // The default radii are left out (CSS Shapes 1 §3.1's serialisation).
    bool all_closest = true;
    for (const std::string & r : out.args) { all_closest = all_closest && r == "closest-side"; }
    if (all_closest) { out.args.clear(); }
    return true;
}

// Everything after `round`, into the radii; empty `words` is no `round`.
[[nodiscard]] bool parse_round(std::span<const std::string_view> words, shape_parts & out) {
    if (words.empty()) { return true; }
    if (!parse_radii(words, out.radii)) { return false; }
    bool all_zero = true;
    for (const std::string & r : out.radii) { all_zero = all_zero && zero(r); }
    if (all_zero) { out.radii.clear(); }
    return true;
}

[[nodiscard]] std::optional<shape_parts> parse_function(std::string_view text) {
    shape_parts out;
    const std::size_t open = text.find('(');
    if (open == std::string_view::npos || !text.ends_with(')')) { return std::nullopt; }
    out.function = ascii_lower_copy(text.substr(0, open));
    const std::string_view body = text.substr(open + 1, text.size() - open - 2);
    const std::vector<std::string_view> words = words_of(body);
    const auto round_at = [&words]() {
        std::size_t i = 0;
        while (i < words.size() && !ascii_iequals(words[i], "round")) { ++i; }
        return i;
    };
    if (out.function == "circle" || out.function == "ellipse") {
        if (!parse_radial(words, out.function == "circle" ? 1 : 2, out)) { return std::nullopt; }
        return out;
    }
    if (out.function == "inset" || out.function == "xywh" || out.function == "rect") {
        const std::size_t r = round_at();
        if (r == words.size() - 1 && !words.empty()) { return std::nullopt; }
        const std::size_t count = out.function == "inset" ? r : 4;
        if (r == 0 || r > 4 || (out.function != "inset" && r != 4)) { return std::nullopt; }
        for (std::size_t i = 0; i < count; ++i) {
            if (out.function == "rect" && ascii_iequals(words[i], "auto")) {
                out.args.emplace_back("auto");
                continue;
            }
            std::optional<std::string> one =
                length_percentage(words[i], out.function == "xywh" && i >= 2);
            if (!one) { return std::nullopt; }
            out.args.push_back(std::move(*one));
        }
        if (!parse_round(std::span{words}.subspan(r < words.size() ? r + 1 : r), out)) {
            return std::nullopt;
        }
        return out;
    }
    if (out.function == "polygon" || out.function == "path") {
        std::vector<std::string_view> items = split_top_level(body, ",");
        for (std::string_view & item : items) { item = trim(item, html_whitespace); }
        if (items.empty()) { return std::nullopt; }
        // The leading item may be a fill rule and, in a polygon, a `round`.
        const std::vector<std::string_view> first = words_of(items.front());
        std::size_t k = 0;
        bool leading = false;
        while (k < first.size()) {
            if (ascii_iequals_any(first[k], {"nonzero", "evenodd"}) && out.fill_rule.empty()) {
                out.fill_rule = ascii_lower_copy(first[k]);
                leading = true;
                ++k;
            } else if (out.function == "polygon" && ascii_iequals(first[k], "round") &&
                       out.round.empty() && k + 1 < first.size()) {
                static const property_syntax nonnegative{"",  value_kind::length, "", "", false,
                                                         true};
                if (!typed_component(first[k + 1], nonnegative, out.round)) { return std::nullopt; }
                leading = true;
                k += 2;
            } else {
                break;
            }
        }
        if (leading) {
            if (k != first.size()) { return std::nullopt; }
            items.erase(items.begin());
        }
        if (out.fill_rule == "nonzero") { out.fill_rule.clear(); }
        if (zero(out.round)) { out.round.clear(); }
        if (out.function == "path") {
            if (items.size() != 1) { return std::nullopt; }
            const token_stream ts = tokenize(items.front());
            const scan found = scan_tokens(ts);
            if (found.significant.size() != 1 ||
                ts.tokens[found.significant.front()].type != token_type::string) {
                return std::nullopt;
            }
            out.path = string_text(ts.value_of(ts.tokens[found.significant.front()]));
            return out;
        }
        if (items.empty()) { return std::nullopt; }
        for (const std::string_view item : items) {
            const std::vector<std::string_view> xy = words_of(item);
            if (xy.size() != 2) { return std::nullopt; }
            for (const std::string_view c : xy) {
                std::optional<std::string> one = length_percentage(c, false);
                if (!one) { return std::nullopt; }
                out.points.push_back(std::move(*one));
            }
        }
        return out;
    }
    return std::nullopt;
}

[[nodiscard]] std::string serialize(const shape_parts & s) {
    std::string out = s.function + "(";
    const auto round = [&s]() -> std::string {
        return s.radii.empty() ? std::string{} : " round " + radii_text(s.radii);
    };
    if (s.function == "circle" || s.function == "ellipse") {
        out += joined(s.args);
        if (!s.position.empty()) { out += (s.args.empty() ? "at " : " at ") + s.position; }
    } else if (s.function == "inset") {
        out += fold_four(s.args) + round();
    } else if (s.function == "xywh" || s.function == "rect") {
        out += joined(s.args) + round();
    } else if (s.function == "polygon") {
        std::vector<std::string> items;
        std::string lead = s.fill_rule;
        if (!s.round.empty()) { lead += (lead.empty() ? "" : " ") + ("round " + s.round); }
        if (!lead.empty()) { items.push_back(lead); }
        for (std::size_t i = 0; i + 1 < s.points.size(); i += 2) {
            items.push_back(s.points[i] + " " + s.points[i + 1]);
        }
        out += joined(items, ", ");
    } else if (s.function == "path") {
        out += (s.fill_rule.empty() ? "" : s.fill_rule + ", ") + s.path;
    }
    out += ")";
    if (!s.box.empty()) { out += " " + s.box; }
    return out;
}

// A shape and its box, in either order; nullopt when `text` is not one of
// the shapes above. `default_box` is the one left unwritten.
[[nodiscard]] std::optional<shape_parts> parse_shape_value(std::string_view text,
                                                           std::string_view default_box) {
    const std::vector<std::string_view> parts = words_of(text);
    if (parts.empty() || parts.size() > 2) { return std::nullopt; }
    std::optional<shape_parts> shape;
    std::string box;
    for (const std::string_view part : parts) {
        if (has_keyword(geometry_boxes, part) && box.empty() && part.find('(') == part.npos) {
            box = ascii_lower_copy(part);
        } else if (!shape) {
            shape = parse_function(part);
            if (!shape) { return std::nullopt; }
        } else {
            return std::nullopt;
        }
    }
    if (!shape) { return std::nullopt; }
    shape->box = box == default_box ? std::string{} : box;
    return shape;
}

[[nodiscard]] std::string_view default_box_of(std::string_view property) {
    return property == "shape-outside" ? "margin-box" : "border-box";
}

// One component's computed text: its math folded against `ctx`, a length
// in pixels, a percentage and a mix of the two in canonical form.
[[nodiscard]] std::string computed_component(std::string_view text, const length_context & ctx,
                                             bool nonnegative) {
    if (ascii_iequals_any(text, {"auto", "closest-side", "farthest-side"})) {
        return std::string{text};
    }
    const math_answer answer = evaluate_math(text, ctx);
    if (answer.outcome != math_outcome::resolved) { return std::string{text}; }
    std::string out = serialize_calc(answer.value);
    return nonnegative ? non_negative(out) : out;
}

[[nodiscard]] std::optional<calc_result> amount(std::string_view text) {
    const math_answer answer = evaluate_math(text, length_context{});
    if (answer.outcome != math_outcome::resolved) { return std::nullopt; }
    return answer.value;
}

// `100% - a - b` for xywh() and rect(), in canonical form.
[[nodiscard]] std::optional<std::string> remainder(std::string_view a, std::string_view b) {
    calc_result out;
    out.has_percent = true;
    out.percent = 100;
    for (const std::string_view term : {a, b}) {
        if (term.empty()) { continue; }
        const std::optional<calc_result> v = amount(term);
        if (!v) { return std::nullopt; }
        out.px -= v->px;
        out.percent -= v->percent;
    }
    return serialize_calc(out);
}

} // namespace

namespace detail {

bool match_basic_shape(std::string_view property, std::string_view text, std::string & out) {
    if (!ascii_iequals_any(property, {"clip-path", "shape-outside"})) { return false; }
    // A box alone is a value too, written as itself.
    const std::vector<std::string_view> parts = words_of(text);
    bool any_function = false;
    for (const std::string_view part : parts) {
        any_function = any_function || part.find('(') != std::string_view::npos;
    }
    if (!any_function) { return false; }
    std::optional<shape_parts> shape = parse_shape_value(text, default_box_of(property));
    if (!shape) {
        // A url(), an image or a shape() is not this file's.
        for (const std::string_view part : parts) {
            if (ascii_istarts_with(part, "url(") || ascii_istarts_with(part, "shape(") ||
                part.find("gradient(") != std::string_view::npos ||
                ascii_istarts_with(part, "image(") || ascii_istarts_with(part, "image-set(") ||
                ascii_istarts_with(part, "cross-fade(")) {
                return false;
            }
        }
        out.clear();
        return true;
    }
    out = serialize(*shape);
    return true;
}

} // namespace detail

std::string computed_basic_shape(std::string_view property, std::string_view text,
                                 const length_context & ctx) {
    std::optional<shape_parts> s = parse_shape_value(text, default_box_of(property));
    if (!s) { return {}; }
    for (std::string & a : s->args) {
        a = computed_component(a, ctx, s->function == "circle" || s->function == "ellipse");
    }
    for (std::string & r : s->radii) { r = computed_component(r, ctx, true); }
    for (std::string & p : s->points) { p = computed_component(p, ctx, false); }
    if (!s->round.empty()) { s->round = computed_component(s->round, ctx, true); }
    // An omitted centre is still the centre, and the computed value says so.
    if ((s->function == "circle" || s->function == "ellipse") && s->position.empty()) {
        s->position = "50% 50%";
    }
    if (!s->position.empty()) {
        const std::string physical = computed_position(s->position, "", "");
        if (!physical.empty()) {
            std::string resolved;
            for (const std::string_view half : words_of(physical)) {
                resolved += (resolved.empty() ? "" : " ") + computed_component(half, ctx, false);
            }
            s->position = std::move(resolved);
        }
    }
    // xywh() and rect() compute to the inset() they describe (§3.2).
    if (s->function == "xywh" && s->args.size() == 4) {
        const std::optional<std::string> right = remainder(s->args[0], s->args[2]);
        const std::optional<std::string> bottom = remainder(s->args[1], s->args[3]);
        if (right && bottom) {
            s->args = {s->args[1], *right, *bottom, s->args[0]};
            s->function = "inset";
        }
    } else if (s->function == "rect" && s->args.size() == 4) {
        const auto side = [&](std::size_t i, bool from_end) -> std::optional<std::string> {
            if (s->args[i] == "auto") { return std::string{"0%"}; }
            return from_end ? remainder(s->args[i], "") : std::optional{s->args[i]};
        };
        const std::optional<std::string> top = side(0, false), right = side(1, true),
                                         bottom = side(2, true), left = side(3, false);
        if (top && right && bottom && left) {
            s->args = {*top, *right, *bottom, *left};
            s->function = "inset";
        }
    }
    return serialize(*s);
}

std::optional<std::string> blend_basic_shapes(std::string_view property, std::string_view a,
                                              std::string_view b, double wa, double wb) {
    std::optional<shape_parts> x = parse_shape_value(a, default_box_of(property));
    std::optional<shape_parts> y = parse_shape_value(b, default_box_of(property));
    if (!x || !y || x->function != y->function || x->box != y->box ||
        x->fill_rule != y->fill_rule || x->function == "path") {
        return std::nullopt;
    }
    bool ok = true;
    const auto mix = [&](std::string_view p, std::string_view q, bool nonnegative) {
        if (p == q && ascii_iequals_any(p, {"auto", "closest-side", "farthest-side"})) {
            return std::string{p};
        }
        const std::optional<calc_result> u = amount(p.empty() ? "0px" : p);
        const std::optional<calc_result> v = amount(q.empty() ? "0px" : q);
        if (!u || !v) {
            ok = false;
            return std::string{};
        }
        calc_result out;
        out.px = wa * u->px + wb * v->px;
        out.percent = wa * u->percent + wb * v->percent;
        out.has_percent = u->has_percent || v->has_percent;
        std::string text = serialize_calc(out);
        return nonnegative ? non_negative(text) : text;
    };
    const auto mix_all = [&](std::vector<std::string> & p, const std::vector<std::string> & q,
                             bool nonnegative) {
        if (p.size() != q.size()) {
            ok = false;
            return;
        }
        for (std::size_t i = 0; i < p.size(); ++i) { p[i] = mix(p[i], q[i], nonnegative); }
    };
    const bool radial = x->function == "circle" || x->function == "ellipse";
    if (x->function == "inset") {
        x->args = four(x->args);
        y->args = four(y->args);
    }
    if (radial) {
        // An omitted radius is `closest-side` and blends with nothing else.
        if (x->args.size() != y->args.size()) { return std::nullopt; }
        // An omitted position is the centre.
        if (x->position.empty()) { x->position = "50% 50%"; }
        if (y->position.empty()) { y->position = "50% 50%"; }
    }
    mix_all(x->args, y->args, radial);
    if (!x->position.empty() || !y->position.empty()) {
        const std::vector<std::string_view> p = words_of(x->position), q = words_of(y->position);
        if (p.size() != 2 || q.size() != 2) { return std::nullopt; }
        x->position = mix(p[0], q[0], false) + " " + mix(p[1], q[1], false);
    }
    if (!x->radii.empty() || !y->radii.empty()) {
        if (x->radii.empty()) { x->radii.assign(8, "0px"); }
        if (y->radii.empty()) { y->radii.assign(8, "0px"); }
        mix_all(x->radii, y->radii, true);
    }
    if (x->function == "polygon") {
        mix_all(x->points, y->points, false);
        if (!x->round.empty() || !y->round.empty()) {
            x->round = mix(x->round, y->round, true);
            if (zero(x->round)) { x->round.clear(); }
        }
    }
    if (!ok) { return std::nullopt; }
    return serialize(*x);
}

} // namespace ctbrowser::style::css
