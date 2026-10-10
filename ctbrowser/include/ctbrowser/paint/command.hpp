//===- command.hpp - The display list ------------------------- -*- C++ -*-===//
//
// The display list: what to draw, recorded once and then never changed.
//
// The list outlives the frame that recorded it. Without that, every scroll,
// caret blink and hover would have to re-run layout to get a fresh list; with
// it, the compositor moves a layer and re-composites, and nothing is
// re-recorded.
//
// Immutability is what makes that safe. A recorded list is handed out as
// shared_ptr<const display_list>, so the raster threads, the compositor and
// hit testing can all read the same one while the next frame is being
// recorded beside it.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/values.hpp>

namespace ctbrowser::paint {

using ctbrowser::color;
using ctbrowser::node_id;
using ctbrowser::point;
using ctbrowser::rect;

/// What a paint_command does.
enum class paint_op : std::uint8_t {
    fill_rect,    ///< A solid (possibly translucent, rounded or ring) box.
    fill_ellipse, ///< The same, bounded by an ellipse; a radio button is round.
    text_run,     ///< One visual line of text, already broken by layout.
    image,        ///< A bitmap, such as a <canvas>.
    push_clip,    ///< Intersect the clip with `bounds` until the matching pop.
    pop_clip,
};

/// Pixels a paint command draws, in the same 0xAARRGGBB the raster uses.
///
/// Shared and mutable, a deliberate exception to the display list being
/// immutable. A <canvas> is content a script draws into continuously;
/// snapshotting it into every recording would copy a megapixel per frame. A
/// canvas is instead re-rastered when it changes, which the browser marks.
///
/// The cost: a canvas drawn into between recording and rastering shows its new
/// contents, not the ones that were recorded. That is what a browser does too.
struct bitmap {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels; // width * height

    bitmap() = default;
    bitmap(int w, int h)
        : width(w < 0 ? 0 : w), height(h < 0 ? 0 : h),
          pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0) {}

    [[nodiscard]] bool empty() const noexcept { return pixels.empty(); }
    [[nodiscard]] std::uint32_t at(int x, int y) const noexcept {
        if (x < 0 || y < 0 || x >= width || y >= height) { return 0; }
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x)];
    }
    void put(int x, int y, std::uint32_t argb) noexcept {
        if (x < 0 || y < 0 || x >= width || y >= height) { return; }
        pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x)] = argb;
    }
};

/// The face a run is drawn in. It is carried on the command because the
/// rasterizer has no way back to the element: by the time a tile is drawn, the
/// DOM and the cascade are out of reach.
///
/// `family` is the resolved name, not the CSS list: "Fira Sans", not
/// "Fira Sans, Helvetica, sans-serif". Layout chooses among the alternatives
/// once, rather than the rasterizer doing it per glyph. It is layout's type
/// because paint already depends on layout.
using font_face = layout::text_face;

/// The line a text run is decorated with.
enum class text_decoration : std::uint8_t {
    none,
    underline,
    line_through
};

/// `border-radius`, per corner, already resolved to px and already scaled so
/// that two radii on one side cannot exceed it (CSS Backgrounds 3 §5.1).
/// Bootstrap's `.rounded-pill` asks for 50rem (800px) on a 20px-tall badge, and
/// the pill shape depends on that being cut down to half the height rather
/// than clamped per corner or ignored.
struct corner_radii {
    float top_left = 0, top_right = 0, bottom_right = 0, bottom_left = 0;

    [[nodiscard]] bool empty() const noexcept {
        return top_left <= 0 && top_right <= 0 && bottom_right <= 0 && bottom_left <= 0;
    }
    /// Every radius reduced by `by`, floored at zero: the inner edge of a border
    /// that thick. CSS Backgrounds 3 §5.2: the inner curve's radius is the outer
    /// one less the border width, and a border thicker than its radius squares
    /// the corner off rather than curving it the other way.
    [[nodiscard]] corner_radii inset(float by) const noexcept {
        const auto less = [by](float r) { return r - by > 0 ? r - by : 0.0f; };
        return corner_radii{less(top_left), less(top_right), less(bottom_right), less(bottom_left)};
    }
    [[nodiscard]] friend bool operator==(const corner_radii &, const corner_radii &) = default;
};

/// One recorded drawing operation. Which fields matter depends on `op`.
struct paint_command {
    paint_op op = paint_op::fill_rect;
    rect bounds;          // fill: the box. text: the run's box. clip: the region.
    color fill;           // fill: the colour. text: the text colour.
    float font_size = 16; // text only
    font_face face;       // text only
    text_decoration decoration = text_decoration::none; // text only
    std::string text;                                   // text only, UTF-8
    std::shared_ptr<const bitmap> pixels;               // image only
    /// Fill only; zero radii and zero ring are a plain rectangle.
    ///
    /// Radii ride on fill_rect rather than being a second op, the way
    /// `decoration` rides on text_run: a rounded fill is a fill, a consumer that
    /// ignores the radii still draws the right rectangle in the right place,
    /// and the rasterizer takes a fast path when they are zero.
    corner_radii radii;
    /// Fill only: draw a ring this thick along the inside edge, hollow within,
    /// which is what a rounded border is and what four edge rectangles cannot
    /// be. Zero means a solid fill.
    ///
    /// A border cannot be drawn as "fill the box in the border colour, then
    /// fill the inside in the background colour": `.btn-outline-primary` has a
    /// transparent background, so that would flood the button.
    float ring = 0;
    node_id source; // painted-command provenance, for inspection and debugging goldens

    [[nodiscard]] friend bool operator==(const paint_command &, const paint_command &) = default;
};

/// A box that participates in pointer hit testing, in the same back-to-front
/// order as the commands beside it.
///
/// This is deliberately separate from paint_command. A transparent element is
/// still clickable, while `fill()` correctly records no command for a
/// transparent colour. Recording the region explicitly keeps hit testing tied
/// to stacking order without inventing invisible drawing operations.
struct hit_region {
    rect bounds;
    node_id source;

    [[nodiscard]] friend bool operator==(const hit_region &, const hit_region &) = default;
};

/// A recorded, back-to-front list of paint commands plus the hit regions
/// beside them.
class display_list {
public:
    /// Records `where` as a hit region for `source`, clipped to the current
    /// clip.
    void hit(const rect & where, node_id source) {
        if (where.empty() || !source) { return; }
        const rect clipped = hit_clips_.empty() ? where : where.intersected(hit_clips_.back());
        if (!clipped.empty()) { hit_regions_.push_back(hit_region{clipped, source}); }
    }

    /// Records a solid rectangle. It is the rounded fill with no radius; the
    /// rasterizer takes its fast path for that command.
    void fill(const rect & where, color c, node_id source = {}) {
        fill_rounded(where, c, {}, 0, source);
    }

    /// Records a rounded fill, optionally a ring rather than a solid: a rounded
    /// background is `ring = 0`, a rounded border is `ring = the border width`.
    ///
    /// The radii are scaled here, in the one place every producer goes
    /// through, so no caller can record radii the rasterizer would have to
    /// defend itself against. CSS Backgrounds 3 §5.1: if the two radii on any
    /// side add up to more than that side, every radius is multiplied by the
    /// smallest factor that makes them all fit. Scaling per corner instead
    /// would round a wide pill into a circle.
    void fill_rounded(const rect & where, color c, corner_radii radii, float ring = 0,
                      node_id source = {}) {
        if (where.empty() || c.transparent()) { return; }
        float scale = 1.0f;
        const auto limit = [&scale](float side, float a, float b) {
            if (a + b > 0 && a + b > side) { scale = std::min(scale, side / (a + b)); }
        };
        limit(where.width, radii.top_left, radii.top_right);
        limit(where.width, radii.bottom_left, radii.bottom_right);
        limit(where.height, radii.top_left, radii.bottom_left);
        limit(where.height, radii.top_right, radii.bottom_right);
        if (scale < 1.0f) {
            radii = corner_radii{radii.top_left * scale, radii.top_right * scale,
                                 radii.bottom_right * scale, radii.bottom_left * scale};
        }
        paint_command cmd;
        cmd.op = paint_op::fill_rect;
        cmd.bounds = where;
        cmd.fill = c;
        cmd.radii = radii;
        cmd.ring = ring > 0 ? ring : 0.0f;
        cmd.source = source;
        commands_.push_back(std::move(cmd));
        bounds_ = bounds_.united(where);
    }

    /// Records a filled ellipse inscribed in `where`, for radio buttons, which
    /// drawn square would look like checkboxes. It does not extend bounds().
    void fill_ellipse(const rect & where, color c, node_id source = {}) {
        if (where.empty() || c.transparent()) { return; }
        paint_command cmd;
        cmd.op = paint_op::fill_ellipse;
        cmd.bounds = where;
        cmd.fill = c;
        cmd.source = source;
        commands_.push_back(std::move(cmd));
    }

    /// Records one line of text in `where`.
    void text(const rect & where, std::string run, float font_size, color c, node_id source = {},
              font_face face = {}, text_decoration decoration = text_decoration::none) {
        if (run.empty() || c.transparent()) { return; }
        // Field by field rather than positionally, so adding a field to
        // paint_command cannot silently shift the others.
        paint_command cmd;
        cmd.op = paint_op::text_run;
        cmd.bounds = where;
        cmd.fill = c;
        cmd.font_size = font_size;
        cmd.face = std::move(face);
        cmd.decoration = decoration;
        cmd.text = std::move(run);
        cmd.source = source;
        commands_.push_back(std::move(cmd));
        bounds_ = bounds_.united(where);
    }

    /// Records a bitmap scaled into `where`. Nearest-neighbour at raster time,
    /// and 1:1 in the common case, since a canvas is laid out at its own pixel
    /// size.
    void draw_image(const rect & where, std::shared_ptr<const bitmap> image, node_id source = {}) {
        if (where.empty() || !image || image->empty()) { return; }
        paint_command cmd;
        cmd.op = paint_op::image;
        cmd.bounds = where;
        cmd.pixels = std::move(image);
        cmd.source = source;
        commands_.push_back(std::move(cmd));
        bounds_ = bounds_.united(where);
    }

    /// Intersects the clip with `where` until the matching pop_clip().
    void push_clip(const rect & where) {
        paint_command cmd;
        cmd.op = paint_op::push_clip;
        cmd.bounds = where;
        commands_.push_back(std::move(cmd));
        hit_clips_.push_back(hit_clips_.empty() ? where : hit_clips_.back().intersected(where));
    }
    void pop_clip() {
        paint_command cmd;
        cmd.op = paint_op::pop_clip;
        commands_.push_back(std::move(cmd));
        if (!hit_clips_.empty()) { hit_clips_.pop_back(); }
    }

    [[nodiscard]] std::span<const paint_command> commands() const noexcept { return commands_; }
    /// The topmost hit region containing `at`, or an empty id.
    [[nodiscard]] node_id hit_test(point at) const noexcept {
        for (const hit_region & region : std::views::reverse(hit_regions_)) {
            if (region.bounds.contains(at)) { return region.source; }
        }
        return node_id{};
    }

    /// Multiplies the alpha of every command recorded since index `from` by
    /// `factor`; this is how `opacity` applies to a subtree. Callers note
    /// size() before recording the subtree. Doing it here covers backgrounds,
    /// borders, text, markers and replaced content without any of them knowing
    /// about opacity.
    ///
    /// Approximation: CSS fades the composited group, so overlapping commands
    /// inside the subtree show through each other; fading each command instead
    /// makes overlaps darker. The two agree when the subtree does not overlap
    /// itself. A true group needs an off-screen surface in the rasterizer.
    void fade_from(std::size_t from, float factor) {
        if (factor >= 1 || from >= commands_.size()) { return; }
        const float clamped = factor < 0 ? 0.0f : factor;
        for (std::size_t i = from; i < commands_.size(); ++i) {
            const color c = commands_[i].fill;
            commands_[i].fill = color::rgba(
                c.red(), c.green(), c.blue(),
                static_cast<std::uint8_t>(static_cast<float>(c.alpha()) * clamped + 0.5f));
        }
    }
    [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }
    [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }

    /// The union of the fill, text and image commands recorded: the layer's
    /// content extent, and what decides which tiles a layer needs.
    /// fill_ellipse does not extend it.
    [[nodiscard]] const rect & bounds() const noexcept { return bounds_; }

    /// Everything this list would draw inside `region`. Tile rasterization asks
    /// this so a tile only pays for the commands that touch it.
    [[nodiscard]] std::vector<paint_command> intersecting(const rect & region) const {
        std::vector<paint_command> out;
        int depth_skipped = 0;
        for (const paint_command & c : commands_) {
            switch (c.op) {
            case paint_op::push_clip:
                // A clip that misses the region excludes everything inside it,
                // so the whole group can be dropped rather than clipped away
                // pixel by pixel later. Once inside a dropped group every nested
                // clip must deepen too, or the matching pops unbalance the count.
                if (depth_skipped > 0 || !c.bounds.intersects(region)) {
                    ++depth_skipped;
                } else {
                    out.push_back(c);
                }
                break;
            case paint_op::pop_clip:
                if (depth_skipped > 0) {
                    --depth_skipped;
                } else {
                    out.push_back(c);
                }
                break;
            default:
                if (depth_skipped == 0 && c.bounds.intersects(region)) { out.push_back(c); }
                break;
            }
        }
        return out;
    }

private:
    std::vector<paint_command> commands_;
    std::vector<hit_region> hit_regions_;
    // The effective intersection at each active clip depth while the mutable
    // list is being recorded. Regions store that intersection directly, so a
    // shared const list needs no clip-stack replay for every pointer move.
    std::vector<rect> hit_clips_;
    rect bounds_;
};

} // namespace ctbrowser::paint
