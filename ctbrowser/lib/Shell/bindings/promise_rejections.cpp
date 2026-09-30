// dom_bindings - unhandled promise rejections, HTML 8.1.7.x
// (https://html.spec.whatwg.org/#unhandled-promise-rejections).
//
// The VM's HostPromiseRejectionTracker (context::set_rejection_tracker) says
// "reject" when a promise is rejected with no reaction attached and "handle"
// when one is attached to such a promise afterwards; the host keeps the two
// lists the specification names and turns them into events. A rejected
// promise joins the ABOUT-TO-BE-NOTIFIED list; at the END of each microtask
// checkpoint that list is taken, whole, into a "notify about rejected
// promises" task of its own (8.1.7.3 step 5), which fires
// `unhandledrejection` at the window for each promise STILL unhandled when it
// runs, and what stays unhandled joins the OUTSTANDING list. A "handle" for a
// promise in the first list just drops it; for one in the second it queues
// `rejectionhandled` there and then.
// `unhandledrejection` is cancelable - `preventDefault` is how a page says
// it has dealt with the rejection - and the one that is not cancelled is
// reported to the console, as a browser does.
//
// Before this the tracker fired into nothing: html/webappapis/scripting/
// processing-model-2/unhandled-promise-rejections (seven files) timed out
// waiting for an event that never came.

#include "events/internal.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace ctbrowser::shell {

using namespace detail;

namespace {

[[nodiscard]] value reason_of(value promise) {
    if (!promise.is_object()) { return value::undefined(); }
    const value * held =
        static_cast<script::object_object *>(promise.as_heap())->find(script::promise_result_key);
    return held == nullptr ? value::undefined() : *held;
}

[[nodiscard]] bool erase_promise(std::vector<value> & list, value promise) {
    const auto at = std::ranges::find_if(
        list, [&](const value & held) { return held.as_heap() == promise.as_heap(); });
    if (at == list.end()) { return false; }
    list.erase(at);
    return true;
}

} // namespace

void dom_bindings::install_promise_rejections(context & cx) {
    cx.set_rejection_tracker([this](value promise, bool handled) {
        primary().track_promise_rejection(promise, handled);
    });
    // AT THE END OF THE CHECKPOINT, not at the rejection: HTML's "perform a
    // microtask checkpoint" step 5 is what queues the notification task, so a
    // task queued while the rejecting script ran - a <details> toggle, a
    // message - comes before it and may still attach a handler
    // (promise-rejection-events.html, "delayed handling").
    cx.set_checkpoint_hook([this] { primary().queue_rejection_notification(); });
}

void dom_bindings::track_promise_rejection(value promise, bool handled) {
    if (cx_ == nullptr || !promise.is_object()) { return; }
    if (!handled) {
        rejections_to_notify_.push_back(promise);
        return;
    }
    if (erase_promise(rejections_to_notify_, promise)) { return; }
    if (!erase_promise(outstanding_rejections_, promise)) { return; }
    // HandlePromiseRejection's host steps 3-4: a task, queued now.
    auto * task = cx_->allocate<script::native_object>(
        "rejectionhandled", [this, promise](context & c, std::span<value>) {
            (void)fire_rejection_event(c, "rejectionhandled", promise, false);
            return value::undefined();
        });
    // THE PROMISE IS IN A C++ CAPTURE, which the collector cannot see.
    task->retained.push_back(promise);
    (void)add_timer(value::object(task), 0, false);
}

// ONE TASK PER CHECKPOINT, carrying the list as it was then: a checkpoint in
// the middle of a script (an event dispatched with an empty stack) must not
// sweep up a promise the rest of the script rejects, whose handler a task the
// script queues next may still attach.
void dom_bindings::queue_rejection_notification() {
    if (cx_ == nullptr || rejections_to_notify_.empty()) { return; }
    std::vector<value> list;
    list.swap(rejections_to_notify_);
    auto * task = cx_->allocate<script::native_object>(
        "notify about rejected promises", [this, list](context & c, std::span<value>) {
            for (const value & promise : list) {
                if (context::promise_is_handled(promise)) { continue; }
                const bool cancelled = fire_rejection_event(c, "unhandledrejection", promise, true);
                if (!cancelled) {
                    console_.push_back("Uncaught (in promise) " +
                                       describe_thrown(c, reason_of(promise)));
                }
                if (!context::promise_is_handled(promise)) {
                    outstanding_rejections_.push_back(promise);
                }
            }
            return value::undefined();
        });
    task->retained = list;
    (void)add_timer(value::object(task), 0, false);
}

// A PromiseRejectionEvent at the window, trusted.
bool dom_bindings::fire_rejection_event(context & c, std::string_view type, value promise,
                                        bool cancelable) {
    value event = make_event_object(c, type, false, cancelable);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    if (const value ctor = c.global("PromiseRejectionEvent");
        ctor.is_kind(script::heap_kind::native)) {
        const value * proto =
            static_cast<script::native_object *>(ctor.as_heap())->find("prototype");
        if (proto != nullptr && proto->is_object()) { object->prototype = *proto; }
    }
    object->set("promise", promise);
    object->set("reason", reason_of(promise));
    object->set(std::string{trusted_property}, value::boolean(true));
    object->set(std::string{initialised_property}, value::boolean(true));
    // AT THE WINDOW STEP: the event does not bubble, so a dispatch from the
    // document would never reach it.
    object->set("target", c.global("window"));
    return dispatch_to(event, path_step{node_id{}, listen_on::window});
}

} // namespace ctbrowser::shell
