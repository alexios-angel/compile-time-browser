#include <ctbrowser/shell/timer_queue.hpp>

#include <algorithm>
#include <limits>

namespace ctbrowser::shell {

void timer_queue::schedule(std::uint32_t id, double now_ms, double delay_ms, bool repeating,
                           timer_source source) {
    const double interval = std::max(0.0, delay_ms);
    entries_.push_back(entry{id, now_ms + interval, interval, repeating, false, source});
}

bool timer_queue::entry::due(double now_ms, bool queued_tasks_only) const noexcept {
    return !cancelled && due_ms <= now_ms && !(queued_tasks_only && source == timer_source::page);
}

std::optional<std::size_t> timer_queue::find_active(std::uint32_t id) const noexcept {
    const auto timer = std::ranges::find_if(entries_, [id](const entry & candidate) {
        return candidate.id == id && !candidate.cancelled;
    });
    if (timer == entries_.end()) { return std::nullopt; }
    return static_cast<std::size_t>(timer - entries_.begin());
}

bool timer_queue::begin(std::uint32_t id, double now_ms, bool eligible) noexcept {
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

void timer_queue::cancel(std::uint32_t id) noexcept {
    for (entry & timer : entries_) {
        if (timer.id == id) { timer.cancelled = true; }
    }
}

void timer_queue::discard_cancelled() {
    std::erase_if(entries_, [](const entry & timer) { return timer.cancelled; });
}

double timer_queue::next_delay(double now_ms) const noexcept {
    double soonest = std::numeric_limits<double>::infinity();
    for (const entry & timer : entries_) {
        if (!timer.cancelled) { soonest = std::min(soonest, std::max(0.0, timer.due_ms - now_ms)); }
    }
    return soonest;
}

} // namespace ctbrowser::shell
