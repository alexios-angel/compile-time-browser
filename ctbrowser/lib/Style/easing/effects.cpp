#include "internal.hpp"

namespace ctbrowser::style::easing_detail {

[[nodiscard]] std::string_view primitive_of(std::string_view name) {
    if (name.starts_with("translate")) { return "translate"; }
    if (name.starts_with("scale")) { return "scale"; }
    return name;
}

// A 2D transform list, or nothing for `none`, or nullopt for one this does
// not model - a 3D function, a percentage, an `em` - which stays discrete.
// ponytail: 2D only, matching the computed-style serialiser; a 3D list
// needs matrix3d() there first.
[[nodiscard]] std::optional<std::vector<transform_fn>> parse_transforms(std::string_view text) {
    using css::token_type;
    std::vector<transform_fn> out;
    if (ascii_iequals(text, "none")) { return out; }
    const css::token_stream ts = css::tokenize(text);
    std::size_t at = 0;
    for (;;) {
        while (ts.tokens[at].type == token_type::whitespace) { ++at; }
        if (ts.tokens[at].type == token_type::eof) { break; }
        if (ts.tokens[at].type != token_type::function) { return std::nullopt; }
        const std::string_view raw = ts.text_of(ts.tokens[at]);
        transform_fn fn;
        fn.name = ascii_lower_copy(raw.substr(0, raw.size() - 1));
        ++at;
        std::vector<bool> is_length;
        for (;;) {
            while (ts.tokens[at].type == token_type::whitespace) { ++at; }
            const css::css_token & t = ts.tokens[at];
            if (t.type == token_type::eof) { return std::nullopt; }
            ++at;
            if (t.type == token_type::close_paren) { break; }
            if (t.type == token_type::comma) { continue; }
            fn.pct.push_back(0);
            if (t.type == token_type::number) {
                fn.args.push_back(t.number);
                is_length.push_back(false);
            } else if (t.type == token_type::percentage) {
                fn.args.push_back(0);
                fn.pct.back() = t.number;
                is_length.push_back(true);
            } else if (t.type == token_type::dimension) {
                const std::string unit = ascii_lower_copy(ts.unit_of(t));
                if (unit == "px") {
                    fn.args.push_back(t.number);
                    is_length.push_back(true);
                } else if (unit == "deg") {
                    fn.args.push_back(t.number);
                    is_length.push_back(false);
                } else if (unit == "rad") {
                    fn.args.push_back(t.number * 180.0 / std::numbers::pi);
                    is_length.push_back(false);
                } else if (unit == "grad") {
                    fn.args.push_back(t.number * 0.9);
                    is_length.push_back(false);
                } else if (unit == "turn") {
                    fn.args.push_back(t.number * 360.0);
                    is_length.push_back(false);
                } else {
                    return std::nullopt;
                }
            } else {
                return std::nullopt;
            }
        }
        const std::size_t n = fn.args.size();
        const bool translate = fn.name.starts_with("translate");
        for (std::size_t i = 0; i < n; ++i) {
            if (is_length[i] != translate && !(translate && fn.args[i] == 0.0)) {
                return std::nullopt;
            }
            if (!translate && fn.pct[i] != 0) { return std::nullopt; }
        }
        // Canonical two-argument forms, so the same primitive always pairs.
        const auto append = [&fn](double v) { fn.args.push_back(v), fn.pct.push_back(0); };
        const auto prepend = [&fn](double v) {
            fn.args.insert(fn.args.begin(), v), fn.pct.insert(fn.pct.begin(), 0);
        };
        if (fn.name == "translatex" && n == 1) {
            fn.name = "translate", append(0);
        } else if (fn.name == "translatey" && n == 1) {
            fn.name = "translate", prepend(0);
        } else if (fn.name == "translate" && n == 1) {
            append(0);
        } else if (fn.name == "scalex" && n == 1) {
            fn.name = "scale", append(1);
        } else if (fn.name == "scaley" && n == 1) {
            fn.name = "scale", prepend(1);
        } else if (fn.name == "scale" && n == 1) {
            append(fn.args[0]);
        } else if (fn.name == "skew" && n == 1) {
            append(0);
        }
        const std::size_t count = fn.args.size();
        const bool shaped =
            (fn.name == "matrix" && count == 6) || (fn.name == "translate" && count == 2) ||
            (fn.name == "scale" && count == 2) || (fn.name == "rotate" && count == 1) ||
            (fn.name == "skew" && count == 2) || (fn.name == "skewx" && count == 1) ||
            (fn.name == "skewy" && count == 1);
        if (!shaped) { return std::nullopt; }
        out.push_back(std::move(fn));
    }
    return out;
}

[[nodiscard]] matrix2d multiply(const matrix2d & m, const matrix2d & n) {
    return {m[0] * n[0] + m[2] * n[1],        m[1] * n[0] + m[3] * n[1],
            m[0] * n[2] + m[2] * n[3],        m[1] * n[2] + m[3] * n[3],
            m[0] * n[4] + m[2] * n[5] + m[4], m[1] * n[4] + m[3] * n[5] + m[5]};
}

[[nodiscard]] double radians(double deg) {
    return deg * std::numbers::pi / 180.0;
}

[[nodiscard]] matrix2d matrix_of(const transform_fn & fn) {
    const std::vector<double> & a = fn.args;
    if (fn.name == "matrix") { return {a[0], a[1], a[2], a[3], a[4], a[5]}; }
    if (fn.name == "translate") { return {1, 0, 0, 1, a[0], a[1]}; }
    if (fn.name == "scale") { return {a[0], 0, 0, a[1], 0, 0}; }
    if (fn.name == "rotate") {
        const double r = radians(a[0]);
        return {std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0};
    }
    if (fn.name == "skew") {
        return {1, std::tan(radians(a[1])), std::tan(radians(a[0])), 1, 0, 0};
    }
    if (fn.name == "skewx") { return {1, 0, std::tan(radians(a[0])), 1, 0, 0}; }
    return {1, std::tan(radians(a[0])), 0, 1, 0, 0}; // skewy
}

[[nodiscard]] matrix2d matrix_of(const std::vector<transform_fn> & list) {
    matrix2d m{1, 0, 0, 1, 0, 0};
    for (const transform_fn & fn : list) { m = multiply(m, matrix_of(fn)); }
    return m;
}

[[nodiscard]] decomposed2d decompose(const matrix2d & m) {
    double row0x = m[0], row0y = m[1], row1x = m[2], row1y = m[3];
    decomposed2d d{m[4], m[5], 0, 0, 0, 1, 0, 0, 1};
    d.sx = std::hypot(row0x, row0y);
    if (d.sx != 0) { row0x /= d.sx, row0y /= d.sx; }
    double skew = row0x * row1x + row0y * row1y;
    row1x -= row0x * skew, row1y -= row0y * skew;
    d.sy = std::hypot(row1x, row1y);
    if (d.sy != 0) { row1x /= d.sy, row1y /= d.sy, skew /= d.sy; }
    if (row0x * row1y - row0y * row1x < 0) {
        d.sx = -d.sx;
        row0x = -row0x, row0y = -row0y;
    }
    d.angle = std::atan2(row0y, row0x) * 180.0 / std::numbers::pi;
    // The residual is the matrix with its rotation taken back out, so that
    // recomposing rotate(angle) * residual gives the rows back.
    const double sn = -row0y, cs = row0x;
    const double m11 = row0x, m12 = row0y, m21 = row1x, m22 = row1y;
    d.m11 = cs * m11 + sn * m21, d.m12 = cs * m12 + sn * m22;
    d.m21 = -sn * m11 + cs * m21, d.m22 = -sn * m12 + cs * m22;
    return d;
}

[[nodiscard]] matrix2d recompose(const decomposed2d & d) {
    matrix2d m{1, 0, 0, 1, d.tx, d.ty};
    const double r = radians(d.angle);
    m = multiply(m, {std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0});
    m = multiply(m, {d.m11, d.m12, d.m21, d.m22, 0, 0});
    return multiply(m, {d.sx, 0, 0, d.sy, 0, 0});
}

[[nodiscard]] std::string matrix_text(const matrix2d & m) {
    std::string out{"matrix("};
    for (std::size_t i = 0; i < 6; ++i) {
        if (i != 0) { out += ", "; }
        // Six decimals, which is where the computed serialiser rounds anyway.
        out += css::serialize_number(std::round(m[i] * 1e6) / 1e6);
    }
    return out + ')';
}

[[nodiscard]] std::string function_text(const transform_fn & fn) {
    std::string out = fn.name + '(';
    const bool translate = fn.name == "translate";
    const bool angles = fn.name == "rotate" || fn.name.starts_with("skew");
    for (std::size_t i = 0; i < fn.args.size(); ++i) {
        if (i != 0) { out += ", "; }
        if (translate) {
            css::calc_result length;
            length.px = fn.args[i];
            length.percent = fn.pct[i];
            length.has_percent = fn.pct[i] != 0;
            out += css::serialize_calc(length);
            continue;
        }
        out += css::serialize_number(fn.args[i]);
        if (angles) { out += "deg"; }
    }
    return out + ')';
}

[[nodiscard]] transform_fn identity_like(const transform_fn & fn) {
    transform_fn out{fn.name, {}, {}};
    if (fn.name == "matrix") {
        out.args = {1, 0, 0, 1, 0, 0};
    } else if (fn.name == "scale") {
        out.args = {1, 1};
    } else {
        out.args.assign(fn.args.size(), 0.0);
    }
    out.pct.assign(out.args.size(), 0.0);
    return out;
}

// §12: function by function while the lists match - the shorter padded
// with the identity of its partner - else the whole lists as matrices,
// decomposed. `none` on both sides stays `none`.
[[nodiscard]] std::optional<std::string> interpolate_transform(std::string_view from,
                                                               std::string_view to, double p) {
    std::optional<std::vector<transform_fn>> a = parse_transforms(from);
    std::optional<std::vector<transform_fn>> b = parse_transforms(to);
    if (!a || !b) { return std::nullopt; }
    if (a->empty() && b->empty()) { return "none"; }
    // The endpoints are themselves, as written: `translateY(90%)` at 1 is
    // `translateY(90%)` and not its two-argument spelling.
    if (p == 0 && !a->empty()) { return std::string{from}; }
    if (p == 1 && !b->empty()) { return std::string{to}; }
    const auto lerp = [p](double x, double y) { return (1 - p) * x + p * y; };
    const std::size_t shorter = std::min(a->size(), b->size());
    bool matched = true;
    for (std::size_t i = 0; i < shorter && matched; ++i) {
        // Two matrix() functions interpolate by decomposition (§12.1), not
        // term by term: they send the whole lists down the matrix path.
        matched = primitive_of((*a)[i].name) == primitive_of((*b)[i].name) &&
                  ((*a)[i].name != "matrix" || (*a)[i].args == (*b)[i].args);
    }
    if (matched) {
        for (std::size_t i = a->size(); i < b->size(); ++i) {
            a->push_back(identity_like((*b)[i]));
        }
        for (std::size_t i = b->size(); i < a->size(); ++i) {
            b->push_back(identity_like((*a)[i]));
        }
        std::string out;
        for (std::size_t i = 0; i < a->size(); ++i) {
            transform_fn fn = (*a)[i];
            for (std::size_t k = 0; k < fn.args.size(); ++k) {
                fn.args[k] = lerp((*a)[i].args[k], (*b)[i].args[k]);
                fn.pct[k] = lerp((*a)[i].pct[k], (*b)[i].pct[k]);
            }
            if (i != 0) { out += ' '; }
            out += function_text(fn);
        }
        return out;
    }
    // A percentage has no matrix until the box exists: such a pair that does
    // not match function by function stays discrete.
    for (const std::vector<transform_fn> * list : {&*a, &*b}) {
        for (const transform_fn & fn : *list) {
            if (std::ranges::any_of(fn.pct, [](double v) { return v != 0; })) {
                return std::nullopt;
            }
        }
    }
    decomposed2d x = decompose(matrix_of(*a));
    const decomposed2d y = decompose(matrix_of(*b));
    // The rotation goes the short way round, and a flip of scale on one
    // side is undone on the other (§12.2).
    if ((x.sx < 0 && y.sy < 0) || (x.sy < 0 && y.sx < 0)) {
        x.sx = -x.sx, x.sy = -x.sy;
        x.angle += x.angle < 0 ? 180 : -180;
    }
    if (std::fabs(x.angle - y.angle) > 180) {
        if (x.angle > y.angle) {
            x.angle -= 360;
        } else {
            x.angle += 360;
        }
    }
    const decomposed2d z{lerp(x.tx, y.tx),   lerp(x.ty, y.ty),       lerp(x.sx, y.sx),
                         lerp(x.sy, y.sy),   lerp(x.angle, y.angle), lerp(x.m11, y.m11),
                         lerp(x.m12, y.m12), lerp(x.m21, y.m21),     lerp(x.m22, y.m22)};
    return matrix_text(recompose(z));
}

// A filter list in computed form - percentages as numbers, lengths in px -
// as its functions; `none` is the empty list; nothing for a list this does
// not model (a `url()`).
[[nodiscard]] std::optional<std::vector<filter_fn>> parse_filters(std::string_view text) {
    std::vector<filter_fn> out;
    if (text.empty() || ascii_iequals(text, "none")) { return out; }
    const std::string computed = css::computed_filter(text, {});
    for (const std::string_view raw :
         split_top_level(computed.empty() ? text : computed, html_whitespace)) {
        const std::string_view item = trim(raw, html_whitespace);
        if (item.empty()) { continue; }
        const std::size_t open = item.find('(');
        if (open == std::string_view::npos || !item.ends_with(')')) { return std::nullopt; }
        filter_fn fn{
            ascii_lower_copy(item.substr(0, open)),
            std::string{trim(item.substr(open + 1, item.size() - open - 2), html_whitespace)}};
        if (fn.name == "url" || fn.name == "src") { return std::nullopt; }
        out.push_back(std::move(fn));
    }
    return out;
}

// The value a missing or empty function argument means (§11.2's lacuna).
[[nodiscard]] std::string_view filter_identity(std::string_view name) {
    if (name == "blur") { return "0px"; }
    if (name == "hue-rotate") { return "0deg"; }
    if (name == "drop-shadow") { return "rgba(0, 0, 0, 0) 0px 0px 0px"; }
    if (name == "grayscale" || name == "invert" || name == "sepia") { return "0"; }
    return "1"; // brightness, contrast, opacity, saturate
}

// §11.2: function by function while the lists match, the shorter padded
// with the missing functions' lacuna values; a drop-shadow is a shadow. No
// result goes negative, and the four amounts that saturate at 1 stop there.
[[nodiscard]] std::optional<std::string> interpolate_filter(std::string_view from,
                                                            std::string_view to, double p,
                                                            const css::length_context & ctx) {
    std::optional<std::vector<filter_fn>> a = parse_filters(from);
    std::optional<std::vector<filter_fn>> b = parse_filters(to);
    if (!a || !b) { return std::nullopt; }
    if (a->empty() && b->empty()) { return "none"; }
    for (std::size_t i = 0; i < std::min(a->size(), b->size()); ++i) {
        if ((*a)[i].name != (*b)[i].name) { return std::nullopt; }
    }
    for (std::size_t i = a->size(); i < b->size(); ++i) { a->push_back({(*b)[i].name, ""}); }
    for (std::size_t i = b->size(); i < a->size(); ++i) { b->push_back({(*a)[i].name, ""}); }
    std::string out;
    for (std::size_t i = 0; i < a->size(); ++i) {
        const std::string & name = (*a)[i].name;
        const std::string_view x = (*a)[i].args.empty() ? filter_identity(name) : (*a)[i].args;
        const std::string_view y = (*b)[i].args.empty() ? filter_identity(name) : (*b)[i].args;
        bool ok = true;
        std::string piece;
        if (name == "drop-shadow") {
            piece = interpolate_pair("text-shadow", computed_shape("text-shadow", x, ctx),
                                     computed_shape("text-shadow", y, ctx), p, ctx, ok);
        } else {
            const std::optional<numeric_pair> n = numeric_of(x, y, ctx);
            if (!n) { return std::nullopt; }
            css::calc_result mixed = mix(n->a, n->b, p);
            mixed.px = std::max(mixed.px, 0.0);
            if (name == "grayscale" || name == "invert" || name == "opacity" || name == "sepia") {
                mixed.px = std::min(mixed.px, 1.0);
            }
            piece = css::serialize_calc(mixed);
        }
        if (!ok) { return std::nullopt; }
        if (i != 0) { out += ' '; }
        out += name + '(' + piece + ')';
    }
    return out;
}

// CSS Transforms 2 §14, accumulation of two transform lists: function by
// function when the lists match in length and primitive - a translate sums,
// a scale sums its excess over 1, a rotate or skew sums its angles, a matrix
// pair is decomposed, summed the same way and recomposed - and otherwise the
// keyframe's list is appended to the underlying one. A matrix that cannot
// be decomposed (singular) leaves the keyframe's value alone.
[[nodiscard]] std::optional<std::string> accumulate_transform(std::string_view underlying,
                                                              std::string_view value) {
    const std::optional<std::vector<transform_fn>> a = parse_transforms(underlying);
    const std::optional<std::vector<transform_fn>> b = parse_transforms(value);
    if (!a || !b) { return std::nullopt; }
    if (a->empty()) { return std::string{value}; }
    if (b->empty()) { return std::string{underlying}; }
    if (a->size() != b->size()) { return std::nullopt; }
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (primitive_of((*a)[i].name) != primitive_of((*b)[i].name)) { return std::nullopt; }
    }
    std::string out;
    for (std::size_t i = 0; i < a->size(); ++i) {
        const transform_fn & x = (*a)[i];
        const transform_fn & y = (*b)[i];
        transform_fn fn = x;
        if (fn.name == "matrix") {
            const matrix2d mx = matrix_of(x), my = matrix_of(y);
            if (mx[0] * mx[3] - mx[1] * mx[2] == 0 || my[0] * my[3] - my[1] * my[2] == 0) {
                return std::string{value};
            }
            const decomposed2d dx = decompose(mx), dy = decompose(my);
            const decomposed2d sum{dx.tx + dy.tx,     dx.ty + dy.ty,       dx.sx + dy.sx - 1,
                                   dx.sy + dy.sy - 1, dx.angle + dy.angle, dx.m11 + dy.m11 - 1,
                                   dx.m12 + dy.m12,   dx.m21 + dy.m21,     dx.m22 + dy.m22 - 1};
            if (i != 0) { out += ' '; }
            out += matrix_text(recompose(sum));
            continue;
        }
        for (std::size_t k = 0; k < fn.args.size(); ++k) {
            fn.args[k] = fn.name == "scale" ? x.args[k] + y.args[k] - 1 : x.args[k] + y.args[k];
            fn.pct[k] = x.pct[k] + y.pct[k];
        }
        if (i != 0) { out += ' '; }
        out += function_text(fn);
    }
    return out;
}

// `none`, `<angle>`, `x|y|z <angle>` or `<number>{3} <angle>`, the axis as
// given; nothing for anything else.
[[nodiscard]] std::optional<rotation> rotation_of(std::string_view text,
                                                  const css::length_context & ctx) {
    rotation out;
    if (ascii_iequals(text, "none")) { return out; }
    std::vector<std::string_view> parts;
    for (const std::string_view part : split_top_level(text, html_whitespace)) {
        if (!part.empty()) { parts.push_back(part); }
    }
    if (parts.empty() || parts.size() == 3 || parts.size() > 4) { return std::nullopt; }
    const css::math_answer angle = css::evaluate_math(parts.back(), ctx);
    if (angle.outcome != css::math_outcome::resolved ||
        angle.value.type != css::numeric_type::angle) {
        return std::nullopt;
    }
    out.angle = angle.value.px;
    if (parts.size() == 2) {
        const std::string axis = ascii_lower_copy(parts[0]);
        if (axis == "x") {
            out.x = 1, out.z = 0;
        } else if (axis == "y") {
            out.y = 1, out.z = 0;
        } else if (axis != "z") {
            return std::nullopt;
        }
    } else if (parts.size() == 4) {
        double v[3];
        for (std::size_t i = 0; i < 3; ++i) {
            const std::optional<double> n = number_of(parts[i]);
            if (!n) { return std::nullopt; }
            v[i] = *n;
        }
        out.x = v[0], out.y = v[1], out.z = v[2];
    }
    return out;
}

// Two rotations about one axis - or one of them by nothing, which takes the
// other's axis - interpolate by angle; different axes stay discrete.
// ponytail: the spec's quaternion slerp for differing axes, when a test
// reads one.
[[nodiscard]] std::optional<std::string> interpolate_rotate(std::string_view from,
                                                            std::string_view to, double p,
                                                            const css::length_context & ctx) {
    std::optional<rotation> a = rotation_of(from, ctx);
    std::optional<rotation> b = rotation_of(to, ctx);
    if (!a || !b) { return std::nullopt; }
    const auto unit = [](rotation & r) {
        const double n = std::hypot(r.x, r.y, r.z);
        if (n == 0) { return false; }
        r.x /= n, r.y /= n, r.z /= n;
        return true;
    };
    if (!unit(*a) || !unit(*b)) { return std::nullopt; }
    if (a->angle == 0) { a->x = b->x, a->y = b->y, a->z = b->z; }
    if (b->angle == 0) { b->x = a->x, b->y = a->y, b->z = a->z; }
    const auto text = [](double x, double y, double z, double angle) {
        const auto tidy = [](double v) { return std::fabs(v) < 5e-7 ? 0.0 : v; }; // no `-0`
        return css::serialize_number(tidy(x)) + " " + css::serialize_number(tidy(y)) + " " +
               css::serialize_number(tidy(z)) + " " + css::serialize_number(tidy(angle)) + "deg";
    };
    const auto near = [](double u, double v) { return std::fabs(u - v) < 1e-6; };
    if (near(a->x, b->x) && near(a->y, b->y) && near(a->z, b->z)) {
        return text(a->x, a->y, a->z, (1 - p) * a->angle + p * b->angle);
    }
    // Different axes: the two rotations as unit quaternions, slerped
    // (CSS Transforms 2 §9's interpolation of rotate3d()), then back to an
    // axis and an angle.
    const auto quaternion = [](const rotation & r) {
        const double half = radians(r.angle) / 2;
        return std::array<double, 4>{r.x * std::sin(half), r.y * std::sin(half),
                                     r.z * std::sin(half), std::cos(half)};
    };
    std::array<double, 4> q1 = quaternion(*a), q2 = quaternion(*b);
    double dot = q1[0] * q2[0] + q1[1] * q2[1] + q1[2] * q2[2] + q1[3] * q2[3];
    if (dot < 0) {
        for (double & v : q2) { v = -v; }
        dot = -dot;
    }
    dot = std::min(dot, 1.0);
    const double theta = std::acos(dot);
    std::array<double, 4> q;
    for (std::size_t i = 0; i < 4; ++i) {
        q[i] = theta < 1e-6 ? (1 - p) * q1[i] + p * q2[i]
                            : (std::sin((1 - p) * theta) * q1[i] + std::sin(p * theta) * q2[i]) /
                                  std::sin(theta);
    }
    const double norm = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (double & v : q) { v /= norm; }
    // The canonical spelling of a rotation is the one under a half turn.
    if (q[3] < 0) {
        for (double & v : q) { v = -v; }
    }
    const double angle = 2 * std::acos(std::clamp(q[3], -1.0, 1.0));
    const double s = std::sin(angle / 2);
    if (std::fabs(s) < 1e-9) { return text(0, 0, 1, 0); }
    return text(q[0] / s, q[1] / s, q[2] / s, angle * 180.0 / std::numbers::pi);
}

} // namespace ctbrowser::style::easing_detail
