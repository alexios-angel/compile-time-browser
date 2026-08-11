#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

// The display list: what to draw, recorded once and then never changed.
//
// This is the object the previous engine never had. the previous engine's layout returned a
// std::vector<paint_cmd> that the shell consumed immediately and threw away, so every scroll, every
// caret blink and every hover re-ran the whole layout to get a new one. A
// display list that outlives the frame changes what scrolling costs: the
// compositor moves a layer and re-composites, and nothing is re-recorded.
//
// Immutability is what makes that safe rather than merely possible. A recorded
// list is handed out as shared_ptr<const display_list>, so the raster threads,
// the compositor and hit testing can all read the same one while the next
// frame is being recorded beside it.

namespace ctbrowser::paint {

using ctbrowser::color;
using ctbrowser::node_id;
using ctbrowser::rect;

enum class paint_op : std::uint8_t {
    fill_rect,    // a solid (possibly translucent) box
    fill_ellipse, // the same, bounded by an ellipse - a radio button is round
    text_run,     // one visual line of text, already broken by layout
    image,        // a bitmap - a <canvas>, and later an <img>
    push_clip,    // intersect the clip with `bounds` until the matching pop
    pop_clip,
};

// Pixels a paint command draws, in the same 0xAARRGGBB the raster uses.
//
// SHARED and MUTABLE, which is a deliberate exception to the display list being
// immutable. A <canvas> is content a script draws into continuously; snapshotting
// it into every recording would copy a megapixel per frame to preserve a
// property nothing needs here. What the immutability buys elsewhere - a recording
// the compositor can re-read while the next one is built - a canvas gets by being
// re-rastered when it changes, which the browser marks.
//
// The honest cost: a canvas drawn into between recording and rastering shows its
// NEW contents, not the ones that were recorded. That is what a browser does too.
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

// WHICH face a run is drawn in. Carried on the command because the rasterizer
// has no way back to the element: by the time a tile is drawn, the DOM and the
// cascade are behind it. the previous engine carried exactly these four things for the same
// reason.
//
// `family` is the resolved name, not the CSS list - "Fira Sans", not
// "Fira Sans, Helvetica, sans-serif". Choosing among the alternatives is
// layout's job, and doing it once there rather than per tile is the difference
// between resolving a font list once and resolving it for every glyph.
struct font_face {
    std::string family; // "" = whatever the backend calls its default
    bool bold = false;
    bool italic = false;

    [[nodiscard]] friend bool operator==(const font_face &, const font_face &) = default;
};

enum class text_decoration : std::uint8_t {
    none,
    underline,
    line_through
};

// `border-radius`, per corner, already resolved to px and already SCALED so that
// two radii on one side cannot exceed it (CSS Backgrounds 3 §5.1). The scaling is
// not an edge case here: Bootstrap's `.rounded-pill` asks for 50rem - 800px - on
// a 20px-tall badge, and the whole visual effect depends on that being cut down
// to half the height rather than clamped per corner or ignored.
struct corner_radii {
    float top_left = 0, top_right = 0, bottom_right = 0, bottom_left = 0;

    [[nodiscard]] bool empty() const noexcept {
        return top_left <= 0 && top_right <= 0 && bottom_right <= 0 && bottom_left <= 0;
    }
    // Every radius reduced by `by`, floored at zero - the inner edge of a border
    // that thick. CSS Backgrounds 3 §5.2: the inner curve's radius is the outer
    // one less the border width, and a border thicker than its radius squares the
    // corner off rather than curving it the other way.
    [[nodiscard]] corner_radii inset(float by) const noexcept {
        const auto less = [by](float r) { return r - by > 0 ? r - by : 0.0f; };
        return corner_radii{less(top_left), less(top_right), less(bottom_right), less(bottom_left)};
    }
    [[nodiscard]] friend bool operator==(const corner_radii &, const corner_radii &) = default;
};

struct paint_command {
    paint_op op = paint_op::fill_rect;
    rect bounds;          // fill: the box. text: the run's box. clip: the region.
    color fill;           // fill: the colour. text: the text colour.
    float font_size = 16; // text only
    font_face face;       // text only
    text_decoration decoration = text_decoration::none; // text only
    std::string text;                                   // text only, UTF-8
    std::shared_ptr<const bitmap> pixels;               // image only
    // FILL ONLY, and both default to "an ordinary rectangle" so every existing
    // producer and every existing test is unaffected.
    //
    // Radii ride on fill_rect rather than arriving as a second op, the way
    // `decoration` rides on text_run: a rounded fill IS a fill, every consumer
    // that ignores the radii still draws the right rectangle in the right place,
    // and the rasterizer takes a fast path when they are zero. A parallel
    // `fill_round_rect` would make every switch two cases wide forever.
    corner_radii radii;
    // A RING that thick along the inside edge, hollow within - which is what a
    // rounded border is, and what four edge rectangles cannot be. Zero means a
    // solid fill.
    //
    // One op for both because a border and a background differ only in where the
    // paint stops: `.btn-outline-primary` is a transparent background with a 1px
    // ring, and drawing that as "fill the whole box in the border colour, then
    // fill the inside in the background colour" would flood the button.
    float ring = 0;
    node_id source; // provenance, for hit testing and for debugging goldens

    [[nodiscard]] friend bool operator==(const paint_command &, const paint_command &) = default;
};

class display_list {
public:
    void fill(const rect & where, color c, node_id source = {}) {
        if (where.empty() || c.transparent()) { return; } // nothing to draw, nothing to record
        paint_command cmd;
        cmd.op = paint_op::fill_rect;
        cmd.bounds = where;
        cmd.fill = c;
        cmd.source = source;
        commands_.push_back(std::move(cmd));
        bounds_ = bounds_.united(where);
    }

    // The rounded form, and optionally a RING rather than a solid: a rounded
    // background is `ring = 0`, a rounded border is `ring = the border width`.
    //
    // THE SCALING HAPPENS HERE, in the one place every producer goes through, so
    // no caller can record radii the rasterizer would have to defend itself
    // against. CSS Backgrounds 3 §5.1: if the two radii on any side add up to
    // more than that side, every radius is multiplied by the smallest factor
    // that makes them all fit. That is what turns `.rounded-pill`'s 800px into
    // half the box's height, and doing it per corner instead would round a wide
    // pill into a circle.
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

    // A filled ellipse inscribed in `where`. Radio buttons are the reason this
    // exists: drawn as a square they are indistinguishable from a checkbox, and
    // "round" is the entire visual difference between the two controls.
    void fill_ellipse(const rect & where, color c, node_id source = {}) {
        if (where.empty() || c.transparent()) { return; }
        paint_command cmd;
        cmd.op = paint_op::fill_ellipse;
        cmd.bounds = where;
        cmd.fill = c;
        cmd.source = source;
        commands_.push_back(std::move(cmd));
    }

    void text(const rect & where, std::string run, float font_size, color c, node_id source = {},
              font_face face = {}, text_decoration decoration = text_decoration::none) {
        if (run.empty() || c.transparent()) { return; }
        // Field by field rather than positionally: the command grew a face and
        // a decoration, and a positional initialiser silently shifts every
        // field after the one that was inserted.
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

    // A bitmap scaled into `where`. Nearest-neighbour at raster time, and 1:1
    // in the case that matters - a canvas is laid out at its own pixel size.
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

    void push_clip(const rect & where) {
        paint_command cmd;
        cmd.op = paint_op::push_clip;
        cmd.bounds = where;
        commands_.push_back(std::move(cmd));
    }
    void pop_clip() {
        paint_command cmd;
        cmd.op = paint_op::pop_clip;
        commands_.push_back(std::move(cmd));
    }

    [[nodiscard]] std::span<const paint_command> commands() const noexcept { return commands_; }

    // Multiply the alpha of everything recorded since `from` - paired with the
    // existing size(), which a caller notes before recording a subtree so it can
    // fade exactly what that subtree added - which is what
    // `opacity` does to a subtree.
    //
    // ONE PLACE RATHER THAN ONE PER COMMAND KIND, and that is the reason it is
    // here rather than at the eight sites in the recorder that pick a colour: it
    // catches the background, the border, the text, the list marker and whatever
    // the replaced painter drew, without any of them knowing opacity exists.
    //
    // THE APPROXIMATION, stated because it is real: CSS composites the subtree
    // into a group and fades the GROUP, so where two of its own commands overlap
    // the lower one shows through the upper. Fading each command instead makes
    // the overlap darker. The two are identical wherever a subtree does not
    // overlap itself, which is every disabled control Bootstrap fades; a true
    // group needs an off-screen surface per opacity, which is a rasterizer
    // change rather than a recorder one.
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

    // The union of everything recorded - the layer's content extent, and what
    // decides which tiles a layer needs.
    [[nodiscard]] const rect & bounds() const noexcept { return bounds_; }

    // Everything this list would draw inside `region`. Tile rasterization asks
    // this so a tile only pays for the commands that touch it, which is the
    // whole reason to tile at all.
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
    rect bounds_;
};

} // namespace ctbrowser::paint
