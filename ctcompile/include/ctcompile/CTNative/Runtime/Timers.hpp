//===- Timers.hpp - Native setTimeout ---------------------------*- C++ -*-===//
//
// setTimeout for generated native code, and the timer pass a native host runs
// when its clock moves. The deadlines are kept by ctbrowser's own timer_queue
// (the bookkeeping the VM's bindings use), with the callback's C++ callable
// beside it: the table owns each callback, and the captures bound into it,
// until it has run or the program ends.
//
// Only programs that call setTimeout include this header.
//
//===----------------------------------------------------------------------===//

#pragma once

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

    /// setTimeout(handler, timeout) (HTML 8.6, "timer initialisation steps")
    /// with a Function handler: the timeout is converted as a `long`
    /// (ToInt32), a negative one counts as 0, and the returned handle is
    /// greater than zero. Returns the handle.
    std::uint32_t add(callback run, double timeout_ms) {
        const std::uint32_t id = ++next_id_;
        callbacks_.emplace(id, std::make_unique<callback>(std::move(run)));
        schedule_.schedule(id, now_ms_, ctbrowser::number_to_int32(timeout_ms), false,
                           ctbrowser::shell::timer_source::page);
        return id;
    }

    /// Moves the clock by `elapsed_ms`, then runs every timer due at the new
    /// time, each as its own task in the order they were scheduled, as the
    /// VM's dom_bindings::run_timers does after browser::tick moves its clock.
    /// A timer a callback schedules waits for the next advance(), even with a
    /// 0 delay. A callback that throws is reported (reported()) and the rest
    /// still run. Returns how many ran.
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

/// setTimeout(handler, timeout) (HTML 8.6), as generated code spells it:
/// `ctnative::setTimeout(f, delay)`. Returns the timer's handle as a Number.
struct set_timeout_function {
    template <std::invocable F> js_num operator()(F && handler, js_num timeout) const {
        const std::uint32_t id =
            timers::program().add(timers::callback{std::forward<F>(handler)}, timeout.value());
        return js_num{static_cast<double>(id)};
    }
};
inline constexpr set_timeout_function setTimeout{};

} // namespace ctnative
