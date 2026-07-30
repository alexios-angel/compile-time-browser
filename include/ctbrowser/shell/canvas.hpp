#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/shell/composite.hpp>

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
    // The INVERSE, or the identity when there is none. A canvas transform is
    // affine and 2x3, so this is one determinant and six terms - and a page
    // cannot compose transforms without it: p5's beginClip expresses a clip path
    // relative to where the clip began as `getTransform().inverse().multiply(...)`.
    //
    // A singular matrix (zero determinant) has flattened everything to a line, so
    // there is nothing to invert; the identity is what a browser hands back, and
    // it keeps a page's arithmetic finite rather than filling it with NaN.
    [[nodiscard]] transform inverse() const noexcept {
        const float det = a * d - b * c;
        if (det == 0) { return transform{}; }
        const float inv = 1.0f / det;
        return transform{
            d * inv, -b * inv, -c * inv, a * inv, (c * f - d * e) * inv, (b * e - a * f) * inv};
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
    canvas_context(std::shared_ptr<bitmap> pixels, const raster::font_backend * fonts = nullptr)
        : pixels_(std::move(pixels)), fonts_(fonts) {}

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
    // `globalCompositeOperation`. See shell/composite.hpp for why every mode is
    // implemented rather than the two that look common: p5's tint() alone needs
    // four of them, and blendMode() is fourteen more names for these.
    composite composite_mode = composite::source_over;
    float font_size = 10;
    // The FAMILY is part of the state, not just the size. It was dropped on the
    // floor - `ctx.font = "16px Arial"` kept the 16 and threw "Arial" away - so
    // every canvas drew in the bitmap font whatever it asked for, at a
    // monospaced 8-pixel cell. A HUD positioned from the right edge by real
    // Arial metrics then ran off the canvas.
    std::string font_family;
    bool font_bold = false;
    bool font_italic = false;

    // The SPEC STRINGS exactly as script set them - "16px Arial", "#0095DD".
    // Kept because restore() has to put them back on the JavaScript object, and
    // the parsed forms cannot be turned back into text faithfully: a colour is
    // a packed integer by then, and a font has been split into size, family and
    // two flags. Storing what was written is both simpler and lossless, and it
    // means `ctx.fillStyle` reads back what the page assigned.
    // WHERE THE TEXT SITS RELATIVE TO THE POINT IT WAS GIVEN.
    //
    // The single most-used canvas property this engine did not have: p5.js
    // alone sets textAlign 229 times, and every chart, label and HUD on the web
    // positions text with it. Without them `fillText(s, x, y)` always started at
    // x on the alphabetic baseline, so a right-aligned label ran off the edge it
    // was aligned to and a centred one was centred nowhere.
    //
    // Stored as the spec's strings so save/restore and the property round-trip
    // need no encoding, and an unknown value simply behaves as the default.
    std::string text_align = "start";
    std::string text_baseline = "alphabetic";

    std::string font_spec = "10px sans-serif";
    std::string fill_spec = "#000000";
    std::string stroke_spec = "#000000";
    // As script set it, for the same round-tripping reason as the others.
    std::string composite_spec = "source-over";

    // The backend that both MEASURES and DRAWS this canvas's text. One object
    // answers both, which is the rule the rest of the engine already follows
    // (docs/raster.md): text lands where it was measured only if the thing that
    // measured it is the thing that drew it.
    void set_fonts(const raster::font_backend * fonts) noexcept { fonts_ = fonts; }
    [[nodiscard]] const raster::font_backend & fonts() const noexcept {
        return fonts_ != nullptr ? *fonts_ : raster::font8x8_fonts();
    }

    // The advance of `text` in the current font. `measureText` and `fillText`
    // both go through this rather than each computing their own.
    [[nodiscard]] float measure_text(std::string_view text) const;

    // New pixels at a new size, IN PLACE. Setting canvas.width resets the
    // canvas, and the object has to survive that: script holds a context whose
    // every method captured this address.
    void reset_surface(int width, int height);

    void save();
    void restore();
    void translate(float x, float y) { transform_ = transform::translation(x, y).then(transform_); }
    void scale(float x, float y) { transform_ = transform::scaling(x, y).then(transform_); }
    void rotate(float radians) { transform_ = transform::rotation(radians).then(transform_); }
    void reset_transform() { transform_ = transform{}; }
    // REPLACE the matrix, rather than compose with it. A library that keeps its
    // own transform stack - p5.js does, and calls this once per frame from
    // resetMatrix() - needs to be able to say what the matrix IS, not only how
    // to nudge it, or its stack and the canvas's drift apart within a frame.
    void set_transform(const transform & t) { transform_ = t; }
    void multiply_transform(const transform & t) { transform_ = t.then(transform_); }
    [[nodiscard]] const transform & current_transform() const noexcept { return transform_; }

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
    // Beziers, FLATTENED into line segments the way arc() already is: the
    // rasteriser takes polygons, so a curve has to become one somewhere.
    //
    // Flattened in DEVICE space, after the control points are transformed. The
    // transform is affine, so the curve through the transformed control points
    // is the transform of the curve - and choosing the segment count from the
    // device-space extent means a scaled-up curve gets more segments rather
    // than the same few, magnified into visible facets.
    // An ellipse is an arc with two radii and a rotation. It is not a nicety:
    // it is how a canvas draws a circle of a given width and height, which is
    // p5.js's `ellipse()` and `circle()` and therefore most of what a sketch
    // puts on screen.
    void ellipse(float x, float y, float rx, float ry, float rotation, float from, float to,
                 bool anticlockwise);
    void quadratic_curve_to(float cx, float cy, float x, float y);
    void bezier_curve_to(float c1x, float c1y, float c2x, float c2y, float x, float y);

    // WHICH POINTS ARE INSIDE a path that overlaps itself. The spec's default
    // is nonzero, and it is not a nicety: a star, a figure-of-eight and any
    // shape drawn as one continuous self-crossing path are solid under nonzero
    // and holed under even-odd. `ctx.fill('evenodd')` asks for the other one.
    enum class fill_rule {
        nonzero,
        even_odd
    };

    // Scanline fill under either rule. Nonzero by default, as the spec says.
    void fill(fill_rule rule = fill_rule::nonzero);

    // INTERSECT the current path into the clip region. Everything drawn
    // afterwards is confined to it, and `restore()` puts the old region back -
    // which is the only way a page ever removes one.
    //
    // A per-pixel mask rather than a path list: the region is an INTERSECTION of
    // arbitrary paths, and intersecting two scanline polygons exactly is a
    // clipping-polygon algorithm, where a mask is one AND per pixel. Held
    // through a shared_ptr so save() copies a pointer rather than the buffer -
    // a page that saves and restores around every shape would otherwise pay for
    // the whole canvas each time.
    void clip(fill_rule rule = fill_rule::nonzero);

    void stroke();

    // --- text and images --------------------------------------------------

    // Drawn through the same font backend the page text around it uses, in the
    // family `ctx.font` asked for, so a canvas and the document look like one
    // page - and so that measureText and fillText agree. `y` is the BASELINE,
    // per spec. With no real fonts loaded the backend is font8x8 and this is
    // the bitmap font it always was.
    void fill_text(std::string_view text, float x, float y);

    // The nine-argument drawImage: a rectangle out of the source, into a
    // rectangle on the canvas. Sprite sheets are the reason this form exists.
    void draw_image_region(const bitmap & source, float sx, float sy, float sw, float sh, float dx,
                           float dy, float dw, float dh) {
        if (!pixels_ || source.empty() || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) { return; }
        const pass composited{*this};
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
        // The face travels with save()/restore() like every other bit of state.
        // Leaving it out is the classic follow-on bug: a restore() puts the
        // size back and leaves the family from whatever drew last.
        std::string font_family;
        bool font_bold;
        bool font_italic;
        // And the spec strings, because everything here except the transform is
        // ALSO a JavaScript property, and the bindings' sync() copies those
        // onto this object before every call. Popping the C++ stack alone was
        // undone by the very next draw - the transform survived a restore()
        // only because it is the one piece of state script cannot assign
        // directly. `restore` writes these back to the JS object.
        std::string font_spec;
        std::string fill_spec;
        std::string stroke_spec;
        std::string text_align;
        std::string text_baseline;
        composite composite_mode;
        std::string composite_spec;
        // The clip region in force. A clip is undone by restore() and by
        // nothing else, so it belongs on this stack like the rest of the state.
        std::shared_ptr<const std::vector<std::uint8_t>> clip;
    };

    // A COMPOSITE PASS, for the five operators where a pixel the source never
    // touched still has to change (see clears_untouched). The backdrop is
    // snapshotted and the surface cleared, so an untouched pixel ends up
    // transparent - which is the formula's own answer for as = 0 - and a touched
    // one composites against the snapshot rather than against the cleared
    // surface it is being written into.
    //
    // Every public draw brackets itself with one. For source-over and the blend
    // modes it does nothing at all and costs a bool test.
    class pass {
    public:
        explicit pass(canvas_context & canvas) : canvas_(&canvas) {
            if (!clears_untouched(canvas.composite_mode) || !canvas.pixels_) { return; }
            canvas.backdrop_ = std::make_shared<bitmap>(*canvas.pixels_);
            std::ranges::fill(canvas.pixels_->pixels, std::uint32_t{0});
        }
        pass(const pass &) = delete;
        pass & operator=(const pass &) = delete;
        ~pass() { canvas_->backdrop_.reset(); }

    private:
        canvas_context * canvas_;
    };

    // What a write composites AGAINST: the snapshot during a clearing pass, and
    // the surface itself otherwise.
    [[nodiscard]] std::uint32_t backdrop_at(int x, int y) const {
        return backdrop_ ? backdrop_->at(x, y) : pixels_->at(x, y);
    }
    std::shared_ptr<const bitmap> backdrop_;

    void touch() { ++revision_; }
    void move_to_device(point p) { subpaths_.push_back(subpath{{p}, false}); }
    void line_to_device(point p);

    [[nodiscard]] color with_alpha(color c) const;

    void blend(int x, int y, color c);

    void span_row(int y, float from, float to, color c);

    // Walk the current path a scanline at a time and hand each inside-span to
    // `emit(y, from, to)`. Shared by fill() and clip() so the winding rule has
    // ONE implementation - two copies is two chances for a clip to disagree
    // with the fill it was derived from.
    void for_each_span(fill_rule rule, const std::function<void(int, float, float)> & emit);

    // Whether this pixel is outside the clip region. One test at every write
    // site, and it is a single bool check when nothing has clipped.
    [[nodiscard]] bool clipped_out(int x, int y) const noexcept {
        if (!clip_) { return false; }
        if (x < 0 || y < 0 || x >= width() || y >= height()) { return true; }
        return (*clip_)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width()) +
                        static_cast<std::size_t>(x)] == 0;
    }

    std::shared_ptr<const std::vector<std::uint8_t>> clip_;

    // An axis-aligned rect through the CTM. Only translation and scale are
    // honoured for the fast path; a rotated fillRect goes through the path
    // filler instead, which is the same answer more slowly.
    void fill_axis_rect(float x, float y, float w, float h, color c);

    // clearRect writes rather than blends: it REPLACES the pixels, which is
    // what makes it able to make them transparent again.
    void write_axis_rect(float x, float y, float w, float h, std::uint32_t argb);

    void line(point a, point b, float thickness);

    std::shared_ptr<bitmap> pixels_;
    const raster::font_backend * fonts_ = nullptr;
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
    // Remembered AND stamped on every context that already exists: real fonts
    // are loaded by use_real_fonts(), which can run either side of a page
    // making its contexts, and clear() on reload must not lose the backend.
    void set_fonts(const raster::font_backend * fonts);

    [[nodiscard]] canvas_context * context_for(node_id id, int width, int height);
    // Setting a canvas's width or height RESETS it - new pixels, cleared, and
    // the drawing state back to its defaults. That is the spec, and it is also
    // the only way the surface can follow an element that is resized after its
    // context was made. Does nothing when the size is unchanged, because
    // `canvas.width = canvas.width` is a deliberate idiom for clearing and
    // anything else assigning the same number is not asking for a clear.
    void resize(node_id id, int width, int height);
    [[nodiscard]] const canvas_context * find(node_id id) const;
    [[nodiscard]] std::shared_ptr<const bitmap> pixels_of(node_id id) const;
    // Sum of every canvas's revision. The browser compares it between frames to
    // decide whether anything was drawn - cheaper than asking each one, and it
    // cannot miss a change because revisions only ever increase.
    [[nodiscard]] std::uint64_t total_revision() const;
    void clear() { canvases_.clear(); }

private:
    // A CONTEXT'S ADDRESS IS STABLE FOR AS LONG AS ITS CANVAS EXISTS.
    //
    // Every method bound onto a script `CanvasRenderingContext2D` captures a
    // `canvas_context *`, so the object cannot move. Holding them by value in
    // the map meant that creating a SECOND canvas reallocated the storage and
    // left the first page's context object pointing at freed memory - a page
    // with two canvases was one insertion away from drawing into a dangling
    // pointer, and p5.js makes two before it draws anything.
    flat_map<std::uint64_t, std::unique_ptr<canvas_context>> canvases_;
    const raster::font_backend * fonts_ = nullptr;
};

} // namespace ctbrowser::shell
