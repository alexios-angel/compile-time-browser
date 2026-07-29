#include <ctbrowser/shell/canvas.hpp>

// canvas: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::shell {

void canvas_context::reset_surface(int width, int height) {
    pixels_ = std::make_shared<bitmap>(width, height);
    // A resize RESETS the canvas: the pixels are cleared and the drawing state
    // goes back to its defaults, which includes an empty save stack and an
    // identity transform.
    subpaths_.clear();
    stack_.clear();
    transform_ = ctbrowser::shell::transform{};
    ++revision_;
}

void canvas_context::save() {
    stack_.push_back(state{transform_, fill_style, stroke_style, line_width, global_alpha,
                           font_size, font_family, font_bold, font_italic, font_spec, fill_spec,
                           stroke_spec, text_align, text_baseline});
}

void canvas_context::restore() {
    if (stack_.empty()) { return; }
    const state & s = stack_.back();
    transform_ = s.matrix;
    fill_style = s.fill;
    stroke_style = s.stroke;
    line_width = s.line_width;
    global_alpha = s.alpha;
    font_size = s.font_size;
    font_family = s.font_family;
    font_bold = s.font_bold;
    font_italic = s.font_italic;
    font_spec = s.font_spec;
    fill_spec = s.fill_spec;
    stroke_spec = s.stroke_spec;
    text_align = s.text_align;
    text_baseline = s.text_baseline;
    stack_.pop_back();
}

void canvas_context::clear_rect(float x, float y, float w, float h) {
    // TRANSPARENT, not white. A cleared canvas shows the page through it,
    // which is what makes an overlay canvas work at all.
    write_axis_rect(x, y, w, h, 0);
}

void canvas_context::stroke_rect(float x, float y, float w, float h) {
    const float t = std::max(1.0f, line_width);
    fill_axis_rect(x, y, w, t, stroke_style);
    fill_axis_rect(x, y + h - t, w, t, stroke_style);
    fill_axis_rect(x, y, t, h, stroke_style);
    fill_axis_rect(x + w - t, y, t, h, stroke_style);
}

void canvas_context::move_to(float x, float y) {
    subpaths_.push_back(subpath{{transform_.apply(x, y)}, false});
}

void canvas_context::line_to(float x, float y) {
    if (subpaths_.empty()) { return move_to(x, y); }
    subpaths_.back().points.push_back(transform_.apply(x, y));
}

void canvas_context::close_path() {
    if (!subpaths_.empty()) { subpaths_.back().closed = true; }
}

void canvas_context::rect_path(float x, float y, float w, float h) {
    move_to(x, y);
    line_to(x + w, y);
    line_to(x + w, y + h);
    line_to(x, y + h);
    close_path();
}

void canvas_context::arc(float x, float y, float radius, float from, float to, bool anticlockwise) {
    if (radius <= 0) { return; }
    constexpr int segments_per_turn = 64;
    float sweep = to - from;
    if (anticlockwise && sweep > 0) { sweep -= 6.28318530718f; }
    if (!anticlockwise && sweep < 0) { sweep += 6.28318530718f; }
    const int steps =
        std::max(2, static_cast<int>(std::fabs(sweep) / 6.28318530718f * segments_per_turn) + 1);
    for (int i = 0; i <= steps; ++i) {
        const float t = from + sweep * static_cast<float>(i) / static_cast<float>(steps);
        const float px = x + radius * std::cos(t);
        const float py = y + radius * std::sin(t);
        if (i == 0 && subpaths_.empty()) {
            move_to_device(transform_.apply(px, py));
        } else if (i == 0) {
            line_to_device(transform_.apply(px, py));
        } else {
            line_to_device(transform_.apply(px, py));
        }
    }
}

void canvas_context::ellipse(float x, float y, float rx, float ry, float rotation, float from,
                             float to, bool anticlockwise) {
    if (rx <= 0 || ry <= 0) { return; }
    constexpr float tau = 6.28318530718f;
    // Segment count from the LARGER radius, so a long thin ellipse is as smooth
    // along its long axis as a circle of that size would be.
    const int segments_per_turn =
        std::clamp(static_cast<int>(std::max(rx, ry) * 1.5f) + 8, 16, 256);
    float sweep = to - from;
    if (anticlockwise && sweep > 0) { sweep -= tau; }
    if (!anticlockwise && sweep < 0) { sweep += tau; }
    const int steps = std::max(
        2, static_cast<int>(std::fabs(sweep) / tau * static_cast<float>(segments_per_turn)) + 1);
    const float cos_r = std::cos(rotation);
    const float sin_r = std::sin(rotation);
    for (int i = 0; i <= steps; ++i) {
        const float t = from + sweep * static_cast<float>(i) / static_cast<float>(steps);
        const float ex = rx * std::cos(t);
        const float ey = ry * std::sin(t);
        const point p = transform_.apply(x + ex * cos_r - ey * sin_r, y + ex * sin_r + ey * cos_r);
        if (i == 0 && subpaths_.empty()) {
            move_to_device(p);
        } else {
            line_to_device(p);
        }
    }
}

namespace {

// How many segments a curve of this on-screen size needs. Enough that the
// straight-line error is under about a third of a pixel, and capped so a curve
// stretched across a huge transform cannot cost unbounded time.
int curve_steps(point a, point b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float extent = std::sqrt(dx * dx + dy * dy);
    return std::clamp(static_cast<int>(extent / 3.0f) + 4, 4, 128);
}

} // namespace

void canvas_context::quadratic_curve_to(float cx, float cy, float x, float y) {
    const point control = transform_.apply(cx, cy);
    const point end = transform_.apply(x, y);
    if (subpaths_.empty()) { move_to_device(control); }
    const point start = subpaths_.back().points.back();
    const int steps = curve_steps(start, end);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float u = 1 - t;
        line_to_device(point{u * u * start.x + 2 * u * t * control.x + t * t * end.x,
                             u * u * start.y + 2 * u * t * control.y + t * t * end.y});
    }
}

void canvas_context::bezier_curve_to(float c1x, float c1y, float c2x, float c2y, float x, float y) {
    const point one = transform_.apply(c1x, c1y);
    const point two = transform_.apply(c2x, c2y);
    const point end = transform_.apply(x, y);
    if (subpaths_.empty()) { move_to_device(one); }
    const point start = subpaths_.back().points.back();
    const int steps = curve_steps(start, end);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float u = 1 - t;
        line_to_device(point{u * u * u * start.x + 3 * u * u * t * one.x + 3 * u * t * t * two.x +
                                 t * t * t * end.x,
                             u * u * u * start.y + 3 * u * u * t * one.y + 3 * u * t * t * two.y +
                                 t * t * t * end.y});
    }
}

void canvas_context::fill(fill_rule rule) {
    if (!pixels_) { return; }
    float min_y = 1e30f, max_y = -1e30f;
    for (const subpath & s : subpaths_) {
        for (const point & p : s.points) {
            min_y = std::min(min_y, p.y);
            max_y = std::max(max_y, p.y);
        }
    }
    if (min_y > max_y) { return; }
    const int top = std::max(0, static_cast<int>(std::floor(min_y)));
    const int bottom = std::min(pixels_->height - 1, static_cast<int>(std::ceil(max_y)));

    // A crossing carries its DIRECTION, which is what the two fill rules
    // disagree about.
    //
    // Even-odd fills between alternate crossings; nonzero adds +1 for an edge
    // crossing downwards and -1 for one crossing upwards, and fills wherever
    // the running total is not zero. They differ exactly where a path overlaps
    // itself: a five-pointed star drawn as one continuous path is solid under
    // nonzero and has a pentagonal HOLE under even-odd. The spec's default is
    // nonzero, and this drew even-odd - so every self-overlapping shape, which
    // is most of what beginShape/vertex is used for, came out hollow.
    struct crossing {
        float x;
        int direction;
    };
    std::vector<crossing> crossings;
    for (int y = top; y <= bottom; ++y) {
        crossings.clear();
        const float scan = static_cast<float>(y) + 0.5f;
        for (const subpath & s : subpaths_) {
            const std::size_t n = s.points.size();
            if (n < 2) { continue; }
            for (std::size_t i = 0; i < n; ++i) {
                const point & a = s.points[i];
                const point & b = s.points[(i + 1) % n];
                // An unclosed subpath is still CLOSED FOR FILLING - that is
                // what the spec means by an implicit close - so the wrap edge
                // is included here, unlike in stroke().
                if ((a.y <= scan && b.y > scan) || (b.y <= scan && a.y > scan)) {
                    crossings.push_back(crossing{a.x + (scan - a.y) / (b.y - a.y) * (b.x - a.x),
                                                 b.y > a.y ? 1 : -1});
                }
            }
        }
        std::ranges::sort(crossings, {}, &crossing::x);
        if (rule == fill_rule::even_odd) {
            for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
                span_row(y, crossings[i].x, crossings[i + 1].x, fill_style);
            }
            continue;
        }
        int winding = 0;
        for (std::size_t i = 0; i + 1 < crossings.size(); ++i) {
            winding += crossings[i].direction;
            if (winding != 0) { span_row(y, crossings[i].x, crossings[i + 1].x, fill_style); }
        }
    }
    touch();
}

void canvas_context::stroke() {
    const float t = std::max(1.0f, line_width);
    for (const subpath & s : subpaths_) {
        const std::size_t n = s.points.size();
        for (std::size_t i = 0; i + 1 < n; ++i) { line(s.points[i], s.points[i + 1], t); }
        if (s.closed && n > 2) { line(s.points[n - 1], s.points[0], t); }
    }
    touch();
}

float canvas_context::measure_text(std::string_view text) const {
    return fonts().advance(text, font_size, font_family, font_bold, font_italic);
}

void canvas_context::fill_text(std::string_view text, float x, float y) {
    if (!pixels_ || text.empty()) { return; }
    const raster::font_backend & backend = fonts();
    const float width = measure_text(text);
    const float ascent = backend.ascent(font_size, font_family, font_bold, font_italic);
    const float descent = backend.descent(font_size, font_family, font_bold, font_italic);
    if (width <= 0 || ascent + descent <= 0) { return; }

    // `draw_run` writes into a raster::surface and a canvas owns a
    // paint::bitmap. They have the same pixel layout but surface OWNS its
    // storage, so rather than reshape a type the whole raster path passes by
    // value, the run is drawn into a scratch surface the size of its own
    // bounding box and copied back.
    //
    // The scratch is SEEDED FROM THE DESTINATION first. Against a blank one,
    // blend_over would composite an antialiased glyph edge onto transparent
    // black and that premultiplied result would then be blended into the
    // canvas a second time - a dark halo around every glyph.
    // textAlign shifts along x by a share of the run's width, and
    // textBaseline shifts along y between the font's own metrics. Both are
    // applied in USER space, before the transform: a rotated centred label must
    // be centred on its own baseline, not on the screen's x axis.
    float shift_x = 0;
    if (text_align == "center") {
        shift_x = -width / 2;
    } else if (text_align == "right" || text_align == "end") {
        shift_x = -width;
    }
    // `start` and `end` follow the text direction, which is always
    // left-to-right here - there is no bidi in this engine, and saying so is
    // better than implying an rtl case that would silently be wrong.
    float shift_y = 0;
    if (text_baseline == "top" || text_baseline == "hanging") {
        shift_y = ascent;
    } else if (text_baseline == "middle") {
        shift_y = (ascent - descent) / 2;
    } else if (text_baseline == "bottom" || text_baseline == "ideographic") {
        shift_y = -descent;
    }
    const point origin = transform_.apply(x + shift_x, y + shift_y); // the BASELINE, per spec
    // A pixel of margin each side for antialiasing and any overhang, and an
    // integral left/top so the fraction stays in `where` and sub-pixel
    // positioning survives the round trip.
    const int left = static_cast<int>(std::floor(origin.x)) - 1;
    const int top = static_cast<int>(std::floor(origin.y - ascent)) - 1;
    const int w = static_cast<int>(std::ceil(width)) + 3;
    const int h = static_cast<int>(std::ceil(ascent + descent)) + 3;

    // Clipped to the canvas, so a run drawn mostly off the edge costs only the
    // part that lands - and an entirely off-canvas run costs nothing.
    const int from_x = std::max(0, left);
    const int from_y = std::max(0, top);
    const int to_x = std::min(pixels_->width, left + w);
    const int to_y = std::min(pixels_->height, top + h);
    if (from_x >= to_x || from_y >= to_y) { return; }

    raster::surface scratch{w, h};
    for (int sy = from_y; sy < to_y; ++sy) {
        const std::span<std::uint32_t> row = scratch.row(sy - top);
        for (int sx = from_x; sx < to_x; ++sx) {
            row[static_cast<std::size_t>(sx - left)] = pixels_->at(sx, sy);
        }
    }

    // `where.y` is the TOP of the run box, not the baseline: both backends add
    // their own ascent. Passing the baseline through is the failure mode here,
    // and it puts every glyph one ascent too low.
    const rect where{origin.x - static_cast<float>(left),
                     (origin.y - ascent) - static_cast<float>(top), width, ascent + descent};

    ctbrowser::paint::paint_command run;
    run.op = ctbrowser::paint::paint_op::text_run;
    run.bounds = where;
    run.text = std::string{text};
    run.font_size = font_size;
    run.face = ctbrowser::paint::font_face{font_family, font_bold, font_italic};
    run.fill = with_alpha(fill_style); // globalAlpha, as every other verb honours it

    backend.draw_run(where, run, raster::pixel_rect{0, 0, w, h}, scratch);

    for (int sy = from_y; sy < to_y; ++sy) {
        const std::span<const std::uint32_t> row = scratch.row(sy - top);
        for (int sx = from_x; sx < to_x; ++sx) {
            pixels_->put(sx, sy, row[static_cast<std::size_t>(sx - left)]);
        }
    }
    touch();
}

void canvas_context::draw_image(const bitmap & source, float x, float y, float w, float h) {
    if (!pixels_ || source.empty() || w <= 0 || h <= 0) { return; }
    const point at = transform_.apply(x, y);
    const int left = static_cast<int>(at.x);
    const int top = static_cast<int>(at.y);
    for (int dy = 0; dy < static_cast<int>(h); ++dy) {
        const int sy =
            static_cast<int>(static_cast<float>(dy) / h * static_cast<float>(source.height));
        for (int dx = 0; dx < static_cast<int>(w); ++dx) {
            const int sx =
                static_cast<int>(static_cast<float>(dx) / w * static_cast<float>(source.width));
            const std::uint32_t texel = source.at(sx, sy);
            if ((texel >> 24) == 0) { continue; }
            blend(left + dx, top + dy, color{texel});
        }
    }
    touch();
}

void canvas_context::line_to_device(point p) {
    if (subpaths_.empty()) { return move_to_device(p); }
    subpaths_.back().points.push_back(p);
}

color canvas_context::with_alpha(color c) const {
    if (global_alpha >= 1) { return c; }
    const auto a = static_cast<std::uint8_t>(static_cast<float>(c.alpha()) *
                                             std::clamp(global_alpha, 0.0f, 1.0f));
    return color::rgba(c.red(), c.green(), c.blue(), a);
}

void canvas_context::blend(int x, int y, color c) {
    if (!pixels_) { return; }
    pixels_->put(x, y, raster::blend_over(pixels_->at(x, y), with_alpha(c)));
}

void canvas_context::span_row(int y, float from, float to, color c) {
    const int left = std::max(0, static_cast<int>(std::ceil(from - 0.5f)));
    const int right = std::min(pixels_->width - 1, static_cast<int>(std::floor(to - 0.5f)));
    for (int x = left; x <= right; ++x) { blend(x, y, c); }
}

void canvas_context::fill_axis_rect(float x, float y, float w, float h, color c) {
    if (transform_.b != 0 || transform_.c != 0) {
        const auto saved_fill = fill_style;
        fill_style = c;
        const auto saved = std::move(subpaths_);
        subpaths_.clear();
        rect_path(x, y, w, h);
        fill();
        subpaths_ = std::move(saved);
        fill_style = saved_fill;
        return;
    }
    const point at = transform_.apply(x, y);
    const float sw = w * transform_.a;
    const float sh = h * transform_.d;
    for (int py = static_cast<int>(at.y); py < static_cast<int>(at.y + sh); ++py) {
        for (int px = static_cast<int>(at.x); px < static_cast<int>(at.x + sw); ++px) {
            blend(px, py, c);
        }
    }
    touch();
}

void canvas_context::write_axis_rect(float x, float y, float w, float h, std::uint32_t argb) {
    if (!pixels_) { return; }
    const point at = transform_.apply(x, y);
    const float sw = w * transform_.a;
    const float sh = h * transform_.d;
    for (int py = static_cast<int>(at.y); py < static_cast<int>(at.y + sh); ++py) {
        for (int px = static_cast<int>(at.x); px < static_cast<int>(at.x + sw); ++px) {
            pixels_->put(px, py, argb);
        }
    }
    touch();
}

void canvas_context::line(point a, point b, float thickness) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const int steps = std::max(1, static_cast<int>(std::max(std::fabs(dx), std::fabs(dy))));
    const int half = std::max(0, static_cast<int>(thickness / 2));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int px = static_cast<int>(a.x + dx * t);
        const int py = static_cast<int>(a.y + dy * t);
        for (int oy = -half; oy <= half; ++oy) {
            for (int ox = -half; ox <= half; ++ox) { blend(px + ox, py + oy, stroke_style); }
        }
    }
}

void canvas_store::set_fonts(const raster::font_backend * fonts) {
    fonts_ = fonts;
    for (auto & [key, canvas] : canvases_) { canvas->set_fonts(fonts); }
}

canvas_context * canvas_store::context_for(node_id id, int width, int height) {
    auto it = canvases_.find(id.key());
    if (it == canvases_.end()) {
        auto pixels = std::make_shared<bitmap>(width, height);
        it =
            canvases_.emplace(id.key(), std::make_unique<canvas_context>(std::move(pixels), fonts_))
                .first;
    }
    return it->second.get();
}

void canvas_store::resize(node_id id, int width, int height) {
    if (width <= 0 || height <= 0) { return; }
    const auto it = canvases_.find(id.key());
    if (it == canvases_.end()) { return; }
    if (it->second->width() == width && it->second->height() == height) { return; }
    // In place: script is holding a context whose methods captured this
    // address, so the object must be the same one afterwards.
    it->second->reset_surface(width, height);
}

const canvas_context * canvas_store::find(node_id id) const {
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
