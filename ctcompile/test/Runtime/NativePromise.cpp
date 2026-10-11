//===- ctcompile/test/Runtime/NativePromise.cpp - Native Promise tests ----===//
//
// Tests the native runtime's promises (CTNative/Runtime/Promise.hpp) and the
// microtask queue they run on (CTNative/Runtime/Microtasks.hpp): reactions
// run at the checkpoint and never synchronously, chains interleave one job
// at a time, adopting a promise costs the two jobs ECMA-262 orders, the
// resolving functions act once, a rejection skips onFulfilled, and the jobs a
// timer callback queues run before the next timer. The expected orders are
// what Node prints for the same JavaScript, quoted beside each test.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Runtime/Promise.hpp"
#include "ctcompile/CTNative/Runtime/Timers.hpp"

#include <optional>
#include <string>

#include "check.hpp"

namespace {

using ctnative::js_exception;
using ctnative::js_num;
using ctnative::js_promise;
using ctnative::microtasks;
using ctnative::Promise;
using ctnative::setTimeout;
using ctnative::undefined_t;

/// Appends `step` to `out`, space-separated.
void log(std::string & out, const std::string & step) {
    if (!out.empty()) { out += ' '; }
    out += step;
}

/// `p.then(f)` as generated code spells it.
template <class P, class F> auto then(const P & p, F f) {
    return Promise.prototype.then.call(p, std::move(f));
}

// Promise.resolve().then(() => log("a1")).then(() => log("a2"));
// Promise.resolve().then(() => log("b1")).then(() => log("b2"));
// new Promise((r) => r(Promise.resolve(1))).then(() => log("x"));
// Promise.resolve().then(() => log("1")).then(() => log("2")).then(() => log("3"));
// Promise.resolve(5).then((v) => Promise.resolve(v + 1)).then((v) => log("h" + v));
// Promise.resolve().then(() => log("c1")).then(() => log("c2"))
//     .then(() => log("c3")).then(() => log("c4"));
// setTimeout(() => { log("A"); Promise.resolve().then(() => log("Q")); }, 0);
// setTimeout(() => log("C"), 0);
// Promise.resolve().then(() => log("P"));
// log("sync");
//
// Node: sync a1 b1 1 c1 P a2 b2 2 c2 x 3 c3 h6 c4 A Q C
void test_job_order_matches_node() {
    std::string out;
    const auto step = [&out](const char * name) {
        return [&out, name](const auto &) {
            log(out, name);
            return undefined_t{};
        };
    };
    (void)then(then(Promise.resolve(undefined_t{}), step("a1")), step("a2"));
    (void)then(then(Promise.resolve(undefined_t{}), step("b1")), step("b2"));
    (void)then(
        Promise.construct<js_num>([](auto & resolve) { resolve(Promise.resolve(js_num{1.0})); }),
        step("x"));
    (void)then(then(then(Promise.resolve(undefined_t{}), step("1")), step("2")), step("3"));
    (void)then(then(Promise.resolve(js_num{5.0}),
                    [](const js_num & v) { return Promise.resolve(js_num{v.value() + 1.0}); }),
               [&out](const js_num & v) {
                   log(out, "h" + std::to_string(static_cast<int>(v.value())));
                   return undefined_t{};
               });
    (void)then(then(then(then(Promise.resolve(undefined_t{}), step("c1")), step("c2")), step("c3")),
               step("c4"));
    (void)setTimeout(
        [&out, step] {
            log(out, "A");
            (void)then(Promise.resolve(undefined_t{}), step("Q"));
        },
        js_num{0.0});
    (void)setTimeout([&out] { log(out, "C"); }, js_num{0.0});
    (void)then(Promise.resolve(undefined_t{}), step("P"));
    log(out, "sync");
    // The end of the script is a checkpoint; the timers come after it.
    CHECK_EQ(microtasks::program().checkpoint(), std::size_t{19});
    CHECK_EQ(ctnative::timers::program().advance(0), std::size_t{2});
    CHECK_EQ(out, std::string{"sync a1 b1 1 c1 P a2 b2 2 c2 x 3 c3 h6 c4 A Q C"});
    CHECK_EQ(microtasks::program().pending(), std::size_t{0});
}

// A reaction never runs before the checkpoint, even on a settled promise. A
// handler that takes no value and returns nothing, as a lifted JavaScript
// `function () {}` does, is called without the value and fulfills its
// promise with undefined.
void test_reactions_wait_for_the_checkpoint() {
    bool ran = false;
    (void)then(Promise.resolve(js_num{1.0}), [&ran] { ran = true; });
    CHECK(!ran);
    CHECK_EQ(microtasks::program().checkpoint(), std::size_t{1});
    CHECK(ran);
}

// let resolve; const p = new Promise((r) => { resolve = r; });
// p.then((v) => log(v)); resolve(1); resolve(2);
// Node logs 1: only the first call of a resolving function counts.
void test_resolving_functions_act_once() {
    std::string out;
    std::optional<ctnative::promise_resolve_function<js_num, ctnative::no_rejection>> later;
    const auto p = Promise.construct<js_num>([&later](auto & resolve) { later.emplace(resolve); });
    (void)then(p, [&out](const js_num & v) {
        log(out, std::to_string(static_cast<int>(v.value())));
        return undefined_t{};
    });
    (void)microtasks::program().checkpoint();
    CHECK(out.empty());
    (void)(*later)(js_num{1.0});
    (void)(*later)(js_num{2.0});
    (void)microtasks::program().checkpoint();
    CHECK_EQ(out, std::string{"1"});
}

// Promise.resolve(p) === p
void test_resolve_of_a_promise_is_that_promise() {
    const auto p = Promise.resolve(js_num{3.0});
    CHECK(Promise.resolve(p) == p);
    CHECK(!(Promise.resolve(js_num{3.0}) == p));
    (void)microtasks::program().checkpoint();
}

// new Promise((_, reject) => reject("no")).then(() => log("f")).then(() => log("g"));
// new Promise(() => { throw "thrown"; }).then(() => log("t"));
// Promise.resolve(1).then(() => { throw "late"; }).then(() => log("u"));
// Node logs nothing: a rejection skips onFulfilled down the chain.
void test_a_rejection_skips_on_fulfilled() {
    std::string out;
    const auto logs = [&out](const char * name) {
        return [&out, name](const auto &) {
            log(out, name);
            return undefined_t{};
        };
    };
    (void)then(then(Promise.construct<undefined_t, std::string>(
                        [](auto &, auto & reject) { reject(std::string{"no"}); }),
                    logs("f")),
               logs("g"));
    (void)then(Promise.construct<undefined_t, std::string>(
                   [](auto &, auto &) { throw js_exception<std::string>{"thrown"}; }),
               logs("t"));
    using rejectable = js_promise<js_num, std::string>;
    const rejectable one = Promise.construct<js_num, std::string>(
        [](auto & resolve, auto &) { resolve(js_num{1.0}); });
    (void)then(
        then(one, [](const js_num &) -> undefined_t { throw js_exception<std::string>{"late"}; }),
        logs("u"));
    (void)microtasks::program().checkpoint();
    CHECK(out.empty());
    CHECK_EQ(microtasks::program().pending(), std::size_t{0});
}

// A job that queues another runs it in the same checkpoint, and a checkpoint
// reached from inside a job does not nest.
void test_a_checkpoint_does_not_nest() {
    std::string out;
    microtasks & queue = microtasks::program();
    queue.enqueue([&] {
        log(out, "outer");
        queue.enqueue([&] { log(out, "queued"); });
        CHECK_EQ(queue.checkpoint(), std::size_t{0});
        log(out, "after");
    });
    CHECK_EQ(queue.checkpoint(), std::size_t{2});
    CHECK_EQ(out, std::string{"outer after queued"});
}

} // namespace

int main() {
    test_job_order_matches_node();
    test_reactions_wait_for_the_checkpoint();
    test_resolving_functions_act_once();
    test_resolve_of_a_promise_is_that_promise();
    test_a_rejection_skips_on_fulfilled();
    test_a_checkpoint_does_not_nest();
    REPORT("native_promise");
}
