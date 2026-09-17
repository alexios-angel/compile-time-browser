// dom_bindings - CSSOM View §5 on the Document: `scrollingElement`,
// `elementFromPoint`, `elementsFromPoint` and `caretPositionFromPoint`.

#include "internal.hpp"

#include <ctbrowser/layout/overflow.hpp>

namespace ctbrowser::shell {

using namespace detail;

// The hit test over the FRAGMENT tree rather than the recorded display list
// the pointer path asks (browser::hit_test): a script asks between layout
// and paint, and the display list is stale then. Every fragment whose border
// box holds the point - in viewport coordinates, so scrolled containers and
// the page scroll are taken off, and clipped to every `overflow` ancestor's
// padding box - names its element, a text run naming its parent. Painted-
// last first: a positioned box sits above the in-flow content around it,
// later siblings above earlier ones and a box above its ancestors. The root
// closes the list, as §5 says.
//
// ponytail: no `z-index`, `pointer-events: none` or transform here; the
// stacking-context order the recorder computes is the upgrade when a test
// wants it.
std::vector<node_id> dom_bindings::elements_from_point(double x, double y, bool all) {
    std::vector<node_id> out;
    flush_layout();
    if (fragments_ == nullptr || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
        x > viewport_width_ || y > viewport_height_) {
        return out;
    }
    const point at{static_cast<float>(x), static_cast<float>(y)};
    const point viewport = viewport_scroll();
    // (paint rank, tree order) per hit: rank 1 for a positioned box and
    // everything inside it, 0 for in-flow content.
    struct hit {
        int rank;
        std::size_t order;
        node_id node;
    };
    std::vector<hit> hits;
    std::size_t order = 0;
    const auto txn = doc_->read();
    const auto walk = [&](auto && self, const layout::fragment & f, float dx, float dy, bool fixed,
                          int rank, std::optional<rect> clip) -> void {
        const bool is_fixed =
            fixed || (f.box != nullptr && f.box->position == layout::position_kind::fixed);
        rect box = f.absolute_bounds(dx, dy);
        if (!is_fixed) {
            box.x -= viewport.x;
            box.y -= viewport.y;
        }
        const int own_rank =
            std::max(rank, f.box != nullptr && (f.box->is_positioned() || is_fixed) ? 1 : 0);
        const std::size_t here = order++;
        const bool inside = box.contains(at) && (!clip || clip->contains(at));
        if (inside && f.source) {
            node_id named = f.source;
            if (txn.kind(named).value_or(node_kind::element) != node_kind::element) {
                named = txn.parent(named);
            }
            if (named && txn.kind(named).value_or(node_kind::element) == node_kind::element) {
                hits.push_back(hit{own_rank, here, named});
            }
        }
        // Into the children in the container's scrolled coordinates.
        point inner{f.bounds.x + dx, f.bounds.y + dy};
        std::optional<rect> inner_clip = clip;
        if (f.box != nullptr && f.source) {
            if (f.box->scroll_container) {
                const point offset = scroll_offset_of(f.source);
                inner.x -= offset.x;
                inner.y -= offset.y;
            }
            if (f.box->clips_overflow) {
                const rect pad = layout::padding_box_of(f);
                const rect region{box.x + pad.x, box.y + pad.y, pad.width, pad.height};
                inner_clip = clip ? clip->intersected(region) : region;
            }
        }
        for (const layout::fragment & child : f.children) {
            self(self, child, inner.x, inner.y, is_fixed, own_rank, inner_clip);
        }
    };
    walk(walk, *fragments_, 0, 0, false, 0, std::nullopt);
    std::ranges::stable_sort(hits, [](const hit & a, const hit & b) {
        return a.rank != b.rank ? a.rank > b.rank : a.order > b.order;
    });
    for (const hit & h : hits) {
        if (std::ranges::find(out, h.node) == out.end()) { out.push_back(h.node); }
        if (!all) { break; }
    }
    const node_id root = txn.root();
    if (root && (out.empty() || out.back() != root)) {
        if (!all && !out.empty()) { return out; }
        out.push_back(root);
    }
    return out;
}

void dom_bindings::install_document_geometry(context & cx, script::object_object & doc) {
    define_getter(cx, doc, "scrollingElement", [this](context & c, std::span<value>) {
        const node_id which = scrolling_element();
        return which ? wrap(c, which) : value::null();
    });
    set_method(cx, doc, "elementFromPoint", [this](context & c, std::span<value> args) {
        if (args.size() < 2) {
            c.throw_error("TypeError", "elementFromPoint: 2 arguments required");
            return value::undefined();
        }
        const std::vector<node_id> found =
            elements_from_point(context::to_number(args[0]), context::to_number(args[1]), false);
        return found.empty() ? value::null() : wrap(c, found.front());
    });
    set_method(cx, doc, "elementsFromPoint", [this](context & c, std::span<value> args) {
        if (args.size() < 2) {
            c.throw_error("TypeError", "elementsFromPoint: 2 arguments required");
            return value::undefined();
        }
        const std::vector<node_id> found =
            elements_from_point(context::to_number(args[0]), context::to_number(args[1]), true);
        const value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const node_id id : found) { items->items.push_back(wrap(c, id)); }
        return out;
    });
    // caretPositionFromPoint: the element under the point as the caret node,
    // with offset 0 - the insertion point within a text run is what the
    // browser's own click-to-caret path knows (browser/input.cpp) and this
    // object model does not reach it yet. null off the viewport.
    set_method(cx, doc, "caretPositionFromPoint", [this](context & c, std::span<value> args) {
        if (args.size() < 2) {
            c.throw_error("TypeError", "caretPositionFromPoint: 2 arguments required");
            return value::undefined();
        }
        const std::vector<node_id> found =
            elements_from_point(context::to_number(args[0]), context::to_number(args[1]), false);
        if (found.empty()) { return value::null(); }
        auto * position = c.allocate<script::object_object>();
        position->set("offsetNode", wrap(c, found.front()));
        position->set("offset", value::number(0));
        set_method(c, *position, "getClientRect",
                   [this, id = found.front()](context & cc, std::span<value>) {
                       const rect box = client_rect_of(id);
                       auto * out = cc.allocate<script::object_object>();
                       const auto set = [&](const char * name, float v) {
                           out->set(name, value::number(static_cast<double>(v)));
                       };
                       set("x", box.x);
                       set("y", box.y);
                       set("left", box.x);
                       set("top", box.y);
                       set("width", 0);
                       set("height", box.height);
                       set("right", box.x);
                       set("bottom", box.y + box.height);
                       return value::object(out);
                   });
        return value::object(position);
    });
}

} // namespace ctbrowser::shell
