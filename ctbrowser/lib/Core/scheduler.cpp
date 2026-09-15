#include <ctbrowser/core/scheduler.hpp>

namespace ctbrowser {

scheduler::scheduler(std::size_t worker_count) {
    if (worker_count == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        worker_count = hw > 1 ? hw - 1 : 1;
    }
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back([this](const std::stop_token & stop) { run(stop); });
    }
}

scheduler::~scheduler() {
    for (std::jthread & w : workers_) { w.request_stop(); }
}

void scheduler::submit(task t) {
    // Pushed under the lock the waiters evaluate their predicate under, so a
    // notify cannot slip between a worker's "empty" check and its wait() and
    // be lost - a lost wakeup here is a pool that sleeps through the work it
    // was just given.
    {
        const std::lock_guard lock{mutex_};
        tasks_.push_back(std::move(t));
    }
    idle_.notify_one();
}

bool scheduler::run_one() {
    task t;
    {
        const std::lock_guard lock{mutex_};
        if (tasks_.empty()) { return false; }
        t = std::move(tasks_.front());
        tasks_.pop_front();
    }
    t();
    return true;
}

void scheduler::run(const std::stop_token & stop) {
    while (!stop.stop_requested()) {
        if (run_one()) { continue; }
        std::unique_lock lock{mutex_};
        idle_.wait(lock, stop, [&] { return !tasks_.empty(); });
    }
}

} // namespace ctbrowser
