//===- Timers.hpp - Native setTimeout ---------------------------*- C++ -*-===//
//
// setTimeout, setInterval and their clear functions for generated native code,
// and the timer pass a native host runs when its clock moves. The deadlines are kept by ctbrowser's
// own timer_queue (the bookkeeping the VM's bindings use), with the callback's C++ callable beside
// it: the table owns each callback, and the captures bound into it, until it has run or the program
// ends.
//
// Only programs that use timers include this header.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Microtasks.hpp"
#include "Number.hpp"

#include <ctbrowser/core/number.hpp>
#include <ctbrowser/shell/timer_queue.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ctnative {

/// A timer's handle (HTML 8.6 "the handle"): what setTimeout and setInterval
/// return and clearTimeout and clearInterval take. A program may keep it and
/// pass it back; the native lowering refuses any use that would read it as a
/// Number, since its value need not equal the VM's, whose host timers share
/// the count. A default-constructed handle names no timer.
struct timer_id {
    std::uint32_t value = 0;

    constexpr timer_id() = default;
    explicit constexpr timer_id(std::uint32_t handle) noexcept : value(handle) {}
    /// The handle a null or undefined becomes where a program keeps either in
    /// a handle's place (a field that starts null): the lowering converts
    /// only those, through ToNumber (0 and NaN), and neither names a timer.
    constexpr timer_id(js_num /*absent*/) noexcept {}

    /// Whether the handle names a timer: every handle a program gets does,
    /// as the VM's handles are all greater than zero.
    [[nodiscard]] constexpr bool is_set() const noexcept { return value != 0; }
    friend constexpr bool operator==(timer_id, timer_id) = default;
};

/// The timers a native program scheduled. One table per program
/// (timers::program()), as a program is one realm with one clock.
///
/// The clock starts where the VM's does (1 ms) and moves only when the host
/// calls advance(), as the VM's moves only between ticks.
class timers {
public:
    /// A callback as the table calls it. The native lowering passes only
    /// callbacks that declare no parameter and take no extra arguments.
    using callback = std::move_only_function<void()>;

    /// The table every native setTimeout schedules into.
    static timers & program() {
        static timers table;
        return table;
    }

    /// setTimeout or, `repeating`, setInterval (HTML 8.6, "timer
    /// initialisation steps") with a Function handler: the timeout is
    /// converted as a `long` (ToInt32) and a negative one counts as 0. Returns
    /// the handle. An interval runs again each time its timeout passes after
    /// a run, until it is cleared.
    timer_id add(callback run, double timeout_ms, bool repeating = false) {
        const std::uint32_t id = ++next_id_;
        callbacks_.emplace(id, std::make_unique<callback>(std::move(run)));
        schedule_.schedule(id, now_ms_, ctbrowser::number_to_int32(timeout_ms), repeating,
                           ctbrowser::shell::timer_source::page);
        return timer_id{id};
    }

    /// clearTimeout(handle) and clearInterval(handle), which are one
    /// operation (HTML 8.6): the timer, if still scheduled, never runs again.
    /// A handle that names no timer, or one that ran or was cleared, does
    /// nothing. A timer clearing itself finishes its run.
    void clear(timer_id handle) noexcept {
        if (handle.is_set()) { schedule_.cancel(handle.value); }
    }

    /// Moves the clock by `elapsed_ms`, then runs every timer due at the new
    /// time, each as its own task in the order they were scheduled, as the
    /// VM's dom_bindings::run_timers does after browser::tick moves its clock.
    /// A timer a callback schedules waits for the next advance(), even with a
    /// 0 delay. A callback that throws is reported (reported()) and the rest
    /// still run. The promise jobs each callback queued run before the next
    /// one starts. Returns how many ran.
    std::size_t advance(double elapsed_ms) {
        now_ms_ += elapsed_ms;
        std::vector<std::uint32_t> due;
        for (const auto & entry : schedule_.entries()) {
            if (entry.due(now_ms_)) { due.push_back(entry.id); }
        }
        std::size_t ran = 0;
        for (const std::uint32_t id : due) {
            if (!schedule_.begin(id, now_ms_)) { continue; }
            // The callable stays at one address while it schedules others.
            callback & run = *callbacks_.at(id);
            ++ran;
            try {
                run();
            } catch (const std::exception & thrown) { reported_.emplace_back(thrown.what()); }
            // A timer is a task, and a task ends in a microtask checkpoint.
            microtasks::program().checkpoint();
        }
        for (const auto & entry : schedule_.entries()) {
            if (entry.cancelled) { callbacks_.erase(entry.id); }
        }
        schedule_.discard_cancelled();
        return ran;
    }

    /// How many timers are still waiting.
    [[nodiscard]] std::size_t pending() const noexcept { return callbacks_.size(); }

    /// The messages of the exceptions callbacks threw, oldest first.
    [[nodiscard]] const std::vector<std::string> & reported() const noexcept { return reported_; }

private:
    ctbrowser::shell::timer_queue schedule_;
    std::unordered_map<std::uint32_t, std::unique_ptr<callback>> callbacks_;
    std::vector<std::string> reported_;
    double now_ms_ = 1;
    std::uint32_t next_id_ = 0;
};

/// setTimeout(handler, timeout) and setInterval(handler, timeout) (HTML 8.6),
/// as generated code spells them: `ctnative::setTimeout(f, delay)`. Each
/// returns the timer's handle.
template <bool Repeating> struct set_timer_function {
    template <std::invocable F> timer_id operator()(F && handler, js_num timeout) const {
        return timers::program().add(timers::callback{std::forward<F>(handler)}, timeout.value(),
                                     Repeating);
    }
};
inline constexpr set_timer_function<false> setTimeout{};
inline constexpr set_timer_function<true> setInterval{};

/// clearTimeout(handle) and clearInterval(handle) (HTML 8.6):
/// `ctnative::clearTimeout(id)`.
struct clear_timer_function {
    void operator()(timer_id handle) const noexcept { timers::program().clear(handle); }
};
inline constexpr clear_timer_function clearTimeout{};
inline constexpr clear_timer_function clearInterval{};

} // namespace ctnative
