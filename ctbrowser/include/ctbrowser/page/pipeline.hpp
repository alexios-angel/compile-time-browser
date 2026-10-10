//===- ctbrowser/page/pipeline.hpp - Cascade and layout passes -*- C++ -*-===//
//
// The cascade and layout passes a page runs for a frame, with the viewport
// rules around them: the root's scrollbars taken out of the viewport units,
// and a second layout pass at the width a scrollbar leaves. The shell's
// browser and a page with no script engine both run these, so the two lay a
// document out the same way.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <utility>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/style/style.hpp>

namespace ctbrowser::page {

/// The viewport a document's units resolve against once its root's
/// scrollbars are taken out. CSS Values 4 §6.1.2 excludes them when the root
/// is `overflow: scroll` (a root that always shows them), and only then. Both
/// the page and a frame call this with their resolved styles.
[[nodiscard]] std::pair<float, float> viewport_less_scroll_root(atom_table & atoms,
                                                                const style::style_map & resolved,
                                                                node_id root, float width,
                                                                float height, float scrollbar);

/// Whether the viewport's overflow hides its scrollbar (CSS Overflow 3 §3.3).
/// The root's `overflow-y` propagates to the viewport, and when the root's is
/// `visible` the body's does instead. A viewport at `hidden` or `clip` shows
/// no scrollbar but still scrolls programmatically, which is what WPT
/// scrollintoview.html's `body { padding: 4000px; overflow: hidden }`
/// measures against innerWidth.
[[nodiscard]] bool viewport_hides_overflow(atom_table & atoms, const style::style_map & resolved,
                                           const read_txn & txn);

/// Runs the cascade over the document `txn` reads, for a window of `width` by
/// `height`. When the root asks for scrollbars, the viewport units exclude
/// them, so the cascade is run again at that size when it differs from the
/// engine's environment.
[[nodiscard]] style::style_map resolve_page_styles(style::engine & styles, const read_txn & txn,
                                                   atom_table & atoms, float width, float height,
                                                   float scrollbar_width);

/// What one page layout produced.
struct page_layout {
    layout::fragment fragments;
    /// The width layout ran at: the window's, less the scrollbar when the
    /// page overflows. `clientWidth` and percentages are relative to this.
    float layout_width = 0;
    /// The document's height: its scrolling area's (CSSOM View §2).
    float content_height = 0;
    /// The viewport scrolling area's width.
    float content_width = 0;
    /// False when the viewport's propagated overflow hides the scrollbar.
    bool scrollbar_shown = true;
};

/// Lays `boxes` out for a window of `width` by `height`. When the page
/// overflows and shows a scrollbar, layout runs a second time at the width
/// the scrollbar leaves. One more pass is enough: narrowing a page can only
/// make it taller, so a page that overflowed still overflows.
[[nodiscard]] page_layout lay_out_page(const layout::box_node & boxes,
                                       const layout::measure_text_fn & measure, atom_table & atoms,
                                       const style::style_map & resolved, const read_txn & txn,
                                       float width, float height, float scrollbar_width);

} // namespace ctbrowser::page
