#include <ctbrowser/core/scheduler.hpp>

namespace ctbrowser {

scheduler::scheduler(std::size_t worker_count) {
    if (worker_count == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        worker_count = hw > 1 ? hw - 1 : 1;
    }
    queues_ = std::vector<queue>(worker_count);
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back([this, i](const std::stop_token & stop) { run(i, stop); });
    }
}

scheduler::~scheduler() {
    for (std::jthread & w : workers_) { w.request_stop(); }
    {
        const std::lock_guard lock{idle_mutex_};
        stopping_ = true;
    }
    idle_.notify_all();
}

void scheduler::submit(task t) {
    const std::size_t i = next_.fetch_add(1, std::memory_order_relaxed) % queues_.size();
    queue & q = queues_[i];
    {
        const std::lock_guard lock{q.mutex};
        q.items.push_back(std::move(t));
    }
    // Under the idle lock, not merely after an atomic increment. A worker
    // evaluates the predicate holding this lock and wait() releases it
    // atomically, so a notify that does not take it can be delivered
    // between the two and lost - and a lost wakeup here is a pool that
    // sleeps through the work it was just given.
    {
        const std::lock_guard lock{idle_mutex_};
        pending_.fetch_add(1, std::memory_order_relaxed);
    }
    idle_.notify_one();
}

bool scheduler::pop_local(std::size_t i, task & out) {
    queue & q = queues_[i];
    const std::lock_guard lock{q.mutex};
    if (q.items.empty()) { return false; }
    out = std::move(q.items.back());
    q.items.pop_back();
    pending_.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

bool scheduler::steal(std::size_t thief, task & out) {
    for (std::size_t n = 1; n < queues_.size(); ++n) {
        queue & q = queues_[(thief + n) % queues_.size()];
        const std::lock_guard lock{q.mutex};
        if (q.items.empty()) { continue; }
        out = std::move(q.items.front());
        q.items.pop_front();
        pending_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

bool scheduler::run_one(std::size_t i) {
    task t;
    if (pop_local(i, t) || steal(i, t)) {
        t();
        return true;
    }
    return false;
}

void scheduler::run(std::size_t i, const std::stop_token & stop) {
    identity me{this, i};
    current_ = &me;
    while (!stop.stop_requested()) {
        if (run_one(i)) { continue; }
        std::unique_lock lock{idle_mutex_};
        idle_.wait(lock, [&] {
            return pending_.load(std::memory_order_relaxed) > 0 || stopping_ ||
                   stop.stop_requested();
        });
    }
    current_ = nullptr;
}

} // namespace ctbrowser
