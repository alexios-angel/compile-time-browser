//===- timer_queue.hpp - Timer scheduling -----------------------*- C++ -*-===//
//
// The deadline bookkeeping behind setTimeout/setInterval and the host's own
// queued tasks. Callbacks are stored by the caller, not here. Header-only, so
// a host without the script engine (the native runtime's Timers.hpp) keeps
// the same schedule as the VM's bindings.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace ctbrowser::shell {

/// Who scheduled a timer: the browser itself (host) or page script (page).
/// Before DOMContentLoaded only host tasks run (see entry::due).
enum class timer_source : std::uint8_t {
    host,
    page
};

/// Scheduling state only. The caller owns callback payloads and invokes them
/// between begin() and discard_cancelled(), after copying the due payloads.
class timer_queue {
public:
    /// One scheduled timer.
    struct entry {
        std::uint32_t id;
        double due_ms;
        double interval_ms;
        bool repeating;
        bool cancelled;
        timer_source source;

        /// Whether the timer should fire at `now_ms`; with
        /// `queued_tasks_only`, page timers never are.
        [[nodiscard]] bool due(double now_ms, bool queued_tasks_only = false) const noexcept {
            return !cancelled && due_ms <= now_ms &&
                   !(queued_tasks_only && source == timer_source::page);
        }
    };

    void schedule(std::uint32_t id, double now_ms, double delay_ms, bool repeating,
                  timer_source source = timer_source::host) {
        const double interval = std::max(0.0, delay_ms);
        entries_.push_back(entry{id, now_ms + interval, interval, repeating, false, source});
    }

    [[nodiscard]] std::optional<std::size_t> find_active(std::uint32_t id) const noexcept {
        const auto timer = std::ranges::find_if(entries_, [id](const entry & candidate) {
            return candidate.id == id && !candidate.cancelled;
        });
        if (timer == entries_.end()) { return std::nullopt; }
        return static_cast<std::size_t>(timer - entries_.begin());
    }

    /// Rechecks cancellation and retires a one-shot or rearms an interval
    /// before its callback is invoked; returns whether to invoke it. A
    /// snapshot's deadline is not checked a second time. An ineligible
    /// callback (for example, a removed owner) is cancelled instead.
    [[nodiscard]] bool begin(std::uint32_t id, double now_ms, bool eligible = true) noexcept {
        const auto index = find_active(id);
        if (!index) { return false; }
        entry & timer = entries_[*index];
        if (eligible && timer.repeating) {
            timer.due_ms = now_ms + timer.interval_ms;
        } else {
            timer.cancelled = true;
        }
        return eligible;
    }

    void cancel(std::uint32_t id) noexcept {
        for (entry & timer : entries_) {
            if (timer.id == id) { timer.cancelled = true; }
        }
    }

    void discard_cancelled() {
        std::erase_if(entries_, [](const entry & timer) { return timer.cancelled; });
    }

    /// Milliseconds until the soonest active timer is due; infinity when none
    /// is.
    [[nodiscard]] double next_delay(double now_ms) const noexcept {
        double soonest = std::numeric_limits<double>::infinity();
        for (const entry & timer : entries_) {
            if (!timer.cancelled) {
                soonest = std::min(soonest, std::max(0.0, timer.due_ms - now_ms));
            }
        }
        return soonest;
    }

    /// Registration order, including cancelled entries until
    /// discard_cancelled(). A caller keeping parallel payloads removes the same
    /// cancelled positions. schedule() and discard_cancelled() invalidate this
    /// view.
    [[nodiscard]] std::span<const entry> entries() const noexcept { return entries_; }

private:
    std::vector<entry> entries_;
};

} // namespace ctbrowser::shell
