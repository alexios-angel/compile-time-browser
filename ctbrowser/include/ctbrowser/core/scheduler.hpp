//===- scheduler.hpp - Thread pool ---------------------------- -*- C++ -*-===//
//
// A thread pool: one queue, one mutex, one condition variable.
//
// Deliberately not work-stealing: the only caller is parallel_for over raster
// tiles, whose tasks are independent and whose caller helps drain the pool, so
// a nested parallel_for cannot deadlock. Consider a lock-free Chase-Lev deque
// only if profiling shows this mutex mattering.
//
// parallel_for is defined here because it is a template; everything else is in
// lib/Core/scheduler.cpp.
//
//===----------------------------------------------------------------------===//

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

namespace ctbrowser {

/// A thread pool for raster tiles.
class scheduler {
public:
    using task = std::function<void()>;

    /// 0 means one worker per hardware thread minus one: the calling thread
    /// participates in parallel_for, so it is a worker too.
    explicit scheduler(std::size_t worker_count = 0);

    ~scheduler();

    scheduler(const scheduler &) = delete;
    scheduler & operator=(const scheduler &) = delete;

    [[nodiscard]] std::size_t worker_count() const noexcept { return workers_.size(); }

    /// Queues `t` to run on some worker.
    void submit(task t);

    /// Runs f(i) for every i in [0, n) across the pool and returns once all are
    /// done. The calling thread helps, so parallel_for from inside a task
    /// cannot deadlock waiting on a pool that is busy running it.
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
        // Run queued tasks while waiting instead of blocking.
        while (!done.try_wait()) {
            if (!run_one()) { std::this_thread::yield(); }
        }
    }

private:
    [[nodiscard]] bool run_one();

    // An idle worker sleeps on idle_ until there is work rather than polling:
    // polling every millisecond on every hardware thread measured about 65% of
    // an otherwise idle machine.
    void run(const std::stop_token & stop);

    std::mutex mutex_;
    std::deque<task> tasks_;
    std::condition_variable_any idle_;
    // Destroyed first: workers join before the state they use, including
    // when starting a later worker throws during construction.
    std::vector<std::jthread> workers_;
};

} // namespace ctbrowser
