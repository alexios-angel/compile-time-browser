#include <ctbrowser/shell/timer_queue.hpp>

#include "check.hpp"
#include <cmath>
#include <limits>
#include <vector>

using ctbrowser::shell::timer_queue;
using ctbrowser::shell::timer_source;

namespace {

std::vector<std::uint32_t> due(const timer_queue & queue, double now_ms,
                               bool queued_tasks_only = false) {
    std::vector<std::uint32_t> result;
    for (const auto & timer : queue.entries()) {
        if (timer.due(now_ms, queued_tasks_only)) { result.push_back(timer.id); }
    }
    return result;
}

void test_order_and_task_sources() {
    timer_queue queue;
    CHECK(std::isinf(queue.next_delay(0)));
    queue.schedule(1, 0, 50, false, timer_source::page);
    queue.schedule(2, 0, 5, false, timer_source::host);
    queue.schedule(3, 0, -1, false, timer_source::page);
    CHECK_EQ(queue.next_delay(0), 0.0);
    CHECK_EQ(due(queue, 0), (std::vector<std::uint32_t>{3}));
    CHECK_EQ(due(queue, 100), (std::vector<std::uint32_t>{1, 2, 3}));
    CHECK_EQ(due(queue, 100, true), (std::vector<std::uint32_t>{2}));
    CHECK(queue.begin(2, 100));
    CHECK(!queue.find_active(2));
    CHECK_EQ(queue.entries().size(), 3u);
    CHECK(due(queue, 100, true).empty());
    queue.discard_cancelled();
    CHECK_EQ(queue.entries().size(), 2u);
    CHECK_EQ(due(queue, 100), (std::vector<std::uint32_t>{1, 3}));
}

void test_mutation_during_dispatch() {
    timer_queue queue;
    queue.schedule(1, 0, 0, false);
    queue.schedule(2, 0, 0, false);
    queue.schedule(3, 0, 0, false);
    std::vector<std::uint32_t> ran;
    for (const auto id : due(queue, 0)) {
        if (!queue.begin(id, 0)) { continue; }
        ran.push_back(id);
        if (id == 1) {
            queue.cancel(2);
            queue.schedule(4, 0, 0, false);
        }
    }
    CHECK_EQ(ran, (std::vector<std::uint32_t>{1, 3}));
    CHECK_EQ(queue.entries().size(), 4u);
    CHECK_EQ(due(queue, 0), (std::vector<std::uint32_t>{4}));
    queue.discard_cancelled();
    CHECK_EQ(queue.entries().size(), 1u);
    CHECK(queue.begin(4, 0));
    CHECK(!queue.begin(4, 0));
    queue.discard_cancelled();
    CHECK(std::isinf(queue.next_delay(0)));
}

void test_intervals_and_nested_batches() {
    timer_queue queue;
    queue.schedule(1, 0, 10, true);
    CHECK(due(queue, 9).empty());
    CHECK(queue.begin(1, 100));
    CHECK_EQ(queue.next_delay(100), 10.0);
    CHECK(due(queue, 100).empty());
    CHECK_EQ(due(queue, 110), (std::vector<std::uint32_t>{1}));
    // An earlier callback may run a nested batch. An outer snapshot still
    // invokes an active interval, even though that batch moved its deadline.
    CHECK(queue.begin(1, 110));
    CHECK(queue.begin(1, 110));
    CHECK_EQ(queue.next_delay(110), 10.0);
    queue.cancel(1);
    CHECK(!queue.begin(1, 110));
    queue.discard_cancelled();

    queue.schedule(2, 0, 0, true);
    CHECK(queue.begin(2, 0));
    CHECK_EQ(due(queue, 0), (std::vector<std::uint32_t>{2}));
    queue.cancel(2);
    queue.discard_cancelled();
}

void test_caller_ids_and_delay_values() {
    timer_queue queue;
    queue.schedule(0, 0, std::numeric_limits<double>::quiet_NaN(), false);
    queue.schedule(0, 0, 0, false);
    queue.schedule(9, 0, std::numeric_limits<double>::infinity(), false);
    CHECK_EQ(due(queue, 0), (std::vector<std::uint32_t>{0, 0}));
    CHECK(!queue.begin(0, 0, false));
    CHECK(queue.entries()[0].cancelled);
    CHECK(!queue.entries()[1].cancelled);
    CHECK(queue.find_active(0));
    queue.cancel(0);
    CHECK(queue.entries()[1].cancelled);
    CHECK(!queue.find_active(0));
    queue.discard_cancelled();
    CHECK_EQ(queue.entries().size(), 1u);
    CHECK(std::isinf(queue.next_delay(0)));
}

} // namespace

int main() {
    test_order_and_task_sources();
    test_mutation_during_dispatch();
    test_intervals_and_nested_batches();
    test_caller_ids_and_delay_values();
    REPORT("timer_queue");
}
