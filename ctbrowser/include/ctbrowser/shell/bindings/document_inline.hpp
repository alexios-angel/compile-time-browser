#pragma once

#include "../bindings.hpp"

namespace ctbrowser::shell {

// TIME AS A SCRIPT OBSERVES IT: `performance.now()` and an event's
// timeStamp. The engine's one clock moves only between ticks, so within a
// script two readings were equal forever and `while (performance.now() <
// t)` never ended (Event-timestamp-safe-resolution.html spins until two
// events differ). Each observation advances it by the 5 us a browser
// coarsens to, counted from the tick's start - so it is still a function
// of the page's own behaviour and a golden stays a golden. The first
// reading of a tick is the clock itself; an event the ENGINE makes reads
// the clock, not this.
[[nodiscard]] inline auto dom_bindings::observed_now() -> double {
    return now_ms_ + 0.005 * static_cast<double>(time_reads_++);
}

// THE LAYOUT FLUSH. A box read from script - offsetX of a dispatched
// click, getBoundingClientRect - is read from the layout AS THE SCRIPT
// LEFT IT, which before the first frame is no layout at all. The browser
// installs the same flush its getComputedStyle wrapper does; anything
// reading `box_of` calls this first. Only what is stale runs.
inline auto dom_bindings::set_layout_hook(std::function<void()> hook) -> void {
    flush_layout_ = std::move(hook);
}

// WHERE THE VIEWPORT'S SCROLL POSITION LIVES for this document: the
// browser's, for the page. Unset, the bindings keep one of their own (a
// frame's document). See the SCROLLING section above.
inline auto dom_bindings::set_viewport_scroll_hooks(std::function<point()> get,
                                                    std::function<void(point)> set) -> void {
    viewport_scroll_get_ = std::move(get);
    viewport_scroll_set_ = std::move(set);
}

// A FRAME'S DOCUMENT FLUSHES THROUGH THE PAGE'S HOOK: only the primary
// bindings are given one, and the browser's flush lays out every frame
// whose document moved (frames_stale) - so a frame's `scrollWidth` read
// right after an innerHTML write answered from no layout at all.
inline auto dom_bindings::flush_layout() -> void {
    if (flush_layout_) {
        flush_layout_();
    } else if (primary_ != nullptr && primary_->flush_layout_) {
        primary_->flush_layout_();
    }
}

inline auto dom_bindings::note_unstarted_script(node_id id) -> void {
    unstarted_scripts_.push_back(id);
}

[[nodiscard]] inline auto dom_bindings::moved_by_mutation() const -> std::span<const node_id> {
    return moved_by_mutation_;
}

inline auto dom_bindings::set_script_runner(script_runner run) -> void {
    run_script_ = std::move(run);
}

} // namespace ctbrowser::shell
