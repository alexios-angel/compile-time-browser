#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <latch>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// A thread pool: one queue, one mutex, one condition variable.
//
// It was a work-stealing pool - a deque per worker, LIFO for the owner and
// FIFO for thieves, a round-robin submit - on the argument that the consumers
// would be recursive layout tasks. They never were: the only caller is
// parallel_for over raster tiles, whose tasks are independent and whose CALLER
// drains the pool, so a nested parallel_for cannot deadlock on either design.
// Everything the stealing bought was paid for on every submit and never
// measured. A lock-free Chase-Lev deque is the next step if profiling ever
// shows this one mutex mattering; it is much harder to get right and there is
// no evidence yet that it is needed.
//
// parallel_for is the only member that stays in this header, because it is a
// template. Everything else lives in scheduler.cpp.

namespace ctbrowser {

class scheduler {
public:
    using task = std::function<void()>;

    // 0 means "one worker per hardware thread, minus this one" - the calling
    // thread participates in parallel_for, so it is a worker too.
    explicit scheduler(std::size_t worker_count = 0);

    ~scheduler();

    scheduler(const scheduler &) = delete;
    scheduler & operator=(const scheduler &) = delete;

    [[nodiscard]] std::size_t worker_count() const noexcept { return workers_.size(); }

    void submit(task t);

    // Run f(0..n) across the pool and return once every index is done. The
    // CALLING thread helps, so parallel_for from inside a task cannot deadlock
    // waiting on a pool that is busy running it.
    template <typename F> void parallel_for(std::size_t n, F && f) {
        if (n == 0) { return; }
        if (n == 1 || workers_.empty()) {
            f(std::size_t{0});
            return;
        }
        std::latch done{static_cast<std::ptrdiff_t>(n)};
        for (std::size_t i = 0; i < n; ++i) {
            submit([&f, &done, i] {
                f(i);
                done.count_down();
            });
        }
        // help out instead of blocking idle
        while (!done.try_wait()) {
            if (!run_one()) { std::this_thread::yield(); }
        }
    }

private:
    [[nodiscard]] bool run_one();

    // An idle worker SLEEPS on idle_ until there is work, rather than waking
    // up to look: a pool polling every millisecond on every hardware thread
    // was measured at about 65% of a machine doing nothing.
    void run(const std::stop_token & stop);

    std::mutex mutex_;
    std::deque<task> tasks_;
    std::condition_variable_any idle_;
    // Destroyed first: workers join before the state they use, including
    // when starting a later worker throws during construction.
    std::vector<std::jthread> workers_;
};

} // namespace ctbrowser
