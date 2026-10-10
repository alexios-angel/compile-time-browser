//===- fragment.hpp - Layout output fragments ----------------- -*- C++ -*-===//
//
// The output of layout, which is immutable once produced, plus the
// constraints a box is laid out into.
//
// A fragment records where a box ended up. Fragments are immutable, so the
// compositor, hit testing and paint can read them while the next layout runs
// without anything changing underneath them.
//
// It is a tree rather than a flat list because one box can produce several
// fragments: an inline that wraps across lines, or a block split across pages
// or columns. Nothing here fragments across pages or columns yet, but the
// shape allows it, which is why the type is named for the possibility.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

#include <ctbrowser/layout/box.hpp>
#include <ctbrowser/layout/values.hpp>

namespace ctbrowser::layout {

using ctbrowser::node_id;
using ctbrowser::rect;

/// One set of adjoining vertical margins.
///
/// A collapsed margin cannot be represented by its used value while it is
/// still being combined. `10px`, `-20px`, `15px` collapses to -5px: collapsing
/// the first pair to -10px and then combining that scalar with 15px would
/// produce 5px instead. Keeping the largest positive and the most-negative
/// member makes the operation associative, which matters for an empty block
/// whose own two margins join the margins of both siblings around it.
struct margin_strut {
    float positive = 0;
    float negative = 0;

    void append(float value) noexcept {
        if (value >= 0) {
            positive = std::max(positive, value);
        } else {
            negative = std::min(negative, value);
        }
    }
    void append(const margin_strut & other) noexcept {
        positive = std::max(positive, other.positive);
        negative = std::min(negative, other.negative);
    }
    [[nodiscard]] float value() const noexcept { return positive + negative; }
};

/// The vertical margins a laid-out block exposes to its containing block.
///
/// These travel with the fragment because a percentage margin can only be
/// resolved once the formatting context has decided the box's containing width.
/// A parallel worker already returns a fragment, so carrying the struts here
/// lets the ordinary sequential assembly collapse siblings without a second
/// layout model or a width-guessing prepass. Paint and hit testing ignore this
/// metadata.
struct block_margin_state {
    margin_strut before;
    margin_strut after;
    /// True when the block's own top and bottom margins collapse through it
    /// (an empty block), so `before` and `after` join one strut.
    bool through = false;
};

/// Where one box (or one line of a box) ended up, with its children.
struct fragment {
    rect bounds;                    // border box, in the containing block's space
    node_id source;                 // empty for anonymous boxes
    const box_node * box = nullptr; // borrowed; the box tree outlives layout
    std::string text;               // text fragments carry their run
    std::vector<fragment> children;
    block_margin_state block_margins;
    /// Set by inline_flow when it produced a line box. A real line box may have
    /// zero geometric height, but it still prevents its block container from
    /// collapsing through.
    bool has_line_box = false;
    /// The height the content came to, as a border box, before a stated height
    /// or a min/max clamp replaced it: what a keyword or a calc-size() basis on
    /// the block axis names (CSS Sizing 3 §5.1: a keyword height is the
    /// automatic height). A column flex item asks this for its content-based
    /// main size, because its fragment's height already honours the item's own
    /// `height`. Negative where the flow did not say (a replaced box, a table).
    float auto_height = -1;
    /// The used margins, which differ from the computed ones exactly where a
    /// margin was `auto` (a centred block, an absolutely positioned box between
    /// two offsets). CSSOM reports margins as used values for an element with a
    /// box (computed-style-005), and this is the only place that knows what a
    /// flow made of `auto`. Written by the flow that placed the box.
    float margin_top = 0, margin_right = 0, margin_bottom = 0, margin_left = 0;

    [[nodiscard]] std::size_t count() const noexcept {
        std::size_t n = 1;
        for (const fragment & c : children) { n += c.count(); }
        return n;
    }
    /// This fragment's bounds offset by (`dx`, `dy`). Bounds are stored relative
    /// to the containing block, so a caller walking down accumulates the
    /// parents' offsets and passes them here.
    [[nodiscard]] rect absolute_bounds(float dx = 0, float dy = 0) const noexcept {
        return rect{bounds.x + dx, bounds.y + dy, bounds.width, bounds.height};
    }
    /// The first fragment in this subtree generated by node `id`, or null.
    [[nodiscard]] const fragment * find(node_id id) const noexcept {
        if (source == id) { return this; }
        for (const fragment & c : children) {
            if (const fragment * hit = c.find(id)) { return hit; }
        }
        return nullptr;
    }
};

/// What a box is being laid out into.
struct constraints {
    float available_width = 0;
    float available_height = 0; // 0 means "as tall as it needs to be"
    float font_size = 16;
    /// The used border-box width this box must take, already decided by the
    /// parent's formatting context. Negative means "use the block rules". Set
    /// for an item in a horizontal flex line (its main size comes from the
    /// freeze loop over the whole line, not from its own `width`) and for a
    /// table cell (its column width). The box needs it before its children are
    /// laid out, or its text wraps at the wrong place.
    ///
    /// `available_width` stays the containing block's width alongside it,
    /// because that is what a percentage margin or padding on this box resolves
    /// against. outer_width_of is what applies it, and nothing propagates it
    /// downward: every formatting context builds its children's constraints
    /// fresh.
    float forced_width = -1;
    /// This box is the root of an independent formatting context, so its own
    /// top/bottom margins cannot collapse with its first/last child. Sibling
    /// margins inside it still collapse with each other.
    ///
    /// Flex items, table cells and absolutely positioned boxes arrive through
    /// layout_box like ordinary blocks; this bit carries the fact their caller
    /// knows and the box alone cannot infer. It deliberately does not propagate
    /// to child constraints.
    bool suppress_margin_collapse = false;
};

/// Intrinsic sizes, for the shrink-to-fit and table-column cases that need to
/// know how wide content wants to be before deciding how wide to make it.
struct intrinsic_sizes {
    float min_content = 0;
    float max_content = 0;
};

} // namespace ctbrowser::layout
