// dom_bindings - `matchMedia` and `MediaQueryList`, CSSOM View §4.2
// (https://drafts.csswg.org/cssom-view/#the-mediaquerylist-interface).
//
// A list is an EventTarget with two pieces of state in private slots: the
// media text (the list SERIALISED - the same serialiser `MediaList.mediaText`
// uses, so `(min-width: 10px) and (min-height: 10px)` is neither sorted nor
// deduplicated, and a query that does not parse is "not all") and the
// document it belongs to. `matches` is evaluated LIVE against that document's
// environment - the same evaluation the cascade gives an `@media` rule - so
// a frame's list reads the frame's viewport, not the page's.
//
// §13 "evaluate media queries and report changes": whoever changes an
// environment (the browser on a resize, the frame layout when a frame's box
// moved) calls report_media_query_changes, which compares every listened-to
// list's answer with the one it last reported and, for each that flipped,
// fires a `change` MediaQueryListEvent one tick later - the scroll steps'
// shape, so no script runs inside a layout. `addListener`/`removeListener`
// are the aliases of `addEventListener("change", ...)` the specification
// says they are, and `onchange` is an ordinary event handler IDL attribute.
//
// The list used to be a plain object with two data properties, `matches` read
// once at creation and listener methods that accepted and forgot (css/
// cssom-view's eight MediaQueryList files; Phaser asks `(orientation:
// portrait)` and Babylon `(pointer: fine)`).

#include "events/internal.hpp"
#include "stylesheets/internal.hpp"

#include <ctbrowser/style/css/media.hpp>
#include <ctbrowser/style/engine.hpp>

#include <string>
#include <vector>

namespace ctbrowser::shell {

using namespace detail;

namespace {

const std::string media_slot = std::string{script::private_key_prefix} + "mql:media";
const std::string matches_slot = std::string{script::private_key_prefix} + "mql:matches";
const std::string document_slot = std::string{script::private_key_prefix} + "mql:document";

[[nodiscard]] script::object_object * object_of(value v) {
    return v.is_object() ? static_cast<script::object_object *>(v.as_heap()) : nullptr;
}

[[nodiscard]] std::string slot_string(context & cx, value list, const std::string & slot) {
    auto * object = object_of(list);
    const value * held = object == nullptr ? nullptr : object->find(slot);
    return held == nullptr ? std::string{} : cx.to_string(*held);
}

} // namespace

bool dom_bindings::media_query_matches(std::string_view media) {
    return style::css::evaluate(style::css::parse_media_query_list(media),
                                selector_engine().environment());
}

bool dom_bindings::is_media_query_list(value v) const {
    const value wanted = primary().media_query_list_prototype_;
    if (!v.is_object() || !wanted.is_object()) { return false; }
    value link = static_cast<script::object_object *>(v.as_heap())->prototype;
    for (int depth = 0; depth < 64 && link.is_object(); ++depth) {
        if (link.as_heap() == wanted.as_heap()) { return true; }
        link = static_cast<script::object_object *>(link.as_heap())->prototype;
    }
    return false;
}

// Which bindings' document a list names: the page's, or one of the documents
// it made (a frame's). The page's when the slot names nobody.
dom_bindings & dom_bindings::owner_of_media_query_list(value list) {
    auto * object = object_of(list);
    const value * held = object == nullptr ? nullptr : object->find(document_slot);
    dom_bindings & top = primary();
    if (held == nullptr || top.is_the_document(*held)) { return top; }
    for (const auto & made : top.secondary_documents_) {
        if (made->is_the_document(*held)) { return *made; }
    }
    return top;
}

value dom_bindings::match_media(context & cx, std::string_view text) {
    auto * list = cx.allocate<script::object_object>();
    list->prototype = primary().media_query_list_prototype_;
    const std::string media = serialize_media_query_list(parse_media_query_list(text));
    list->set(media_slot, cx.string(media));
    list->set(matches_slot, value::boolean(media_query_matches(media)));
    list->set(document_slot, document_);
    return value::object(list);
}

void dom_bindings::report_media_query_changes() {
    if (cx_ == nullptr) { return; }
    dom_bindings & top = primary();
    // THE LISTS SOMEBODY IS LISTENING TO - the handler's listener counts
    // (HTML 8.1.8.1 registers one when `onchange` is set). A list nobody
    // listens to has nothing to report and reads `matches` live anyway.
    // ponytail: every listener of every document is scanned per report; a
    // per-list registry if a page holds thousands of listeners.
    std::vector<value> lists;
    const auto collect = [&](const dom_bindings & owner) {
        for (const listener & l : owner.listeners_) {
            if (l.on != listen_on::object || !is_media_query_list(l.host) ||
                &owner_of_media_query_list(l.host) != this) {
                continue;
            }
            bool seen = false;
            for (const value & have : lists) { seen = seen || have.as_heap() == l.host.as_heap(); }
            if (!seen) { lists.push_back(l.host); }
        }
    };
    collect(top);
    for (const auto & made : top.secondary_documents_) { collect(*made); }
    for (const value & list : lists) {
        auto * object = object_of(list);
        const value * last = object->find(matches_slot);
        const bool now = media_query_matches(slot_string(*cx_, list, media_slot));
        if (last != nullptr && context::truthy(*last) == now) { continue; }
        object->set(matches_slot, value::boolean(now));
        pending_media_changes_.push_back(list);
    }
    if (pending_media_changes_.empty() || media_changes_queued_) { return; }
    media_changes_queued_ = true;
    (void)top.add_timer(
        native(
            *cx_, "report media query changes",
            [this](context & c, std::span<value>) {
                media_changes_queued_ = false;
                std::vector<value> due;
                due.swap(pending_media_changes_);
                for (const value & list : due) {
                    value event = make_event_object(c, "change", false, false);
                    auto * carrier = object_of(event);
                    // The interface's prototype, off its global constructor,
                    // as fire_animation_event reads AnimationEvent's.
                    if (const value ctor = c.global("MediaQueryListEvent");
                        ctor.is_kind(script::heap_kind::native)) {
                        const value * proto =
                            static_cast<script::native_object *>(ctor.as_heap())->find("prototype");
                        if (proto != nullptr && proto->is_object()) { carrier->prototype = *proto; }
                    }
                    carrier->set("media", c.string(slot_string(c, list, media_slot)));
                    carrier->set("matches", *object_of(list)->find(matches_slot));
                    carrier->set(std::string{trusted_property}, value::boolean(true));
                    carrier->set(std::string{initialised_property}, value::boolean(true));
                    (void)dispatch_to(event, path_step{node_id{}, listen_on::object, list});
                }
                return value::undefined();
            }),
        0, false);
}

void dom_bindings::install_media_queries(context & cx) {
    auto * proto = cx.allocate<script::object_object>();
    proto->prototype = event_target_prototype_;
    media_query_list_prototype_ = value::object(proto);
    define_getter(cx, *proto, "media", [](context & c, std::span<value>) {
        return c.string(slot_string(c, c.current_this(), media_slot));
    });
    define_getter(cx, *proto, "matches", [this](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!is_media_query_list(self)) {
            c.throw_error("TypeError", "Illegal invocation");
            return value::undefined();
        }
        return value::boolean(
            owner_of_media_query_list(self).media_query_matches(slot_string(c, self, media_slot)));
    });
    define_getter(
        cx, *proto, "onchange",
        [this](context & c, std::span<value>) {
            return event_handler_get(c, c.current_this(), "onchange");
        },
        [this](context & c, std::span<value> a) {
            event_handler_set(c, c.current_this(), "onchange", arg(a, 0));
            return value::undefined();
        });
    // The two aliases, as the IDL says: `addListener(cb)` is
    // `addEventListener("change", cb)` and forwards to it so a listener added
    // one way can be removed the other (addListener-removeListener.html).
    for (const auto & [alias, method] : {std::pair{"addListener", "addEventListener"},
                                         std::pair{"removeListener", "removeEventListener"}}) {
        set_method(
            cx, *proto, alias,
            [method](context & c, std::span<value> a) {
                const value self = c.current_this();
                const value callback = arg(a, 0);
                if (!callback.is_object_like()) { return value::undefined(); }
                const value fn = c.lookup_property(self, method);
                const value args[] = {c.string("change"), callback};
                (void)c.call(fn, args, self);
                return value::undefined();
            },
            script::attr_builtin);
    }
    auto * ctor =
        cx.allocate<script::native_object>("MediaQueryList", [](context & c, std::span<value>) {
            c.throw_error("TypeError", "Illegal constructor");
            return value::undefined();
        });
    ctor->define("prototype", media_query_list_prototype_, script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    proto->define("@@toStringTag", cx.string("MediaQueryList"), script::attr_configurable);
    cx.define_global("MediaQueryList", value::object(ctor));
    // A bare global, as `getComputedStyle` is: `window` is a proxy that falls
    // back to the globals. A frame's window carries its own (frames.cpp).
    cx.define_native("matchMedia", [this](context & c, std::span<value> args) {
        return match_media(c, args.empty() ? std::string{} : c.to_string(args[0]));
    });
}

} // namespace ctbrowser::shell
