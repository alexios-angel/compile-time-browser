// dom_bindings - `window.postMessage`, HTML 9.4.3: a message between the
// windows of one agent, which since frames run their own scripts in their own
// realms (bindings/frames.cpp) means between a page and its frames.
//
// WHAT IT IS: the "window post message steps" - the origin check, the
// structured clone, and a TASK that fires a MessageEvent at the target window
// later. The task is what a page observes most: the event never arrives
// inside postMessage, so `w.postMessage(x); w.onmessage = f` still hears x.
//
// WHAT IS NOT HERE: MessagePort. With no ports the transfer list holds
// ArrayBuffers or nothing - anything else is the DataCloneError a
// non-transferable value gets (StructuredSerializeWithTransfer step 4) - and
// `ports` is always the empty frozen array.

#include "internal.hpp"

#include "../events/internal.hpp"

#include <ctbrowser/shell/net/url.hpp>

#include <string>
#include <vector>

namespace ctbrowser::shell {

namespace {

// A window's origin, serialized - from ITS DOCUMENT'S URL, as the `origin`
// getter computes it, and never by reading that property: a page can shadow
// `window.origin`, and the event's `origin` and the targetOrigin check are
// the receiver's only proof of who sent it (ACCESS-006).
[[nodiscard]] std::string origin_of(std::string_view href) {
    const std::string origin = href.empty() ? std::string{} : location_parts(href).origin;
    return origin.empty() ? std::string{"null"} : origin;
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
        const dom_bindings * sender = source.is_heap() ? find_bindings_of(source) : nullptr;
        const std::string source_origin =
            origin_of(sender == nullptr ? std::string_view{} : sender->location_href_);
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
        // Step 7: serialize NOW, so a later change to `message` is not seen
        // and a value that cannot be cloned throws at the caller - WITH the
        // transfer list, whose ArrayBuffers are detached here and arrive in
        // the event's data.
        value data = value::undefined();
        if (const value clone = c.global("structuredClone"); clone.is_callable()) {
            value options = value::undefined();
            if (!transfer.is_undefined()) {
                options = c.make_object();
                static_cast<script::object_object *>(options.as_heap())->set("transfer", transfer);
            }
            const context::rooted keep_options{c, options};
            const value both[2] = {message, options};
            data = c.call(clone, both);
            if (c.throw_pending()) { return value::undefined(); }
        }
        // Step 8: the task, on the event loop every window here shares.
        dom_bindings & top = primary_ == nullptr ? *this : *primary_;
        auto * task = c.allocate<script::native_object>(
            "postMessage task",
            [this, data, source, source_origin, target_origin](context & t, std::span<value>) {
                // The target's origin is checked when the task RUNS: it may
                // have navigated in between.
                if (target_origin != "*" && target_origin != origin_of(location_href_)) {
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
    // `crypto` rides here because this is the one installer the page's window
    // and every frame's window both run (window.cpp, frames.cpp).
    install_crypto(cx, window, [this](context & c, std::string_view name, std::string message) {
        throw_dom_exception(c, name, std::move(message));
    });
    // THE [Serializable] INTERFACES this engine has (HTML 2.7.2), each with the
    // own properties its binding keeps an instance's state in - which is what
    // structuredClone copies of one, and all it copies: a page's expando stays
    // behind. Every other interface's instance is a DataCloneError there (and
    // so here, which clones through it).
    //
    // A File is a Blob with a name, File.prototype inheriting Blob's
    // (window.cpp): a File clones as a File and a Blob as a Blob, and the two
    // lists are one because a Blob simply has no `name` to copy.
    const std::vector<std::string> blob{"size", "type", "__bytes", "name", "lastModified"};
    cx.set_serializable_interface("Blob", blob);
    cx.set_serializable_interface("File", blob);
    // data_transfer.cpp's files_key: the list's Files.
    cx.set_serializable_interface("FileList", {CTBROWSER_PRIVATE_KEY("ctbrowser:file-list")});
    cx.set_serializable_interface("ImageData", {"width", "height", "data"});
    // exceptions.cpp's private_key and message_key: name and message.
    cx.set_serializable_interface(
        "DOMException", {"__ctbrowser_domexception_name", "__ctbrowser_domexception_message"});
    for (const char * point : {"DOMPoint", "DOMPointReadOnly"}) {
        cx.set_serializable_interface(point, {"x", "y", "z", "w"});
    }
    for (const char * rect : {"DOMRect", "DOMRectReadOnly"}) {
        cx.set_serializable_interface(rect, {"x", "y", "width", "height"});
    }
    cx.set_serializable_interface("DOMQuad", {"p1", "p2", "p3", "p4"});
}

} // namespace ctbrowser::shell
