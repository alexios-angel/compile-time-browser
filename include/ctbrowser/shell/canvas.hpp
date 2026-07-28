#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>

// The 2D canvas.
//
// A canvas is the one place a page draws pixels directly, and it is what every
// game, chart and visualisation on the web is built on. the previous engine kept the pixels on
// the DOM node; here they live in a store keyed by node_id, for the same reason
// layout results do - the node is document content, and a mutable megapixel
// buffer is not.
//
// The drawing model is the spec's: a current transform, a path built in user
// space and transformed at verb time, source-over compositing, and a state
// stack. Getting the transform timing wrong is the classic canvas bug - points
// must be transformed as they are ADDED to the path, not when it is filled, or
// a translate between moveTo and lineTo moves the wrong end of the line.

namespace ctbrowser::shell {

using ctbrowser::paint::bitmap;

// A 2x3 affine transform, the shape canvas actually uses.
struct transform {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    [[nodiscard]] point apply(float x, float y) const noexcept {
        return point{a * x + c * y + e, b * x + d * y + f};
    }
    [[nodiscard]] transform then(const transform & next) const noexcept {
        return transform{a * next.a + b * next.c,          a * next.b + b * next.d,
                         c * next.a + d * next.c,          c * next.b + d * next.d,
                         e * next.a + f * next.c + next.e, e * next.b + f * next.d + next.f};
    }
    [[nodiscard]] static transform translation(float x, float y) noexcept {
        return transform{1, 0, 0, 1, x, y};
    }
    [[nodiscard]] static transform scaling(float x, float y) noexcept {
        return transform{x, 0, 0, y, 0, 0};
    }
    [[nodiscard]] static transform rotation(float radians) noexcept {
        const float s = std::sin(radians);
        const float c = std::cos(radians);
        return transform{c, s, -s, c, 0, 0};
    }
};

// One canvas's pixels and drawing state.
class canvas_context {
public:
    canvas_context(std::shared_ptr<bitmap> pixels) : pixels_(std::move(pixels)) {}

    [[nodiscard]] const std::shared_ptr<bitmap> & surface() const noexcept { return pixels_; }
    [[nodiscard]] int width() const noexcept { return pixels_ ? pixels_->width : 0; }
    [[nodiscard]] int height() const noexcept { return pixels_ ? pixels_->height : 0; }

    // Everything a script has drawn is recorded here so the browser knows the
    // tile needs redrawing. A canvas that changes without saying so shows a
    // stale frame, which is the failure everyone hits once.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    // --- state -----------------------------------------------------------

    color fill_style = color::rgba(0, 0, 0);
    color stroke_style = color::rgba(0, 0, 0);
    float line_width = 1;
    float global_alpha = 1;
    float font_size = 10;

    void save();
    void restore();
    void translate(float x, float y) { transform_ = transform::translation(x, y).then(transform_); }
    void scale(float x, float y) { transform_ = transform::scaling(x, y).then(transform_); }
    void rotate(float radians) { transform_ = transform::rotation(radians).then(transform_); }
    void reset_transform() { transform_ = transform{}; }

    // --- rectangles -------------------------------------------------------

    void fill_rect(float x, float y, float w, float h) { fill_axis_rect(x, y, w, h, fill_style); }
    void clear_rect(float x, float y, float w, float h);
    void stroke_rect(float x, float y, float w, float h);

    // --- paths ------------------------------------------------------------

    void begin_path() { subpaths_.clear(); }
    void move_to(float x, float y);
    void line_to(float x, float y);
    void close_path();
    void rect_path(float x, float y, float w, float h);
    // Arcs are flattened to line segments. A canvas rasterizer that draws true
    // curves needs a scanline converter with them; segments at this density are
    // indistinguishable at the sizes a canvas is drawn.
    void arc(float x, float y, float radius, float from, float to, bool anticlockwise);

    // Even-odd scanline fill. The spec's default is nonzero winding; even-odd
    // differs only for self-intersecting paths, which is a real limitation and
    // is written down rather than hidden.
    void fill();

    void stroke();

    // --- text and images --------------------------------------------------

    // font8x8, the same glyphs the page rasterizer uses, so a canvas and the
    // text around it look like one document. `y` is the BASELINE, per spec.
    void fill_text(std::string_view text, float x, float y);

    // The nine-argument drawImage: a rectangle out of the source, into a
    // rectangle on the canvas. Sprite sheets are the reason this form exists.
    void draw_image_region(const bitmap & source, float sx, float sy, float sw, float sh, float dx,
                           float dy, float dw, float dh) {
        if (!pixels_ || source.empty() || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) { return; }
        const point at = transform_.apply(dx, dy);
        const int left = static_cast<int>(at.x);
        const int top = static_cast<int>(at.y);
        for (int y = 0; y < static_cast<int>(dh); ++y) {
            const int source_y = static_cast<int>(sy + static_cast<float>(y) / dh * sh);
            for (int x = 0; x < static_cast<int>(dw); ++x) {
                const int source_x = static_cast<int>(sx + static_cast<float>(x) / dw * sw);
                const std::uint32_t texel = source.at(source_x, source_y);
                // Alpha TEST, not a blend: sprite sheets are drawn with a fully
                // transparent background, and blending each edge pixel would
                // leave a halo.
                if ((texel >> 24) == 0) { continue; }
                blend(left + x, top + y, color{texel});
            }
        }
        touch();
    }

    void draw_image(const bitmap & source, float x, float y, float w, float h);

private:
    struct subpath {
        std::vector<point> points;
        bool closed = false;
    };
    struct state {
        transform matrix;
        color fill;
        color stroke;
        float line_width;
        float alpha;
        float font_size;
    };

    void touch() { ++revision_; }
    void move_to_device(point p) { subpaths_.push_back(subpath{{p}, false}); }
    void line_to_device(point p);

    [[nodiscard]] color with_alpha(color c) const;

    void blend(int x, int y, color c);

    void span_row(int y, float from, float to, color c);

    // An axis-aligned rect through the CTM. Only translation and scale are
    // honoured for the fast path; a rotated fillRect goes through the path
    // filler instead, which is the same answer more slowly.
    void fill_axis_rect(float x, float y, float w, float h, color c);

    // clearRect writes rather than blends: it REPLACES the pixels, which is
    // what makes it able to make them transparent again.
    void write_axis_rect(float x, float y, float w, float h, std::uint32_t argb);

    void line(point a, point b, float thickness);

    std::shared_ptr<bitmap> pixels_;
    transform transform_;
    std::vector<subpath> subpaths_;
    std::vector<state> stack_;
    std::uint64_t revision_ = 0;
};

// Every canvas in the document. Keyed by node_id, not stored on the node - the
// node is content, and a megapixel buffer that a script mutates continuously is
// not something the DOM should own or copy.
class canvas_store {
public:
    [[nodiscard]] canvas_context * context_for(node_id id, int width, int height);
    [[nodiscard]] const canvas_context * find(node_id id) const;
    [[nodiscard]] std::shared_ptr<const bitmap> pixels_of(node_id id) const;
    // Sum of every canvas's revision. The browser compares it between frames to
    // decide whether anything was drawn - cheaper than asking each one, and it
    // cannot miss a change because revisions only ever increase.
    [[nodiscard]] std::uint64_t total_revision() const;
    void clear() { canvases_.clear(); }

private:
    flat_map<std::uint64_t, canvas_context> canvases_;
};

} // namespace ctbrowser::shell
