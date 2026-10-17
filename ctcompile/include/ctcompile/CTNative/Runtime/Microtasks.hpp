//===- Microtasks.hpp - The native microtask queue --------------*- C++ -*-===//
//
// The microtask queue of a native program: promise jobs wait here until the
// host performs a microtask checkpoint, which it does after the script's top
// level, after each timer callback and after each event dispatch, as the VM
// does. The queue owns each job, and everything bound into it, until the job
// has run.
//
// Programs that use promises include it through Promise.hpp; the timer and
// listener tables include it to run the checkpoint after each task.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ctnative {

/// The microtask queue of a program. One queue per program
/// (microtasks::program()), as a program is one realm with one agent.
class microtasks {
public:
    /// A job as the queue runs it.
    using job = std::move_only_function<void()>;

    /// The queue every native promise job is added to.
    static microtasks & program() {
        static microtasks queue;
        return queue;
    }

    /// HostEnqueuePromiseJob (ECMA-262 9.5.5), which HTML implements as
    /// "queue a microtask" (HTML 8.1.7.2): appends `run` to the queue.
    void enqueue(job run) { jobs_.push_back(std::move(run)); }

    /// HTML 8.1.7.3, "perform a microtask checkpoint": runs the oldest job
    /// until the queue is empty, including jobs queued while it runs. A
    /// checkpoint reached while one is already being performed returns at
    /// once (step 1), so a job that ends a task does not nest a second
    /// drain. A job that throws is reported (reported()) and the rest still
    /// run, as the VM reports an exception and continues. Returns how many
    /// jobs ran.
    std::size_t checkpoint() {
        if (performing_) { return 0; }
        performing_ = true;
        std::size_t ran = 0;
        while (!jobs_.empty()) {
            job run = std::move(jobs_.front());
            jobs_.pop_front();
            ++ran;
            try {
                run();
            } catch (const std::exception & thrown) { reported_.emplace_back(thrown.what()); }
        }
        performing_ = false;
        return ran;
    }

    /// How many jobs are waiting.
    [[nodiscard]] std::size_t pending() const noexcept { return jobs_.size(); }

    /// The messages of the exceptions jobs threw, oldest first.
    [[nodiscard]] const std::vector<std::string> & reported() const noexcept { return reported_; }

private:
    std::deque<job> jobs_;
    std::vector<std::string> reported_;
    bool performing_ = false;
};

} // namespace ctnative
