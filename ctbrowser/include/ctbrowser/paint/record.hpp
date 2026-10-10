//===- record.hpp - Fragment tree to display list ------------- -*- C++ -*-===//
//
// Recording: fragment tree in, display list out.
//
// Paint order is the CSS 2.1 Appendix E subset the box tree can express:
// context decoration; negative contexts; in-flow block decorations; inline,
// text, replaced and marker content; auto/zero positioned entries; positive
// contexts. There are no floats or outlines to form their two missing phases
// yet. Positioned integer z-index, integer-z flex items, opacity, transforms,
// fixed and sticky establish atomic contexts; positioned z-index:auto does not.
//
// Each stacking context is emitted parent-first, so a parent's background is
// never drawn over its children's text; only the chunks Appendix E moves are
// reordered. The display list is consumed back to front.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/style/style.hpp>

#include <ctbrowser/paint/command.hpp>
#include <ctbrowser/paint/layer.hpp>
#include <ctbrowser/paint/values.hpp>

namespace ctbrowser::paint {

using ctbrowser::atom;
using ctbrowser::atom_table;
using ctbrowser::color;
using ctbrowser::layout::fragment;
using ctbrowser::style::computed_style_ptr;

/// Records a laid-out fragment tree into a display list.
class recorder {
public:
    explicit recorder(atom_table & atoms)
        : background_(atoms.intern("background-color")), color_(atoms.intern("color")),
          border_color_(atoms.intern("border-color")), border_width_(atoms.intern("border-width")),
          border_style_(atoms.intern("border-style")), overflow_x_(atoms.intern("overflow-x")),
          overflow_y_(atoms.intern("overflow-y")), box_shadow_(atoms.intern("box-shadow")),
          border_width_sides_{atoms.intern("border-top-width"), atoms.intern("border-right-width"),
                              atoms.intern("border-bottom-width"),
                              atoms.intern("border-left-width")},
          border_style_sides_{atoms.intern("border-top-style"), atoms.intern("border-right-style"),
                              atoms.intern("border-bottom-style"),
                              atoms.intern("border-left-style")},
          border_color_sides_{atoms.intern("border-top-color"), atoms.intern("border-right-color"),
                              atoms.intern("border-bottom-color"),
                              atoms.intern("border-left-color")},
          radius_{atoms.intern("border-top-left-radius"), atoms.intern("border-top-right-radius"),
                  atoms.intern("border-bottom-right-radius"),
                  atoms.intern("border-bottom-left-radius")} {}

    /// The default text colour, when nothing in the cascade says otherwise.
    color default_text_color = color::rgba(0, 0, 0);

    /// Draws a replaced element. The recorder cannot know what one looks like:
    /// a <canvas>'s pixels live in the shell's canvas store, and a form
    /// control's appearance depends on its live value and focus. Asking keeps
    /// paint ignorant of script and widgets, with the display list the only
    /// thing they share.
    ///
    /// It receives two rects: the border box, and the content box the recorder
    /// already computed, so a control's text sits at its real content edge
    /// (stylesheet padding included) without a second hard-coded inset.
    using replaced_painter = std::function<void(node_id, const rect &, const rect &,
                                                const computed_style_ptr &, display_list &)>;
    replaced_painter paint_replaced;

    /// The highlighted part of a text fragment, in the fragment's own space, or
    /// an empty rect for none. A hook rather than a member on the fragment: a
    /// selection is a property of the browsing session, not of the layout, and
    /// the fragment tree is rebuilt by a relayout that a selection outlives.
    ///
    /// Called for every text run; the fill goes under the text, since a
    /// highlight drawn over it would hide what is selected.
    std::function<rect(const fragment &)> selection_of;

    /// Records the fragment tree under `root` into one display list.
    [[nodiscard]] std::shared_ptr<const display_list> record(const fragment & root) const;

    /// Records the whole page as one compositor layer. Stacking-context order
    /// is already resolved inside its display list. Fixed, sticky and scrolling
    /// content do not yet get layers of their own.
    [[nodiscard]] layer_tree record_layers(const fragment & root) const;

private:
    [[nodiscard]] std::string_view prop(const computed_style_ptr & s, atom name) const;

    /// A fragment together with everything a reordered paint needs to retain.
    /// Bounds remain relative in the fragment tree, so `dx`/`dy` locate its
    /// containing block. A stacking context may escape through several ordinary
    /// ancestors; their overflow clips and inherited text colour have to travel
    /// with it or the reordering silently changes both.
    struct paint_ref {
        const fragment * value = nullptr;
        float dx = 0;
        float dy = 0;
        color inherited_text;
        std::vector<rect> clips;
        std::size_t order = 0;
        int level = 0;
    };

    /// The child stacking contexts and positioned descendants a stacking
    /// context paints in their own phases.
    struct context_contents {
        std::vector<paint_ref> contexts;
        std::vector<paint_ref> positioned;
    };

    [[nodiscard]] color text_color_of(const paint_ref & ref) const;
    [[nodiscard]] rect box_of(const paint_ref & ref) const;
    [[nodiscard]] bool clips_children(const fragment & f) const;
    [[nodiscard]] static bool is_flex_item(const fragment & parent, const fragment & child);
    [[nodiscard]] bool establishes_stacking_context(const fragment & f,
                                                    bool flex_item = false) const;
    [[nodiscard]] static int stack_level(const fragment & f, bool flex_item = false);
    [[nodiscard]] static bool is_normal_atomic(const fragment & parent, const fragment & child);
    [[nodiscard]] paint_ref child_ref(const paint_ref & parent, const fragment & child,
                                      color inherited_text) const;

    void collect_context_contents(const paint_ref & parent, std::span<const rect> clips,
                                  std::size_t & order, context_contents & out) const;
    void emit_stacking_context(const paint_ref & root, display_list & into) const;
    void emit_normal_backgrounds(const paint_ref & parent, display_list & into) const;
    void emit_normal_contents(const paint_ref & parent, display_list & into) const;
    void emit_normal_atomic(const paint_ref & ref, display_list & into) const;
    static void push_clips(std::span<const rect> clips, display_list & into);
    static void pop_clips(std::size_t count, display_list & into);

    /// Emits one fragment's box decoration only. Text, generated markers and
    /// replaced content are later Appendix E content; descendants are emitted
    /// by the phase walkers above rather than recursively in source order.
    void emit_decoration(const paint_ref & ref, color text_color, display_list & into) const {
        const fragment & f = *ref.value;
        const rect box = box_of(ref);
        const computed_style_ptr style = f.box != nullptr ? f.box->style : computed_style_ptr{};
        // A pseudo-element's box hits as its originating element.
        into.hit(box, f.source ? f.source : f.box != nullptr ? f.box->origin : node_id{});

        if (!f.text.empty()) { return; }

        // The background and the border share a shape, so the radius is
        // resolved once and both are drawn against it. `radii.empty()` is the
        // fast path all the way down to the rasterizer.
        const corner_radii radii = radii_of(style, box);
        // Paint order: an outer shadow is behind everything the box draws, an
        // inset one is in front of the background and behind the border.
        // Bootstrap 5.3 paints table cell backgrounds (`.table-striped`,
        // `.table-hover`, themed rows) with `inset 0 0 0 9999px <colour>`.
        const std::vector<box_shadow> shadows = parse_box_shadow(prop(style, box_shadow_));
        for (const box_shadow & shadow : shadows) {
            if (shadow.inset || !shadow.sharp()) { continue; }
            emit_shadow(box, radii, shadow, f.source, into);
        }
        if (const auto bg = parse_color(prop(style, background_))) {
            into.fill_rounded(box, *bg, radii, 0, f.source);
        }
        for (const box_shadow & shadow : shadows) {
            if (!shadow.inset || !shadow.sharp()) { continue; }
            emit_shadow(box, radii, shadow, f.source, into);
        }
        emit_border(box, style, radii, f.source, text_color, into);
        // `<table border=1>`: a presentational attribute, not CSS, and one that
        // draws a frame around the table and around every cell, which is what
        // makes a bordered table read as a grid.
        if (f.box != nullptr && f.box->border_px > 0) {
            stroke(box, f.box->border_px, color{ctbrowser::style::ua_table_border}, f.source, into);
        }
    }

    /// Emits the content owned by one fragment: its text, list marker or
    /// replaced content. A list marker is generated inline-level content, not
    /// part of the list item's background/border phase. Keeping it here is what
    /// lets negative contexts paint above the owner's decoration but below its
    /// marker and text.
    void emit_own_content(const paint_ref & ref, color text_color, display_list & into) const {
        const fragment & f = *ref.value;
        const rect box = box_of(ref);
        const computed_style_ptr style = f.box != nullptr ? f.box->style : computed_style_ptr{};

        if (!f.text.empty()) {
            // A text fragment is one visual line (layout already broke it), so
            // the run needs no further measurement here. The face comes along
            // because the rasterizer has no way back to the element: by the
            // time a tile is drawn there is no cascade left to ask.
            font_face face;
            text_decoration decoration = text_decoration::none;
            float size = 16;
            if (f.box != nullptr) {
                size = f.box->font_size;
                face = f.box->face;
                // Underline wins when a page asks for both, which is what a
                // browser does and what `text-decoration: underline
                // line-through` most often means in practice.
                decoration = f.box->underline      ? text_decoration::underline
                             : f.box->line_through ? text_decoration::line_through
                                                   : text_decoration::none;
            }
            if (selection_of) {
                const rect selected = selection_of(f);
                if (!selected.empty()) {
                    into.fill(rect{box.x + selected.x, box.y + selected.y, selected.width,
                                   selected.height},
                              color{ctbrowser::style::ua_selection_highlight}, f.source);
                }
            }
            into.text(box, f.text, size, text_color, f.source, std::move(face), decoration);
            return;
        }

        emit_marker(f, box, text_color, into);

        // Replaced elements paint themselves and have no laid-out children, so
        // the recursion stops here.
        if (f.box != nullptr && f.box->is_replaced()) {
            if (paint_replaced) {
                const layout::resolved_edges e = layout::resolve_edges(
                    *f.box, layout::constraints{box.width, box.height, f.box->font_size});
                const rect content{box.x + e.content_left(), box.y + e.content_top(),
                                   std::max(0.0f, box.width - e.horizontal_inner()),
                                   std::max(0.0f, box.height - e.vertical_inner())};
                paint_replaced(f.source, box, content, style, into);
            }
            return;
        }
    }

    /// Draws the bullet or number in front of a list item, or the triangle in
    /// front of a <summary>. Generated content: there is no element behind
    /// these, so they are drawn rather than laid out, in the gutter the UA
    /// sheet's padding-left already reserves. The number comes from
    /// box_node::list_ordinal, which box_builder counts among the item's
    /// siblings.
    void emit_marker(const fragment & f, const rect & box, color text_color,
                     display_list & into) const {
        if (f.box == nullptr) { return; }
        const float size = f.box->font_size;
        if (f.box->tag == "summary") {
            // The disclosure triangle: right when closed, down when open. Drawn
            // as text so it follows the font, which is what a browser does.
            //
            // Drawn inside the summary's own left padding, unlike a list marker,
            // which sits in the parent's padding at `box.x - size`. Drawing it
            // left of the box would put it off-screen for a <details> at the
            // page margin.
            const bool open = f.box->details_open;
            into.text(rect{box.x + 3, box.y, size, size}, open ? "v" : ">", size, text_color,
                      f.source);
            return;
        }
        // box_node::list_marker already combines both rules (`display:
        // list-item` and a `list-style-type` other than `none`); neither is
        // about the tag, since a `<li class="d-flex">` is not a list item.
        if (!f.box->list_marker) { return; }
        if (f.box->list_ordinal <= 0) {
            // Unordered: a disc, which font8x8 has no glyph for, so it is a
            // filled square scaled to the text. A round one needs a shape the
            // display list does not have.
            const float dot = std::max(2.0f, size * 0.3f);
            into.fill(rect{box.x - size, box.y + (size - dot) / 2, dot, dot}, text_color, f.source);
            return;
        }
        into.text(rect{box.x - size * 1.6f, box.y, size * 1.6f, size},
                  std::to_string(f.box->list_ordinal) + ".", size, text_color, f.source);
    }

    /// Records a rectangle's four edges, `t` thick.
    static void stroke(const rect & box, float t, color c, node_id source, display_list & into);

    /// Records one sharp box shadow. An outer one is the border box moved by
    /// the offset and grown by the spread; an inset one is a ring inside the
    /// box, as thick as the spread.
    ///
    /// The caller skips a blurred shadow rather than drawing it hard-edged: a
    /// sharp rectangle where a soft one belongs is further from Chrome than
    /// nothing. A real blur needs a rasterizer primitive (for `.shadow` and the
    /// focus rings).
    static void emit_shadow(const rect & box, const corner_radii & radii, const box_shadow & shadow,
                            node_id source, display_list & into) {
        if (shadow.inset) {
            // An inset shadow is a ring, and the spread makes it thicker: the
            // shadow lies between the box's edge and the box shrunk by the
            // spread. So `inset 0 0 0 9999px <colour>` floods the box, which
            // Bootstrap 5.3 relies on to colour a table cell.
            //
            // The offset is not modelled: it slides the hole rather than the
            // ring, which a ring of one thickness cannot express, and every
            // inset shadow Bootstrap writes has a zero offset. A recorded known
            // difference.
            into.fill_rounded(box, shadow.paint, radii, std::max(0.0f, shadow.spread), source);
            return;
        }
        // An outer shadow is the border box moved by the offset and grown by the
        // spread, drawn behind everything else the box paints.
        const rect where{box.x + shadow.dx - shadow.spread, box.y + shadow.dy - shadow.spread,
                         box.width + 2 * shadow.spread, box.height + 2 * shadow.spread};
        into.fill_rounded(where, shadow.paint, radii, 0, source);
    }

    /// One edge's used width and colour, per side and falling back to the
    /// uniform property (edge_of). `border-style: none` means a used width of
    /// zero whatever the width says, which is CSS 2.1 §8.5.3 and the same rule
    /// layout applies.
    struct edge {
        float width = 0;
        color paint;
    };

    /// The used edge on `side` (0 top, 1 right, 2 bottom, 3 left).
    [[nodiscard]] edge edge_of(const computed_style_ptr & style, const rect & box, std::size_t side,
                               color text_color) const {
        std::string_view drawn = trim(prop(style, border_style_sides_[side]), html_whitespace);
        if (drawn.empty()) { drawn = trim(prop(style, border_style_), html_whitespace); }
        if (drawn.empty() || drawn == "none" || drawn == "hidden") { return {}; }
        std::string_view width_text = trim(prop(style, border_width_sides_[side]), html_whitespace);
        if (width_text.empty()) { width_text = trim(prop(style, border_width_), html_whitespace); }
        const layout::length w = layout::parse_length(width_text);
        const float t =
            width_text == "medium" ? 3.0f : (w.is_auto() ? 0 : w.resolve(box.width, 16));
        if (t <= 0) { return {}; }
        std::string_view colour = trim(prop(style, border_color_sides_[side]), html_whitespace);
        if (colour.empty()) { colour = trim(prop(style, border_color_), html_whitespace); }
        // `currentcolor` is the initial value and what the shorthand fills in for
        // an omitted colour, so it is the common case rather than an exotic one.
        if (colour.empty() || colour == "currentcolor") { return edge{t, text_color}; }
        const auto c = parse_color(colour);
        return c ? edge{t, *c} : edge{};
    }

    /// Records the box's border: one rounded ring when all four edges match
    /// and there is a radius, otherwise square edge fills.
    void emit_border(const rect & box, const computed_style_ptr & style, const corner_radii & radii,
                     node_id source, color text_color, display_list & into) const {
        const edge top = edge_of(style, box, 0u, text_color);
        const edge right = edge_of(style, box, 1u, text_color);
        const edge bottom = edge_of(style, box, 2u, text_color);
        const edge left = edge_of(style, box, 3u, text_color);
        // Only a uniform border can be a rounded ring, because a ring has no
        // sides to give different colours to. Any other border is drawn edge
        // by edge, square, which is what a divider is anyway: Bootstrap's
        // per-side borders are all on boxes with no radius at that corner.
        const bool uniform = top.width == right.width && right.width == bottom.width &&
                             bottom.width == left.width && top.paint == right.paint &&
                             right.paint == bottom.paint && bottom.paint == left.paint;
        if (!uniform) {
            if (top.width > 0) {
                into.fill(rect{box.x, box.y, box.width, top.width}, top.paint, source);
            }
            if (bottom.width > 0) {
                into.fill(rect{box.x, box.y + box.height - bottom.width, box.width, bottom.width},
                          bottom.paint, source);
            }
            if (left.width > 0) {
                into.fill(rect{box.x, box.y, left.width, box.height}, left.paint, source);
            }
            if (right.width > 0) {
                into.fill(rect{box.x + box.width - right.width, box.y, right.width, box.height},
                          right.paint, source);
            }
            return;
        }
        const float t = top.width;
        if (t <= 0) { return; }
        const color c = top.paint;
        // A rounded border is a ring, not four rectangles: the corners are where
        // the four would have to meet, and they meet on a curve.
        if (!radii.empty()) {
            into.fill_rounded(box, c, radii, t, source);
            return;
        }
        stroke(box, t, c, source, into);
    }

    /// The four corner radii, resolved against the box. Known difference: a
    /// percentage resolves against the box's width on both axes, whereas CSS
    /// uses the height for the vertical radius; only elliptical corners
    /// (including the shorthand's `/` form) are affected.
    [[nodiscard]] corner_radii radii_of(const computed_style_ptr & style, const rect & box) const {
        const auto one = [&](atom name) {
            const layout::length len = layout::parse_length(prop(style, name));
            return len.is_auto() ? 0.0f : std::max(0.0f, len.resolve(box.width, 16));
        };
        return corner_radii{one(radius_[0]), one(radius_[1]), one(radius_[2]), one(radius_[3])};
    }

    atom background_, color_, border_color_, border_width_, border_style_, overflow_x_, overflow_y_;
    atom box_shadow_;
    // Clockwise from the top, which is the order the side shorthands name them.
    std::array<atom, 4> border_width_sides_, border_style_sides_, border_color_sides_;
    // Clockwise from the top left, which is the order border-radius names them.
    std::array<atom, 4> radius_;
};

} // namespace ctbrowser::paint
