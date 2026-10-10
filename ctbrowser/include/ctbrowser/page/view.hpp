//===- ctbrowser/page/view.hpp - A borrowed view of a laid-out page -*- C++ -*-===//
//
// page_view: the trees a page query reads (the document, the cascade's style
// map, the box tree and the fragment tree) plus the viewport, borrowed for one
// call. The script bindings build one per getComputedStyle or
// getBoundingClientRect answer; a page with no script engine builds one the
// same way, so both get the same answers from the same code.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/style/style.hpp>

namespace ctbrowser::page {

/// What the page queries read. Every pointer is borrowed and must stay valid
/// for the call it is passed to; the next layout frees the trees. A null tree
/// means that stage has not run yet, and the queries then answer as for an
/// element with no box.
struct page_view {
    const document * doc = nullptr;
    atom_table * atoms = nullptr;
    /// The cascade's resolved map, keyed by style::engine::key_of.
    const style::style_map * styles = nullptr;
    /// The style engine, for media conditions and registered properties.
    const style::engine * selector_engine = nullptr;
    const layout::box_node * boxes = nullptr;
    const layout::fragment * fragments = nullptr;
    /// The layout viewport: the width layout ran at and the window's height.
    int viewport_width = 0;
    int viewport_height = 0;
    /// How far the viewport is scrolled.
    point viewport_scroll{};
    /// How far a scroll container has been scrolled. Empty means nothing has
    /// been, which is the case for every page no script has scrolled.
    std::function<point(node_id)> scroll_offset_of;
    /// A running animation's values for one element, each replacing the
    /// cascade's text for the property it animates. The third argument is the
    /// underlying (cascaded) value of a property. Empty means no animations.
    std::function<std::vector<std::pair<std::string, std::string>>(
        node_id, float, const std::function<std::string_view(std::string_view)> &)>
        animated_values;
};

/// The cascade's text for \p property of \p id in \p styles, trimmed, or ""
/// when the element has no resolved style or there is no map yet.
[[nodiscard]] std::string_view cascade_value(const style::style_map * styles, atom_table & atoms,
                                             node_id id, std::string_view property);

} // namespace ctbrowser::page
