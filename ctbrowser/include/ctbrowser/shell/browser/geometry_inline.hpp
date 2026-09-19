#pragma once

#include "../browser.hpp"

namespace ctbrowser::shell {

// Tiles are discarded in frame(), not here: every level at or above
// `raster` invalidates them, and doing it in one place is what stops a new
// dirty level from silently forgetting to.
inline auto browser::mark(dirty d) -> void {
    dirty_ = worse(dirty_, d);
}

// THE CONTENT BOX OF A CONTROL, in ONE place.
//
// The caret, the hit test, the selection and the painter all have to agree
// about where a field's text starts: they all ask this, and it asks the
// cascade.
[[nodiscard]] inline auto browser::content_box_of(node_id id, const rect & box) const -> rect {
    const layout::box_node * found = find_box(boxes_, id);
    if (found == nullptr) { return box; }
    const layout::resolved_edges e =
        layout::resolve_edges(*found, layout::constraints{box.width, box.height, found->font_size});
    return rect{box.x + e.content_left(), box.y + e.content_top(),
                std::max(0.0f, box.width - e.horizontal_inner()),
                std::max(0.0f, box.height - e.vertical_inner())};
}

// The line a caret is ON, as an index into `lines`. FIRST match wins: a
// soft break makes end == begin, so a caret sitting on a wrap boundary
// matches BOTH lines, and the painter would draw two carets.
[[nodiscard]] inline auto browser::caret_line(const field_layout & geometry, std::size_t caret)
    -> std::size_t {
    for (std::size_t index = 0; index < geometry.lines.size(); ++index) {
        const auto [begin, end] = geometry.lines[index];
        if (caret >= begin && caret <= end) { return index; }
    }
    return geometry.lines.empty() ? 0 : geometry.lines.size() - 1;
}

// The colour a control's own text is drawn in. A DISABLED control ignores
// the cascade here: `color` on a disabled button is not what a user needs
// to see, and greyed-out is the only signal the control is dead.
[[nodiscard]] inline auto browser::control_text_colour(
    node_id id, const ctbrowser::style::computed_style_ptr & style) -> color {
    return is_disabled(id) ? color{ctbrowser::style::ua_widget_disabled_text} : text_colour(style);
}

[[nodiscard]] inline auto browser::find_box(const layout::box_node & at, node_id id)
    -> const layout::box_node * {
    if (at.source == id) { return &at; }
    for (const layout::box_node & child : at.children) {
        if (const layout::box_node * hit = find_box(child, id)) { return hit; }
    }
    return nullptr;
}

inline auto browser::outline(const rect & box, color c, ctbrowser::paint::display_list & into,
                             node_id id) -> void {
    into.fill(rect{box.x, box.y, box.width, 1}, c, id);
    into.fill(rect{box.x, box.bottom() - 1, box.width, 1}, c, id);
    into.fill(rect{box.x, box.y, 1, box.height}, c, id);
    into.fill(rect{box.right() - 1, box.y, 1, box.height}, c, id);
}

// What the last submission would have sent. There is no network, so
// producing the data and stopping is the honest half of submitting - and it
// is what a test can check.
[[nodiscard]] inline auto browser::last_submission() const noexcept
    -> const std::vector<std::pair<std::string, std::string>> & {
    return last_submission_;
}

// The layout viewport as an integer, falling back to the window before the
// first layout has run. Both callers of observe_viewport go through this:
// bindings are installed LAZILY, the first time a page runs script, which is
// after run_layout - so a setup path that reported options_.width silently
// clobbered the narrower number layout had already used.
[[nodiscard]] inline auto browser::layout_viewport_width() const noexcept -> int {
    return layout_width_ > 0 ? static_cast<int>(layout_width_) : options_.width;
}

} // namespace ctbrowser::shell
