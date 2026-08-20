#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <latch>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// A work-stealing pool.
//
// Stealing rather than a single shared queue because the consumers are
// recursive: laying out an independent formatting context spawns layout of its
// children, and a task that blocks waiting for its own subtasks on a
// single-queue pool deadlocks the pool. Here a waiting worker keeps draining
// work, including work it did not create.
//
// Each worker owns a deque: it pushes and pops its own BACK (LIFO, so a freshly
// spawned subtask is the next thing run and is still cache-hot), while thieves
// take from the FRONT (the oldest, largest, least contended item). That access
// pattern is why a plain mutex per deque is enough - the owner and the thieves
// touch opposite ends and rarely collide. A lock-free Chase-Lev deque is the
// next step if profiling ever shows this mutex mattering; it is much harder to
// get right and there is no evidence yet that it is needed.
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

    [[nodiscard]] std::size_t worker_count() const noexcept { return queues_.size(); }

    // A stable slot for the calling thread, in [0, worker_count()]. Pool workers
    // get their own index; any other thread - including the one that called
    // parallel_for and is helping out - gets worker_count().
    //
    // This exists so a consumer can hand each producer its OWN single-producer
    // queue. "Single producer" then holds by construction rather than by
    // convention, which is the difference between a lock-free handoff that is
    // correct and one that merely has not raced yet.
    [[nodiscard]] std::size_t worker_index() const noexcept {
        return current_ == nullptr || current_->owner != this ? queues_.size() : current_->index;
    }
    // One more than worker_count(): every worker, plus whoever is helping.
    [[nodiscard]] std::size_t producer_slots() const noexcept { return queues_.size() + 1; }

    void submit(task t);

    // Run f(0..n) across the pool and return once every index is done. The
    // CALLING thread helps, so parallel_for from inside a task cannot deadlock
    // waiting on a pool that is busy running it.
    template <typename F> void parallel_for(std::size_t n, F && f) {
        if (n == 0) { return; }
        if (n == 1 || queues_.empty()) {
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
            if (!run_one(0)) { std::this_thread::yield(); }
        }
    }

private:
    struct queue {
        std::mutex mutex;
        std::deque<task> items;
    };

    // Owner pops the back (LIFO, cache-hot); thieves take the front.
    [[nodiscard]] bool pop_local(std::size_t i, task & out);
    [[nodiscard]] bool steal(std::size_t thief, task & out);
    [[nodiscard]] bool run_one(std::size_t i);

    struct identity {
        const scheduler * owner = nullptr;
        std::size_t index = 0;
    };
    // Thread-local rather than a map: worker_index() is called once per tile and
    // must not become the synchronisation the queues exist to avoid.
    static inline thread_local identity * current_ = nullptr;

    // An idle worker SLEEPS until there is work, rather than waking up to look.
    //
    // This used to be `wait_for(1ms)` on the worker's own queue, because submit
    // notifies only the queue it pushed to and a worker discovers STEALABLE
    // work by looking. The cost of that is a pool which never sleeps: one
    // wakeup per millisecond per worker, on every hardware thread, forever -
    // which on an idle page is the entire CPU cost of the application and was
    // measured at about 65% of a machine doing nothing.
    //
    // So the condition is now global - "are there tasks anywhere" - and one
    // shared condvar covers every worker, whichever queue the work landed in.
    void run(std::size_t i, const std::stop_token & stop);

    std::vector<queue> queues_;
    std::vector<std::jthread> workers_;
    std::atomic<std::size_t> next_{0};
    // Tasks queued and not yet taken, anywhere. The predicate every idle
    // worker waits on, so work in ANY queue wakes whoever can steal it.
    std::atomic<std::size_t> pending_{0};
    std::mutex idle_mutex_;
    std::condition_variable idle_;
    bool stopping_ = false;
};

} // namespace ctbrowser
