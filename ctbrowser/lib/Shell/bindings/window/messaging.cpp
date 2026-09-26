// dom_bindings - `window.postMessage`, HTML 9.4.3: a message between the
// windows of one agent, which since frames run their own scripts in their own
// realms (bindings/frames.cpp) means between a page and its frames.
//
// WHAT IT IS: the "window post message steps" - the origin check, the
// structured clone, and a TASK that fires a MessageEvent at the target window
// later. The task is what a page observes most: the event never arrives
// inside postMessage, so `w.postMessage(x); w.onmessage = f` still hears x.
//
// WHAT IS NOT HERE: MessagePort. With no ports the transfer list can only be
// empty, so a non-empty one is refused with the DataCloneError a
// non-transferable value gets (StructuredSerializeWithTransfer step 4), and
// `ports` is always the empty frozen array.

#include "internal.hpp"

#include "../events/internal.hpp"

#include <ctbrowser/shell/net/url.hpp>

#include <string>

namespace ctbrowser::shell {

namespace {

// A window's origin, serialized - what its `origin` property answers.
[[nodiscard]] std::string origin_of(context & c, value window) {
    if (!window.is_heap()) { return "null"; }
    const value origin = c.lookup_property(window, "origin");
    return origin.is_string() ? c.to_string(origin) : std::string{"null"};
}

} // namespace

void dom_bindings::install_post_message(context & cx, script::object_object & window) {
    const value post = native(cx, "postMessage", [this](context & c, std::span<value> a) {
        if (a.empty()) {
            c.throw_error("TypeError", "Failed to execute 'postMessage' on 'Window': 1 argument "
                                       "required, but only 0 present.");
            return value::undefined();
        }
        const value message = a[0];
        // (message, targetOrigin, transfer) or (message, WindowPostMessageOptions).
        std::string target_origin = "/";
        value transfer = value::undefined();
        if (a.size() > 1 && a[1].is_object() && !a[1].is_callable()) {
            const value given = dict_member(c, a[1], "targetOrigin");
            if (c.throw_pending()) { return value::undefined(); }
            if (!given.is_undefined()) { target_origin = c.to_string(given); }
            transfer = dict_member(c, a[1], "transfer");
        } else if (a.size() > 1) {
            target_origin = c.to_string(a[1]);
            transfer = arg(a, 2);
        }
        if (c.throw_pending()) { return value::undefined(); }
        // The INCUMBENT settings: the realm of the code that called - whose
        // window is the event's `source` and whose origin its `origin`.
        const value source = c.current_realm().global_this;
        const std::string source_origin = origin_of(c, source);
        // Steps 5-6: "/" is the incumbent's own origin, "*" is any, and
        // anything else must parse as a URL, whose origin it names.
        if (target_origin == "/") {
            target_origin = source_origin;
        } else if (target_origin != "*") {
            const location_url parsed = location_parts(target_origin);
            if (parsed.href.empty()) {
                throw_dom_exception(c, "SyntaxError",
                                    "Failed to execute 'postMessage' on 'Window': Invalid target "
                                    "origin '" +
                                        target_origin + "'.");
                return value::undefined();
            }
            target_origin = parsed.origin;
        }
        if (transfer.is_object_like() && c.lookup_property(transfer, "length").is_number() &&
            context::to_number(c.lookup_property(transfer, "length")) > 0) {
            throw_dom_exception(c, "DataCloneError",
                                "Failed to execute 'postMessage' on 'Window': this engine has no "
                                "transferable objects.");
            return value::undefined();
        }
        // Step 7: serialize NOW, so a later change to `message` is not seen
        // and a value that cannot be cloned throws at the caller.
        value data = value::undefined();
        if (const value clone = c.global("structuredClone"); clone.is_callable()) {
            const value one[1] = {message};
            data = c.call(clone, one);
            if (c.throw_pending()) { return value::undefined(); }
        }
        // Step 8: the task, on the event loop every window here shares.
        dom_bindings & top = primary_ == nullptr ? *this : *primary_;
        auto * task = c.allocate<script::native_object>(
            "postMessage task",
            [this, data, source, source_origin, target_origin](context & t, std::span<value>) {
                // The target's origin is checked when the task RUNS: it may
                // have navigated in between.
                if (target_origin != "*" && target_origin != origin_of(t, window_view_)) {
                    return value::undefined();
                }
                const value event = make_event_object(t, "message", false, false);
                auto * object = static_cast<script::object_object *>(event.as_heap());
                // The interfaces are the page's, whichever window receives -
                // a frame's bindings adopt them (second_document.cpp).
                const value proto =
                    (primary_ == nullptr ? this : primary_)->message_event_prototype_;
                if (proto.is_object()) { object->prototype = proto; }
                object->set("data", data);
                object->set("origin", t.string(source_origin));
                object->set("lastEventId", t.string(""));
                object->set("source", source.is_heap() ? source : value::null());
                const value ports = t.make_array();
                t.prevent_extensions(ports);
                object->set("ports", ports);
                object->set(std::string{detail::trusted_property}, value::boolean(true));
                object->set(std::string{detail::initialised_property}, value::boolean(true));
                (void)dispatch_to(event, path_step{node_id{}, listen_on::window});
                return value::undefined();
            });
        // THE CLONE AND THE SOURCE ARE IN A C++ CAPTURE, which the collector
        // cannot see (native_object::retained).
        task->retained.push_back(data);
        task->retained.push_back(source);
        (void)top.add_timer(value::object(task), 0, false);
        return value::undefined();
    });
    window.set("postMessage", post);
    // The window is the global object, so the bare name is the same method.
    cx.define_global("postMessage", post);
}

} // namespace ctbrowser::shell
