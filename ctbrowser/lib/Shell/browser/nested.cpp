// browser - nested browsing contexts: a frame's document laid out at the size
// its <iframe> box got.
//
// The bindings build a frame's document (bindings/frames.cpp) and said, in
// their own header, that "a frame lays out as an empty box" - which was true
// of the DOM half and is where every read inside a frame stopped: `100vw` in a
// 200px frame computed against the window, `@media (width: 100vw)` in a
// frame's sheet answered for the page, and `frame.contentWindow
// .getComputedStyle(div).height` was the empty declaration an unrendered
// element gets (css-values' viewport-units-*, 142 subtests over five files,
// and every cssom file that measures inside an iframe).
//
// This is the rendering half: each loaded frame gets a style engine over its
// own sheets with the media environment of ITS viewport, the cascade, the box
// tree and layout, run by the same code the page runs through, and the frame's
// bindings observe the result exactly as the page's do. Recursively, so a
// frame's frame is laid out inside it. Nothing is painted - the box on screen
// stays empty, which is the painting half and a display list nested in the
// page's.
//
// WHEN. After every page layout, because the frame's size comes from it, and
// on any read when a frame's document has moved: the bindings over a frame
// have no mutation hook (a secondary document is built without one), so
// instead of plumbing one the flush compares the document's version and the
// CSSOM's edit stamp with what the last layout was made from. That is what
// `frames_stale` is, and it is proportionate: a frame is re-run only when
// something in it changed.

#include "internal.hpp"

namespace ctbrowser::shell {

namespace {

// The content box of a replaced element's fragment: its border box less the
// border and padding the box resolved. What the frame's viewport is.
[[nodiscard]] std::pair<float, float> content_size(const fragment & frag) {
    float width = frag.bounds.width;
    float height = frag.bounds.height;
    if (const box_node * box = frag.box; box != nullptr) {
        const float em = box->font_size;
        width -= box->border.left.resolve(0, em) + box->border.right.resolve(0, em) +
                 box->padding.left.resolve(0, em) + box->padding.right.resolve(0, em);
        height -= box->border.top.resolve(0, em) + box->border.bottom.resolve(0, em) +
                  box->padding.top.resolve(0, em) + box->padding.bottom.resolve(0, em);
    }
    return {std::max(0.0f, width), std::max(0.0f, height)};
}

} // namespace

void browser::layout_frames() {
    std::vector<std::unique_ptr<frame_layout>> made;
    // The owner's fragments give each frame its size; a frame's own fragments
    // give its frames theirs.
    const auto walk = [&](auto && self, dom_bindings & owner, const fragment & fragments) -> void {
        for (const dom_bindings::loaded_frame & each : owner.loaded_frames()) {
            const fragment * frag = fragments.find(each.element);
            // A frame with no box - `display: none`, or an owner that has not
            // laid out - renders nothing, and its document reads as unrendered,
            // which is what getComputedStyle-detached-subtree asserts.
            if (frag == nullptr) { continue; }
            const auto [width, height] = content_size(*frag);
            document & doc = each.bindings->owned_document();
            // Kept across layouts when nothing moved: the style engine holds
            // the parsed sheets, and re-parsing them for a page that only
            // scrolled is the cost "a frame runs only what changed" forbids.
            std::unique_ptr<frame_layout> layout;
            for (auto & old : frame_layouts_) {
                if (old && old->bindings == each.bindings) { layout = std::move(old); }
            }
            const bool fresh = !layout;
            if (fresh) {
                layout = std::make_unique<frame_layout>();
                layout->element = each.element;
                layout->bindings = each.bindings;
                layout->styles = std::make_unique<ctbrowser::style::engine>(atoms_);
                layout->styles->set_text_measure([this](std::string_view text, float size,
                                                        std::string_view family, bool bold,
                                                        bool italic) {
                    return fonts().advance(text, size, family, bold, italic);
                });
                layout->styles->add_sheet(ctbrowser::style::ua_css, ctbrowser::style::ua_origin);
                each.bindings->observe_style_engine(*layout->styles);
            }
            const bool moved = fresh || layout->version != doc.version() ||
                               layout->style_stamp != each.bindings->style_stamp() ||
                               layout->width != width || layout->height != height;
            if (moved) {
                ctbrowser::style::css::media_environment env = layout->styles->environment();
                env.viewport_width = width;
                env.viewport_height = height;
                (void)layout->styles->set_environment(env);
                // THE FRAME'S SHEETS, as the page's are collected - and, once a
                // script has edited them through the CSSOM, the object model's
                // text, for the reason refresh_author_styles gives.
                std::string css = collect_author_styles(doc, each.bindings, false);
                if (each.bindings->style_stamp() != 0) { css = each.bindings->author_style_text(); }
                layout->styles->clear_origin(ctbrowser::style::author_origin);
                if (!css.empty()) {
                    layout->styles->add_sheet(css, ctbrowser::style::author_origin);
                }
                const auto txn = doc.read();
                layout->resolved = layout->styles->resolve_all(txn);
                ctbrowser::layout::box_builder builder{atoms_, layout->resolved, measure()};
                layout->boxes = builder.build(txn, txn.root());
                const ctbrowser::layout::engine eng{measure()};
                layout->fragments = eng.run(layout->boxes, width, height);
                layout->version = doc.version();
                layout->style_stamp = each.bindings->style_stamp();
                layout->width = width;
                layout->height = height;
                each.bindings->observe_layout(&layout->fragments);
                each.bindings->observe_boxes(&layout->boxes);
                each.bindings->observe_styles(&layout->resolved);
                each.bindings->observe_viewport(static_cast<int>(width), static_cast<int>(height));
                (void)each.bindings->refresh_wrappers();
            }
            self(self, *each.bindings, layout->fragments);
            made.push_back(std::move(layout));
        }
    };
    if (bindings_) { walk(walk, *bindings_, fragments_); }
    // A frame that left the tree loses its layout with it; its bindings keep
    // answering as an unrendered document, pointers cleared first so they
    // never read a freed tree.
    for (auto & old : frame_layouts_) {
        if (old) {
            old->bindings->observe_layout(nullptr);
            old->bindings->observe_boxes(nullptr);
            old->bindings->observe_styles(nullptr);
        }
    }
    frame_layouts_ = std::move(made);
}

bool browser::frames_stale() const {
    if (!bindings_) { return false; }
    const auto walk = [&](auto && self, const dom_bindings & owner) -> bool {
        for (const dom_bindings::loaded_frame & each : owner.loaded_frames()) {
            const auto known = std::ranges::find_if(
                frame_layouts_, [&](const auto & l) { return l && l->bindings == each.bindings; });
            if (known == frame_layouts_.end()) { return true; }
            if ((*known)->version != each.bindings->owned_document().version() ||
                (*known)->style_stamp != each.bindings->style_stamp()) {
                return true;
            }
            if (self(self, *each.bindings)) { return true; }
        }
        return false;
    };
    return walk(walk, *bindings_);
}

void browser::flush_for_read() {
    if (dirty_ >= dirty::styles) { resolve_styles(); }
    if (dirty_ >= dirty::layout) {
        run_layout();
    } else if (frames_stale()) {
        layout_frames();
    }
    if (dirty_ > dirty::paint) { dirty_ = dirty::paint; }
}

} // namespace ctbrowser::shell
