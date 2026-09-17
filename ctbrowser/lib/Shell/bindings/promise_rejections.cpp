// dom_bindings - unhandled promise rejections, HTML 8.1.7.x
// (https://html.spec.whatwg.org/#unhandled-promise-rejections).
//
// The VM's HostPromiseRejectionTracker (context::set_rejection_tracker) says
// "reject" when a promise is rejected with no reaction attached and "handle"
// when one is attached to such a promise afterwards; the host keeps the two
// lists the specification names and turns them into events. A rejected
// promise joins the ABOUT-TO-BE-NOTIFIED list; the "notify about rejected
// promises" task - queued the first time the list fills, run from the
// page's timer queue so every microtask of the script that rejected has
// drained first - fires `unhandledrejection` at the window for each promise
// STILL unhandled (a handler attached in a later microtask counts), and what
// stays unhandled joins the OUTSTANDING list. A "handle" for a promise in the
// first list just drops it; for one in the second it queues `rejectionhandled`.
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
        static_cast<script::object_object *>(promise.as_heap())->find(std::string_view{"__value"});
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
}

void dom_bindings::track_promise_rejection(value promise, bool handled) {
    if (cx_ == nullptr || !promise.is_object()) { return; }
    if (handled) {
        if (erase_promise(rejections_to_notify_, promise)) { return; }
        if (!erase_promise(outstanding_rejections_, promise)) { return; }
        rejections_handled_late_.push_back(promise);
    } else {
        rejections_to_notify_.push_back(promise);
    }
    if (rejection_task_queued_) { return; }
    rejection_task_queued_ = true;
    (void)add_timer(
        native(
            *cx_, "notify about rejected promises",
            [this](context & c, std::span<value>) {
                rejection_task_queued_ = false;
                std::vector<value> due;
                due.swap(rejections_to_notify_);
                std::vector<value> late;
                late.swap(rejections_handled_late_);
                const auto fire = [&](std::string_view type, value promise, bool cancelable) {
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
                    return dispatch_event(type, node_id{}, event);
                };
                for (const value & promise : due) {
                    if (context::promise_is_handled(promise)) { continue; }
                    const bool cancelled = fire("unhandledrejection", promise, true);
                    if (!cancelled) {
                        console_.push_back("Uncaught (in promise) " +
                                           describe_thrown(c, reason_of(promise)));
                    }
                    if (!context::promise_is_handled(promise)) {
                        outstanding_rejections_.push_back(promise);
                    }
                }
                for (const value & promise : late) {
                    (void)fire("rejectionhandled", promise, false);
                }
                return value::undefined();
            }),
        0, false);
}

} // namespace ctbrowser::shell
