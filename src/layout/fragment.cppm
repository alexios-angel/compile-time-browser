module;
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

export module ctbrowser.layout:fragment;

import :box;
import :values;

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

export namespace ctbrowser::layout {

using ctbrowser::node_id;
using ctbrowser::rect;

struct fragment {
    rect bounds;                    // border box, in the containing block's space
    node_id source;                 // empty for anonymous boxes
    const box_node * box = nullptr; // borrowed; the box tree outlives layout
    std::string text;               // text fragments carry their run
    std::vector<fragment> children;

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
};

// Intrinsic sizes, for the shrink-to-fit and table-column cases that need to
// know how wide content wants to be before deciding how wide to make it.
struct intrinsic_sizes {
    float min_content = 0;
    float max_content = 0;
};

} // namespace ctbrowser::layout
