#include <ctbrowser/layout/overflow.hpp>

#include <algorithm>

// overflow: the function bodies. The header says what a scrolling area is;
// this says which fragments contribute to it and how far.

namespace ctbrowser::layout {
namespace {

// A rectangle held as its four edges, because the union CSS Overflow 3 asks
// for is a union of EDGES - the left and top are pinned to the padding edge
// and only the right and bottom grow - and rect::united would drop a
// zero-height padding box on the floor before anything had grown past it.
struct edges {
    float left = 0, top = 0, right = 0, bottom = 0;

    void grow(const rect & r) noexcept {
        left = std::min(left, r.x);
        top = std::min(top, r.y);
        right = std::max(right, r.x + r.width);
        bottom = std::max(bottom, r.y + r.height);
    }
    [[nodiscard]] rect as_rect() const noexcept {
        return rect{left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
    }
};

// A descendant's contribution to its parent's scrollable overflow: its border
// box, a flex item's margin box, and - unless the descendant clips - its own
// scrollable overflow. Zero-area border boxes contribute nothing themselves
// (CSS Overflow 3 §3.3) but what they contain still does.
//
// `icb` says the initial containing block is the nearest containing block
// above `f` for absolutely positioned boxes: true while measuring the
// document's area and no positioned box has been passed on the way down. An
// absolute box with no positioned ancestor is placed against the viewport
// and belongs to the viewport's area rather than to any element's.
[[nodiscard]] edges scrollable_overflow(const fragment & f, bool icb) noexcept;

void contribute(const fragment & child, const fragment & parent, bool icb, edges & into) noexcept {
    const box_node * box = child.box;
    const bool parent_positioned = parent.box != nullptr && parent.box->is_positioned();
    if (box != nullptr && box->is_out_of_flow()) {
        // A fixed box scrolls with nothing; an absolute one belongs to the
        // nearest positioned ancestor, or to the viewport when there is none.
        if (box->position == position_kind::fixed) { return; }
        if (!icb && !parent_positioned) { return; }
    }
    rect border = child.bounds;
    if (parent.box != nullptr && parent.box->kind == box_kind::flex && box != nullptr &&
        !box->is_out_of_flow()) {
        border = rect{border.x - child.margin_left, border.y - child.margin_top,
                      border.width + child.margin_left + child.margin_right,
                      border.height + child.margin_top + child.margin_bottom};
    }
    // A box with no extent on EITHER axis is nothing; one with an extent on
    // one axis still reaches - a 0x2000 child makes its parent 2000 tall to
    // scroll, with `overflow: visible` as with `hidden`, which is what
    // scrollWidthHeight-contain-layout pins and stricter reading of "zero
    // area" would break.
    if (border.width > 0 || border.height > 0) { into.grow(border); }
    // A box that clips its overflow shows nothing past its padding edge, so
    // its contribution ends at its border box.
    if (box != nullptr && box->clips_overflow) { return; }
    const bool positioned = box != nullptr && box->is_positioned();
    const rect inner = scrollable_overflow(child, icb && !positioned).as_rect();
    if (inner.width <= 0 && inner.height <= 0) { return; }
    into.grow(rect{child.bounds.x + inner.x, child.bounds.y + inner.y, inner.width, inner.height});
}

edges scrollable_overflow(const fragment & f, bool icb) noexcept {
    const resolved_edges e = edges_of(f);
    const rect padding = padding_box_of(f);
    edges out{padding.x, padding.y, padding.x + padding.width, padding.y + padding.height};
    // THE IN-FLOW EXTENT, for the end padding a scroll container keeps past
    // its content: the content box, stretched down to the last in-flow
    // child's bottom margin edge - a block container's content ends where its
    // cursor stopped, whatever spilled sideways - or, in a flex container,
    // around every item's margin box.
    const bool flex = f.box != nullptr && f.box->kind == box_kind::flex;
    edges inflow{e.content_left(), e.content_top(),
                 std::max(e.content_left(), f.bounds.width - e.border_right - e.pad_right),
                 std::max(e.content_top(), f.bounds.height - e.border_bottom - e.pad_bottom)};
    for (const fragment & child : f.children) {
        contribute(child, f, icb, out);
        if (child.box != nullptr && child.box->is_out_of_flow()) { continue; }
        const rect margin_box{child.bounds.x - child.margin_left, child.bounds.y - child.margin_top,
                              child.bounds.width + child.margin_left + child.margin_right,
                              child.bounds.height + child.margin_top + child.margin_bottom};
        if (flex) {
            if (margin_box.width > 0 || margin_box.height > 0) { inflow.grow(margin_box); }
        } else {
            inflow.bottom = std::max(inflow.bottom, margin_box.y + margin_box.height);
        }
    }
    if (f.box != nullptr && f.box->scroll_container) {
        out.right = std::max(out.right, inflow.right + e.pad_right);
        out.bottom = std::max(out.bottom, inflow.bottom + e.pad_bottom);
    }
    return out;
}

} // namespace

resolved_edges edges_of(const fragment & f) noexcept {
    if (f.box == nullptr) { return resolved_edges{}; }
    return resolve_edges(*f.box, constraints{f.bounds.width, f.bounds.height, f.box->font_size});
}

rect padding_box_of(const fragment & f) noexcept {
    const resolved_edges e = edges_of(f);
    return rect{e.border_left, e.border_top,
                std::max(0.0f, f.bounds.width - e.border_left - e.border_right),
                std::max(0.0f, f.bounds.height - e.border_top - e.border_bottom)};
}

rect scrolling_area_of(const fragment & f) noexcept {
    const rect padding = padding_box_of(f);
    const edges grown = scrollable_overflow(f, false);
    // Pinned at the padding edge: whatever spilled up or left is unreachable.
    return rect{padding.x, padding.y, std::max(padding.width, grown.right - padding.x),
                std::max(padding.height, grown.bottom - padding.y)};
}

rect viewport_scrolling_area(const fragment & document, float viewport_width,
                             float viewport_height) noexcept {
    const edges grown = scrollable_overflow(document, true);
    return rect{0, 0, std::max(viewport_width, grown.right),
                std::max(viewport_height, grown.bottom)};
}

} // namespace ctbrowser::layout
