//===- ctbrowser/lib/Shell/page/canvas.cpp - Canvas 2D pixel operations ---===//
//
// The pixel half of the canvas 2D context: scanline filling, clipping,
// shadows, text, drawImage, compositing and the per-document canvas store.
// canvas_path.cpp holds the geometry (paths, strokes, dashes). canvas.hpp says
// what each method does; this file says how.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/page/canvas.hpp>

#include <charconv>
#include <limits>

namespace ctbrowser::shell {

namespace {

/// A bitmap of \p width x \p height, or an empty one when that is more than
/// 2^26 pixels (256 MiB of ARGB). `canvas.width = 2147483647` is legal and must
/// read back, but a canvas larger than the cap gets an empty bitmap, as a
/// browser that cannot allocate one does, rather than aborting the process.
[[nodiscard]] std::shared_ptr<bitmap> make_surface(int width, int height) {
    constexpr std::int64_t most_pixels = std::int64_t{1} << 26;
    if (width < 0 || height < 0 || std::int64_t{width} * std::int64_t{height} > most_pixels) {
        return std::make_shared<bitmap>(0, 0);
    }
    return std::make_shared<bitmap>(width, height);
}

} // namespace

void canvas_context::reset_surface(int width, int height) {
    pixels_ = make_surface(width, height);
    // A resize resets the canvas: the pixels are cleared and the drawing state
    // goes back to its defaults, which includes an empty save stack and an
    // identity transform.
    path_.subpaths.clear();
    stack_.clear();
    state_ = state{};
    ++revision_;
}

void canvas_context::reset() {
    if (pixels_) { std::ranges::fill(pixels_->pixels, std::uint32_t{0}); }
    path_.subpaths.clear();
    stack_.clear();
    state_ = state{};
    touch();
}

void canvas_context::save() {
    stack_.push_back(state_);
}

void canvas_context::restore() {
    // An unbalanced restore() does nothing, per spec - a page that restores
    // more than it saved is common, and must not lose its state for it.
    if (stack_.empty()) { return; }
    state_ = std::move(stack_.back());
    stack_.pop_back();
}

bool canvas_context::shadows_drawn() const noexcept {
    return state_.shadow_color.alpha() != 0 &&
           (state_.shadow_blur != 0 || state_.shadow_x != 0 || state_.shadow_y != 0);
}

// --- rectangles ---------------------------------------------------------------

void canvas_context::fill_rect(float x, float y, float w, float h) {
    canvas_path rect;
    path_builder{rect, state_.matrix}.rect(x, y, w, h);
    paint_path(rect, fill_rule::nonzero, state_.fill);
}

void canvas_context::clear_rect(float x, float y, float w, float h) {
    if (!pixels_) { return; }
    // Transparent, not white, and written rather than blended: a cleared
    // canvas shows the page through it, which is what makes an overlay canvas
    // work at all. Through the transform and inside the clip, like any shape.
    canvas_path rect;
    path_builder{rect, state_.matrix}.rect(x, y, w, h);
    for_each_span(rect, fill_rule::nonzero, 0, height() - 1,
                  [&](int row, int x0, int x1) { write_span(row, x0, x1, 0); });
    touch();
}

void canvas_context::stroke_rect(float x, float y, float w, float h) {
    // Always the closed rectangle: one with no height is a line there and
    // back, with joins at its ends and no caps (2d.strokeRect.zero.4), and
    // one with no area at all strokes nothing.
    if (w == 0 && h == 0) { return; }
    canvas_path outline;
    path_builder{outline, state_.matrix}.rect(x, y, w, h);
    stroke(outline);
}

// --- filling --------------------------------------------------------------------

void canvas_context::for_each_span(const canvas_path & path, fill_rule rule, int top, int bottom,
                                   const std::function<void(int, int, int)> & emit) const {
    // An edge table: every edge is listed once with the rows it spans, sorted
    // by where it starts, and each scanline looks only at the edges active on
    // it. A stroke is an outline of a quad per segment and a join per corner,
    // so a stroked circle is a few hundred edges, too many to test on every
    // row.
    struct edge {
        float x0, y0, x1, y1;
        int direction;
    };
    std::vector<edge> edges;
    for (const canvas_path::subpath & s : path.subpaths) {
        const std::size_t n = s.points.size();
        if (n < 2) { continue; }
        for (std::size_t i = 0; i < n; ++i) {
            const point & a = s.points[i];
            // An unclosed subpath is still closed for filling - that is what
            // the spec means by an implicit close - so the wrap edge is
            // included here, unlike in the stroker.
            const point & b = s.points[(i + 1) % n];
            if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) ||
                !std::isfinite(b.y)) {
                return;
            }
            if (a.y == b.y) { continue; }
            edges.push_back(a.y < b.y ? edge{a.x, a.y, b.x, b.y, 1} : edge{b.x, b.y, a.x, a.y, -1});
        }
    }
    if (edges.empty()) { return; }
    std::ranges::sort(edges, {}, &edge::y0);
    const auto clamp_row = [](float v) { return static_cast<int>(std::clamp(v, -1.0e9f, 1.0e9f)); };
    float max_y = -1e30f;
    for (const edge & e : edges) { max_y = std::max(max_y, e.y1); }
    const int first = std::max(top, clamp_row(std::floor(edges.front().y0)));
    const int last = std::min(bottom, clamp_row(std::ceil(max_y)));

    // A crossing carries its direction, which is what the two fill rules
    // disagree about: even-odd fills between alternate crossings, nonzero
    // wherever the running total of +1 down and -1 up is not zero. They differ
    // exactly where a path overlaps itself - a five-pointed star drawn as one
    // path is solid under nonzero and holed under even-odd.
    struct crossing {
        float x;
        int direction;
    };
    std::vector<crossing> crossings;
    std::vector<std::size_t> active;
    std::size_t next = 0;
    // A pixel is inside when its centre is, half-open on the right and the
    // bottom: an edge at x = 10.5 leaves pixel 10 on its left side only, so a
    // one-pixel line at x = 10 is one pixel wide rather than two.
    const auto emit_between = [&](int y, float from, float to) {
        const int x0 = clamp_row(std::ceil(from - 0.5f));
        const int x1 = clamp_row(std::ceil(to - 0.5f)) - 1;
        if (x1 >= x0) { emit(y, x0, x1); }
    };
    for (int y = first; y <= last; ++y) {
        const float scan = static_cast<float>(y) + 0.5f;
        while (next < edges.size() && edges[next].y0 <= scan) { active.push_back(next++); }
        std::erase_if(active, [&](std::size_t i) { return edges[i].y1 <= scan; });
        crossings.clear();
        for (const std::size_t i : active) {
            const edge & e = edges[i];
            if (e.y0 > scan) { continue; }
            // In double: float differences near 1e38 overflow to inf or NaN,
            // which would break the sort's strict weak ordering.
            const double x = static_cast<double>(e.x0) + (static_cast<double>(scan) - e.y0) /
                                                             (static_cast<double>(e.y1) - e.y0) *
                                                             (static_cast<double>(e.x1) - e.x0);
            if (!std::isfinite(x)) { continue; }
            crossings.push_back(
                crossing{static_cast<float>(std::clamp(x, -3e38, 3e38)), e.direction});
        }
        std::ranges::sort(crossings, {}, &crossing::x);
        if (rule == fill_rule::even_odd) {
            for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
                emit_between(y, crossings[i].x, crossings[i + 1].x);
            }
            continue;
        }
        int winding = 0;
        for (std::size_t i = 0; i + 1 < crossings.size(); ++i) {
            winding += crossings[i].direction;
            if (winding != 0) { emit_between(y, crossings[i].x, crossings[i + 1].x); }
        }
    }
}

void canvas_context::sample_row(const canvas_paint & paint, int y, int x0, int x1,
                                std::vector<std::uint32_t> & out) const {
    out.assign(static_cast<std::size_t>(x1 - x0 + 1), 0u);
    const auto byte = [](float v) {
        return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    if (paint.gradient) {
        // Gradients are in the user space of the transform in force when the
        // shape is drawn, so every pixel centre goes back through it.
        if (!state_.matrix.invertible()) { return; }
        const transform inv = state_.matrix.inverse();
        for (int x = x0; x <= x1; ++x) {
            const point u = inv.apply(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            if (const auto c = paint.gradient->sample(u.x, u.y)) {
                out[static_cast<std::size_t>(x - x0)] =
                    color::rgba(byte((*c)[0]), byte((*c)[1]), byte((*c)[2]), byte((*c)[3])).argb;
            }
        }
        return;
    }
    if (!paint.pattern || !paint.pattern->image || paint.pattern->image->empty()) { return; }
    const canvas_pattern & pattern = *paint.pattern;
    const transform to_device = pattern.matrix.then(state_.matrix);
    if (!to_device.invertible()) { return; }
    const transform inv = to_device.inverse();
    const bitmap & image = *pattern.image;
    // The texels a sample may read: inside the source rectangle for a
    // drawImage, wrapped on a repeating axis, and clamped to the image
    // otherwise (whether a sample is inside a non-repeating one at all is
    // decided before the texels are read).
    int lo_x = 0;
    int lo_y = 0;
    int hi_x = image.width - 1;
    int hi_y = image.height - 1;
    if (pattern.source_rect) {
        const auto & r = *pattern.source_rect;
        lo_x = std::clamp(static_cast<int>(std::floor(r[0])), 0, hi_x);
        lo_y = std::clamp(static_cast<int>(std::floor(r[1])), 0, hi_y);
        hi_x = std::clamp(static_cast<int>(std::ceil(r[2])) - 1, lo_x, hi_x);
        hi_y = std::clamp(static_cast<int>(std::ceil(r[3])) - 1, lo_y, hi_y);
    }
    const bool wrap_x = pattern.repeat_x && !pattern.source_rect;
    const bool wrap_y = pattern.repeat_y && !pattern.source_rect;
    const auto texel = [&](int tx, int ty) {
        tx = wrap_x ? ((tx % image.width) + image.width) % image.width : std::clamp(tx, lo_x, hi_x);
        ty = wrap_y ? ((ty % image.height) + image.height) % image.height
                    : std::clamp(ty, lo_y, hi_y);
        return image.at(tx, ty);
    };
    // Image smoothing (HTML 4.12.5.1.16): with imageSmoothingEnabled, bilinear
    // between the four texels round the sample - in premultiplied colour, so a
    // transparent neighbour does not darken an edge. A draw that maps texels
    // one to one onto pixels reads each texel's centre, where the two filters
    // agree, so it takes the nearest texel and the cost of one.
    const bool one_to_one = inv.a == 1 && inv.d == 1 && inv.b == 0 && inv.c == 0 &&
                            inv.e == std::floor(inv.e) && inv.f == std::floor(inv.f);
    const bool smooth = state_.image_smoothing && !one_to_one;
    const auto width_f = static_cast<float>(image.width);
    const auto height_f = static_cast<float>(image.height);
    for (int x = x0; x <= x1; ++x) {
        const point p = inv.apply(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
        float u = p.x;
        float v = p.y;
        if (pattern.source_rect) {
            const auto & r = *pattern.source_rect;
            u = std::clamp(u, r[0], std::nextafter(r[2], r[0]));
            v = std::clamp(v, r[1], std::nextafter(r[3], r[1]));
        }
        if ((!wrap_x && (u < 0 || u >= width_f)) || (!wrap_y && (v < 0 || v >= height_f))) {
            continue;
        }
        if (!smooth) {
            out[static_cast<std::size_t>(x - x0)] =
                texel(static_cast<int>(std::floor(u)), static_cast<int>(std::floor(v)));
            continue;
        }
        const float fx = u - 0.5f;
        const float fy = v - 0.5f;
        const int ix = static_cast<int>(std::floor(fx));
        const int iy = static_cast<int>(std::floor(fy));
        const float wx = fx - static_cast<float>(ix);
        const float wy = fy - static_cast<float>(iy);
        std::array<float, 4> sum{}; // premultiplied r, g, b and a
        const auto add = [&](int tx, int ty, float weight) {
            const color c{texel(tx, ty)};
            const float a = static_cast<float>(c.alpha()) * weight;
            sum[0] += static_cast<float>(c.red()) * a;
            sum[1] += static_cast<float>(c.green()) * a;
            sum[2] += static_cast<float>(c.blue()) * a;
            sum[3] += a;
        };
        add(ix, iy, (1 - wx) * (1 - wy));
        add(ix + 1, iy, wx * (1 - wy));
        add(ix, iy + 1, (1 - wx) * wy);
        add(ix + 1, iy + 1, wx * wy);
        if (sum[3] <= 0) { continue; }
        const auto channel = [&](float premultiplied) {
            return static_cast<std::uint8_t>(std::clamp(premultiplied / sum[3], 0.0f, 255.0f) +
                                             0.5f);
        };
        out[static_cast<std::size_t>(x - x0)] =
            color::rgba(channel(sum[0]), channel(sum[1]), channel(sum[2]),
                        static_cast<std::uint8_t>(std::clamp(sum[3], 0.0f, 255.0f) + 0.5f))
                .argb;
    }
}

void canvas_context::paint_path(const canvas_path & path, fill_rule rule,
                                const canvas_paint & paint) {
    if (!pixels_ || path.empty()) { return; }
    if (shadows_drawn()) { draw_shadow(path, rule, paint); }
    const pass composited{*this};
    if (paint.is_solid()) {
        for_each_span(path, rule, 0, height() - 1,
                      [&](int y, int x0, int x1) { blend_span(y, x0, x1, paint.solid); });
    } else {
        std::vector<std::uint32_t> row;
        for_each_span(path, rule, 0, height() - 1, [&](int y, int x0, int x1) {
            x0 = std::max(0, x0);
            x1 = std::min(width() - 1, x1);
            if (x1 < x0) { return; }
            sample_row(paint, y, x0, x1, row);
            blend_row(y, x0, x1, row.data());
        });
    }
    touch();
}

void canvas_context::fill(const canvas_path & path, fill_rule rule) {
    paint_path(path, rule, state_.fill);
}

void canvas_context::stroke(const canvas_path & path) {
    // The dashes worth building are the ones that can land - on the canvas, or
    // off it by no more than a shadow's offset and blur can bring back.
    rect cull{0, 0, static_cast<float>(width()), static_cast<float>(height())};
    if (shadows_drawn()) {
        const auto sx = static_cast<float>(state_.shadow_x);
        const auto sy = static_cast<float>(state_.shadow_y);
        const float blur = 1025; // draw_shadow's 1024 px reach cap, plus a pixel
        cull = rect{std::min(0.0f, -sx) - blur, std::min(0.0f, -sy) - blur,
                    cull.width + std::fabs(sx) + 2 * blur, cull.height + std::fabs(sy) + 2 * blur};
    }
    paint_path(stroke_outline(path, cull), fill_rule::nonzero, state_.stroke);
}

void canvas_context::draw_focus_ring(const canvas_path & path) {
    state saved = state_;
    state_ = state{};
    state_.clip = saved.clip;
    state_.clip_width = saved.clip_width;
    state_.clip_height = saved.clip_height;
    state_.line_width = 2;
    state_.stroke.solid = color::rgba(0x00, 0x5f, 0xcc);
    stroke(path);
    state_ = std::move(saved);
}

/// Intersects the clip with \p path; never replaces it. Two clips in a row
/// leave the region they have in common, which is what makes nesting them
/// work, and is why the old mask is read while the new one is built.
void canvas_context::clip(const canvas_path & path, fill_rule rule) {
    if (!pixels_ || width() <= 0 || height() <= 0) { return; }
    auto mask = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<std::size_t>(width()) * static_cast<std::size_t>(height()), 0);
    // Hoisted out of the per-span lambda, which is hot (Phaser clips every
    // frame): with no previous clip a span is a fill of 1s; with one it copies
    // the previous mask's bits, which intersects the two.
    const int w = width();
    std::uint8_t * into = mask->data();
    const std::uint8_t * previous = state_.clip ? state_.clip->data() : nullptr;
    // A stale mask clips out everything, and so does anything intersected with
    // it: the new mask stays all zero.
    if (!clip_stale()) {
        for_each_span(path, rule, 0, height() - 1, [&](int y, int x0, int x1) {
            const int left = std::max(0, x0);
            const int right = std::min(w - 1, x1);
            if (right < left) { return; }
            const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
            const auto span = static_cast<std::size_t>(right - left) + 1U;
            std::uint8_t * out = into + row + static_cast<std::size_t>(left);
            if (previous == nullptr) {
                std::fill_n(out, span, std::uint8_t{1});
                return;
            }
            const std::uint8_t * was = previous + row + static_cast<std::size_t>(left);
            for (std::size_t i = 0; i < span; ++i) {
                out[i] = static_cast<std::uint8_t>(was[i] != 0);
            }
        });
    }
    state_.clip = std::move(mask);
    state_.clip_width = w;
    state_.clip_height = height();
}

// --- shadows --------------------------------------------------------------------

void canvas_context::draw_shadow(const canvas_path & path, fill_rule rule,
                                 const canvas_paint & paint) {
    // Offsets and blur are in CSS pixels, not through the transform. Only the
    // shape's own box, moved by the offset and widened by the blur, can be
    // shaded - so that box on the canvas is all that is rasterised, blurred
    // and composited, and a glow round a small shape costs the shape, not the
    // canvas. The shadow of a shape off the canvas can still land on it.
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = min_x;
    double max_x = -min_x;
    double max_y = -min_x;
    for (const canvas_path::subpath & s : path.subpaths) {
        for (const point & p : s.points) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) { return; } // fills nothing
            min_x = std::min(min_x, static_cast<double>(p.x));
            min_y = std::min(min_y, static_cast<double>(p.y));
            max_x = std::max(max_x, static_cast<double>(p.x));
            max_y = std::max(max_y, static_cast<double>(p.y));
        }
    }
    if (min_x > max_x) { return; }
    // Rasterising clamps rows to a billion pixels either way, so a shadow
    // thrown further than that cannot come back to the canvas - and the int
    // arithmetic below stays in range.
    const double shift_x = std::round(state_.shadow_x);
    const double shift_y = std::round(state_.shadow_y);
    if (std::fabs(shift_x) > 1e9 || std::fabs(shift_y) > 1e9) { return; }
    const int ox = static_cast<int>(shift_x);
    const int oy = static_cast<int>(shift_y);
    // A Gaussian of standard deviation shadowBlur / 2, as three box blurs each
    // way - within a few percent of the true curve, and a constant cost per
    // pixel whatever the blur, where a direct kernel is quadratic in it. The
    // box widths are the ones whose three-fold convolution has the Gaussian's
    // variance (Kovesi, "Fast almost-Gaussian filtering"). A blur past a
    // million pixels is transparent everywhere, and bounded so the widths fit.
    const double sigma = std::min(state_.shadow_blur / 2.0, 1.0e6);
    std::array<int, 3> radius{};
    if (sigma > 0) {
        const double ideal = std::sqrt(12.0 * sigma * sigma / 3.0 + 1.0);
        int lower = static_cast<int>(std::floor(ideal));
        if (lower % 2 == 0) { --lower; }
        const double m = (12.0 * sigma * sigma - 3.0 * lower * lower - 12.0 * lower - 9.0) /
                         (-4.0 * lower - 4.0);
        const auto smaller = std::lround(m);
        for (int i = 0; i < 3; ++i) {
            radius[static_cast<std::size_t>(i)] = ((i < smaller ? lower : lower + 2) - 1) / 2;
        }
    }
    // How far the blur carries a pixel: the three boxes end to end.
    // ponytail: past 1024 pixels a shadow is spread too thin to matter, and
    // the buffer stops growing with it.
    const int reach = std::min(radius[0] + radius[1] + radius[2], 1024);

    // The canvas pixels the shadow can shade, and the buffer round them: every
    // shape pixel the blur carries onto one of them.
    const double x0 = std::max(0.0, std::floor(min_x) + ox - reach - 1);
    const double y0 = std::max(0.0, std::floor(min_y) + oy - reach - 1);
    const double x1 = std::min(width() - 1.0, std::ceil(max_x) + ox + reach + 1);
    const double y1 = std::min(height() - 1.0, std::ceil(max_y) + oy + reach + 1);
    if (x0 > x1 || y0 > y1) { return; }
    const int shade_left = static_cast<int>(x0);
    const int shade_top = static_cast<int>(y0);
    const int shade_right = static_cast<int>(x1);
    const int shade_bottom = static_cast<int>(y1);
    const int left = shade_left - reach;
    const int top = shade_top - reach;
    const int w = shade_right - shade_left + 1 + 2 * reach;
    const int h = shade_bottom - shade_top + 1 + 2 * reach;
    std::vector<float> alpha(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0f);
    std::vector<std::uint32_t> row;
    // Shape pixel (x, y) shades canvas pixel (x + ox, y + oy).
    for_each_span(path, rule, top - oy, top - oy + h - 1, [&](int y, int x0s, int x1s) {
        x0s = std::max(left - ox, x0s);
        x1s = std::min(left - ox + w - 1, x1s);
        if (x1s < x0s) { return; }
        float * out =
            alpha.data() + static_cast<std::size_t>(y + oy - top) * static_cast<std::size_t>(w);
        const int at = ox - left;
        if (paint.is_solid()) {
            const float a = static_cast<float>(paint.solid.alpha()) / 255.0f;
            for (int x = x0s; x <= x1s; ++x) { out[x + at] = a; }
            return;
        }
        sample_row(paint, y, x0s, x1s, row);
        for (int x = x0s; x <= x1s; ++x) {
            out[x + at] = static_cast<float>(row[static_cast<std::size_t>(x - x0s)] >> 24) / 255.0f;
        }
    });
    if (sigma > 0) {
        std::vector<float> scratch(alpha.size(), 0.0f);
        const auto box = [&](std::vector<float> & from, std::vector<float> & to, int r,
                             bool across) {
            const int lines = across ? h : w;
            const int length = across ? w : h;
            const auto index = [&](int line, int i) {
                return across ? static_cast<std::size_t>(line) * static_cast<std::size_t>(w) +
                                    static_cast<std::size_t>(i)
                              : static_cast<std::size_t>(i) * static_cast<std::size_t>(w) +
                                    static_cast<std::size_t>(line);
            };
            const float scale = 1.0f / static_cast<float>(2 * r + 1);
            for (int line = 0; line < lines; ++line) {
                float sum = 0;
                for (int i = 0; i <= std::min(r, length - 1); ++i) { sum += from[index(line, i)]; }
                for (int i = 0; i < length; ++i) {
                    to[index(line, i)] = sum * scale;
                    if (i + r + 1 < length) { sum += from[index(line, i + r + 1)]; }
                    if (i - r >= 0) { sum -= from[index(line, i - r)]; }
                }
            }
        };
        for (const int r : radius) {
            box(alpha, scratch, r, true);
            box(scratch, alpha, r, false);
        }
    }
    const pass composited{*this};
    const color shade = state_.shadow_color;
    for (int y = shade_top; y <= shade_bottom; ++y) {
        const std::size_t source_row =
            static_cast<std::size_t>(y - top) * static_cast<std::size_t>(w);
        for (int x = shade_left; x <= shade_right; ++x) {
            const float a = alpha[source_row + static_cast<std::size_t>(x - left)];
            if (a <= 0 || clipped_out(x, y)) { continue; }
            const auto shadow_alpha = static_cast<std::uint8_t>(
                std::clamp(static_cast<float>(shade.alpha()) * a, 0.0f, 255.0f) + 0.5f);
            const color source =
                with_alpha(color::rgba(shade.red(), shade.green(), shade.blue(), shadow_alpha));
            pixels_->put(x, y, composite_one(x, y, pixels_->at(x, y), source));
        }
    }
}

// --- text --------------------------------------------------------------------------

namespace {

/// letterSpacing or wordSpacing - a CSS length the setter already checked - in
/// pixels at the current font size. A font-relative unit follows the current
/// font, so `1em` doubles when the font does (2d.text.drawing.style.
/// letterSpacing.change.font). ponytail: ex, ch and cap are fixed fractions
/// of the em rather than the face's own metrics, and the viewport units are 0.
[[nodiscard]] double spacing_px(std::string_view css, double font_size) {
    double amount = 0;
    const auto [end, ec] = std::from_chars(css.data(), css.data() + css.size(), amount);
    if (ec != std::errc{} || amount == 0) { return 0; }
    const std::string_view unit = css.substr(static_cast<std::size_t>(end - css.data()));
    struct factor {
        std::string_view unit;
        double em;
        double px;
    };
    static constexpr factor table[] = {
        {"px", 0, 1},     {"em", 1, 0},         {"rem", 0, 16},        {"ex", 0.5, 0},
        {"rex", 0, 8},    {"ch", 0.5, 0},       {"rch", 0, 8},         {"ic", 1, 0},
        {"ric", 0, 16},   {"cap", 0.7, 0},      {"rcap", 0, 11.2},     {"lh", 1.2, 0},
        {"rlh", 0, 19.2}, {"cm", 0, 96 / 2.54}, {"mm", 0, 9.6 / 2.54}, {"q", 0, 2.4 / 2.54},
        {"in", 0, 96},    {"pt", 0, 96.0 / 72}, {"pc", 0, 16}};
    for (const factor & f : table) {
        if (f.unit == unit) { return amount * (f.em * font_size + f.px); }
    }
    return 0;
}

/// The byte length of the UTF-8 sequence starting at \p at, clamped to the
/// text's end.
[[nodiscard]] std::size_t code_point_length(std::string_view text, std::size_t at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    const std::size_t n = lead < 0x80    ? 1
                          : lead >= 0xF0 ? 4
                          : lead >= 0xE0 ? 3
                          : lead >= 0xC0 ? 2
                                         : 1;
    return std::min(n, text.size() - at);
}

} // namespace

float canvas_context::measure_text(std::string_view text) const {
    const float advance = fonts().advance(text, state_.font_size, state_.font_family,
                                          state_.font_bold, state_.font_italic);
    // letterSpacing after every character and wordSpacing after every space
    // (HTML 4.12.5.1.4, CSS Text 3 §8).
    const double letters = spacing_px(state_.letter_spacing, state_.font_size);
    const double words = spacing_px(state_.word_spacing, state_.font_size);
    if (letters == 0 && words == 0) { return advance; }
    double extra = 0;
    for (std::size_t at = 0; at < text.size(); at += code_point_length(text, at)) {
        extra += letters + (text[at] == ' ' ? words : 0);
    }
    return advance + static_cast<float>(extra);
}

void canvas_context::fill_text(std::string_view text, float x, float y) {
    draw_text(text, x, y, false);
}

void canvas_context::stroke_text(std::string_view text, float x, float y) {
    draw_text(text, x, y, true);
}

void canvas_context::draw_text(std::string_view text_in, float x, float y, bool stroke) {
    // "Replace all ASCII whitespace in text with U+0020 SPACE characters"
    // (HTML 4.12.5.1.5, the text preparation algorithm) - a tab or a newline is a
    // space here, never a line break.
    std::string text{text_in};
    std::ranges::replace_if(
        text, [](char c) { return c == '\t' || c == '\n' || c == '\f' || c == '\r'; }, ' ');
    if (!pixels_ || text.empty()) { return; }
    // With spacing, the run is set a character at a time, each at the pen
    // position the spacing moved it to, because the backend draws a run as one.
    const double letters = spacing_px(state_.letter_spacing, state_.font_size);
    const double words = spacing_px(state_.word_spacing, state_.font_size);
    if (letters != 0 || words != 0) {
        const float total = measure_text(text);
        const bool rtl = state_.direction == "rtl";
        const std::string & align = state_.text_align;
        float pen = x;
        if (align == "center") {
            pen -= total / 2;
        } else if (align == "right" || (align == "end" && !rtl) || (align == "start" && rtl)) {
            pen -= total;
        }
        const state saved = state_;
        state_.letter_spacing = "0px";
        state_.word_spacing = "0px";
        state_.text_align = "left";
        for (std::size_t at = 0; at < text.size();) {
            const std::size_t n = code_point_length(text, at);
            const std::string glyph = text.substr(at, n);
            draw_text(glyph, pen, y, stroke);
            pen += measure_text(glyph) + static_cast<float>(letters + (glyph == " " ? words : 0));
            at += n;
        }
        state_ = saved;
        return;
    }
    const raster::font_backend & backend = fonts();
    const float width = measure_text(text);
    const float ascent =
        backend.ascent(state_.font_size, state_.font_family, state_.font_bold, state_.font_italic);
    const float descent =
        backend.descent(state_.font_size, state_.font_family, state_.font_bold, state_.font_italic);
    if (width <= 0 || ascent + descent <= 0) { return; }

    // `draw_run` writes into a raster::surface and a canvas owns a
    // paint::bitmap. They have the same pixel layout but surface owns its
    // storage, so the run is drawn into a scratch surface the size of its own
    // bounding box and copied back.
    //
    // The scratch is seeded from the destination first. Against a blank one,
    // blend_over would composite an antialiased glyph edge onto transparent
    // black and that premultiplied result would then be blended into the
    // canvas a second time - a dark halo around every glyph.
    //
    // textAlign shifts along x by a share of the run's width, and
    // textBaseline shifts along y between the font's own metrics. Both are
    // applied in user space, before the transform: a rotated centred label must
    // be centred on its own baseline, not on the screen's x axis. `start` and
    // `end` follow the direction, and `rtl` is the only one that flips them.
    const bool rtl = state_.direction == "rtl";
    const std::string & align = state_.text_align;
    float shift_x = 0;
    if (align == "center") {
        shift_x = -width / 2;
    } else if (align == "right" || (align == "end" && !rtl) || (align == "start" && rtl)) {
        shift_x = -width;
    }
    const std::string & baseline = state_.text_baseline;
    float shift_y = 0;
    if (baseline == "top" || baseline == "hanging") {
        shift_y = ascent;
    } else if (baseline == "middle") {
        shift_y = (ascent - descent) / 2;
    } else if (baseline == "bottom" || baseline == "ideographic") {
        shift_y = -descent;
    }
    const point origin = state_.matrix.apply(x + shift_x, y + shift_y); // the baseline
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y)) { return; }
    // The run's box: a pixel of margin each side for antialiasing and any
    // overhang, an integral left/top so the fraction stays in `where` and
    // sub-pixel positioning survives the round trip, and for a stroke the
    // half line width it reaches past the glyphs' edges. In double: a page
    // may put text a long way off the canvas.
    const double reach = stroke ? state_.line_width / 2 * state_.matrix.scale_bound() : 0.0;
    const int pad = stroke ? static_cast<int>(std::ceil(std::min(reach, 256.0))) + 1 : 0;
    const double box_left = std::floor(static_cast<double>(origin.x)) - 1 - pad;
    const double box_top = std::floor(static_cast<double>(origin.y - ascent)) - 1 - pad;
    const double box_right = box_left + std::ceil(width) + 3 + 2 * pad;
    const double box_bottom = box_top + std::ceil(ascent + descent) + 3 + 2 * pad;
    // Only the part that can land is rasterised: the box clipped to the canvas
    // - grown by the pad for a stroke, whose band at the edge depends on the
    // glyphs that far beyond it. A run drawn mostly off the edge costs only
    // the part that lands, and an entirely off-canvas run costs nothing.
    if (box_right <= 0 || box_bottom <= 0 || box_left >= pixels_->width ||
        box_top >= pixels_->height) {
        return;
    }
    const int left = static_cast<int>(std::max(box_left, -static_cast<double>(pad)));
    const int top = static_cast<int>(std::max(box_top, -static_cast<double>(pad)));
    const int w =
        static_cast<int>(std::min(box_right, static_cast<double>(pixels_->width + pad))) - left;
    const int h =
        static_cast<int>(std::min(box_bottom, static_cast<double>(pixels_->height + pad))) - top;

    // The glyphs as a coverage mask, painted through the drawing model like
    // any shape, for everything the direct path below cannot do: an outline,
    // a gradient or pattern, an operator other than source-over.
    if (stroke || !state_.fill.is_solid() || state_.composite_mode != composite::source_over) {
        paint_glyphs(text, origin, ascent, width, reach, pad, left, top, w, h, stroke);
        return;
    }
    // With no pad the box is all on the canvas.
    raster::surface scratch{w, h};
    for (int sy = 0; sy < h; ++sy) {
        const std::span<std::uint32_t> row = scratch.row(sy);
        for (int sx = 0; sx < w; ++sx) {
            row[static_cast<std::size_t>(sx)] = pixels_->at(left + sx, top + sy);
        }
    }

    // `where.y` is the top of the run box, not the baseline: both backends add
    // their own ascent. Passing the baseline through is the failure mode here,
    // and it puts every glyph one ascent too low.
    const rect where{origin.x - static_cast<float>(left),
                     (origin.y - ascent) - static_cast<float>(top), width, ascent + descent};

    ctbrowser::paint::paint_command run;
    run.op = ctbrowser::paint::paint_op::text_run;
    run.bounds = where;
    run.text = std::string{text};
    run.font_size = state_.font_size;
    run.face =
        ctbrowser::paint::font_face{state_.font_family, state_.font_bold, state_.font_italic};
    run.fill = with_alpha(state_.fill.solid); // globalAlpha, as every other verb honours it

    backend.draw_run(where, run, raster::pixel_rect{0, 0, w, h}, scratch);

    for (int sy = 0; sy < h; ++sy) {
        const std::span<const std::uint32_t> row = scratch.row(sy);
        for (int sx = 0; sx < w; ++sx) {
            // Text is clipped like everything else - a clip that let glyphs
            // through would be visible on the first label drawn near an edge.
            if (clipped_out(left + sx, top + sy)) { continue; }
            pixels_->put(left + sx, top + sy, row[static_cast<std::size_t>(sx)]);
        }
    }
    touch();
}

namespace {

/// The initial value of a cell that is not a distance-transform source.
constexpr double far_away = 1e12;
/// Replaces every cell of the \p w x \p h \p grid with the squared distance to
/// the nearest cell holding 0 (other cells hold `far_away`). This is
/// Felzenszwalb and Huttenlocher's exact Euclidean distance transform
/// ("Distance Transforms of Sampled Functions"): the lower envelope of a
/// parabola per cell, down every column and then along every row. O(w * h),
/// however far the distances run.
void squared_distances(std::vector<double> & grid, std::size_t w, std::size_t h) {
    const std::size_t longest = std::max(w, h);
    std::vector<double> f(longest);
    std::vector<double> z(longest + 1);
    std::vector<std::size_t> v(longest);
    constexpr double infinity = std::numeric_limits<double>::infinity();
    const auto line = [&](std::size_t start, std::size_t stride, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) { f[i] = grid[start + i * stride]; }
        // Where the parabolas rooted at q and p cross.
        const auto meet = [&](std::size_t q, std::size_t p) {
            const auto dq = static_cast<double>(q);
            const auto dp = static_cast<double>(p);
            return ((f[q] + dq * dq) - (f[p] + dp * dp)) / (2 * dq - 2 * dp);
        };
        std::size_t k = 0;
        v[0] = 0;
        z[0] = -infinity;
        z[1] = infinity;
        for (std::size_t q = 1; q < n; ++q) {
            double s = meet(q, v[k]);
            while (s <= z[k]) { s = meet(q, v[--k]); }
            ++k;
            v[k] = q;
            z[k] = s;
            z[k + 1] = infinity;
        }
        k = 0;
        for (std::size_t q = 0; q < n; ++q) {
            while (z[k + 1] < static_cast<double>(q)) { ++k; }
            const double d = static_cast<double>(q) - static_cast<double>(v[k]);
            grid[start + q * stride] = d * d + f[v[k]];
        }
    };
    for (std::size_t x = 0; x < w; ++x) { line(x, w, h); }
    for (std::size_t y = 0; y < h; ++y) { line(y * w, 1, w); }
}

/// The stroke of a coverage mask: 1 for the cells within reach of the glyphs'
/// edge - an inside one (coverage at least half) with an outside one no further
/// than \p in_reach, or an outside one with an inside one no further than
/// \p out_reach. Beyond the mask counts as outside.
[[nodiscard]] std::vector<std::uint8_t> stroke_band(const raster::surface & mask, int in_reach,
                                                    int out_reach) {
    const auto w = static_cast<std::size_t>(mask.width());
    const auto h = static_cast<std::size_t>(mask.height());
    // One cell of outside all round.
    const std::size_t gw = w + 2;
    const std::size_t gh = h + 2;
    std::vector<std::uint8_t> inside(gw * gh, 0);
    for (std::size_t y = 0; y < h; ++y) {
        const std::span<const std::uint32_t> row = mask.row(static_cast<int>(y));
        for (std::size_t x = 0; x < w; ++x) {
            inside[(y + 1) * gw + x + 1] = static_cast<std::uint8_t>((row[x] >> 24) >= 128);
        }
    }
    std::vector<std::uint8_t> band(w * h, 0);
    std::vector<double> grid(gw * gh);
    for (const bool to_inside : {false, true}) {
        const int reach = to_inside ? out_reach : in_reach;
        if (reach <= 0) { continue; }
        for (std::size_t i = 0; i < grid.size(); ++i) {
            grid[i] = (inside[i] != 0) == to_inside ? 0.0 : far_away;
        }
        squared_distances(grid, gw, gh);
        const double limit = static_cast<double>(reach) * static_cast<double>(reach);
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                const std::size_t at = (y + 1) * gw + x + 1;
                if ((inside[at] != 0) != to_inside && grid[at] <= limit) { band[y * w + x] = 1; }
            }
        }
    }
    return band;
}

} // namespace

/// Text through the mask. The font backend gives glyph coverage, not outlines,
/// so a fill is that coverage and a stroke is traced on it: a pixel is on the
/// stroke when the glyph's edge passes within half the line width of it - an
/// inside pixel with an outside one that near, or the reverse. ponytail: a
/// mask-traced outline is a pixel coarse at the corners; glyph outlines from
/// the backend would make strokeText exact.
void canvas_context::paint_glyphs(const std::string & text, point origin, float ascent,
                                  float advance, double reach, int pad, int left, int top, int w,
                                  int h, bool stroke) {
    const raster::font_backend & backend = fonts();
    raster::surface scratch{w, h};
    ctbrowser::paint::paint_command run;
    run.op = ctbrowser::paint::paint_op::text_run;
    run.bounds = rect{origin.x - static_cast<float>(left),
                      (origin.y - ascent) - static_cast<float>(top), advance,
                      ascent + backend.descent(state_.font_size, state_.font_family,
                                               state_.font_bold, state_.font_italic)};
    run.text = text;
    run.font_size = state_.font_size;
    run.face =
        ctbrowser::paint::font_face{state_.font_family, state_.font_bold, state_.font_italic};
    run.fill = color::rgba(255, 255, 255);
    backend.draw_run(run.bounds, run, raster::pixel_rect{0, 0, w, h}, scratch);
    // The inner half of the band reaches ceil(reach) in, the outer half
    // floor(reach) out, so a one-pixel line is one pixel and not two - and
    // neither past the pad, the most of the glyphs the scratch holds beyond
    // the canvas.
    std::vector<std::uint8_t> band;
    if (stroke) {
        band = stroke_band(scratch, std::min(pad, std::max(1, static_cast<int>(std::ceil(reach)))),
                           std::min(pad, static_cast<int>(std::floor(reach))));
    }
    const canvas_paint & paint = stroke ? state_.stroke : state_.fill;
    const pass composited{*this};
    std::vector<std::uint32_t> colors;
    for (int sy = 0; sy < h; ++sy) {
        const int y = top + sy;
        if (y < 0 || y >= height()) { continue; }
        for (int sx = 0; sx < w; ++sx) {
            const int x = left + sx;
            if (x < 0 || x >= width() || clipped_out(x, y)) { continue; }
            const auto at = static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) +
                            static_cast<std::size_t>(sx);
            const std::uint32_t cover = stroke
                                            ? (band[at] != 0 ? 255u : 0u)
                                            : scratch.row(sy)[static_cast<std::size_t>(sx)] >> 24;
            if (cover == 0) { continue; }
            color source = paint.solid;
            if (!paint.is_solid()) {
                sample_row(paint, y, x, x, colors);
                source = color{colors[0]};
            }
            const auto a = static_cast<std::uint8_t>((source.alpha() * cover + 127) / 255);
            source = color::rgba(source.red(), source.green(), source.blue(), a);
            pixels_->put(x, y, composite_one(x, y, pixels_->at(x, y), with_alpha(source)));
        }
    }
    touch();
}

// --- images ---------------------------------------------------------------------

void canvas_context::draw_image(const std::shared_ptr<const bitmap> & source, double sx, double sy,
                                double sw, double sh, double dx, double dy, double dw, double dh) {
    if (!pixels_ || !source || source->empty()) { return; }
    // Negative sizes select the same rectangle from its other corner.
    const auto flip = [](double & at, double & size) {
        if (size < 0) {
            at += size;
            size = -size;
        }
    };
    flip(sx, sw);
    flip(sy, sh);
    flip(dx, dw);
    flip(dy, dh);
    if (sw == 0 || sh == 0 || dw == 0 || dh == 0) { return; }
    // "When the source rectangle is outside the source image, the source
    // rectangle must be clipped to the source image and the destination
    // rectangle must be clipped in the same proportion."
    const double scale_x = dw / sw;
    const double scale_y = dh / sh;
    const double cx0 = std::max(sx, 0.0);
    const double cy0 = std::max(sy, 0.0);
    const double cx1 = std::min(sx + sw, static_cast<double>(source->width));
    const double cy1 = std::min(sy + sh, static_cast<double>(source->height));
    if (cx1 <= cx0 || cy1 <= cy0) { return; }
    dx += (cx0 - sx) * scale_x;
    dy += (cy0 - sy) * scale_y;
    dw = (cx1 - cx0) * scale_x;
    dh = (cy1 - cy0) * scale_y;

    // Painted as a pattern over the destination rectangle: the source
    // rectangle mapped onto it, so the transform, the clip, globalAlpha,
    // shadows and every operator are the same code as for any other shape.
    auto pattern = std::make_shared<canvas_pattern>();
    pattern->image = source;
    pattern->repeat_x = false;
    pattern->repeat_y = false;
    pattern->matrix =
        transform::translation(static_cast<float>(-cx0), static_cast<float>(-cy0))
            .then(transform::scaling(static_cast<float>(scale_x), static_cast<float>(scale_y)))
            .then(transform::translation(static_cast<float>(dx), static_cast<float>(dy)));
    pattern->source_rect = std::array<float, 4>{static_cast<float>(cx0), static_cast<float>(cy0),
                                                static_cast<float>(cx1), static_cast<float>(cy1)};
    canvas_paint paint;
    paint.pattern = std::move(pattern);
    canvas_path rect;
    path_builder{rect, state_.matrix}.rect(dx, dy, dw, dh);
    paint_path(rect, fill_rule::nonzero, paint);
}

// --- compositing ------------------------------------------------------------------

canvas_context::pass::pass(canvas_context & canvas) : canvas_(&canvas) {
    if (!clears_untouched(canvas.state_.composite_mode) || !canvas.pixels_) { return; }
    canvas.backdrop_ = std::make_shared<bitmap>(*canvas.pixels_);
    std::ranges::fill(canvas.pixels_->pixels, std::uint32_t{0});
}

canvas_context::pass::~pass() {
    canvas_context & canvas = *canvas_;
    // "Within the clipping region": outside it a clearing operator changes
    // nothing, so those pixels go back to what they were.
    if (canvas.backdrop_ && canvas.state_.clip && canvas.pixels_) {
        const std::vector<std::uint8_t> & mask = *canvas.state_.clip;
        const bool stale = canvas.clip_stale();
        std::vector<std::uint32_t> & out = canvas.pixels_->pixels;
        const std::vector<std::uint32_t> & was = canvas.backdrop_->pixels;
        for (std::size_t i = 0; i < out.size() && i < mask.size(); ++i) {
            if (stale || mask[i] == 0) { out[i] = was[i]; }
        }
    }
    canvas.backdrop_.reset();
}

color canvas_context::with_alpha(color c) const {
    // `clear` is `copy` of nothing: Fa = Fb = 0, so every pixel in the clip
    // becomes transparent black.
    if (state_.clear_operator) { return color{0}; }
    if (state_.alpha >= 1) { return c; }
    const auto a = static_cast<std::uint8_t>(
        static_cast<double>(c.alpha()) * std::clamp(state_.alpha, 0.0, 1.0) + 0.5);
    return color::rgba(c.red(), c.green(), c.blue(), a);
}

std::uint32_t canvas_context::composite_one(int x, int y, std::uint32_t dst, color source) const {
    // blend_over is exact only onto an opaque backdrop; onto a transparent or
    // translucent one it is the formula's general case, or a half-transparent
    // green over nothing comes out half-dark green.
    if (state_.composite_mode == composite::source_over && (dst >> 24) == 0xFFu) {
        return raster::blend_over(dst, source);
    }
    return composite_pixel(state_.composite_mode, backdrop_at(x, y), source.argb);
}

void canvas_context::blend_span(int y, int x0, int x1, color c) {
    if (!pixels_) { return; }
    bitmap & target = *pixels_;
    if (y < 0 || y >= target.height) { return; }
    const int left = std::max(0, x0);
    const int right = std::min(target.width - 1, x1);
    if (left > right) { return; }

    const color source = with_alpha(c);
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(target.width);
    std::uint32_t * out = target.pixels.data() + row;
    // The clip mask is the same shape as the bitmap, so the row offset serves
    // both and the per-pixel index arithmetic disappears.
    if (clip_stale()) { return; }
    const std::uint8_t * mask = state_.clip ? state_.clip->data() + row : nullptr;
    for (int x = left; x <= right; ++x) {
        if (mask != nullptr && mask[x] == 0) { continue; }
        out[x] = composite_one(x, y, out[x], source);
    }
}

void canvas_context::blend_row(int y, int x0, int x1, const std::uint32_t * colors) {
    if (!pixels_ || y < 0 || y >= pixels_->height) { return; }
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(pixels_->width);
    std::uint32_t * out = pixels_->pixels.data() + row;
    if (clip_stale()) { return; }
    const std::uint8_t * mask = state_.clip ? state_.clip->data() + row : nullptr;
    for (int x = x0; x <= x1; ++x) {
        if (mask != nullptr && mask[x] == 0) { continue; }
        out[x] = composite_one(x, y, out[x], with_alpha(color{colors[x - x0]}));
    }
}

void canvas_context::write_span(int y, int x0, int x1, std::uint32_t argb) {
    if (!pixels_) { return; }
    bitmap & target = *pixels_;
    if (y < 0 || y >= target.height) { return; }
    const int left = std::max(0, x0);
    const int right = std::min(target.width - 1, x1);
    if (left > right) { return; }
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(target.width);
    std::uint32_t * out = target.pixels.data() + row;
    if (clip_stale()) { return; }
    const std::uint8_t * mask = state_.clip ? state_.clip->data() + row : nullptr;
    if (mask == nullptr) {
        std::fill(out + left, out + right + 1, argb);
        return;
    }
    for (int x = left; x <= right; ++x) {
        if (mask[x] != 0) { out[x] = argb; }
    }
}

// --- the store --------------------------------------------------------------------

void canvas_store::set_fonts(const raster::font_backend * fonts) {
    fonts_ = fonts;
    for (auto & [key, canvas] : canvases_) { canvas->set_fonts(fonts); }
}

canvas_context * canvas_store::context_for(node_id id, int width, int height) {
    auto it = canvases_.find(id.key());
    if (it == canvases_.end()) {
        auto pixels = make_surface(width, height);
        it =
            canvases_.emplace(id.key(), std::make_unique<canvas_context>(std::move(pixels), fonts_))
                .first;
    }
    return it->second.get();
}

void canvas_store::resize(node_id id, int width, int height) {
    if (width < 0 || height < 0) { return; }
    const auto it = canvases_.find(id.key());
    if (it == canvases_.end()) { return; }
    // In place: script is holding a context that finds this object by its
    // canvas, so it must be the same one afterwards.
    it->second->reset_surface(width, height);
}

const canvas_context * canvas_store::find(node_id id) const {
    const auto it = canvases_.find(id.key());
    return it == canvases_.end() ? nullptr : it->second.get();
}

canvas_context * canvas_store::find(node_id id) {
    const auto it = canvases_.find(id.key());
    return it == canvases_.end() ? nullptr : it->second.get();
}

std::shared_ptr<const bitmap> canvas_store::pixels_of(node_id id) const {
    const canvas_context * found = find(id);
    return found == nullptr ? nullptr : found->surface();
}

std::uint64_t canvas_store::total_revision() const {
    std::uint64_t sum = 0;
    for (const auto & [key, canvas] : canvases_) { sum += canvas->revision(); }
    return sum;
}

} // namespace ctbrowser::shell
