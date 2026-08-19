#pragma once
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/style/style.hpp>

#include <ctbrowser/paint/command.hpp>
#include <ctbrowser/paint/layer.hpp>
#include <ctbrowser/paint/values.hpp>

// Recording: fragment tree in, display list out.
//
// Paint order here is document order with each box's background emitted before
// its children, which is already back-to-front for the subset that exists
// (no z-index, no stacking contexts, no floats). the previous engine needed a separate
// collect_backgrounds() pre-pass because it wrote into a flat command vector
// where a parent's background would otherwise land on top of its children's
// text. Emitting in tree order into a list that is CONSUMED in order gets the
// same result without the second traversal.
//
// The parts that are genuinely absent - z-index, opacity groups, transforms,
// borders as anything but four fills - are absent because they need stacking
// contexts, and stacking contexts belong with the compositor work in stage 6.

namespace ctbrowser::paint {

using ctbrowser::atom;
using ctbrowser::atom_table;
using ctbrowser::color;
using ctbrowser::layout::fragment;
using ctbrowser::style::computed_style_ptr;

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

    // The default text colour, when nothing in the cascade says otherwise.
    color default_text_color = color::rgba(0, 0, 0);

    // What a REPLACED element draws. The recorder cannot know: a <canvas>'s
    // pixels live in the shell's canvas store, and a form control's appearance
    // depends on its live value and focus. Rather than teach paint about either,
    // it asks - which keeps this module ignorant of script and of widgets, and
    // keeps the display list the only thing they have to agree on.
    //
    // TWO RECTS, and that is the whole of the centralisation: the BORDER box, and
    // the CONTENT box the recorder already worked out to draw the background and
    // the border in. A control's text sits at its content edge, and until this
    // was passed the shell had its own constant for that - which layout had a
    // second copy of, and which no stylesheet could change. Bootstrap's
    // `.form-control` has 12px of padding where the constant said 6.
    using replaced_painter = std::function<void(node_id, const rect &, const rect &,
                                                const computed_style_ptr &, display_list &)>;
    replaced_painter paint_replaced;

    // The highlighted part of a text fragment, in the fragment's own space, or
    // an empty rect for none. A HOOK rather than a member on the fragment: a
    // selection is a property of the browsing session, not of the layout, and
    // the fragment tree is rebuilt by a relayout that a selection outlives.
    //
    // Called for every text run, and the fill goes UNDER the text - a highlight
    // drawn over it would hide what is selected.
    std::function<rect(const fragment &)> selection_of;

    [[nodiscard]] std::shared_ptr<const display_list> record(const fragment & root) const;

    // One page, one layer, for now. Layer assignment is a stacking-context
    // question (position:fixed, transforms, will-change, scrollers), and the
    // tree cannot answer it before stacking contexts exist - so this returns
    // the honest one-layer answer rather than a guess at the shape.
    [[nodiscard]] layer_tree record_layers(const fragment & root) const;

private:
    [[nodiscard]] std::string_view prop(const computed_style_ptr & s, atom name) const;

    void emit(const fragment & f, float dx, float dy, color inherited_text,
              display_list & into) const {
        // OPACITY IS APPLIED TO WHAT THIS SUBTREE APPENDS, noted here and folded
        // in at the end. That is one place rather than one per command kind, and
        // it is what makes it catch the background, the border, the text, the
        // list marker and whatever the replaced painter drew - none of which has
        // to know opacity exists. See display_list::fade_from for what the
        // approximation is and where it differs from a real group.
        const float opacity = f.box != nullptr ? f.box->opacity : 1.0f;
        const std::size_t opaque_from = into.size();
        emit_opaque(f, dx, dy, inherited_text, into);
        into.fade_from(opaque_from, opacity);
    }

    void emit_opaque(const fragment & f, float dx, float dy, color inherited_text,
                     display_list & into) const {
        const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
        const computed_style_ptr style = f.box != nullptr ? f.box->style : computed_style_ptr{};

        color text_color = inherited_text;
        if (const auto c = parse_color(prop(style, color_))) { text_color = *c; }

        if (!f.text.empty()) {
            // A text fragment IS one visual line - layout already broke it - so
            // the run needs no further measurement here. The FACE comes along
            // because the rasterizer has no way back to the element: by the
            // time a tile is drawn there is no cascade left to ask.
            font_face face;
            text_decoration decoration = text_decoration::none;
            float size = 16;
            if (f.box != nullptr) {
                size = f.box->font_size;
                face.family = f.box->face.family;
                face.bold = f.box->face.bold;
                face.italic = f.box->face.italic;
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

        // THE BACKGROUND AND THE BORDER SHARE A SHAPE, so the radius is resolved
        // once and both are drawn against it. A square box takes the same two
        // calls it always did - `radii.empty()` is the fast path all the way down
        // to the rasterizer - so nothing that has no radius moves.
        const corner_radii radii = radii_of(style, box);
        // CSS PAINT ORDER: an OUTER shadow is behind everything the box draws, an
        // INSET one is in front of the background and behind the border. Bootstrap
        // 5.3 paints every table cell's background with an inset one -
        // `inset 0 0 0 9999px <colour>` - so a `.table-striped` has no stripes at
        // all without this, and neither has a `.table-hover` or any themed row.
        const std::vector<box_shadow> shadows = parse_box_shadow(prop(style, box_shadow_));
        for (const box_shadow & shadow : shadows) {
            if (shadow.inset || !shadow.sharp()) { continue; }
            emit_shadow(box, radii, shadow, f.source, into);
        }
        if (const auto bg = parse_color(prop(style, background_))) {
            if (radii.empty()) {
                into.fill(box, *bg, f.source);
            } else {
                into.fill_rounded(box, *bg, radii, 0, f.source);
            }
        }
        for (const box_shadow & shadow : shadows) {
            if (!shadow.inset || !shadow.sharp()) { continue; }
            emit_shadow(box, radii, shadow, f.source, into);
        }
        emit_border(box, style, radii, f.source, text_color, into);
        emit_marker(f, box, text_color, into);
        // `<table border=1>`: a presentational attribute, not CSS, and one that
        // draws a frame around the table AND around every cell - which is what
        // the attribute has always meant and what makes a bordered table read
        // as a grid.
        if (f.box != nullptr && f.box->border_px > 0) {
            stroke(box, f.box->border_px, color{ctbrowser::style::ua_table_border}, f.source, into);
        }

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

        // `overflow: hidden` is the one clip that exists so far. It is here
        // rather than in a later pass because a clip has to bracket exactly the
        // subtree it applies to, which only the recursion knows.
        const bool clips = ascii_iequals(prop(style, overflow_x_), "hidden") ||
                           ascii_iequals(prop(style, overflow_y_), "hidden");
        if (clips) { into.push_clip(box); }
        for (const fragment & child : f.children) { emit(child, box.x, box.y, text_color, into); }
        if (clips) { into.pop_clip(); }
    }

    // Borders as four fills. Not a shortcut that needs apologising for: a solid
    // border IS four rects, and the cases that are not (radii, dashes, per-side
    // colours) need their own commands rather than a wider version of this one.
    // The bullet or number in front of a list item, and the triangle in front of
    // a <summary>. Generated content: there is no element behind these, so they
    // are drawn rather than laid out, in the gutter the UA sheet's padding-left
    // already reserves.
    //
    // The ORDINAL is counted among an item's siblings rather than stored, so
    // `<ol>` numbering follows the document and needs nothing on the box.
    void emit_marker(const fragment & f, const rect & box, color text_color,
                     display_list & into) const {
        if (f.box == nullptr) { return; }
        const float size = f.box->font_size;
        if (f.box->tag == "summary") {
            // The disclosure triangle: right when closed, down when open. Drawn
            // as text so it follows the font, which is what a browser does.
            //
            // INSIDE its own left padding, not to the left of the box. A list
            // marker sits at `box.x - size` because the gutter belongs to the
            // PARENT - `ul { padding-left: 40px }` - so that lands inside the
            // list. A summary's gutter is its OWN padding-left, so the same
            // arithmetic put the triangle outside the element: for a <details>
            // at the page margin that is a negative x, off the left edge of the
            // window, which is why no triangle ever appeared.
            const bool open = f.box->details_open;
            into.text(rect{box.x + 3, box.y, size, size}, open ? "v" : ">", size, text_color,
                      f.source);
            return;
        }
        // ONE FLAG, TWO RULES - see box_node::list_marker. The marker belongs to
        // `display: list-item` and to a `list-style-type` other than `none`, and
        // neither is a question about the TAG: a `<li class="d-flex">` is a flex
        // container, not a list item, and Bootstrap's list groups are full of
        // them.
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

    // A rectangle's four edges, `t` thick.
    static void stroke(const rect & box, float t, color c, node_id source, display_list & into);

    // A shadow rectangle. An OUTER one is the border box moved by the offset and
    // grown by the spread; an INSET one is the border box moved by the offset and
    // SHRUNK by it, clipped to the box - which for Bootstrap's 9999px spread is
    // simply the whole box flooded, and that is exactly the effect the framework
    // is after.
    //
    // A BLURRED shadow is skipped by the caller rather than drawn hard-edged: a
    // sharp black rectangle where a soft one belongs is further from Chrome than
    // nothing, not closer. A real blur is a rasterizer primitive and is the next
    // step for `.shadow` and the focus rings.
    static void emit_shadow(const rect & box, const corner_radii & radii, const box_shadow & shadow,
                            node_id source, display_list & into) {
        if (shadow.inset) {
            // AN INSET SHADOW IS A RING, and the spread makes it THICKER rather
            // than smaller - the shadow lies between the box's edge and the box
            // shrunk by the spread. That is why `inset 0 0 0 9999px <colour>`
            // floods: the inner hole is shrunk out of existence, which is
            // precisely what Bootstrap 5.3 relies on to colour a table cell.
            // Reading the spread the other way round drew nothing at all.
            //
            // The OFFSET is not modelled - it slides the hole rather than the
            // ring, so a ring of one thickness cannot express it - and every
            // inset shadow Bootstrap writes has a zero offset. A recorded known
            // difference rather than a wrong ring.
            into.fill_rounded(box, shadow.paint, radii, std::max(0.0f, shadow.spread), source);
            return;
        }
        // An OUTER shadow is the border box moved by the offset and grown by the
        // spread, drawn behind everything else the box paints.
        const rect where{box.x + shadow.dx - shadow.spread, box.y + shadow.dy - shadow.spread,
                         box.width + 2 * shadow.spread, box.height + 2 * shadow.spread};
        if (radii.empty()) {
            into.fill(where, shadow.paint, source);
        } else {
            into.fill_rounded(where, shadow.paint, radii, 0, source);
        }
    }

    // One edge's used width and colour, per side and falling back to the uniform
    // property. `border-style: none` means a used width of ZERO whatever the
    // width says, which is CSS 2.1 §8.5.3 and the same rule layout applies.
    struct edge {
        float width = 0;
        color paint;
    };

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

    void emit_border(const rect & box, const computed_style_ptr & style, const corner_radii & radii,
                     node_id source, color text_color, display_list & into) const {
        const edge top = edge_of(style, box, 0u, text_color);
        const edge right = edge_of(style, box, 1u, text_color);
        const edge bottom = edge_of(style, box, 2u, text_color);
        const edge left = edge_of(style, box, 3u, text_color);
        // FOUR DIFFERENT EDGES IS THE GENERAL CASE and one uniform border the
        // special one - but the special one is the only one that can be a rounded
        // RING, because a ring has no sides to give different colours to. So it
        // keeps its own path and everything else is drawn edge by edge, square,
        // which is what a divider is anyway: Bootstrap's per-side borders are all
        // on boxes whose radius is zero at that corner or absent entirely.
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
        // A ROUNDED BORDER IS A RING, not four rectangles: the corners are where
        // the four would have to meet, and they meet on a curve. One command
        // rather than four also gets the transparent case right, which the
        // obvious alternative does not - `.btn-outline-primary` is a 1px ring
        // around NOTHING, and "fill the box in the border colour, then fill the
        // inside in the background colour" would flood the button.
        if (!radii.empty()) {
            into.fill_rounded(box, c, radii, t, source);
            return;
        }
        stroke(box, t, c, source, into);
    }

    // The four corner radii, resolved against the box. A percentage is against
    // the box's own WIDTH here, where CSS resolves the horizontal radius against
    // the width and the vertical against the height - the difference only shows
    // on an elliptical corner, which is the same thing the shorthand's `/` form
    // asks for and the same known difference.
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
