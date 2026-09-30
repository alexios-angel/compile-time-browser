#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/shell/page/composite.hpp>

// The 2D canvas.
//
// A canvas is the one place a page draws pixels directly. The pixels live in
// a store keyed by node_id rather than on the DOM node, for the same reason
// layout results do - the node is document content, and a mutable megapixel
// buffer is not.
//
// The drawing model is the spec's (HTML 4.12.5.1): a current transform, a path
// built in user space and transformed as it is BUILT, a paint (a colour, a
// gradient or a pattern) evaluated per pixel, the stroke traced into an outline
// and filled, and a state stack. Getting the transform timing wrong is the
// classic canvas bug - points must be transformed as they are ADDED to the
// path, not when it is filled, or a translate between moveTo and lineTo moves
// the wrong end of the line - while a stroke's WIDTH is in the space of the
// transform in force when stroke() is called.

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
    [[nodiscard]] bool invertible() const noexcept {
        const float det = a * d - b * c;
        return det != 0 && std::isfinite(det);
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
    // How far one user unit can stretch on screen - the larger singular value,
    // bounded above by the root of the squared column lengths. Used only to
    // pick how many segments a curve needs, so an over-estimate is harmless.
    [[nodiscard]] float scale_bound() const noexcept {
        return std::sqrt(std::max(a * a + b * b, c * c + d * d));
    }
};

// --- paint --------------------------------------------------------------
//
// What a fill or a stroke is painted WITH (4.12.5.1.10). A gradient is shared
// between the script object that made it and every state that holds it,
// because a stop added after the gradient was assigned still changes what the
// next draw paints - that is 2d.gradient.object.update, and it is the spec.

struct canvas_gradient {
    enum class kind : std::uint8_t {
        linear,
        radial,
        conic
    };
    struct stop {
        double offset = 0;
        // NOT premultiplied, 0..1: gradients interpolate "without
        // premultiplying the alpha value".
        std::array<float, 4> rgba{};
    };
    kind shape = kind::linear;
    // linear: (x0, y0) -> (x1, y1). radial: the two circles. conic: (x0, y0)
    // is the centre and `angle` the start angle.
    double x0 = 0, y0 = 0, r0 = 0, x1 = 0, y1 = 0, r1 = 0, angle = 0;
    // In the order they were added, stably sorted by offset - two stops at one
    // offset make a hard edge, and which is which is the order of addition.
    std::vector<stop> stops;

    void add_stop(double offset, color c);
    // The colour at a point in USER space, or nullopt where this gradient
    // paints nothing (a degenerate one, or outside a radial's cone).
    [[nodiscard]] std::optional<std::array<float, 4>> sample(double x, double y) const;
};

struct canvas_pattern {
    std::shared_ptr<const bitmap> image;
    bool repeat_x = true;
    bool repeat_y = true;
    // Pattern space to user space - `pattern.setTransform()`.
    transform matrix;
    // drawImage paints through a pattern too: the source rectangle, which a
    // sample at the very edge of the destination is clamped into rather than
    // reading the next sprite on the sheet. Unset for createPattern.
    std::optional<std::array<float, 4>> source_rect;
};

struct canvas_paint {
    color solid = color::rgba(0, 0, 0);
    std::shared_ptr<canvas_gradient> gradient;
    std::shared_ptr<const canvas_pattern> pattern;
    [[nodiscard]] bool is_solid() const noexcept { return !gradient && !pattern; }
};

// --- paths ---------------------------------------------------------------
//
// A path is a list of subpaths of DEVICE-space points: every verb transforms
// its points as it adds them, with whatever matrix it was handed. The
// context's own path is built through its current transform; a Path2D is
// replayed through the transform in force when it is drawn.
struct canvas_path {
    struct subpath {
        std::vector<point> points;
        bool closed = false;
    };
    std::vector<subpath> subpaths;

    [[nodiscard]] bool empty() const noexcept { return subpaths.empty(); }
};

// One corner's radii for roundRect, already validated and scaled.
struct corner_radius {
    double x = 0, y = 0;
};

// THE PATH VERBS, once, for both the context's path and a Path2D replay
// (4.12.5.1.14). Arguments are in user space; `ctm` maps them to the device.
// Callers have already rejected non-finite arguments and thrown for the
// negative radii the spec makes an IndexSizeError.
class path_builder {
public:
    path_builder(canvas_path & path, const transform & ctm) : path_(&path), ctm_(ctm) {}

    void move_to(double x, double y);
    void line_to(double x, double y);
    void close_path();
    void quadratic_curve_to(double cx, double cy, double x, double y);
    void bezier_curve_to(double c1x, double c1y, double c2x, double c2y, double x, double y);
    void arc_to(double x1, double y1, double x2, double y2, double radius);
    void arc(double x, double y, double radius, double from, double to, bool anticlockwise);
    void ellipse(double x, double y, double rx, double ry, double rotation, double from, double to,
                 bool anticlockwise);
    void rect(double x, double y, double w, double h);
    // Corners in the order top-left, top-right, bottom-right, bottom-left.
    void round_rect(double x, double y, double w, double h, std::array<corner_radius, 4> radii);

private:
    [[nodiscard]] point device(double x, double y) const noexcept {
        return ctm_.apply(static_cast<float>(x), static_cast<float>(y));
    }
    void ensure_subpath(double x, double y);
    void line_to_device(point p);
    // The last point of the current subpath, in USER space - what arcTo
    // measures its corner from.
    [[nodiscard]] std::optional<std::pair<double, double>> last_user_point() const;
    // Points along an elliptical arc, rotation included, joined to the path.
    void elliptic_arc(double x, double y, double rx, double ry, double rotation, double from,
                      double sweep);

    canvas_path * path_;
    transform ctm_;
};

// One canvas's pixels and drawing state.
class canvas_context {
public:
    enum class fill_rule : std::uint8_t {
        nonzero,
        even_odd
    };
    enum class line_cap : std::uint8_t {
        butt,
        round,
        square
    };
    enum class line_join : std::uint8_t {
        round,
        bevel,
        miter
    };

    // THE DRAWING STATE (4.12.5.1.2): everything save() pushes and restore()
    // pops, in one struct so the two cannot disagree about what it contains -
    // the list of fields copied by hand is where the old stack lost the font
    // family, then the text alignment, then the composite operator.
    //
    // Every attribute is kept TWICE where the spec's getter has to give back
    // text: parsed, for drawing, and as the string the getter returns.
    struct state {
        transform matrix;
        canvas_paint fill;
        canvas_paint stroke;
        std::string fill_spec = "#000000";
        std::string stroke_spec = "#000000";
        double line_width = 1;
        line_cap cap = line_cap::butt;
        line_join join = line_join::miter;
        double miter_limit = 10;
        std::vector<double> dash;
        double dash_offset = 0;
        double alpha = 1;
        composite composite_mode = composite::source_over;
        std::string composite_spec = "source-over";
        // `clear`, which composite.hpp has no operator for: drawn as `copy`
        // with a transparent source, which is the same Porter-Duff pair.
        bool clear_operator = false;
        color shadow_color = color{0};
        std::string shadow_spec = "rgba(0, 0, 0, 0)";
        double shadow_blur = 0;
        double shadow_x = 0;
        double shadow_y = 0;
        bool image_smoothing = true;
        std::string smoothing_quality = "low";
        std::string filter = "none";
        // The FAMILY is part of the state, not just the size. It was dropped
        // on the floor once - `ctx.font = "16px Arial"` kept the 16 and threw
        // "Arial" away - so every canvas drew in the bitmap font.
        float font_size = 10;
        std::string font_family;
        bool font_bold = false;
        bool font_italic = false;
        std::string font_spec = "10px sans-serif";
        // WHERE THE TEXT SITS RELATIVE TO THE POINT IT WAS GIVEN. p5.js alone
        // sets textAlign 229 times. Stored as the spec's keywords; the setters
        // refuse anything else, so the drawing code can compare strings.
        std::string text_align = "start";
        std::string text_baseline = "alphabetic";
        std::string direction = "inherit";
        std::string letter_spacing = "0px";
        std::string word_spacing = "0px";
        std::string font_kerning = "auto";
        std::string font_stretch = "normal";
        std::string font_variant_caps = "normal";
        std::string text_rendering = "auto";
        std::string lang = "inherit";
        // The clip region in force. A clip is undone by restore() and by
        // nothing else, so it belongs on this stack like the rest of the state.
        // A per-pixel mask rather than a path list: the region is an
        // INTERSECTION of arbitrary paths, and intersecting two scanline
        // polygons exactly is a clipping-polygon algorithm, where a mask is one
        // AND per pixel. Shared, so save() copies a pointer and not a buffer.
        std::shared_ptr<const std::vector<std::uint8_t>> clip;
        // The bitmap size the mask was built for. A surface re-dimensioned
        // under a live clip leaves the mask the wrong shape, and indexing it
        // with the new width reads past its end - so a mismatch is clipped out.
        int clip_width = 0;
        int clip_height = 0;
    };

    canvas_context(std::shared_ptr<bitmap> pixels, const raster::font_backend * fonts = nullptr)
        : pixels_(std::move(pixels)), fonts_(fonts) {}

    [[nodiscard]] const std::shared_ptr<bitmap> & surface() const noexcept { return pixels_; }
    [[nodiscard]] int width() const noexcept { return pixels_ ? pixels_->width : 0; }
    [[nodiscard]] int height() const noexcept { return pixels_ ? pixels_->height : 0; }

    // Everything a script has drawn is recorded here so the browser knows the
    // tile needs redrawing. A canvas that changes without saying so shows a
    // stale frame, which is the failure everyone hits once.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    // SOMETHING ELSE DREW INTO THESE PIXELS. A canvas has one surface whichever
    // context it handed out, and a WebGL draw writes it through
    // raster::draw_triangles rather than through any method here - so it has to
    // say so, or the revision does not move and the browser shows the frame
    // before. Public for exactly that caller; everything inside touches its own.
    void note_foreign_draw() { touch(); }

    [[nodiscard]] state & current() noexcept { return state_; }
    [[nodiscard]] const state & current() const noexcept { return state_; }

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

    // New pixels at a new size, IN PLACE. Setting canvas.width or height - even
    // to the value it already has, which is the `canvas.width = canvas.width`
    // idiom - resets the bitmap AND the context (4.12.5 "set bitmap
    // dimensions"). The object has to survive it: script holds a context whose
    // every method finds this address.
    void reset_surface(int width, int height);
    // `ctx.reset()` (4.12.5.1.3): transparent black, an empty path and stack,
    // every attribute at its default.
    void reset();

    void save();
    void restore();
    // How many states save() has pushed and restore() not yet popped - the
    // level a binding keeps its per-state script objects at.
    [[nodiscard]] std::size_t depth() const noexcept { return stack_.size(); }
    void translate(float x, float y) { concat(transform::translation(x, y)); }
    void scale(float x, float y) { concat(transform::scaling(x, y)); }
    void rotate(float radians) { concat(transform::rotation(radians)); }
    void reset_transform() { state_.matrix = transform{}; }
    // REPLACE the matrix, rather than compose with it. A library that keeps its
    // own transform stack - p5.js does, and calls this once per frame from
    // resetMatrix() - needs to be able to say what the matrix IS.
    void set_transform(const transform & t) { state_.matrix = t; }
    void multiply_transform(const transform & t) { concat(t); }
    [[nodiscard]] const transform & current_transform() const noexcept { return state_.matrix; }

    // --- rectangles -------------------------------------------------------
    //
    // None of the three touches the current path: each builds its own.

    void fill_rect(float x, float y, float w, float h);
    void clear_rect(float x, float y, float w, float h);
    void stroke_rect(float x, float y, float w, float h);

    // --- the current default path -----------------------------------------

    void begin_path() { path_.subpaths.clear(); }
    [[nodiscard]] path_builder build() { return path_builder{path_, state_.matrix}; }
    [[nodiscard]] const canvas_path & path() const noexcept { return path_; }
    [[nodiscard]] canvas_path & path() noexcept { return path_; }

    // Scanline fill under either rule, of the current path or of `path`.
    void fill(fill_rule rule = fill_rule::nonzero) { fill(path_, rule); }
    void fill(const canvas_path & path, fill_rule rule);
    void stroke() { stroke(path_); }
    void stroke(const canvas_path & path);
    // INTERSECT the path into the clip region. Everything drawn afterwards is
    // confined to it, and `restore()` puts the old region back - which is the
    // only way a page ever removes one.
    void clip(fill_rule rule = fill_rule::nonzero) { clip(path_, rule); }
    void clip(const canvas_path & path, fill_rule rule);
    // (x, y) in CSS pixels on the canvas - NOT through the transform.
    [[nodiscard]] bool is_point_in_path(const canvas_path & path, double x, double y,
                                        fill_rule rule) const;
    [[nodiscard]] bool is_point_in_stroke(const canvas_path & path, double x, double y) const;
    // drawFocusIfNeeded's ring along `path`: two device pixels of the focus
    // colour, subject to the clip and to nothing else of the state - no
    // shadow, alpha, operator or line style (4.12.5.1.15).
    void draw_focus_ring(const canvas_path & path);

    // --- text and images --------------------------------------------------

    // Drawn through the same font backend the page text around it uses, in the
    // family `ctx.font` asked for, so a canvas and the document look like one
    // page - and so that measureText and fillText agree. `y` is the BASELINE,
    // per spec. With no real fonts loaded the backend is font8x8.
    void fill_text(std::string_view text, float x, float y);
    void stroke_text(std::string_view text, float x, float y);

    // drawImage in its nine-argument shape: a rectangle out of the source, into
    // a rectangle on the canvas, through the transform. The shorter forms are
    // the same call with the whole source. Negative sizes flip to the positive
    // rectangle and the source is clipped to the image, per 4.12.5.1.16.
    void draw_image(const std::shared_ptr<const bitmap> & source, double sx, double sy, double sw,
                    double sh, double dx, double dy, double dw, double dh);

    // Whether a draw casts a shadow (4.12.5.1.18): a colour that is not
    // transparent, and a blur or an offset that is not zero.
    [[nodiscard]] bool shadows_drawn() const noexcept;

private:
    void touch() { ++revision_; }
    void draw_text(std::string_view text, float x, float y, bool stroke);
    // The glyphs as a coverage mask over the canvas rectangle (left, top, w,
    // h), painted - or, for a stroke, traced `reach` either side and painted.
    void paint_glyphs(const std::string & text, point origin, float ascent, float advance,
                      double reach, int pad, int left, int top, int w, int h, bool stroke);
    void concat(const transform & t) { state_.matrix = t.then(state_.matrix); }

    // Walk a path a scanline at a time and hand each inside-span, as the
    // first and last pixel whose CENTRE is inside, to `emit(y, x0, x1)`.
    // Shared by fill, stroke and clip so the winding rule has ONE
    // implementation.
    // Rows outside [top, bottom] are not visited; columns are not clipped.
    void for_each_span(const canvas_path & path, fill_rule rule, int top, int bottom,
                       const std::function<void(int, int, int)> & emit) const;

    // Paint the spans of `path` with `paint` under the full drawing model:
    // shadow, global alpha, clip and composite operator.
    void paint_path(const canvas_path & path, fill_rule rule, const canvas_paint & paint);

    // The outline a stroke of `path` covers, in device space: every segment,
    // join, cap and dash, each piece wound the same way so that a nonzero fill
    // of the lot is their union. `cull` is the device area the caller looks
    // at - the canvas, or the one point a hit test asks about - and a DASH
    // that cannot reach it is not built.
    [[nodiscard]] canvas_path stroke_outline(const canvas_path & path, const rect & cull) const;

    // The colours of one row of pixels under a gradient or a pattern.
    void sample_row(const canvas_paint & paint, int y, int x0, int x1,
                    std::vector<std::uint32_t> & out) const;

    // A COMPOSITE PASS, for the operators where a pixel the source never
    // touched still has to change (see clears_untouched). The backdrop is
    // snapshotted and the surface cleared, so an untouched pixel ends up
    // transparent - which is the formula's own answer for as = 0 - and a touched
    // one composites against the snapshot. Outside the clip region nothing may
    // change at all, so the destructor puts those pixels back.
    class pass {
    public:
        explicit pass(canvas_context & canvas);
        pass(const pass &) = delete;
        pass & operator=(const pass &) = delete;
        ~pass();

    private:
        canvas_context * canvas_;
    };

    // What a write composites AGAINST: the snapshot during a clearing pass, and
    // the surface itself otherwise.
    [[nodiscard]] std::uint32_t backdrop_at(int x, int y) const {
        return backdrop_ ? backdrop_->at(x, y) : pixels_->at(x, y);
    }
    std::shared_ptr<const bitmap> backdrop_;

    [[nodiscard]] color with_alpha(color c) const;
    // One source pixel onto the surface, already alpha-multiplied.
    [[nodiscard]] std::uint32_t composite_one(int x, int y, std::uint32_t dst, color source) const;

    // ONE ROW AT A TIME, which is how the filled shapes actually arrive. The
    // per-row work - clip row, global alpha, bounds - is hoisted out of the
    // pixel loop; it was 22.9% of a Phaser frame when it was not (see
    // docs/performance.md). For an opaque backdrop the arithmetic is the
    // blend_over it always was, byte for byte, which is what keeps the p5 and
    // Phaser goldens where they are.
    void blend_span(int y, int x0, int x1, color c);
    // The same for a row of per-pixel colours (a gradient, a pattern).
    void blend_row(int y, int x0, int x1, const std::uint32_t * colors);
    // The opaque write path (`clearRect`).
    void write_span(int y, int x0, int x1, std::uint32_t argb);

    // A clip in force that was built for another bitmap size (see
    // state::clip_width): everything is outside it.
    [[nodiscard]] bool clip_stale() const noexcept {
        return state_.clip && (state_.clip_width != width() || state_.clip_height != height());
    }
    [[nodiscard]] bool clipped_out(int x, int y) const noexcept {
        if (!state_.clip) { return false; }
        if (clip_stale()) { return true; }
        if (x < 0 || y < 0 || x >= width() || y >= height()) { return true; }
        return (*state_.clip)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width()) +
                              static_cast<std::size_t>(x)] == 0;
    }

    // The shadow of a shape (4.12.5.1.18): its coverage times the paint's
    // alpha, blurred, offset and drawn in shadowColor under the same clip and
    // operator - before the shape itself.
    void draw_shadow(const canvas_path & path, fill_rule rule, const canvas_paint & paint);

    std::shared_ptr<bitmap> pixels_;
    const raster::font_backend * fonts_ = nullptr;
    state state_;
    canvas_path path_;
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
    // the drawing state back to its defaults - EVEN TO THE SAME SIZE: that is
    // the spec, and `canvas.width = canvas.width` is how a page clears one.
    void resize(node_id id, int width, int height);
    [[nodiscard]] const canvas_context * find(node_id id) const;
    [[nodiscard]] canvas_context * find(node_id id);
    [[nodiscard]] std::shared_ptr<const bitmap> pixels_of(node_id id) const;
    // Sum of every canvas's revision. The browser compares it between frames to
    // decide whether anything was drawn - cheaper than asking each one, and it
    // cannot miss a change because revisions only ever increase.
    [[nodiscard]] std::uint64_t total_revision() const;
    void clear() { canvases_.clear(); }

private:
    // A CONTEXT'S ADDRESS IS STABLE FOR AS LONG AS ITS CANVAS EXISTS. Holding
    // them by value in the map meant that creating a SECOND canvas reallocated
    // the storage and left the first page's context pointing at freed memory,
    // and p5.js makes two before it draws anything.
    flat_map<std::uint64_t, std::unique_ptr<canvas_context>> canvases_;
    const raster::font_backend * fonts_ = nullptr;
};

} // namespace ctbrowser::shell
