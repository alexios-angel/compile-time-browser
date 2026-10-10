//===- timer_queue.hpp - Timer scheduling -----------------------*- C++ -*-===//
//
// The deadline bookkeeping behind setTimeout/setInterval and the host's own
// queued tasks. Callbacks are stored by the caller, not here.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
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
        [[nodiscard]] bool due(double now_ms, bool queued_tasks_only = false) const noexcept;
    };

    void schedule(std::uint32_t id, double now_ms, double delay_ms, bool repeating,
                  timer_source source = timer_source::host);
    [[nodiscard]] std::optional<std::size_t> find_active(std::uint32_t id) const noexcept;
    /// Rechecks cancellation and retires a one-shot or rearms an interval
    /// before its callback is invoked; returns whether to invoke it. A
    /// snapshot's deadline is not checked a second time. An ineligible
    /// callback (for example, a removed owner) is cancelled instead.
    [[nodiscard]] bool begin(std::uint32_t id, double now_ms, bool eligible = true) noexcept;
    void cancel(std::uint32_t id) noexcept;
    void discard_cancelled();
    [[nodiscard]] double next_delay(double now_ms) const noexcept;

    /// Registration order, including cancelled entries until
    /// discard_cancelled(). A caller keeping parallel payloads removes the same
    /// cancelled positions. schedule() and discard_cancelled() invalidate this
    /// view.
    [[nodiscard]] std::span<const entry> entries() const noexcept { return entries_; }

private:
    std::vector<entry> entries_;
};

} // namespace ctbrowser::shell
