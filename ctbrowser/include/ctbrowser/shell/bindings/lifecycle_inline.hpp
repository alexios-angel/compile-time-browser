#pragma once

#include "../bindings.hpp"

namespace ctbrowser::shell {

// `on_mutation` is how the browser learns it has to re-run the pipeline.
// Taking it as a callback rather than a browser reference keeps this
// testable on its own and keeps the dependency pointing one way.
inline dom_bindings::dom_bindings(document & doc, atom_table & atoms, canvas_store & canvases,
                                  form_store & forms, std::function<void()> on_mutation,
                                  std::function<void(node_id)> on_focus)
    : doc_(&doc), atoms_(&atoms), canvases_(&canvases), forms_(&forms),
      on_mutation_(std::move(on_mutation)), on_focus_(std::move(on_focus)) {}

// Whether fetch() may open a socket when the registry misses. Off makes a
// run hermetic, which is what a test wants and what CTBROWSER_NETWORK=0
// selects.
inline auto dom_bindings::allow_network(bool allowed) -> void {
    network_allowed_ = allowed;
}

// NAVIGATION, as state rather than as an action. `location.reload()` cannot
// reload the page where it is called: the reload tears down this context and
// the program still running inside it. So it records the request and the
// browser drains it between ticks.
[[nodiscard]] inline auto dom_bindings::reload_requested() const noexcept -> bool {
    return reload_requested_;
}

// `element.click()` does two things: it dispatches a click event, and - if
// nothing called preventDefault - it performs the element's DEFAULT ACTION.
// The second half belongs to the browser (following a link, toggling a
// checkbox, submitting a form), so it comes in as a hook for the same reason
// on_mutation does: the dependency points one way.
inline auto dom_bindings::set_activate_hook(std::function<void(node_id)> hook) -> void {
    on_activate_ = std::move(hook);
}

// Layout results, so offsetWidth and friends can answer. Set by the
// browser after each layout; null until the first one, and the natives
// return 0 then rather than pretending.
inline auto dom_bindings::observe_layout(const layout::fragment * fragments) -> void {
    fragments_ = fragments;
}

// The cascade's output and the box tree, for getComputedStyle. Three sources
// are needed rather than one because `style::computed_style` is not a
// computed style: it holds only the declarations that MATCHED, as text, with
// no inheritance and no initial values. So a keyword comes from the style
// map, a resolved length from the box tree, and a used size from the
// fragment - see lib/Shell/bindings/computed_style/.
inline auto dom_bindings::observe_styles(const style::style_map * styles) -> void {
    styles_ = styles;
}

inline auto dom_bindings::observe_boxes(const layout::box_node * boxes) -> void {
    boxes_ = boxes;
}

[[nodiscard]] inline auto dom_bindings::loaded_frames() const -> std::vector<loaded_frame> {
    std::vector<loaded_frame> out;
    for (const frame_entry & entry : frames_) {
        if (entry.bindings != nullptr) { out.push_back({entry.element, entry.bindings}); }
    }
    return out;
}

[[nodiscard]] inline auto dom_bindings::owned_document() noexcept -> document & {
    return *doc_;
}

// Milliseconds since the page loaded, for performance.now and the timers.
inline auto dom_bindings::advance_clock(double ms) -> void {
    now_ms_ += ms;
}

[[nodiscard]] inline auto dom_bindings::now_ms() const noexcept -> double {
    return now_ms_;
}

// A resource the BROWSER loaded for an element - a `<link rel=stylesheet>`,
// a `<style>`, a `<script>` - is announced at that element on the next
// tick, the way an `<iframe>`'s load is: `load`, or `error` when the bytes
// were not found. Queued rather than fired because the page's own script
// registers the listener after the element has already been processed.
inline auto dom_bindings::announce_load(node_id id, bool ok) -> void {
    frame_loads_.push_back(pending_frame{id, ok, true});
}

[[nodiscard]] inline auto dom_bindings::pending_timers() const noexcept -> std::size_t {
    return timers_.size();
}

// The first fault a timer or animation-frame callback raised. Empty when
// the page's callbacks are running cleanly.
[[nodiscard]] inline auto dom_bindings::callback_error() const noexcept -> const std::string & {
    return callback_error_;
}

} // namespace ctbrowser::shell
