#include "easing/internal.hpp"

namespace ctbrowser::style {

using namespace easing_detail;

[[nodiscard]] double easing::operator()(double input, bool before) const {
    switch (shape) {
    case kind::linear: return input;
    case kind::bezier: return bezier(input);
    case kind::steps: return step(input, before);
    }
    return input;
}

[[nodiscard]] double easing::bezier(double input) const {
    if (input < 0) {
        if (x1 > 0) { return input * y1 / x1; }
        if (x1 == 0 && y1 == 0 && x2 > 0) { return input * y2 / x2; }
        return 0;
    }
    if (input > 1) {
        if (x2 < 1) { return 1 + (input - 1) * (y2 - 1) / (x2 - 1); }
        if (x2 == 1 && y2 == 1 && x1 < 1) { return 1 + (input - 1) * (y1 - 1) / (x1 - 1); }
        return 1;
    }
    // Solve x(t) = input for t by bisection - the curve's x is monotone
    // because 0 <= x1, x2 <= 1 - then read y(t).
    const auto at = [](double p1, double p2, double t) {
        const double u = 1 - t;
        return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
    };
    double lo = 0, hi = 1, t = input;
    for (int i = 0; i < 60; ++i) {
        t = (lo + hi) / 2;
        const double x = at(x1, x2, t);
        if (std::fabs(x - input) < 1e-12) { break; }
        (x < input ? lo : hi) = t;
    }
    return at(y1, y2, t);
}

[[nodiscard]] double easing::step(double input, bool before) const {
    const double n = steps;
    double current = std::floor(input * n);
    if (position == jump::start || position == jump::both) { current += 1; }
    if (before && input * n == std::floor(input * n)) { current -= 1; }
    if (input >= 0 && current < 0) { current = 0; }
    const double jumps = position == jump::none ? n - 1 : position == jump::both ? n + 1 : n;
    if (input <= 1 && current > jumps) { current = jumps; }
    return current / jumps;
}

// `nullopt` for text that is not an easing, which is a TypeError at every
// place the specification takes one.
[[nodiscard]] std::optional<easing> parse_easing(std::string_view text) {
    const std::string lowered = ascii_lower_copy(trim(text, html_whitespace));
    easing out;
    const auto bezier = [&](double a, double b, double c, double d) {
        out.shape = easing::kind::bezier;
        out.x1 = a;
        out.y1 = b;
        out.x2 = c;
        out.y2 = d;
        return out;
    };
    if (lowered == "linear") { return out; }
    if (lowered == "ease") { return bezier(0.25, 0.1, 0.25, 1); }
    if (lowered == "ease-in") { return bezier(0.42, 0, 1, 1); }
    if (lowered == "ease-out") { return bezier(0, 0, 0.58, 1); }
    if (lowered == "ease-in-out") { return bezier(0.42, 0, 0.58, 1); }
    if (lowered == "step-start") {
        out.shape = easing::kind::steps;
        out.position = easing::jump::start;
        return out;
    }
    if (lowered == "step-end") {
        out.shape = easing::kind::steps;
        return out;
    }
    if (!lowered.ends_with(')')) { return std::nullopt; }
    if (lowered.starts_with("cubic-bezier(")) {
        const auto args =
            split_arguments(std::string_view{lowered}.substr(13, lowered.size() - 14));
        if (args.size() != 4) { return std::nullopt; }
        double n[4];
        for (std::size_t i = 0; i < 4; ++i) {
            const std::optional<double> v = number_of(args[i]);
            if (!v) { return std::nullopt; }
            n[i] = *v;
        }
        if (n[0] < 0 || n[0] > 1 || n[2] < 0 || n[2] > 1) { return std::nullopt; }
        return bezier(n[0], n[1], n[2], n[3]);
    }
    if (lowered.starts_with("steps(")) {
        const auto args = split_arguments(std::string_view{lowered}.substr(6, lowered.size() - 7));
        if (args.empty() || args.size() > 2) { return std::nullopt; }
        const std::optional<int> count = step_count(args[0]);
        if (!count) { return std::nullopt; }
        out.shape = easing::kind::steps;
        out.steps = *count;
        if (args.size() == 2) {
            const std::string_view p = args[1];
            if (p == "start" || p == "jump-start") {
                out.position = easing::jump::start;
            } else if (p == "end" || p == "jump-end") {
                out.position = easing::jump::end;
            } else if (p == "jump-none") {
                if (out.steps < 2) { return std::nullopt; }
                out.position = easing::jump::none;
            } else if (p == "jump-both") {
                out.position = easing::jump::both;
            } else {
                return std::nullopt;
            }
        }
        return out;
    }
    return std::nullopt;
}

// --- interpolation ------------------------------------------------------------

[[nodiscard]] std::string interpolate_text(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx) {
    // Identical endpoints are that value at every progress - `none` to
    // `none` stays `none` rather than becoming its expanded shape.
    if (trim(from, html_whitespace) == trim(to, html_whitespace)) {
        return std::string{trim(from, html_whitespace)};
    }
    bool interpolable = true;
    std::string out = interpolate_pair(property, computed_shape(property, from, ctx),
                                       computed_shape(property, to, ctx), p, ctx, interpolable);
    // A corner radius whose two halves came out equal is written once, as
    // the computed serialiser writes a declared one.
    if (property.starts_with("border-") && property.ends_with("-radius")) {
        const std::vector<std::string_view> halves = split_top_level(out, html_whitespace);
        if (halves.size() == 2 && halves[0] == halves[1]) { return std::string{halves[0]}; }
    }
    return out;
}

[[nodiscard]] bool interpolable_text(std::string_view property, std::string_view from,
                                     std::string_view to) {
    bool interpolable = true;
    const css::length_context ctx;
    (void)interpolate_pair(property, computed_shape(property, from, ctx),
                           computed_shape(property, to, ctx), 0.5, ctx, interpolable);
    return interpolable;
}

[[nodiscard]] std::string with_currentcolor(std::string_view value, std::string_view color) {
    static constexpr std::string_view word = "currentcolor";
    std::string out;
    std::size_t i = 0;
    const auto boundary = [](char c) {
        return !(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_');
    };
    while (i < value.size()) {
        if (value.size() - i >= word.size() && ascii_iequals(value.substr(i, word.size()), word) &&
            (i == 0 || boundary(value[i - 1])) &&
            (i + word.size() == value.size() || boundary(value[i + word.size()]))) {
            out += color;
            i += word.size();
            continue;
        }
        out += value[i++];
    }
    return out;
}

[[nodiscard]] std::string composite_text(std::string_view property, std::string_view underlying,
                                         std::string_view value, composite_op op,
                                         const css::length_context & ctx) {
    if (op == composite_op::replace) { return std::string{value}; }
    const std::string base = computed_shape(property, trim(underlying, html_whitespace), ctx);
    const std::string added = computed_shape(property, trim(value, html_whitespace), ctx);
    if (property == "transform" && op == composite_op::accumulate) {
        if (const std::optional<std::string> sum = accumulate_transform(base, added)) {
            return *sum;
        }
    }
    if (appends(property)) {
        // The underlying list first, then the keyframe's; `none` on either
        // side is the empty list and contributes nothing.
        const bool base_none = base.empty() || ascii_iequals(base, "none");
        const bool added_none = added.empty() || ascii_iequals(added, "none");
        if (base_none) { return added; }
        if (added_none) { return base; }
        return base + (is_shadow(property) ? ", " : " ") + added;
    }
    if (property == "rotate") {
        // CSS Transforms 2 §7.2: `none` adds nothing; about one axis the
        // angles sum; about two the rotations compose as quaternions.
        std::optional<rotation> a = rotation_of(base, ctx);
        std::optional<rotation> b = rotation_of(added, ctx);
        if (!a || !b) { return added; }
        if (b->angle == 0) { return base; }
        if (a->angle == 0) { return added; }
        const auto unit = [](rotation & r) {
            const double n = std::hypot(r.x, r.y, r.z);
            if (n == 0) { return false; }
            r.x /= n, r.y /= n, r.z /= n;
            return true;
        };
        if (!unit(*a) || !unit(*b)) { return added; }
        const auto near = [](double u, double v) { return std::fabs(u - v) < 1e-6; };
        const auto text = [](double x, double y, double z, double angle) {
            const auto tidy = [](double v) { return std::fabs(v) < 5e-7 ? 0.0 : v; }; // no `-0`
            return css::serialize_number(tidy(x)) + " " + css::serialize_number(tidy(y)) + " " +
                   css::serialize_number(tidy(z)) + " " + css::serialize_number(tidy(angle)) +
                   "deg";
        };
        if (near(a->x, b->x) && near(a->y, b->y) && near(a->z, b->z)) {
            return text(a->x, a->y, a->z, a->angle + b->angle);
        }
        // q = qb * qa: the underlying rotation first, then the keyframe's.
        const auto quaternion = [](const rotation & r) {
            const double half = radians(r.angle) / 2;
            return std::array<double, 4>{r.x * std::sin(half), r.y * std::sin(half),
                                         r.z * std::sin(half), std::cos(half)};
        };
        const std::array<double, 4> qa = quaternion(*a), qb = quaternion(*b);
        std::array<double, 4> q{qb[3] * qa[0] + qb[0] * qa[3] + qb[1] * qa[2] - qb[2] * qa[1],
                                qb[3] * qa[1] - qb[0] * qa[2] + qb[1] * qa[3] + qb[2] * qa[0],
                                qb[3] * qa[2] + qb[0] * qa[1] - qb[1] * qa[0] + qb[2] * qa[3],
                                qb[3] * qa[3] - qb[0] * qa[0] - qb[1] * qa[1] - qb[2] * qa[2]};
        if (q[3] < 0) {
            for (double & v : q) { v = -v; }
        }
        const double angle = 2 * std::acos(std::clamp(q[3], -1.0, 1.0));
        const double sn = std::sin(angle / 2);
        if (std::fabs(sn) < 1e-9) { return text(0, 0, 1, 0); }
        return text(q[0] / sn, q[1] / sn, q[2] / sn, angle * 180.0 / std::numbers::pi);
    }
    if (property == "scale") {
        // CSS Transforms 2 §7: scales add by multiplying component by
        // component, and accumulate by summing each one's excess over 1.
        const std::vector<std::string_view> a = split_top_level(base, html_whitespace);
        const std::vector<std::string_view> b = split_top_level(added, html_whitespace);
        if (a.size() != 3 || b.size() != 3) { return added; }
        std::string out;
        for (std::size_t i = 0; i < 3; ++i) {
            const std::optional<numeric_pair> n = numeric_of(a[i], b[i], ctx);
            if (!n || !n->a.is_number) { return added; }
            css::calc_result product = n->a;
            product.px = op == composite_op::add ? n->a.px * n->b.px : n->a.px + n->b.px - 1;
            out += (i == 0 ? "" : " ") + css::serialize_calc(product);
        }
        return out;
    }
    return add_pair(property, base, added, ctx);
}

} // namespace ctbrowser::style
