//===- ctbrowser/lib/Page/client_rects.cpp - Element client rectangles ----===//
//
// Where an element's boxes are in the viewport: the fragment walk behind
// getClientRects, getBoxQuads and getBoundingClientRect. Bare section numbers
// (§7) refer to the CSSOM View specification.
//
//===----------------------------------------------------------------------===//

#include "computed_style/internal.hpp"

#include <ctbrowser/page/client_rects.hpp>

namespace ctbrowser::page {

using namespace detail;

std::string_view cascade_value(const style::style_map * styles, atom_table & atoms, node_id id,
                               std::string_view property) {
    if (styles == nullptr) { return {}; }
    const auto found = styles->find(style::engine::key_of(id));
    if (found == styles->end() || !found->second) { return {}; }
    return trim(found->second->get(atoms.intern(property)), html_whitespace);
}

std::vector<box_geometry> client_boxes(const page_view & view, node_id self,
                                       std::string_view kind) {
    std::vector<box_geometry> out;
    if (view.fragments == nullptr || !self) { return out; }
    const point viewport = view.viewport_scroll;
    const auto walk = [&](auto && walk_, const layout::fragment & at, float dx, float dy,
                          bool fixed, transform matrix) -> void {
        const rect box = at.absolute_bounds(dx, dy);
        const bool is_fixed =
            fixed || (at.box != nullptr && at.box->position == layout::position_kind::fixed);
        // Layout already applies a translation to fragment positions. Replace
        // that shift with the full transform about this box's origin, then
        // compose with its ancestors before bounding the four corners.
        // ponytail: same 2D/px transform subset as computed style; extend the
        // shared parser for 3D and relative transform lengths.
        if (at.box != nullptr && !is_inline_box(at)) {
            if (const auto m = transform_matrix(
                    cascade_value(view.styles, *view.atoms, at.source, "transform"))) {
                const transform local{static_cast<float>((*m)[0]), static_cast<float>((*m)[1]),
                                      static_cast<float>((*m)[2]), static_cast<float>((*m)[3]),
                                      static_cast<float>((*m)[4]), static_cast<float>((*m)[5])};
                style::css::length_context lengths;
                lengths.font_size = at.box->font_size;
                lengths.line_height = at.box->line_height;
                lengths.viewport_width = static_cast<float>(view.viewport_width);
                lengths.viewport_height = static_cast<float>(view.viewport_height);
                style::css::color_context context;
                context.lengths = &lengths;
                const std::string origin = style::css::computed_transform_property(
                    "transform-origin",
                    cascade_value(view.styles, *view.atoms, at.source, "transform-origin"), context,
                    box.width, box.height);
                point pivot{box.width / 2, box.height / 2};
                const auto parts = split_top_level(origin, html_whitespace);
                if (parts.size() >= 2) {
                    pivot = {layout::parse_length(parts[0]).resolve(box.width, lengths.font_size),
                             layout::parse_length(parts[1]).resolve(box.height, lengths.font_size)};
                }
                const point shift{at.box->translate.x.resolve(box.width, lengths.font_size),
                                  at.box->translate.y.resolve(box.height, lengths.font_size)};
                matrix = transform::translation(-box.x - pivot.x, -box.y - pivot.y)
                             .then(local)
                             .then(transform::translation(box.x - shift.x + pivot.x,
                                                          box.y - shift.y + pivot.y))
                             .then(matrix);
            }
        }
        if (at.source == self) {
            rect selected = box;
            const layout::resolved_edges e = layout::edges_of(at);
            if (kind == "margin") {
                selected = {box.x - at.margin_left, box.y - at.margin_top,
                            box.width + at.margin_left + at.margin_right,
                            box.height + at.margin_top + at.margin_bottom};
            } else if (kind == "padding" || kind == "content") {
                const float left = e.border_left + (kind == "content" ? e.pad_left : 0);
                const float top = e.border_top + (kind == "content" ? e.pad_top : 0);
                const float right = e.border_right + (kind == "content" ? e.pad_right : 0);
                const float bottom = e.border_bottom + (kind == "content" ? e.pad_bottom : 0);
                selected = {box.x + left, box.y + top, std::max(0.0f, box.width - left - right),
                            std::max(0.0f, box.height - top - bottom)};
            }
            transform to_viewport = transform::translation(selected.x, selected.y).then(matrix);
            if (!is_fixed) {
                to_viewport = to_viewport.then(transform::translation(-viewport.x, -viewport.y));
            }
            out.push_back({rect{0, 0, selected.width, selected.height}, to_viewport});
        }
        point inner{box.x, box.y};
        if (at.box != nullptr && at.box->scroll_container && at.source && view.scroll_offset_of) {
            const point offset = view.scroll_offset_of(at.source);
            inner.x -= offset.x;
            inner.y -= offset.y;
        }
        for (const layout::fragment & child : at.children) {
            walk_(walk_, child, inner.x, inner.y, is_fixed, matrix);
        }
    };
    walk(walk, *view.fragments, 0, 0, false, transform{});
    return out;
}

rect box_geometry::bounding_rect() const {
    const point origin = to_viewport.apply(0, 0);
    const point x_axis{to_viewport.a * bounds.width, to_viewport.b * bounds.width};
    const point y_axis{to_viewport.c * bounds.height, to_viewport.d * bounds.height};
    return {origin.x + std::min(0.0f, x_axis.x) + std::min(0.0f, y_axis.x),
            origin.y + std::min(0.0f, x_axis.y) + std::min(0.0f, y_axis.y),
            std::abs(x_axis.x) + std::abs(y_axis.x), std::abs(x_axis.y) + std::abs(y_axis.y)};
}

rect bounding_client_rect(std::span<const rect> rects) {
    if (rects.empty()) { return rect{}; }
    std::optional<rect> bound;
    for (const rect & r : rects) {
        if (r.width == 0 || r.height == 0) { continue; }
        if (!bound) {
            bound = r;
            continue;
        }
        const float right = std::max(bound->x + bound->width, r.x + r.width);
        const float bottom = std::max(bound->y + bound->height, r.y + r.height);
        bound->x = std::min(bound->x, r.x);
        bound->y = std::min(bound->y, r.y);
        bound->width = right - bound->x;
        bound->height = bottom - bound->y;
    }
    return bound.value_or(rects.front());
}

} // namespace ctbrowser::page
