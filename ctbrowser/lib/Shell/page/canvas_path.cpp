//===- ctbrowser/lib/Shell/page/canvas_path.cpp - Canvas 2D geometry ------===//
//
// The geometry half of the canvas 2D context: the path verbs (flattened to
// device-space polylines), gradients, the stroker that turns a path into a
// fillable outline (joins, caps, dashes), and hit testing. canvas.cpp holds
// the pixel half.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/page/canvas.hpp>

#include <numbers>

namespace ctbrowser::shell {

namespace {

constexpr double tau = 2.0 * std::numbers::pi;

/// A 2D vector in double precision, for the stroker's geometry.
struct vec {
    double x = 0, y = 0;
};

[[nodiscard]] vec operator+(vec a, vec b) {
    return vec{a.x + b.x, a.y + b.y};
}
[[nodiscard]] vec operator-(vec a, vec b) {
    return vec{a.x - b.x, a.y - b.y};
}
[[nodiscard]] vec operator*(vec a, double k) {
    return vec{a.x * k, a.y * k};
}
[[nodiscard]] double dot(vec a, vec b) {
    return a.x * b.x + a.y * b.y;
}
[[nodiscard]] double cross(vec a, vec b) {
    return a.x * b.y - a.y * b.x;
}
[[nodiscard]] double length(vec a) {
    return std::sqrt(dot(a, a));
}
[[nodiscard]] vec normalized(vec a) {
    const double l = length(a);
    return l > 0 ? a * (1.0 / l) : vec{};
}

/// How many straight pieces an arc of \p radius device pixels needs for the
/// chord to stay within a quarter pixel of the curve, capped at 1024 so a
/// radius blown up by a huge transform cannot cost unbounded time.
[[nodiscard]] int arc_segments(double radius, double sweep) {
    const double r = std::max(radius, 0.5);
    const double step = 2.0 * std::acos(std::clamp(1.0 - 0.25 / r, -1.0, 1.0));
    const double wanted = std::ceil(std::fabs(sweep) / std::max(step, 1e-3));
    return static_cast<int>(std::clamp(wanted, 1.0, 1024.0));
}

/// How many segments to flatten a Bezier into: about one per three device
/// pixels of control-polygon length (\p extent), at least 4 and at most 256.
/// This is a heuristic and does not bound the flattening error.
[[nodiscard]] int curve_steps(double extent) {
    return static_cast<int>(std::clamp(extent / 3.0 + 4.0, 4.0, 256.0));
}

} // namespace

// --- path verbs ------------------------------------------------------------

void path_builder::move_to(double x, double y) {
    path_->subpaths.push_back(canvas_path::subpath{{device(x, y)}, false});
}

void path_builder::ensure_subpath(double x, double y) {
    if (path_->subpaths.empty()) { move_to(x, y); }
}

void path_builder::line_to_device(point p) {
    if (path_->subpaths.empty()) {
        path_->subpaths.push_back(canvas_path::subpath{{p}, false});
        return;
    }
    path_->subpaths.back().points.push_back(p);
}

void path_builder::line_to(double x, double y) {
    // With no subpath, lineTo is a moveTo: "ensure there is a subpath".
    if (path_->subpaths.empty()) {
        move_to(x, y);
        return;
    }
    line_to_device(device(x, y));
}

void path_builder::close_path() {
    if (path_->subpaths.empty()) { return; }
    canvas_path::subpath & last = path_->subpaths.back();
    last.closed = true;
    // "...and create a new subpath whose first point is the same as the
    // previous subpath's first point", so a lineTo after closePath starts
    // where the closed shape did.
    const point first = last.points.front();
    path_->subpaths.push_back(canvas_path::subpath{{first}, false});
}

std::optional<std::pair<double, double>> path_builder::last_user_point() const {
    if (path_->subpaths.empty() || path_->subpaths.back().points.empty()) { return std::nullopt; }
    const point p = path_->subpaths.back().points.back();
    const point user = ctm_.inverse().apply(p.x, p.y);
    return std::pair<double, double>{user.x, user.y};
}

void path_builder::quadratic_curve_to(double cx, double cy, double x, double y) {
    ensure_subpath(cx, cy);
    // Flattened in device space: the transform is affine, so the curve through
    // the transformed control points is the transform of the curve, and the
    // segment count then follows the curve's size on screen.
    const point control = device(cx, cy);
    const point end = device(x, y);
    const point start = path_->subpaths.back().points.back();
    const double extent = std::hypot(control.x - start.x, control.y - start.y) +
                          std::hypot(end.x - control.x, end.y - control.y);
    const int steps = curve_steps(extent);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float u = 1 - t;
        line_to_device(point{u * u * start.x + 2 * u * t * control.x + t * t * end.x,
                             u * u * start.y + 2 * u * t * control.y + t * t * end.y});
    }
}

void path_builder::bezier_curve_to(double c1x, double c1y, double c2x, double c2y, double x,
                                   double y) {
    ensure_subpath(c1x, c1y);
    const point one = device(c1x, c1y);
    const point two = device(c2x, c2y);
    const point end = device(x, y);
    const point start = path_->subpaths.back().points.back();
    const double extent = std::hypot(one.x - start.x, one.y - start.y) +
                          std::hypot(two.x - one.x, two.y - one.y) +
                          std::hypot(end.x - two.x, end.y - two.y);
    const int steps = curve_steps(extent);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float u = 1 - t;
        line_to_device(point{u * u * u * start.x + 3 * u * u * t * one.x + 3 * u * t * t * two.x +
                                 t * t * t * end.x,
                             u * u * u * start.y + 3 * u * u * t * one.y + 3 * u * t * t * two.y +
                                 t * t * t * end.y});
    }
}

void path_builder::elliptic_arc(double x, double y, double rx, double ry, double rotation,
                                double from, double sweep) {
    const int steps = arc_segments(std::max(rx, ry) * ctm_.scale_bound(), sweep);
    const double cos_r = std::cos(rotation);
    const double sin_r = std::sin(rotation);
    const auto at = [&](double t) {
        const double ex = rx * std::cos(t);
        const double ey = ry * std::sin(t);
        return device(x + ex * cos_r - ey * sin_r, y + ex * sin_r + ey * cos_r);
    };
    // Joined to whatever came before by a straight line - or begun, if there
    // is nothing before it.
    line_to_device(at(from));
    if (sweep == 0) { return; }
    // The arc leaves each end along its tangent. A chord leaves the end of an
    // arc turned half a segment away from the curve, so a butt cap or a join
    // there, cut square to the first chord, would tilt with it: at a line
    // width of 180 that is several pixels of the stroke missing beside the
    // arc's end (2d.path.arc.selfintersect.2). A sliver of a step at each end
    // makes the first and last chords the tangents, and the stroker's joins
    // fill the rest.
    const double sliver = 1.0 / (1024.0 * static_cast<double>(steps));
    line_to_device(at(from + sweep * sliver));
    for (int i = 1; i < steps; ++i) {
        line_to_device(at(from + sweep * static_cast<double>(i) / static_cast<double>(steps)));
    }
    line_to_device(at(from + sweep * (1.0 - sliver)));
    line_to_device(at(from + sweep));
}

namespace {

/// The sweep between two angles, per HTML 4.12.5.1.14 "ellipse": a whole
/// turn when the difference is a turn or more in the drawing direction,
/// otherwise the part of a turn from one angle to the other in that direction.
///
/// Going the "wrong" way by a whole number of turns is a whole turn, not
/// nothing: arc(x, y, r, 0, 2 * PI, true) is a circle in every engine, and
/// 2d.line.join.round draws the circle that hides its join with exactly that.
[[nodiscard]] double arc_sweep(double from, double to, bool anticlockwise) {
    if (!anticlockwise) {
        if (to - from >= tau) { return tau; }
        if (from > to) { return tau - std::fmod(from - to, tau); }
        return to - from;
    }
    if (from - to >= tau) { return -tau; }
    if (from < to) { return -(tau - std::fmod(to - from, tau)); }
    return to - from;
}

} // namespace

void path_builder::arc(double x, double y, double radius, double from, double to,
                       bool anticlockwise) {
    ellipse(x, y, radius, radius, 0, from, to, anticlockwise);
}

void path_builder::ellipse(double x, double y, double rx, double ry, double rotation, double from,
                           double to, bool anticlockwise) {
    elliptic_arc(x, y, rx, ry, rotation, from, arc_sweep(from, to, anticlockwise));
}

void path_builder::arc_to(double x1, double y1, double x2, double y2, double radius) {
    ensure_subpath(x1, y1);
    const auto last = last_user_point();
    if (!last) { return; }
    const vec p0{last->first, last->second};
    const vec p1{x1, y1};
    const vec p2{x2, y2};
    // The degenerate cases are all a straight line to (x1, y1): a zero
    // radius, a corner that coincides with an end, or three points on a line.
    const vec in = p1 - p0;
    const vec out = p2 - p1;
    if (radius == 0 || length(in) == 0 || length(out) == 0 ||
        std::fabs(cross(in, out)) <= 1e-9 * length(in) * length(out)) {
        line_to(x1, y1);
        return;
    }
    // The circle of `radius` touching both lines: its tangent points sit
    // r / tan(half the corner angle) back along each, and its centre on the
    // bisector r / sin(half the angle) out from the corner.
    const vec back = normalized(p0 - p1);
    const vec ahead = normalized(p2 - p1);
    const double angle = std::acos(std::clamp(dot(back, ahead), -1.0, 1.0));
    const double along = radius / std::tan(angle / 2);
    const vec t0 = p1 + back * along;
    const vec t1 = p1 + ahead * along;
    const vec centre = p1 + normalized(back + ahead) * (radius / std::sin(angle / 2));
    line_to(t0.x, t0.y);
    const double a0 = std::atan2(t0.y - centre.y, t0.x - centre.x);
    double sweep = std::atan2(t1.y - centre.y, t1.x - centre.x) - a0;
    // The short way round, in the direction the corner turns.
    if (sweep > std::numbers::pi) { sweep -= tau; }
    if (sweep < -std::numbers::pi) { sweep += tau; }
    elliptic_arc(centre.x, centre.y, radius, radius, 0, a0, sweep);
}

void path_builder::rect(double x, double y, double w, double h) {
    path_->subpaths.push_back(canvas_path::subpath{
        {device(x, y), device(x + w, y), device(x + w, y + h), device(x, y + h)}, true});
    move_to(x, y);
}

void path_builder::round_rect(double x, double y, double w, double h,
                              std::array<corner_radius, 4> radii) {
    // "Corner curves must not overlap": scale every radius by the one ratio
    // that makes the tightest side fit.
    const double aw = std::fabs(w);
    const double ah = std::fabs(h);
    const double top = radii[0].x + radii[1].x;
    const double right = radii[1].y + radii[2].y;
    const double bottom = radii[2].x + radii[3].x;
    const double left = radii[0].y + radii[3].y;
    double scale = 1;
    for (const auto & [side, room] :
         {std::pair{top, aw}, std::pair{right, ah}, std::pair{bottom, aw}, std::pair{left, ah}}) {
        if (side > 0) { scale = std::min(scale, room / side); }
    }
    if (scale < 1) {
        for (corner_radius & r : radii) {
            r.x *= scale;
            r.y *= scale;
        }
    }
    // Drawn in a frame where the rectangle is (0, 0, |w|, |h|) and then
    // mirrored for a negative width or height - so the first corner is always
    // the one at (x, y), and a mirrored rectangle winds the other way, which
    // is what the spec's note on direction says.
    const double sx = w < 0 ? -1 : 1;
    const double sy = h < 0 ? -1 : 1;
    const auto user = [&](double u, double v) { return vec{x + sx * u, y + sy * v}; };
    // One quarter ellipse per corner, `start` in the unmirrored frame.
    const auto corner = [&](double cu, double cv, corner_radius r, double start) {
        const int steps = arc_segments(std::max(r.x, r.y) * ctm_.scale_bound(), tau / 4);
        for (int i = 0; i <= steps; ++i) {
            const double t = start + (tau / 4) * static_cast<double>(i) / steps;
            const vec p = user(cu + r.x * std::cos(t), cv + r.y * std::sin(t));
            line_to_device(device(p.x, p.y));
        }
    };
    const vec begin = user(radii[0].x, 0);
    move_to(begin.x, begin.y);
    corner(aw - radii[1].x, radii[1].y, radii[1], -tau / 4);
    corner(aw - radii[2].x, ah - radii[2].y, radii[2], 0);
    corner(radii[3].x, ah - radii[3].y, radii[3], tau / 4);
    corner(radii[0].x, radii[0].y, radii[0], tau / 2);
    path_->subpaths.back().closed = true;
    move_to(x, y);
}

// --- gradients ---------------------------------------------------------------

void canvas_gradient::add_stop(double offset, color c) {
    const stop made{offset,
                    {static_cast<float>(c.red()) / 255.0f, static_cast<float>(c.green()) / 255.0f,
                     static_cast<float>(c.blue()) / 255.0f,
                     static_cast<float>(c.alpha()) / 255.0f}};
    const auto at = std::upper_bound(stops.begin(), stops.end(), offset,
                                     [](double o, const stop & s) { return o < s.offset; });
    stops.insert(at, made);
}

std::optional<std::array<float, 4>> canvas_gradient::sample(double x, double y) const {
    double t = 0;
    switch (shape) {
    case kind::linear: {
        const double dx = x1 - x0;
        const double dy = y1 - y0;
        const double len2 = dx * dx + dy * dy;
        if (len2 == 0) { return std::nullopt; } // "must paint nothing"
        t = ((x - x0) * dx + (y - y0) * dy) / len2;
        break;
    }
    case kind::radial: {
        // The largest w for which the circle at w passes through the point
        // and has a radius that is not negative (HTML 4.12.5.1.10 step 4).
        if (x0 == x1 && y0 == y1 && r0 == r1) { return std::nullopt; }
        const double cdx = x1 - x0;
        const double cdy = y1 - y0;
        const double dr = r1 - r0;
        const double pdx = x - x0;
        const double pdy = y - y0;
        const double a = cdx * cdx + cdy * cdy - dr * dr;
        const double b = pdx * cdx + pdy * cdy + r0 * dr;
        const double c = pdx * pdx + pdy * pdy - r0 * r0;
        const auto radius_ok = [&](double w) { return r0 + w * dr >= 0; };
        if (std::fabs(a) < 1e-12) {
            if (b == 0) { return std::nullopt; }
            const double w = c / (2 * b);
            if (!radius_ok(w)) { return std::nullopt; }
            t = w;
            break;
        }
        const double disc = b * b - a * c;
        if (disc < 0) { return std::nullopt; }
        const double root = std::sqrt(disc);
        const double w1 = (b + root) / a;
        const double w2 = (b - root) / a;
        const double hi = std::max(w1, w2);
        const double lo = std::min(w1, w2);
        if (radius_ok(hi)) {
            t = hi;
        } else if (radius_ok(lo)) {
            t = lo;
        } else {
            return std::nullopt;
        }
        break;
    }
    case kind::conic: {
        double turn = (std::atan2(y - y0, x - x0) - angle) / tau;
        turn -= std::floor(turn);
        t = turn;
        break;
    }
    }
    if (stops.empty()) { return std::array<float, 4>{}; } // transparent black
    const auto after = std::upper_bound(stops.begin(), stops.end(), t,
                                        [](double o, const stop & s) { return o < s.offset; });
    if (after == stops.begin()) { return stops.front().rgba; }
    if (after == stops.end()) { return stops.back().rgba; }
    const stop & lo = *(after - 1);
    const stop & hi = *after;
    const auto k = static_cast<float>((t - lo.offset) / (hi.offset - lo.offset));
    std::array<float, 4> out{};
    for (std::size_t i = 0; i < 4; ++i) { out[i] = lo.rgba[i] + (hi.rgba[i] - lo.rgba[i]) * k; }
    return out;
}

// --- the stroker -------------------------------------------------------------
//
// A stroke is an outline, filled. Every segment becomes a rectangle, every
// corner a join, every open end a cap, each built in the user space of the
// transform in force now - that is where the line width is measured - and
// mapped back to the device. Each piece is wound the same way round, so a
// nonzero fill of all of them is exactly their union: an overlap is painted
// once, which is what 2d.path.stroke.overlap's half-transparent green asks.

canvas_path canvas_context::stroke_outline(const canvas_path & path, const rect & cull) const {
    canvas_path out;
    const transform & m = state_.matrix;
    if (!m.invertible()) { return out; }
    const transform inv = m.inverse();
    const double hw = state_.line_width / 2.0;
    const int circle_steps = arc_segments(hw * static_cast<double>(m.scale_bound()), tau);

    const auto emit = [&](std::vector<vec> poly) {
        double area = 0;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            area += cross(poly[i], poly[(i + 1) % poly.size()]);
        }
        if (std::fabs(area) < 1e-12) { return; }
        if (area < 0) { std::ranges::reverse(poly); }
        canvas_path::subpath piece;
        piece.closed = true;
        piece.points.reserve(poly.size());
        for (const vec & p : poly) {
            piece.points.push_back(m.apply(static_cast<float>(p.x), static_cast<float>(p.y)));
        }
        out.subpaths.push_back(std::move(piece));
    };
    const auto circle = [&](vec c) {
        std::vector<vec> poly;
        poly.reserve(static_cast<std::size_t>(circle_steps));
        for (int i = 0; i < circle_steps; ++i) {
            const double t = tau * i / circle_steps;
            poly.push_back(vec{c.x + hw * std::cos(t), c.y + hw * std::sin(t)});
        }
        emit(std::move(poly));
    };
    const auto join = [&](vec prev, vec at, vec next) {
        const vec d0 = normalized(at - prev);
        const vec d1 = normalized(next - at);
        const double turn = cross(d0, d1);
        const double straight = dot(d0, d1);
        if (std::fabs(turn) < 1e-12 && straight > 0) { return; }
        if (state_.join == line_join::round) {
            circle(at);
            return;
        }
        // The outside of the corner is the side the path turns away from.
        const vec n0 = vec{-d0.y, d0.x} * hw;
        const vec n1 = vec{-d1.y, d1.x} * hw;
        const vec o0 = turn > 0 ? at - n0 : at + n0;
        const vec o1 = turn > 0 ? at - n1 : at + n1;
        emit({at, o0, o1});
        if (state_.join != line_join::miter) { return; }
        // The miter's length over half the line width is 1 / cos(turn / 2);
        // past the limit the corner is a bevel.
        const double cos_half = std::sqrt(std::max(0.0, (1 + straight) / 2));
        if (cos_half <= 1e-9 || 1 / cos_half > state_.miter_limit) { return; }
        const vec tip = at + normalized((o0 - at) + (o1 - at)) * (hw / cos_half);
        emit({at, o0, tip, o1});
    };
    const auto cap = [&](vec end, vec outward) {
        if (state_.cap == line_cap::round) {
            circle(end);
        } else if (state_.cap == line_cap::square) {
            const vec n = vec{-outward.y, outward.x} * hw;
            const vec far = end + outward * hw;
            emit({end + n, far + n, far - n, end - n});
        }
    };
    // One side of a segment: the band between a and b and their offsets. The
    // offsets are the vertices' own normals where the path is smooth (below),
    // and on the inside of a turn tighter than the line is wide the two cross
    // - the band then is the two triangles either side of the crossing, which
    // is what sweeping the normal from a to b covers.
    const auto side = [&](vec a, vec b, vec oa, vec ob) {
        const double denom = cross(oa, ob);
        if (std::fabs(denom) > 1e-12) {
            const double u = cross(b - a, ob) / denom;
            const double w = cross(b - a, oa) / denom;
            if (u > 0 && u < 1 && w > 0 && w < 1) {
                const vec x = a + oa * u;
                emit({a, b, x});
                emit({x, b + ob, a + oa});
                return;
            }
        }
        emit({a, b, b + ob, a + oa});
    };
    const auto stroke_polyline = [&](const std::vector<vec> & pts, bool closed) {
        const std::size_t n = pts.size();
        if (n < 2) { return; }
        const std::size_t segments = closed ? n : n - 1;
        std::vector<vec> dir(segments);
        for (std::size_t i = 0; i < segments; ++i) {
            dir[i] = normalized(pts[(i + 1) % n] - pts[i]);
        }
        // A flattened curve is not a polygon. Where the path turns by less
        // than 10 degrees (every vertex of an arc or a Bezier) the stroke is
        // the curve's, swept by the normal at each vertex (the bisector of
        // the two segments'), and needs no join. A rectangle per chord instead
        // is the stroke of the polygon, and on the inside of an arc narrower
        // than the line it fans a chord's width across the centre, where the
        // curve's own stroke is a point (2d.path.arc.selfintersect.1).
        const double smooth_turn = std::cos(10.0 * std::numbers::pi / 180.0);
        std::vector<std::optional<vec>> smooth(n);
        for (std::size_t i = 0; i < n; ++i) {
            if (!closed && (i == 0 || i + 1 == n)) { continue; }
            const vec in = dir[(i + segments - 1) % segments];
            const vec out = dir[i % segments];
            if (dot(in, out) >= smooth_turn) {
                smooth[i] = normalized(vec{-in.y, in.x} + vec{-out.y, out.x});
            }
        }
        for (std::size_t i = 0; i < segments; ++i) {
            const std::size_t j = (i + 1) % n;
            const vec own{-dir[i].y, dir[i].x};
            const vec na = (smooth[i] ? *smooth[i] : own) * hw;
            const vec nb = (smooth[j] ? *smooth[j] : own) * hw;
            side(pts[i], pts[j], na, nb);
            side(pts[i], pts[j], na * -1.0, nb * -1.0);
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (smooth[i] || (!closed && (i == 0 || i + 1 == n))) { continue; }
            join(pts[(i + n - 1) % n], pts[i], pts[(i + 1) % n]);
        }
        if (closed) { return; }
        cap(pts[0], normalized(pts[0] - pts[1]));
        cap(pts[n - 1], normalized(pts[n - 1] - pts[n - 2]));
    };

    double dash_total = 0;
    for (const double d : state_.dash) { dash_total += d; }
    const bool dashed = !state_.dash.empty() && dash_total > 0 && std::isfinite(dash_total);
    const std::size_t entries = state_.dash.size();

    // Where a dash can show: `cull` grown by the farthest any piece of a dash
    // reaches from its centre line - half the width, a square cap's corner or
    // a miter's tip - through the transform's largest stretch (bounded by its
    // Frobenius norm), and a pixel or two for rounding. A stretch of path
    // outside it only moves the pattern on, arithmetically: a one-pixel dash
    // along a line a billion pixels long costs the dashes that land.
    const double stretch =
        std::sqrt(static_cast<double>(m.a) * m.a + static_cast<double>(m.b) * m.b +
                  static_cast<double>(m.c) * m.c + static_cast<double>(m.d) * m.d);
    const double tip =
        std::max(std::numbers::sqrt2, state_.join == line_join::miter ? state_.miter_limit : 1.0);
    const double spread = hw * stretch * tip + 2;
    const double cull_x0 = static_cast<double>(cull.x) - spread;
    const double cull_y0 = static_cast<double>(cull.y) - spread;
    const double cull_x1 = static_cast<double>(cull.right()) + spread;
    const double cull_y1 = static_cast<double>(cull.bottom()) + spread;
    // The part of the user-space segment a -> b inside that area, as a range
    // of the parameter along it: Liang and Barsky's clip of the segment's
    // device image, whose parameters the affine map preserves. Empty when the
    // first is not below the second.
    const auto visible = [&](vec a, vec b) {
        const vec p{m.a * a.x + m.c * a.y + m.e, m.b * a.x + m.d * a.y + m.f};
        const vec q{m.a * b.x + m.c * b.y + m.e, m.b * b.x + m.d * b.y + m.f};
        const vec d = q - p;
        double t0 = 0;
        double t1 = 1;
        // Keep the t where denom * t <= numer.
        const auto bound = [&](double denom, double numer) {
            if (denom == 0) {
                if (numer < 0) { t1 = -1; }
                return;
            }
            if (denom < 0) {
                t0 = std::max(t0, numer / denom);
            } else {
                t1 = std::min(t1, numer / denom);
            }
        };
        bound(-d.x, p.x - cull_x0);
        bound(d.x, cull_x1 - p.x);
        bound(-d.y, p.y - cull_y0);
        bound(d.y, cull_y1 - p.y);
        return std::pair{t0, t1};
    };
    // ponytail: a stroke of more than a million dash intervals draws nothing,
    // as Skia's does (kMaxDashCount) - each interval is an outline of its own,
    // and this bounds the memory. Culling keeps real pages far below it.
    constexpr double most_intervals = 1e6;
    double intervals = 0;

    for (const canvas_path::subpath & sub : path.subpaths) {
        // Back to user space, with zero-length segments pruned: a moveTo and a
        // lineTo to the same point strokes nothing, whatever the cap.
        std::vector<vec> pts;
        pts.reserve(sub.points.size());
        for (const point & p : sub.points) {
            const point u = inv.apply(p.x, p.y);
            const vec q{u.x, u.y};
            if (pts.empty() || length(q - pts.back()) > 1e-6) { pts.push_back(q); }
        }
        bool closed = sub.closed;
        if (closed && pts.size() > 1 && length(pts.front() - pts.back()) <= 1e-6) {
            pts.pop_back();
        }
        if (pts.size() < 2) { continue; }
        if (closed && pts.size() == 2) {
            // Two points closed is there and back: a line with joins, no caps.
            stroke_polyline(pts, true);
            continue;
        }
        if (!dashed) {
            stroke_polyline(pts, closed);
            continue;
        }
        // Dashes: walk the outline with the pattern, starting lineDashOffset
        // into it, and stroke each "on" run as an open polyline of its own.
        if (closed) { pts.push_back(pts.front()); }
        std::vector<std::pair<double, double>> shown(pts.size() - 1);
        double shown_length = 0;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            shown[i] = visible(pts[i], pts[i + 1]);
            if (shown[i].first < shown[i].second) {
                shown_length += length(pts[i + 1] - pts[i]) * (shown[i].second - shown[i].first);
            }
        }
        if (shown_length > 0) {
            intervals += (shown_length / dash_total + 1) * static_cast<double>(entries);
            if (intervals > most_intervals) { return canvas_path{}; }
        }

        std::size_t index = 0;
        double into = std::fmod(state_.dash_offset, dash_total);
        if (into < 0) { into += dash_total; }
        while (into >= state_.dash[index]) {
            into -= state_.dash[index];
            index = (index + 1) % entries;
        }
        double remaining = state_.dash[index] - into;
        bool on = index % 2 == 0;
        std::vector<vec> run;
        const auto next_entry = [&] {
            index = (index + 1) % entries;
            on = index % 2 == 0;
            remaining = state_.dash[index];
        };
        // The run so far ends here, cut where the path leaves the area - so
        // its cap is out of sight too.
        const auto finish = [&] {
            if (on) { stroke_polyline(run, false); }
            run.clear();
        };
        // `len` along the pattern, drawing nothing: whole periods at once,
        // since the list is even and a period ends where it began.
        const auto skip = [&](double len) {
            if (len < remaining) {
                remaining -= len;
                return;
            }
            len = std::fmod(len - remaining, dash_total);
            for (std::size_t step = 0;; ++step) {
                next_entry();
                if (len < remaining || step == entries) { break; }
                len -= remaining;
            }
            remaining = std::max(0.0, remaining - len);
        };
        const auto walk = [&](vec a, vec b) {
            const double seg = length(b - a);
            double done = 0;
            while (seg - done > remaining) {
                done += remaining;
                const vec q = a + (b - a) * (done / seg);
                if (on) {
                    run.push_back(q);
                    stroke_polyline(run, false);
                    run.clear();
                } else {
                    run = {q};
                }
                next_entry();
            }
            remaining -= seg - done;
            if (on) { run.push_back(b); }
        };
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const vec a = pts[i];
            const vec b = pts[i + 1];
            const double seg = length(b - a);
            const auto [t0, t1] = shown[i];
            if (!(t0 < t1)) {
                finish();
                skip(seg);
                continue;
            }
            if (t0 > 0) {
                finish();
                skip(seg * t0);
            }
            const vec entry = t0 > 0 ? a + (b - a) * t0 : a;
            const vec exit = t1 < 1 ? a + (b - a) * t1 : b;
            if (on && run.empty()) { run.push_back(entry); }
            walk(entry, exit);
            if (t1 < 1) {
                finish();
                skip(seg * (1 - t1));
            }
        }
        finish();
    }
    return out;
}

// --- hit testing ------------------------------------------------------------

namespace {

/// Nonzero or even-odd containment of a device point, with a point on an edge
/// (within 1e-4) counting as inside: "points on the path itself must be
/// considered to be inside the path".
[[nodiscard]] bool contains(const canvas_path & path, double x, double y,
                            canvas_context::fill_rule rule) {
    int winding = 0;
    int crossings = 0;
    for (const canvas_path::subpath & s : path.subpaths) {
        const std::size_t n = s.points.size();
        if (n < 2) { continue; }
        for (std::size_t i = 0; i < n; ++i) {
            const vec a{s.points[i].x, s.points[i].y};
            const vec b{s.points[(i + 1) % n].x, s.points[(i + 1) % n].y};
            // On the segment?
            const vec ab = b - a;
            const vec ap = vec{x, y} - a;
            const double len2 = dot(ab, ab);
            const double t = len2 > 0 ? std::clamp(dot(ap, ab) / len2, 0.0, 1.0) : 0.0;
            if (length(ap - ab * t) <= 1e-4) { return true; }
            if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
                const double cx = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
                if (cx > x) {
                    winding += b.y > a.y ? 1 : -1;
                    ++crossings;
                }
            }
        }
    }
    return rule == canvas_context::fill_rule::even_odd ? (crossings % 2) != 0 : winding != 0;
}

} // namespace

bool canvas_context::is_point_in_path(const canvas_path & path, double x, double y,
                                      fill_rule rule) const {
    if (!std::isfinite(x) || !std::isfinite(y)) { return false; }
    return contains(path, x, y, rule);
}

bool canvas_context::is_point_in_stroke(const canvas_path & path, double x, double y) const {
    if (!std::isfinite(x) || !std::isfinite(y)) { return false; }
    // Only the dashes near the point are built: it is the whole area asked about.
    return contains(stroke_outline(path, rect{static_cast<float>(x), static_cast<float>(y), 0, 0}),
                    x, y, fill_rule::nonzero);
}

} // namespace ctbrowser::shell
