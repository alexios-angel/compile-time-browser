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

// The OUTPUT of layout, and it is immutable.
//
// A fragment is where a box ended up. Immutability is the useful property: the
// compositor, hit testing and the paint pass all read fragments, potentially
// while the next layout is already running, and nothing they read can change
// underneath them. the previous engine had no such object - it wrote geometry onto the DOM node
// and every consumer raced the next layout pass by construction.
//
// It is a tree rather than a flat list because one box can produce SEVERAL
// fragments: an inline that wraps across lines, or a block split across pages
// or columns. Nothing here fragments yet, but the shape is the one that
// allows it, which is why the type is named for the possibility.

namespace ctbrowser::layout {

using ctbrowser::node_id;
using ctbrowser::rect;

// One set of adjoining vertical margins.
//
// A collapsed margin is NOT representable by its used value while it is still
// being combined. `10px`, `-20px`, `15px` collapses to -5px: collapsing the
// first pair to -10px and then combining that scalar with 15px would produce
// 5px instead. Keeping the largest positive and the most-negative member makes
// the operation associative, which is load bearing for an empty block whose
// own two margins join the margins of both siblings around it.
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

// The vertical margins a laid-out block exposes to its containing block.
//
// These travel with the fragment because a percentage margin can only be
// resolved once the formatting context has decided the box's containing width.
// A parallel worker already returns a fragment, so carrying the struts here lets
// the ordinary sequential assembly collapse siblings without a second layout
// model or a width-guessing prepass. Paint and hit testing ignore this metadata.
struct block_margin_state {
    margin_strut before;
    margin_strut after;
    bool through = false;
};

struct fragment {
    rect bounds;                    // border box, in the containing block's space
    node_id source;                 // empty for anonymous boxes
    const box_node * box = nullptr; // borrowed; the box tree outlives layout
    std::string text;               // text fragments carry their run
    std::vector<fragment> children;
    block_margin_state block_margins;
    // Inline-flow result metadata. A real line box may have zero geometric
    // height, but it still prevents its block container collapsing through.
    bool has_line_box = false;

    [[nodiscard]] std::size_t count() const noexcept {
        std::size_t n = 1;
        for (const fragment & c : children) { n += c.count(); }
        return n;
    }
    // Absolute coordinates, for tests and hit testing. Bounds are stored
    // relative to the containing block, so the walk accumulates.
    [[nodiscard]] rect absolute_bounds(float dx = 0, float dy = 0) const noexcept {
        return rect{bounds.x + dx, bounds.y + dy, bounds.width, bounds.height};
    }
    [[nodiscard]] const fragment * find(node_id id) const noexcept {
        if (source == id) { return this; }
        for (const fragment & c : children) {
            if (const fragment * hit = c.find(id)) { return hit; }
        }
        return nullptr;
    }
};

// What a box is being laid out into.
struct constraints {
    float available_width = 0;
    float available_height = 0; // 0 means "as tall as it needs to be"
    float font_size = 16;
    // THE USED WIDTH THIS BOX MUST TAKE, because its parent's formatting context
    // has already decided it. Negative means "work it out from the block rules",
    // which is every case but one today.
    //
    // Flex needs it and cannot be written without it: an item's main size comes
    // out of the freeze loop over its whole LINE, so it cannot be re-derived from
    // the item's own `width` afterwards - and `.row > * { width: 100% }` sitting
    // on every Bootstrap column is the proof, because 100% of the row is not what
    // any column gets. The item still has to know its width BEFORE its children
    // are laid out, or its text wraps at the wrong place, so this cannot be a
    // post-hoc correction to the fragment either.
    //
    // `available_width` stays the CONTAINING BLOCK's width alongside it, because
    // that is what a percentage margin or padding on this box resolves against.
    // Only outer_width_of reads this, and nothing propagates it downward: every
    // formatting context builds its children's constraints fresh.
    float forced_width = -1;
    // This box is the root of an independent formatting context, so its own
    // top/bottom margins cannot collapse with its first/last child. Sibling
    // margins INSIDE it still collapse with each other.
    //
    // Flex items, table cells and absolutely positioned boxes arrive through
    // layout_box like ordinary blocks; this bit carries the fact their caller
    // knows and the box alone cannot infer. It deliberately does not propagate
    // to child constraints.
    bool suppress_margin_collapse = false;
};

// Intrinsic sizes, for the shrink-to-fit and table-column cases that need to
// know how wide content wants to be before deciding how wide to make it.
struct intrinsic_sizes {
    float min_content = 0;
    float max_content = 0;
};

} // namespace ctbrowser::layout
