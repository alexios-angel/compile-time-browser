#include <ctbrowser/shell/canvas.hpp>

// canvas: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::shell {

void canvas_context::save() {
    stack_.push_back(
        state{transform_, fill_style, stroke_style, line_width, global_alpha, font_size});
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

void canvas_context::fill() {
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

    std::vector<float> crossings;
    for (int y = top; y <= bottom; ++y) {
        crossings.clear();
        const float scan = static_cast<float>(y) + 0.5f;
        for (const subpath & s : subpaths_) {
            const std::size_t n = s.points.size();
            if (n < 2) { continue; }
            for (std::size_t i = 0; i < n; ++i) {
                const point & a = s.points[i];
                const point & b = s.points[(i + 1) % n];
                if (i + 1 == n && !s.closed) { continue; } // open subpaths do not wrap
                if ((a.y <= scan && b.y > scan) || (b.y <= scan && a.y > scan)) {
                    crossings.push_back(a.x + (scan - a.y) / (b.y - a.y) * (b.x - a.x));
                }
            }
        }
        std::ranges::sort(crossings);
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            span_row(y, crossings[i], crossings[i + 1], fill_style);
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

void canvas_context::fill_text(std::string_view text, float x, float y) {
    if (!pixels_) { return; }
    const int scale = raster::font8x8_scale(font_size);
    const point origin = transform_.apply(x, y);
    int cell = 0;
    for (const char raw : text) {
        const auto byte = static_cast<unsigned char>(raw);
        if ((byte & 0xC0u) == 0x80u) { continue; } // continuation
        const int left = static_cast<int>(origin.x) + cell * 8 * scale;
        const int top = static_cast<int>(origin.y) - 8 * scale; // baseline to cell top
        ++cell;
        if (byte > 0x7F) { continue; }
        for (int gy = 0; gy < 8; ++gy) {
            for (int gx = 0; gx < 8; ++gx) {
                if (!raster::glyph_pixel(byte, gy, gx)) { continue; }
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        blend(left + gx * scale + sx, top + gy * scale + sy, fill_style);
                    }
                }
            }
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

canvas_context * canvas_store::context_for(node_id id, int width, int height) {
    auto it = canvases_.find(id.key());
    if (it == canvases_.end()) {
        auto pixels = std::make_shared<bitmap>(width, height);
        it = canvases_.emplace(id.key(), canvas_context{std::move(pixels)}).first;
    }
    return &it->second;
}

const canvas_context * canvas_store::find(node_id id) const {
    const auto it = canvases_.find(id.key());
    return it == canvases_.end() ? nullptr : &it->second;
}

std::shared_ptr<const bitmap> canvas_store::pixels_of(node_id id) const {
    const canvas_context * found = find(id);
    return found == nullptr ? nullptr : found->surface();
}

std::uint64_t canvas_store::total_revision() const {
    std::uint64_t sum = 0;
    for (const auto & [key, canvas] : canvases_) { sum += canvas.revision(); }
    return sum;
}

} // namespace ctbrowser::shell
