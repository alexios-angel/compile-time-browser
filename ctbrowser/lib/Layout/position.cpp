#include <ctbrowser/layout/position.hpp>

#include <algorithm>
#include <cstddef>

// position: the function bodies.
// The header says why this is a pass rather than a formatting context; this says
// how the walk carries what the placement needs.

namespace ctbrowser::layout {
namespace {

// The containing block an absolutely positioned box is placed against: the
// PADDING box of the nearest positioned ancestor (CSS 2.1 §10.1), in absolute
// coordinates, or the initial containing block when there is no such ancestor.
//
// The padding box and not the content box, which is the detail that is easy to
// get wrong and invisible until an anchor has padding: `top: 0` on a child of a
// `.position-relative` with 12px of padding puts it at the padding edge, level
// with the padding, not with the text inside it.
struct containing_block {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

// A resolved edge offset, and whether it was given at all. `auto` is not zero
// here - it means "use the static position", which is a completely different
// answer and the one Bootstrap's `.position-absolute` with no offsets relies on.
struct offset {
    float value = 0;
    bool given = false;
};

[[nodiscard]] offset resolve_offset(const length & len, float basis, float font_size) {
    if (len.is_auto()) { return offset{}; }
    return offset{len.resolve(basis, font_size), true};
}

// Where a relative box, or a translated one, moves to.
//
// A RELATIVE BOX IS OFFSET, NOT RESIZED, and it leaves its slot behind: `left`
// and `right` are opposites, so if both are given the left one wins in a
// left-to-right document and the right one is ignored. Giving neither is the
// common case - `.position-relative` exists mostly to be an anchor for something
// else, and then this contributes nothing.
[[nodiscard]] point relative_shift(const box_node & b, const containing_block & cb) {
    const offset left = resolve_offset(b.inset.left, cb.width, b.font_size);
    const offset right = resolve_offset(b.inset.right, cb.width, b.font_size);
    const offset top = resolve_offset(b.inset.top, cb.height, b.font_size);
    const offset bottom = resolve_offset(b.inset.bottom, cb.height, b.font_size);
    return point{left.given ? left.value : (right.given ? -right.value : 0.0f),
                 top.given ? top.value : (bottom.given ? -bottom.value : 0.0f)};
}

// `transform: translate(x, y)`, whose percentages are of the box's OWN size -
// which is what makes `.translate-middle` centre a box on its anchor point
// rather than on anything about its parent.
[[nodiscard]] point translate_shift(const box_node & b, const rect & bounds) {
    return point{b.translate.x.resolve(bounds.width, b.font_size),
                 b.translate.y.resolve(bounds.height, b.font_size)};
}

// One walk, carrying everything a placement needs: where the current fragment is
// in absolute coordinates, and what the nearest positioned ancestor's padding box
// is.
struct positioner {
    const rect & viewport;
    const measure_text_fn & measure;

    void walk(fragment & f, float abs_x, float abs_y, const containing_block & cb,
              const containing_block & fixed_cb) {
        const box_node * box = f.box;
        // A fragment with no box is a line or a generated piece; it still has to
        // pass the offsets down, because its children may be positioned.
        containing_block inner = cb;
        if (box != nullptr && box->is_out_of_flow()) {
            place_out_of_flow(f, abs_x, abs_y,
                              box->position == position_kind::fixed ? fixed_cb : cb);
        } else if (box != nullptr && box->is_positioned()) {
            // `relative` and `sticky`: laid out in flow, then moved. The move is
            // applied to the fragment's own bounds, so every descendant travels
            // with it for free - which is what "relative positioning does not
            // affect layout" means in practice.
            const point shift = relative_shift(*box, cb);
            f.bounds.x += shift.x;
            f.bounds.y += shift.y;
            abs_x += shift.x;
            abs_y += shift.y;
        }
        if (box != nullptr) {
            const point shift = translate_shift(*box, f.bounds);
            f.bounds.x += shift.x;
            f.bounds.y += shift.y;
            abs_x += shift.x;
            abs_y += shift.y;
        }
        // Everything but `static` is an anchor for the descendants below it.
        if (box != nullptr && box->is_positioned()) {
            inner = padding_box_of(*box, f, abs_x + f.bounds.x, abs_y + f.bounds.y);
        }
        for (fragment & child : f.children) {
            walk(child, abs_x + f.bounds.x, abs_y + f.bounds.y, inner, fixed_cb);
        }
    }

    // The padding box: the border box less the border, which is where a
    // positioned descendant's `top: 0` lands.
    [[nodiscard]] static containing_block padding_box_of(const box_node & b, const fragment & f,
                                                         float abs_x, float abs_y) {
        const constraints c{f.bounds.width, f.bounds.height, b.font_size};
        const resolved_edges e = resolve_edges(b, c);
        return containing_block{abs_x + e.border_left, abs_y + e.border_top,
                                std::max(0.0f, f.bounds.width - e.border_left - e.border_right),
                                std::max(0.0f, f.bounds.height - e.border_top - e.border_bottom)};
    }

    // THE PLACEMENT. `f` arrives as an empty fragment at its static position, in
    // its parent's coordinates; `abs_x`/`abs_y` are that parent's absolute origin.
    void place_out_of_flow(fragment & f, float abs_x, float abs_y, const containing_block & cb) {
        const box_node & b = *f.box;
        const float static_abs_x = abs_x + f.bounds.x;
        const float static_abs_y = abs_y + f.bounds.y;

        const offset left = resolve_offset(b.inset.left, cb.width, b.font_size);
        const offset right = resolve_offset(b.inset.right, cb.width, b.font_size);
        const offset top = resolve_offset(b.inset.top, cb.height, b.font_size);
        const offset bottom = resolve_offset(b.inset.bottom, cb.height, b.font_size);

        const constraints outer{cb.width, cb.height, b.font_size};
        const resolved_edges edges = resolve_edges(b, outer);

        // THE WIDTH, and the three cases are genuinely different rules rather than
        // one rule with exceptions (§10.3.7). A stated width is itself; BOTH
        // offsets given with an auto width stretches between them, which is how a
        // full-width overlay is written; and anything else SHRINKS TO FIT, which
        // is why `.position-absolute.top-0.start-0` is the size of its text and
        // not the size of the page.
        float width = 0;
        if (!b.width.is_auto()) {
            width = std::max(0.0f, b.width.resolve(cb.width, b.font_size));
        } else if (left.given && right.given) {
            width = std::max(0.0f, cb.width - left.value - right.value - edges.horizontal_margin());
        } else {
            width = shrink_to_fit_width(b, outer, edges, measure);
        }

        fragment placed =
            layout_box(b, constraints{cb.width, cb.height, b.font_size, width, true}, measure);
        placed.box = &b;
        placed.source = b.source;

        // Where it goes, in absolute coordinates. An `auto` offset means the
        // static position - where the box would have been in flow - which is the
        // whole reason the flows leave a marker behind rather than nothing.
        const float height = placed.bounds.height;
        float x = static_abs_x;
        if (left.given) {
            x = cb.x + left.value + edges.margin_left;
        } else if (right.given) {
            x = cb.x + cb.width - right.value - width - edges.margin_right;
        }
        float y = static_abs_y;
        if (top.given) {
            y = cb.y + top.value + edges.margin_top;
        } else if (bottom.given) {
            y = cb.y + cb.height - bottom.value - height - edges.margin_bottom;
        }

        // Back into the parent's coordinates, which is what a fragment's bounds
        // are relative to.
        placed.bounds.x = x - abs_x;
        placed.bounds.y = y - abs_y;
        f = std::move(placed);
    }
};

} // namespace

void apply_positioning(fragment & root, const rect & viewport, float viewport_height,
                       const measure_text_fn & measure) {
    const containing_block initial{0, 0, viewport.width, viewport.height};
    // A FIXED BOX IS PLACED AGAINST THE WINDOW, not the document. The two are the
    // same only on a page that does not scroll, and `.fixed-bottom` is exactly
    // the case that tells them apart: against the document it lands after the
    // last paragraph, where nobody will ever see it.
    const containing_block fixed{0, 0, viewport.width, viewport_height};
    positioner walker{viewport, measure};
    // The root fragment's own bounds are the document's; the walk adds them in as
    // it descends, so it starts at the origin rather than at the root's position.
    walker.walk(root, -root.bounds.x, -root.bounds.y, initial, fixed);
}

} // namespace ctbrowser::layout
