#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ctbrowser::shell {

enum class timer_source : std::uint8_t {
    host,
    page
};

// Scheduling state only. The caller owns callback payloads and invokes them
// between begin() and discard_cancelled(), after copying the due payloads.
class timer_queue {
public:
    struct entry {
        std::uint32_t id;
        double due_ms;
        double interval_ms;
        bool repeating;
        bool cancelled;
        timer_source source;

        [[nodiscard]] bool due(double now_ms, bool queued_tasks_only = false) const noexcept;
    };

    void schedule(std::uint32_t id, double now_ms, double delay_ms, bool repeating,
                  timer_source source = timer_source::host);
    [[nodiscard]] std::optional<std::size_t> find_active(std::uint32_t id) const noexcept;
    // Recheck cancellation and retire a one-shot or rearm an interval before
    // invoking its callback. A snapshot's deadline is not checked a second time.
    // An ineligible callback (for example, a removed owner) is canceled instead.
    [[nodiscard]] bool begin(std::uint32_t id, double now_ms, bool eligible = true) noexcept;
    void cancel(std::uint32_t id) noexcept;
    void discard_cancelled();
    [[nodiscard]] double next_delay(double now_ms) const noexcept;

    // Registration order, including canceled entries until batch cleanup. A
    // caller retaining parallel payloads removes the same canceled positions.
    // schedule() and discard_cancelled() invalidate this view.
    [[nodiscard]] std::span<const entry> entries() const noexcept { return entries_; }

private:
    std::vector<entry> entries_;
};

} // namespace ctbrowser::shell
