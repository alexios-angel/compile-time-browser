//===- history.cpp - The History interface ----------------------*- C++ -*-===//
//
// `window.history`, the History interface (HTML 7.4.2), installed by
// dom_bindings::install_history.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include "../element/internal.hpp" // document_base_url
#include "internal.hpp"

#include <cmath>
#include <optional>

namespace ctbrowser::shell {

// The session history a History object sees is kept on the object itself in
// private slots, so the collector finds every state a page pushed wherever it
// finds the object. The slots hold the window it belongs to; its entries, each
// an array of [URL, serialized state, scroll restoration mode]; the index of
// the active entry; and `state`, the active entry's state deserialized.
//
// A document is fully active while its window is still one of this page's
// (find_bindings_of): a removed frame's History answers every member with the
// SecurityError the specification gives a document that is not.
//
// ponytail: the entries are this document's only (what pushState made and the
// entry it started on), where a browser counts the top-level traversable's
// joint session history, and a fragment navigation (the Location setters in
// bindings/document/entry.cpp) moves the active entry's URL rather than
// pushing a new one. Cross-document traversal is the upgrade path once a
// navigation keeps its history.
namespace {

const std::string window_slot = std::string{script::private_key_prefix} + "history:window";
const std::string entries_slot = std::string{script::private_key_prefix} + "history:entries";
const std::string index_slot = std::string{script::private_key_prefix} + "history:index";
const std::string state_slot = std::string{script::private_key_prefix} + "history:state";
// The realm's structuredClone, captured before any page script could replace it.
const std::string clone_slot = std::string{script::private_key_prefix} + "history:clone";
// History.prototype, stored on the page's window so frames' History objects
// share it.
const std::string prototype_slot = std::string{script::private_key_prefix} + "History";

enum entry_field : std::size_t {
    entry_url = 0,
    entry_state = 1,
    entry_mode = 2
};

/// The array `v` holds, or null if it is not an array.
[[nodiscard]] script::array_object * array_of(value v) {
    return v.is_array() ? static_cast<script::array_object *>(v.as_heap()) : nullptr;
}

/// StructuredSerializeForStorage and StructuredDeserialize in one call to the
/// realm's structuredClone, which throws the DataCloneError itself. It uses
/// the clone `history` was made with, not whatever the page's global now names.
[[nodiscard]] value structured_clone(context & c, script::object_object & history, value data) {
    const value * clone = history.find(clone_slot);
    if (clone == nullptr || !clone->is_callable()) {
        c.throw_error("TypeError", "History: no structured clone in this realm");
        return value::undefined();
    }
    value one[1] = {data};
    return c.call(*clone, one);
}

/// HTML 7.4.1.3 "can have its URL rewritten": the same origin parts, and for a
/// non-HTTP(S) URL the same path, plus the same query unless it is a file: URL.
[[nodiscard]] bool can_rewrite(const url_record & from, const url_record & to) {
    if (from.scheme != to.scheme || from.username != to.username || from.password != to.password ||
        from.host != to.host || from.port != to.port) {
        return false;
    }
    if (to.scheme == "http" || to.scheme == "https") { return true; }
    if (to.scheme == "file") { return from.path == to.path; }
    return from.path == to.path && from.query == to.query;
}

} // namespace

void dom_bindings::install_history(context & cx, script::object_object & window) {
    script::object_object * page_window = primary().window_object();
    value proto_value = value::undefined();
    if (page_window != nullptr) {
        if (const value * held = page_window->find(prototype_slot); held != nullptr) {
            proto_value = *held;
        }
    }
    auto * history = cx.allocate<script::object_object>();
    history->define(window_slot, window_view_, script::attr_none);
    history->define(clone_slot, cx.global("structuredClone"), script::attr_none);
    const value made_history = value::object(history);
    window.set("history", made_history);
    cx.define_global("history", made_history);
    if (proto_value.is_object()) {
        history->prototype = proto_value;
        return;
    }
    // The interface is made once per page; a frame's History objects share it.
    const interface_pair made = make_interface(cx, "History", [](context & c, std::span<value>) {
        c.throw_error("TypeError", "Illegal constructor: History");
        return value::undefined();
    });
    history->prototype = value::object(made.proto);
    if (page_window != nullptr) {
        page_window->define(prototype_slot, value::object(made.proto), script::attr_none);
    }
    script::object_object & proto = *made.proto;
    dom_bindings * const top = &primary();
    // `own` resolves `this` to its History object and the document it is for.
    // On failure it throws a TypeError (not a History at all) or a
    // SecurityError (the document is no longer fully active) and returns
    // nulls, with the throw pending.
    struct owned {
        script::object_object * history = nullptr;
        dom_bindings * owner = nullptr;
    };
    const auto own = [top](context & c, std::string_view member) -> owned {
        const value self = c.current_this();
        auto * history =
            self.is_object() ? static_cast<script::object_object *>(self.as_heap()) : nullptr;
        const value * window = history == nullptr ? nullptr : history->find(window_slot);
        if (window == nullptr) {
            c.throw_error("TypeError", "Illegal invocation");
            return {};
        }
        dom_bindings * owner = top->find_bindings_of(*window);
        if (owner == nullptr) {
            top->throw_dom_exception(c, "SecurityError",
                                     "Failed to read '" + std::string{member} +
                                         "' from 'History': the document is not fully active.");
            return {};
        }
        // The entry the document started on is made the first time it is
        // asked for: before then the document's address may still move.
        if (history->find(entries_slot) == nullptr) {
            const value entries = c.make_array();
            const value first = c.make_array();
            auto & fields = array_of(first)->items;
            fields.push_back(c.string(owner->location_href_));
            fields.push_back(value::null());
            fields.push_back(c.string("auto"));
            array_of(entries)->items.push_back(first);
            history->define(entries_slot, entries, script::attr_none);
            history->define(index_slot, value::number(0), script::attr_none);
            history->define(state_slot, value::null(), script::attr_none);
        }
        return {history, owner};
    };
    const auto entries_of = [](script::object_object & history) -> std::vector<value> & {
        return array_of(*history.find(entries_slot))->items;
    };
    const auto index_of = [](script::object_object & history) {
        return static_cast<std::size_t>(context::to_number(*history.find(index_slot)));
    };
    // The document's address moves: `document.URL` and the Location object
    // read the new one. The indicated element does not: :target follows a
    // fragment navigation, and this is none (7.4.4 "URL and history update
    // steps" never scroll to a fragment).
    const auto set_address = [](context & c, dom_bindings & owner, const std::string & url) {
        owner.location_href_ = url;
        const std::size_t mark = url.find('#');
        owner.location_hash_ =
            mark == std::string::npos || mark + 1 == url.size() ? std::string{} : url.substr(mark);
        if (auto * doc = owner.document_object()) {
            for (const char * name : {"URL", "documentURI", "baseURI"}) {
                doc->set(name, c.string(url));
            }
        }
    };

    define_getter(cx, proto, "length", [own, entries_of](context & c, std::span<value>) {
        const owned at = own(c, "length");
        if (at.history == nullptr) { return value::undefined(); }
        return value::number(static_cast<double>(entries_of(*at.history).size()));
    });
    define_getter(
        cx, proto, "scrollRestoration",
        [own, entries_of, index_of](context & c, std::span<value>) {
            const owned at = own(c, "scrollRestoration");
            if (at.history == nullptr) { return value::undefined(); }
            return array_of(entries_of(*at.history)[index_of(*at.history)])->items[entry_mode];
        },
        // An enumeration: a value other than "auto" or "manual" is ignored,
        // not thrown at (WebIDL 3.2.18).
        [own, entries_of, index_of](context & c, std::span<value> a) {
            const std::string mode = c.to_string(arg(a, 0));
            if (c.throw_pending()) { return value::undefined(); }
            const owned at = own(c, "scrollRestoration");
            if (at.history == nullptr || (mode != "auto" && mode != "manual")) {
                return value::undefined();
            }
            array_of(entries_of(*at.history)[index_of(*at.history)])->items[entry_mode] =
                c.string(mode);
            return value::undefined();
        });
    define_getter(cx, proto, "state", [own](context & c, std::span<value>) {
        const owned at = own(c, "state");
        if (at.history == nullptr) { return value::undefined(); }
        return *at.history->find(state_slot);
    });

    // go(delta), 7.4.2.3: 0 reloads; anything else traverses by that delta
    // on a later task, and a delta past either end of the entries does nothing.
    const auto go = [top, own, entries_of, index_of,
                     set_address](context & c, std::string_view member, long long delta) {
        const owned at = own(c, member);
        if (at.history == nullptr) { return; }
        if (delta == 0) {
            (void)c.call(c.lookup_property(at.owner->location_, "reload"), {}, at.owner->location_);
            return;
        }
        const value history = value::object(at.history);
        auto * task = c.allocate<script::native_object>(
            "traverse the history", [top, history, delta, entries_of, index_of,
                                     set_address](context & t, std::span<value>) {
                auto * self = static_cast<script::object_object *>(history.as_heap());
                dom_bindings * owner = top->find_bindings_of(*self->find(window_slot));
                if (owner == nullptr) { return value::undefined(); }
                std::vector<value> & entries = entries_of(*self);
                const auto from = static_cast<long long>(index_of(*self));
                const long long to = from + delta;
                if (to < 0 || to >= static_cast<long long>(entries.size())) {
                    return value::undefined();
                }
                // The entry being left keeps the address the document has now,
                // which a fragment navigation may have moved.
                const std::string old_url = owner->location_href_;
                array_of(entries[static_cast<std::size_t>(from)])->items[entry_url] =
                    t.string(old_url);
                const auto & target = array_of(entries[static_cast<std::size_t>(to)])->items;
                const std::string new_url = t.to_string(target[entry_url]);
                self->define(index_slot, value::number(static_cast<double>(to)), script::attr_none);
                set_address(t, *owner, new_url);
                // "Restore the history object state", then `popstate` with it.
                const value state = structured_clone(t, *self, target[entry_state]);
                if (t.throw_pending()) { return value::undefined(); }
                self->define(state_slot, state, script::attr_none);
                value event = owner->make_event(t, "popstate", node_id{});
                auto * object = static_cast<script::object_object *>(event.as_heap());
                object->set("bubbles", value::boolean(false));
                object->set("cancelable", value::boolean(false));
                object->set("state", state);
                object->set("hasUAVisualTransition", value::boolean(false));
                if (const value ctor = t.has_global("PopStateEvent") ? t.global("PopStateEvent")
                                                                     : value::undefined();
                    ctor.is_object_like()) {
                    if (const value p = t.lookup_property(ctor, "prototype"); p.is_object()) {
                        object->prototype = p;
                    }
                }
                // Dispatched at the window step, as a PromiseRejectionEvent is:
                // it does not bubble, so a dispatch from the document would
                // never reach the window.
                object->set("target", *self->find(window_slot));
                object->set("srcElement", *self->find(window_slot));
                (void)owner->dispatch_to(event, path_step{node_id{}, listen_on::window});
                // `hashchange` on a task of its own, when the fragment moved.
                const std::optional<url_record> before = parse_url(old_url);
                const std::optional<url_record> after = parse_url(new_url);
                if (before && after && before->fragment != after->fragment) {
                    const value window = *self->find(window_slot);
                    auto * fire = t.allocate<script::native_object>(
                        "hashchange", [top, window, old_url, new_url](context &, std::span<value>) {
                            if (dom_bindings * at = top->find_bindings_of(window)) {
                                (void)at->dispatch_hash_change(old_url, new_url);
                            }
                            return value::undefined();
                        });
                    fire->retained.push_back(window);
                    (void)top->add_timer(value::object(fire), 0, false);
                }
                return value::undefined();
            });
        // The History object is in a C++ capture, which the collector cannot
        // see, so the task retains it.
        task->retained.push_back(history);
        (void)top->add_timer(value::object(task), 0, false);
    };
    set_operation(cx, proto, "go", 0, [go](context & c, std::span<value> a) {
        const long long delta = number_to_int32(context::to_number(arg(a, 0)));
        if (!c.throw_pending()) { go(c, "go", delta); }
        return value::undefined();
    });
    set_operation(cx, proto, "back", 0, [go](context & c, std::span<value>) {
        go(c, "back", -1);
        return value::undefined();
    });
    set_operation(cx, proto, "forward", 0, [go](context & c, std::span<value>) {
        go(c, "forward", 1);
        return value::undefined();
    });

    // pushState(data, unused, url) and replaceState: the "shared history
    // push/replace state steps" (7.4.2.3), then the URL and history update
    // steps (7.4.4) with this document and no navigation.
    const auto update = [top, own, entries_of, index_of,
                         set_address](context & c, std::span<value> a, bool push) {
        const char * member = push ? "pushState" : "replaceState";
        // The arguments converted first, in order, as WebIDL does.
        const value data = arg(a, 0);
        (void)c.to_string(arg(a, 1));
        if (c.throw_pending()) { return; }
        const value given = arg(a, 2);
        std::optional<std::string> url;
        if (!given.is_nullish()) {
            url = to_usv_string(c.to_string(given));
            if (c.throw_pending()) { return; }
        }
        const owned at = own(c, member);
        if (at.history == nullptr) { return; }
        dom_bindings & owner = *at.owner;
        const value serialized = structured_clone(c, *at.history, data);
        if (c.throw_pending()) { return; }
        const context::rooted keep_serialized{c, serialized};
        std::string new_url = owner.location_href_;
        if (url && !url->empty()) {
            const std::string base = detail::document_base_url(owner.doc_->read(), *owner.atoms_,
                                                               owner.fallback_base_url());
            const std::optional<url_record> parsed =
                base.empty() ? parse_url(*url) : parse_url(*url, base);
            const std::optional<url_record> current = parse_url(owner.location_href_);
            if (!parsed || !current || !can_rewrite(*current, *parsed)) {
                top->throw_dom_exception(c, "SecurityError",
                                         "Failed to execute '" + std::string{member} +
                                             "' on 'History': A history state object with URL '" +
                                             *url + "' cannot be created in a document with URL '" +
                                             owner.location_href_ + "'.");
                return;
            }
            new_url = parsed->serialize();
        }
        std::vector<value> & entries = entries_of(*at.history);
        std::size_t index = index_of(*at.history);
        const auto & active = array_of(entries[index])->items;
        const value mode = active[entry_mode];
        const value entry = c.make_array();
        auto & fields = array_of(entry)->items;
        fields.push_back(c.string(new_url));
        fields.push_back(serialized);
        fields.push_back(mode);
        if (push) {
            // The active entry keeps the address it had; every entry after it
            // is gone, and the new one is the last.
            array_of(entries[index])->items[entry_url] = c.string(owner.location_href_);
            entries.resize(index + 1);
            entries.push_back(entry);
            ++index;
            at.history->define(index_slot, value::number(static_cast<double>(index)),
                               script::attr_none);
        } else {
            entries[index] = entry;
        }
        const value state = structured_clone(c, *at.history, serialized);
        if (c.throw_pending()) { return; }
        at.history->define(state_slot, state, script::attr_none);
        set_address(c, owner, new_url);
    };
    set_operation(cx, proto, "pushState", 2, [update](context & c, std::span<value> a) {
        update(c, a, true);
        return value::undefined();
    });
    set_operation(cx, proto, "replaceState", 2, [update](context & c, std::span<value> a) {
        update(c, a, false);
        return value::undefined();
    });
}

} // namespace ctbrowser::shell
