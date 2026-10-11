//===- ctcompile/test/Runtime/NativeTimers.cpp - Native setTimeout tests --===//
//
// Tests the native runtime's timer table (CTNative/Runtime/Timers.hpp): timers
// scheduled the way generated code schedules them run when the host's clock
// reaches them, in the order they were scheduled as the VM runs them; a timer
// scheduled by a callback waits for the next advance; timeouts convert as a
// `long`; and a throwing callback is reported without stopping the rest.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Runtime/Timers.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "check.hpp"

namespace {

using ctnative::js_num;
using ctnative::setTimeout;
using ctnative::timers;

void test_timers_run_when_due_in_schedule_order() {
    std::string order;
    const js_num first = setTimeout([&] { order += 'a'; }, js_num{10.0});
    const js_num second = setTimeout([&] { order += 'b'; }, js_num{0.0});
    CHECK(first.value() > 0);
    CHECK(second.value() > first.value());
    timers & table = timers::program();
    CHECK_EQ(table.advance(0), std::size_t{1});
    CHECK_EQ(order, std::string{"b"});
    CHECK_EQ(table.advance(9), std::size_t{0});
    CHECK_EQ(table.advance(1), std::size_t{1});
    CHECK_EQ(order, std::string{"ba"});
    CHECK_EQ(table.pending(), std::size_t{0});
}

void test_a_timer_scheduled_by_a_callback_waits() {
    std::string order;
    (void)setTimeout(
        [&] {
            order += 'o';
            (void)setTimeout([&] { order += 'i'; }, js_num{0.0});
        },
        js_num{0.0});
    timers & table = timers::program();
    CHECK_EQ(table.advance(0), std::size_t{1});
    CHECK_EQ(order, std::string{"o"});
    CHECK_EQ(table.advance(0), std::size_t{1});
    CHECK_EQ(order, std::string{"oi"});
}

void test_timeouts_convert_as_a_long() {
    int ran = 0;
    // Negative, NaN and 2**32 (which wraps to 0) are all due at once.
    (void)setTimeout([&] { ++ran; }, js_num{-5.0});
    (void)setTimeout([&] { ++ran; }, js_num{std::numeric_limits<double>::quiet_NaN()});
    (void)setTimeout([&] { ++ran; }, js_num{std::ldexp(1.0, 32)});
    (void)setTimeout([&] { ++ran; }, js_num{2.9}); // truncates to 2
    timers & table = timers::program();
    CHECK_EQ(table.advance(0), std::size_t{3});
    CHECK_EQ(table.advance(2), std::size_t{1});
    CHECK_EQ(ran, 4);
}

void test_a_throwing_callback_is_reported() {
    bool after = false;
    (void)setTimeout([] { throw std::invalid_argument("TypeError: boom"); }, js_num{0.0});
    (void)setTimeout([&] { after = true; }, js_num{0.0});
    timers & table = timers::program();
    const std::size_t before = table.reported().size();
    CHECK_EQ(table.advance(0), std::size_t{2});
    CHECK(after);
    CHECK_EQ(table.reported().size(), before + 1);
    CHECK_EQ(table.reported().back(), std::string{"TypeError: boom"});
}

} // namespace

int main() {
    test_timers_run_when_due_in_schedule_order();
    test_a_timer_scheduled_by_a_callback_waits();
    test_timeouts_convert_as_a_long();
    test_a_throwing_callback_is_reported();
    REPORT("native_timers");
}
