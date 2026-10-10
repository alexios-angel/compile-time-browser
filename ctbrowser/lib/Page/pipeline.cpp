//===- ctbrowser/lib/Page/pipeline.cpp - Cascade and layout passes --------===//
//
// The cascade and layout passes of one frame, and the viewport rules around
// them. See ctbrowser/page/pipeline.hpp.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/page/pipeline.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/layout/overflow.hpp>

#include <algorithm>

namespace ctbrowser::page {

std::pair<float, float> viewport_less_scroll_root(atom_table & atoms,
                                                  const style::style_map & resolved, node_id root,
                                                  float width, float height, float scrollbar) {
    const auto found = resolved.find(style::engine::key_of(root));
    if (found == resolved.end() || !found->second) { return {width, height}; }
    if (found->second->get(atoms.intern("overflow-y")) == "scroll") {
        width = std::max(0.0f, width - scrollbar);
    }
    if (found->second->get(atoms.intern("overflow-x")) == "scroll") {
        height = std::max(0.0f, height - scrollbar);
    }
    return {width, height};
}

bool viewport_hides_overflow(atom_table & atoms, const style::style_map & resolved,
                             const read_txn & txn) {
    const auto overflow_y = [&](node_id id) -> std::string_view {
        if (!id) { return {}; }
        const auto found = resolved.find(style::engine::key_of(id));
        if (found == resolved.end() || !found->second) { return {}; }
        return trim(found->second->get(atoms.intern("overflow-y")), html_whitespace);
    };
    const node_id root = txn.root();
    std::string_view used = overflow_y(root);
    if (used.empty() || ascii_iequals(used, "visible")) {
        for (const node_id child : txn.children(root)) {
            if (txn.element_ns(child) == node_ns::html && txn.local_name(child) == "body") {
                used = overflow_y(child);
                break;
            }
        }
    }
    return ascii_iequals(used, "hidden") || ascii_iequals(used, "clip");
}

style::style_map resolve_page_styles(style::engine & styles, const read_txn & txn,
                                     atom_table & atoms, float width, float height,
                                     float scrollbar_width) {
    style::style_map resolved = styles.resolve_all(txn);
    // A root with `overflow: scroll` always has its scrollbars, and the
    // viewport units exclude them (viewport_less_scroll_root). The cascade is
    // re-run at that size when it differs from the window's, which is the one
    // extra pass a page that asked for scrollbars pays.
    const auto [inner_width, inner_height] =
        viewport_less_scroll_root(atoms, resolved, txn.root(), width, height, scrollbar_width);
    style::css::media_environment env = styles.environment();
    if (env.viewport_width != inner_width || env.viewport_height != inner_height) {
        env.viewport_width = inner_width;
        env.viewport_height = inner_height;
        (void)styles.set_environment(env);
        resolved = styles.resolve_all(txn);
    }
    return resolved;
}

page_layout lay_out_page(const layout::box_node & boxes, const layout::measure_text_fn & measure,
                         atom_table & atoms, const style::style_map & resolved,
                         const read_txn & txn, float width, float height, float scrollbar_width) {
    const layout::engine eng{measure};
    page_layout out;
    // The page is as tall as its scrolling area (CSSOM View §2), not as its
    // root box: an absolutely positioned box below the fold is reachable by
    // scrolling in every browser, and the WPT scroll-behavior-main-frame-window
    // tests lay their whole content out that way. There is no viewport floor:
    // a short page stays shorter than the window.
    const auto page_height = [&](float at_width) {
        return std::max(out.fragments.bounds.height,
                        layout::viewport_scrolling_area(out.fragments, at_width, 0).height);
    };
    out.fragments = eng.run(boxes, width, height);
    out.content_height = page_height(width);

    // Two passes when the page overflows: the scrollbar takes width away from
    // the content, and content laid out at the full width would run under it.
    out.layout_width = width;
    out.scrollbar_shown = !viewport_hides_overflow(atoms, resolved, txn);
    if (scrollbar_width > 0 && out.content_height > height && out.scrollbar_shown) {
        out.layout_width = width - scrollbar_width;
        out.fragments = eng.run(boxes, out.layout_width, height);
        out.content_height = page_height(out.layout_width);
    }
    out.content_width =
        layout::viewport_scrolling_area(out.fragments, out.layout_width, height).width;
    return out;
}

} // namespace ctbrowser::page
