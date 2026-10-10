//===- events/interfaces.cpp - Event interface constructors -----*- C++ -*-===//
//
// dom_bindings' event interfaces a page can name: Event.prototype and its
// methods, the constructor hierarchy from Event down to PointerEvent (plus
// Touch*, Animation/Transition and the other Event subclasses), the legacy
// init* methods, and EventTarget.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

/// Returns the bindings whose target `self` is: the owner of a node wrapper, the
/// bindings whose `document` it is, else this one, which is right for the
/// window and for a standalone EventTarget.
dom_bindings & dom_bindings::target_owner(value self) {
    if (dom_bindings * owner = owner_of(self)) { return *owner; }
    dom_bindings * top = primary_ == nullptr ? this : primary_;
    if (top->is_the_document(self)) { return *top; }
    for (const auto & made : top->secondary_documents_) {
        if (made->is_the_document(self)) { return *made; }
    }
    // A frame's MediaQueryList dispatches changes from its own document.
    if (is_media_query_list(self)) { return owner_of_media_query_list(self); }
    return *this;
}

namespace {

/// `dispatchEvent()` step 2: an event a script dispatches is not trusted, even
/// one the engine fired a moment ago (Event-dispatch-redispatch.html). This
/// applies only past step 1's two refusals, which dispatch_to makes: a trusted
/// `click` a listener tries to dispatch again throws, and stays trusted.
void untrust_for_dispatch(context & cx, value event) {
    if (!event.is_object() || flag_of(cx, event, dispatch_property) ||
        !flag_of(cx, event, initialised_property)) {
        return;
    }
    static_cast<script::object_object *>(event.as_heap())
        ->set(std::string{trusted_property}, value::boolean(false));
}

/// Does `self`'s prototype chain reach `wanted`? dispatchEvent uses it to
/// accept any Event, including an instance of a page's `class X extends Event`,
/// and to reject anything else with a TypeError. The depth cap of 64 links
/// keeps a cyclic chain from hanging.
[[nodiscard]] bool inherits_from(value self, value wanted) {
    if (!self.is_object() || !wanted.is_object()) { return false; }
    value link = static_cast<script::object_object *>(self.as_heap())->prototype;
    for (int depth = 0; depth < 64 && link.is_object(); ++depth) {
        if (link.as_heap() == wanted.as_heap()) { return true; }
        link = static_cast<script::object_object *>(link.as_heap())->prototype;
    }
    return false;
}

} // namespace

/// Installs the Event interface hierarchy (Event, CustomEvent, UIEvent and its
/// subclasses, Touch*, Animation/Transition, ErrorEvent, MessageEvent, ...)
/// and EventTarget. Each interface must satisfy:
///   * `new X(type, init)` reads every member of X's init dictionary and of
///     every dictionary it inherits.
///   * `e instanceof X` and `e instanceof Event` hold (a real prototype chain).
///   * `e.constructor === X` and `e.constructor.name === "X"`; the second is
///     what Event-init-while-dispatching.html reads walking the chain.
///   * A page can extend it (`class MyEvent extends MouseEvent`), which is why
///     each constructor initialises `this` rather than allocating.
void dom_bindings::install_event_interfaces(context & cx) {
    const auto phase_constants = [](auto * target) {
        target->set("NONE", value::number(0));
        target->set("CAPTURING_PHASE", value::number(1));
        target->set("AT_TARGET", value::number(2));
        target->set("BUBBLING_PHASE", value::number(3));
    };

    // Event.prototype holds the event methods. They live on the prototype, as
    // the specification says, rather than being allocated on each event.
    auto * event_proto = cx.allocate<script::object_object>();
    event_prototype_ = value::object(event_proto);
    phase_constants(event_proto);

    // One event object is shared by the whole dispatch, so preventDefault
    // called by any listener is visible to the browser and to every later
    // listener.
    //
    // preventDefault does nothing on an uncancelable event, and nothing from a
    // passive listener (`{passive: true}` is a promise not to cancel, which the
    // DOM enforces). Both refusals are silent, as in browsers.
    set_method(
        cx, *event_proto, "preventDefault",
        [](context & c, std::span<value>) {
            const value target = c.current_this();
            if (target.is_object() && context::truthy(c.lookup_property(target, "cancelable")) &&
                !flag_of(c, target, passive_property)) {
                static_cast<script::object_object *>(target.as_heap())
                    ->set("defaultPrevented", value::boolean(true));
            }
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *event_proto, "stopPropagation",
        [](context & c, std::span<value>) {
            const value target = c.current_this();
            if (target.is_object()) {
                static_cast<script::object_object *>(target.as_heap())
                    ->set(std::string{cancel_bubble_property}, value::boolean(true));
            }
            return value::undefined();
        },
        script::attr_builtin);
    // The immediate flag also stops the remaining listeners at this step, not
    // only the rest of the path; that is the only difference between the two.
    set_method(
        cx, *event_proto, "stopImmediatePropagation",
        [](context & c, std::span<value>) {
            const value target = c.current_this();
            if (target.is_object()) {
                auto * o = static_cast<script::object_object *>(target.as_heap());
                o->set(std::string{cancel_bubble_property}, value::boolean(true));
                o->set(std::string{stop_immediate_property}, value::boolean(true));
            }
            return value::undefined();
        },
        script::attr_builtin);
    // The path the current dispatch built, or an empty list outside one.
    set_method(
        cx, *event_proto, "composedPath",
        [](context & c, std::span<value>) {
            const value held = c.lookup_property(c.current_this(), std::string{path_property});
            return held.is_array() ? held : c.make_array();
        },
        script::attr_builtin);
    // `initEvent` is how an event made by `document.createEvent` is given its
    // type: createEvent hands back an event whose type is the empty string, and
    // a page that never calls this dispatches an event named "".
    set_method(
        cx, *event_proto, "initEvent",
        [](context & c, std::span<value> a) {
            const value target = c.current_this();
            if (!target.is_object()) { return value::undefined(); }
            // The type is mandatory: `initEvent()` with no argument is a TypeError
            // in every browser, not an event named "undefined".
            if (a.empty()) {
                c.throw_error("TypeError",
                              "initEvent requires at least 1 argument, but only 0 were passed");
                return value::undefined();
            }
            // Does nothing while the event is being dispatched (the specification
            // returns early on the dispatch flag): a listener that re-initialises
            // the event it was handed would otherwise rename it and reset its
            // flags underneath the rest of the path.
            if (flag_of(c, target, dispatch_property)) { return value::undefined(); }
            auto * o = static_cast<script::object_object *>(target.as_heap());
            o->set("type", c.string(arg_string(c, a, 0)));
            o->set("bubbles", value::boolean(a.size() > 1 && context::truthy(a[1])));
            o->set("cancelable", value::boolean(a.size() > 2 && context::truthy(a[2])));
            // Re-initialising clears the canceled flag, both propagation flags,
            // the target and the trusted flag, so an event stopped once can be
            // dispatched again. It also sets the initialized flag, which is the
            // only way an event from document.createEvent becomes dispatchable.
            o->set("defaultPrevented", value::boolean(false));
            o->set(std::string{cancel_bubble_property}, value::boolean(false));
            o->set(std::string{stop_immediate_property}, value::boolean(false));
            o->set("target", value::null());
            o->set("srcElement", value::null());
            o->set(std::string{trusted_property}, value::boolean(false));
            o->set(std::string{initialised_property}, value::boolean(true));
            return value::undefined();
        },
        script::attr_builtin);

    // `cancelBubble` and `returnValue` are accessors rather than data
    // properties because both are one-way: `cancelBubble = false` does not
    // restart a stopped propagation and `returnValue = true` does not
    // un-cancel an event.
    const auto accessor_on = [&cx](script::object_object * target, const char * name,
                                   script::native_fn read, script::native_fn write) {
        target->define_accessor(
            name, value::object(cx.allocate<script::native_object>(name, std::move(read))),
            write == nullptr
                ? value::undefined()
                : value::object(cx.allocate<script::native_object>(name, std::move(write))));
    };
    accessor_on(
        event_proto, "cancelBubble",
        [](context & c, std::span<value>) {
            return value::boolean(flag_of(c, c.current_this(), cancel_bubble_property));
        },
        [](context & c, std::span<value> a) {
            const value target = c.current_this();
            if (target.is_object() && !a.empty() && context::truthy(a[0])) {
                static_cast<script::object_object *>(target.as_heap())
                    ->set(std::string{cancel_bubble_property}, value::boolean(true));
            }
            return value::undefined();
        });
    accessor_on(
        event_proto, "returnValue",
        [](context & c, std::span<value>) {
            return value::boolean(
                !context::truthy(c.lookup_property(c.current_this(), "defaultPrevented")));
        },
        [](context & c, std::span<value> a) {
            const value target = c.current_this();
            // `returnValue = false` is preventDefault under another name, so it
            // is refused in a passive listener too.
            if (target.is_object() && !a.empty() && !context::truthy(a[0]) &&
                context::truthy(c.lookup_property(target, "cancelable")) &&
                !flag_of(c, target, passive_property)) {
                static_cast<script::object_object *>(target.as_heap())
                    ->set("defaultPrevented", value::boolean(true));
            }
            return value::undefined();
        });
    // `timeStamp`, on the prototype, reading the slot initialise_event fills.
    accessor_on(
        event_proto, "timeStamp",
        [](context & c, std::span<value>) {
            const value self = c.current_this();
            if (!self.is_object()) { return value::number(0); }
            const value * held = static_cast<script::object_object *>(self.as_heap())
                                     ->find(std::string{timestamp_property});
            return held == nullptr ? value::number(0) : *held;
        },
        nullptr);
    // The single `isTrusted` getter. It is installed here so that every
    // instance can take a copy of it as an own accessor (see initialise_event)
    // and so that there is exactly one per page.
    accessor_on(
        event_proto, "isTrusted",
        [](context & c, std::span<value>) {
            const value self = c.current_this();
            if (!self.is_object()) { return value::boolean(false); }
            const value * held = static_cast<script::object_object *>(self.as_heap())
                                     ->find(std::string{trusted_property});
            return value::boolean(held != nullptr && context::truthy(*held));
        },
        nullptr);

    // The constructors.
    //
    // `member_writer` writes one interface's share of the init dictionary. A
    // subclass's writer runs its parent's first, so members are read in
    // specification order and a member that appears on both (`relatedTarget`
    // is on FocusEventInit and MouseEventInit alike) is written by the more
    // derived one last.
    using member_writer = std::function<void(context &, script::object_object &, value)>;

    const auto constructor_for = [this, &cx](const char * name, value prototype_value,
                                             member_writer write_members) {
        auto * ctor = cx.allocate<script::native_object>(
            name,
            [this, name, prototype_value, write_members](context & c,
                                                         std::span<value> args) -> value {
                const value self = c.current_this();
                // An interface object is not callable: `Event("x")` without
                // `new` is a TypeError. `new` and `super()` both arrive with an
                // object receiver and a plain call arrives with none.
                if (!self.is_object()) {
                    c.throw_error("TypeError", std::string{"Failed to construct '"} + name +
                                                   "': please use the 'new' operator.");
                    return value::undefined();
                }
                if (args.empty()) {
                    c.throw_error("TypeError", std::string{"Failed to construct '"} + name +
                                                   "': 1 argument required, but only 0 present.");
                    return value::undefined();
                }
                // The type is converted before any dictionary member is read,
                // and a `toString` that throws must reach the page
                // (Event-constructors.any.js checks both).
                const std::string type = c.to_string(args[0]);
                const value init = arg(args, 1);
                // Read one member per statement, in order, not as call
                // arguments: each may be a getter with side effects, the tests
                // check the order they ran in, and C++ leaves the evaluation
                // order of call arguments unspecified.
                const bool bubbles = dict_flag(c, init, "bubbles");
                const bool cancelable = dict_flag(c, init, "cancelable");
                const bool composed = dict_flag(c, init, "composed");
                auto * event = static_cast<script::object_object *>(self.as_heap());
                // Set the prototype only if it is missing.
                // `context::make_instance` gives a native constructor's fresh
                // instance no prototype at all (it reads one off a closure or an
                // ordinary object, and a native is neither), so `new Event(...)`
                // arrives bare. `super()` from a page's `class X extends Event`
                // arrives with X.prototype already in place, and overwriting
                // that would flatten the subclass back to an Event.
                if (!event->prototype.is_object()) { event->prototype = prototype_value; }
                initialise_event(c, *event, type, bubbles, cancelable, observed_now(),
                                 is_trusted_getter_of(event_prototype_));
                event->set("composed", value::boolean(composed));
                // A constructed event is initialised, unlike one from
                // document.createEvent; dispatchEvent checks this flag.
                event->set(std::string{initialised_property}, value::boolean(true));
                write_members(c, *event, init);
                return self;
            });
        // The prototype is held in a C++ lambda capture, which the collector
        // cannot see, so it is listed in native_object::retained rather than
        // relying on the constructor's traced `prototype` property.
        ctor->retained.push_back(prototype_value);
        // Non-writable, non-configurable, non-enumerable, like every interface
        // object's `prototype` (WebIDL 3.6).
        ctor->define("prototype", prototype_value, script::attr_none);
        auto * proto = static_cast<script::object_object *>(prototype_value.as_heap());
        proto->define("constructor", value::object(ctor), script::attr_builtin);
        // `@@toStringTag` is the interface's name (WebIDL 3.7.3), which is
        // what `assert_class_string(event, "Event")` reads through
        // Object.prototype.toString.
        if (proto->find(CTBROWSER_SYMBOL_KEY("toStringTag")) == nullptr) {
            proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string(name),
                          script::attr_configurable);
        }
        cx.define_builtin_global(name, value::object(ctor));
        return ctor;
    };
    const auto interface_of = [&cx, &constructor_for](const char * name, value parent_prototype,
                                                      member_writer write_members) {
        auto * proto = cx.allocate<script::object_object>();
        proto->prototype = parent_prototype;
        const value prototype_value = value::object(proto);
        (void)constructor_for(name, prototype_value, std::move(write_members));
        return prototype_value;
    };

    const member_writer no_members = [](context &, script::object_object &, value) {};

    // `Event` itself, on the prototype built above rather than a fresh one.
    phase_constants(constructor_for("Event", event_prototype_, no_members));

    // CustomEvent, whose one member is `detail`.
    custom_event_prototype_ = interface_of("CustomEvent", event_prototype_,
                                           [](context & c, script::object_object & e, value init) {
                                               e.set("detail", dict_object(c, init, "detail"));
                                           });
    // initCustomEvent, the pre-constructor API used after
    // document.createEvent("CustomEvent"). It lives on the prototype so events
    // from createEvent have it too.
    set_method(
        cx, *static_cast<script::object_object *>(custom_event_prototype_.as_heap()),
        "initCustomEvent",
        [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!self.is_object()) { return value::undefined(); }
            if (a.empty()) {
                c.throw_error("TypeError", "initCustomEvent requires at least 1 argument, "
                                           "but only 0 were passed");
                return value::undefined();
            }
            if (flag_of(c, self, dispatch_property)) { return value::undefined(); }
            auto * o = static_cast<script::object_object *>(self.as_heap());
            o->set("type", c.string(arg_string(c, a, 0)));
            o->set("bubbles", value::boolean(a.size() > 1 && context::truthy(a[1])));
            o->set("cancelable", value::boolean(a.size() > 2 && context::truthy(a[2])));
            o->set("detail", a.size() > 3 ? a[3] : value::null());
            o->set("defaultPrevented", value::boolean(false));
            o->set(std::string{cancel_bubble_property}, value::boolean(false));
            o->set(std::string{stop_immediate_property}, value::boolean(false));
            o->set(std::string{initialised_property}, value::boolean(true));
            return value::undefined();
        },
        script::attr_builtin);

    // UIEvent, and everything below it.
    const member_writer ui_members = [](context & c, script::object_object & e, value init) {
        // `view` must be a Window or null: `new UIEvent("x", {view: 7})` is a
        // TypeError, because a nullable interface member is not "anything, or
        // null".
        value view = dict_object(c, init, "view");
        if (!view.is_null()) {
            const value window_view =
                c.has_global("window") ? c.global("window") : value::undefined();
            if (!window_view.is_heap() || view.bits() != window_view.bits()) {
                c.throw_error("TypeError",
                              "Failed to construct 'UIEvent': member view is not of type Window.");
                return;
            }
        }
        e.set("view", view);
        e.set("detail", value::number(dict_number(c, init, "detail")));
        // `which` is UIEvent's, and the two interfaces below give it a meaning:
        // a key's key code, a mouse button numbered from one.
        e.set("which", value::number(0));
    };
    // EventModifierInit, shared by MouseEvent and KeyboardEvent.
    const member_writer modifier_members = [](context & c, script::object_object & e, value init) {
        e.set("ctrlKey", value::boolean(dict_flag(c, init, "ctrlKey")));
        e.set("shiftKey", value::boolean(dict_flag(c, init, "shiftKey")));
        e.set("altKey", value::boolean(dict_flag(c, init, "altKey")));
        e.set("metaKey", value::boolean(dict_flag(c, init, "metaKey")));
    };
    const value ui_prototype = interface_of("UIEvent", event_prototype_, ui_members);

    interface_of("FocusEvent", ui_prototype,
                 [ui_members](context & c, script::object_object & e, value init) {
                     ui_members(c, e, init);
                     e.set("relatedTarget", dict_object(c, init, "relatedTarget"));
                 });

    const member_writer mouse_members =
        [ui_members, modifier_members](context & c, script::object_object & e, value init) {
            ui_members(c, e, init);
            modifier_members(c, e, init);
            const double client_x = dict_number(c, init, "clientX");
            const double client_y = dict_number(c, init, "clientY");
            const double button = dict_number(c, init, "button");
            e.set("screenX", value::number(dict_number(c, init, "screenX")));
            e.set("screenY", value::number(dict_number(c, init, "screenY")));
            e.set("clientX", value::number(client_x));
            e.set("clientY", value::number(client_y));
            // `x`/`y` are aliases of clientX/clientY and `pageX`/`pageY` are the
            // same points in document space; they are the same numbers here, as a
            // constructed event carries no scroll offset of its own.
            e.set("x", value::number(client_x));
            e.set("y", value::number(client_y));
            e.set("pageX", value::number(client_x));
            e.set("pageY", value::number(client_y));
            e.set("offsetX", value::number(client_x));
            e.set("offsetY", value::number(client_y));
            e.set("movementX", value::number(dict_number(c, init, "movementX")));
            e.set("movementY", value::number(dict_number(c, init, "movementY")));
            e.set("button", value::number(button));
            e.set("buttons", value::number(dict_number(c, init, "buttons")));
            e.set("which", value::number(button + 1));
            e.set("relatedTarget", dict_object(c, init, "relatedTarget"));
        };
    const value mouse_prototype = interface_of("MouseEvent", ui_prototype, mouse_members);
    mouse_event_prototype_ = mouse_prototype;
    // `getModifierState('Shift')`: the general form of the four booleans, and
    // the only way to ask about the keys that have no shorthand.
    const auto modifier_state = [](context & c, std::span<value> a) {
        const std::string key = arg_string(c, a, 0);
        const char * property = key == "Control" ? "ctrlKey"
                                : key == "Shift" ? "shiftKey"
                                : key == "Alt"   ? "altKey"
                                : key == "Meta"  ? "metaKey"
                                                 : nullptr;
        if (property == nullptr) { return value::boolean(false); }
        return value::boolean(context::truthy(c.lookup_property(c.current_this(), property)));
    };
    set_method(cx, *static_cast<script::object_object *>(mouse_prototype.as_heap()),
               "getModifierState", modifier_state, script::attr_builtin);

    const value wheel_prototype =
        interface_of("WheelEvent", mouse_prototype,
                     [mouse_members](context & c, script::object_object & e, value init) {
                         mouse_members(c, e, init);
                         e.set("deltaX", value::number(dict_number(c, init, "deltaX")));
                         e.set("deltaY", value::number(dict_number(c, init, "deltaY")));
                         e.set("deltaZ", value::number(dict_number(c, init, "deltaZ")));
                         e.set("deltaMode", value::number(dict_number(c, init, "deltaMode")));
                     });
    {
        auto * proto = static_cast<script::object_object *>(wheel_prototype.as_heap());
        proto->set("DOM_DELTA_PIXEL", value::number(0));
        proto->set("DOM_DELTA_LINE", value::number(1));
        proto->set("DOM_DELTA_PAGE", value::number(2));
    }

    pointer_event_prototype_ = interface_of(
        "PointerEvent", mouse_prototype,
        [mouse_members](context & c, script::object_object & e, value init) {
            mouse_members(c, e, init);
            e.set("pointerId", value::number(dict_number(c, init, "pointerId")));
            e.set("width", value::number(dict_member(c, init, "width").is_undefined()
                                             ? 1.0
                                             : dict_number(c, init, "width")));
            e.set("height", value::number(dict_member(c, init, "height").is_undefined()
                                              ? 1.0
                                              : dict_number(c, init, "height")));
            e.set("pressure", value::number(dict_number(c, init, "pressure")));
            e.set("tangentialPressure", value::number(dict_number(c, init, "tangentialPressure")));
            e.set("tiltX", value::number(dict_number(c, init, "tiltX")));
            e.set("tiltY", value::number(dict_number(c, init, "tiltY")));
            e.set("twist", value::number(dict_number(c, init, "twist")));
            e.set("pointerType", c.string(dict_string(c, init, "pointerType")));
            e.set("isPrimary", value::boolean(dict_flag(c, init, "isPrimary")));
        });

    // Touch, TouchList and TouchEvent. No touch event is ever dispatched, but
    // the interfaces exist (as in desktop Chrome) because pages and WPT tests
    // construct them, for example to check that a passive touchstart listener
    // cannot cancel (default_passive_value). Pages that detect touch support
    // should test `'ontouchstart' in window`, which handler_attributes.cpp
    // deliberately leaves undefined.
    //
    // A TouchList is not an Array: it has `length`, `item()` and indexed
    // properties (`touches[0]`), but no map or forEach. NamedNodeMap has the
    // same shape here.
    auto * touch_list_proto = cx.allocate<script::object_object>();
    const value touch_list_prototype = value::object(touch_list_proto);
    set_method(
        cx, *touch_list_proto, "item",
        [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!self.is_object()) { return value::null(); }
            // Out of range is `null`, not undefined, because the getter returns
            // `Touch?`. The bounds are checked on the double before the cast
            // (2^32 = 4294967296), because converting a NaN or a 1e300 to an
            // integer type is undefined behaviour.
            const double at = a.empty() ? 0.0 : context::to_number(a[0]);
            if (!(at >= 0.0) || at >= 4294967296.0) { return value::null(); }
            const value * held = static_cast<script::object_object *>(self.as_heap())
                                     ->find(std::to_string(static_cast<std::uint32_t>(at)));
            return held == nullptr ? value::null() : *held;
        },
        script::attr_builtin);
    // Not constructible: a TouchList has no constructor in the IDL, so the
    // interface object throws rather than returning an empty object.
    {
        auto * list_ctor =
            cx.allocate<script::native_object>("TouchList", [](context & c, std::span<value>) {
                c.throw_error("TypeError",
                              "Illegal constructor: TouchList cannot be constructed by a page");
                return value::undefined();
            });
        list_ctor->retained.push_back(touch_list_prototype);
        list_ctor->define("prototype", touch_list_prototype, script::attr_none);
        touch_list_proto->define("constructor", value::object(list_ctor), script::attr_builtin);
        cx.define_builtin_global("TouchList", value::object(list_ctor));
    }

    // `new Touch({identifier: 0, target: el})`. Both members are required by
    // TouchInit, and WebIDL raises a TypeError for a missing required member,
    // so a Touch with no target is refused rather than made with a null one.
    auto * touch_proto = cx.allocate<script::object_object>();
    const value touch_prototype = value::object(touch_proto);
    {
        auto * touch_ctor = cx.allocate<script::native_object>(
            "Touch", [touch_prototype](context & c, std::span<value> args) -> value {
                const value self = c.current_this();
                if (!self.is_object()) {
                    c.throw_error("TypeError",
                                  "Failed to construct 'Touch': please use the 'new' operator.");
                    return value::undefined();
                }
                const value init = arg(args, 0);
                if (!init.is_object_like()) {
                    c.throw_error("TypeError", "Failed to construct 'Touch': parameter 1 is not of "
                                               "type 'TouchInit'.");
                    return value::undefined();
                }
                for (const char * required : {"identifier", "target"}) {
                    if (!dict_member(c, init, required).is_undefined()) { continue; }
                    c.throw_error("TypeError",
                                  std::string{"Failed to construct 'Touch': required member "} +
                                      required + " is undefined.");
                    return value::undefined();
                }
                auto * touch = static_cast<script::object_object *>(self.as_heap());
                if (!touch->prototype.is_object()) { touch->prototype = touch_prototype; }
                touch->set("identifier", value::number(dict_number(c, init, "identifier")));
                touch->set("target", dict_member(c, init, "target"));
                for (const char * name :
                     {"screenX", "screenY", "clientX", "clientY", "pageX", "pageY", "radiusX",
                      "radiusY", "rotationAngle", "force", "altitudeAngle", "azimuthAngle"}) {
                    touch->set(name, value::number(dict_number(c, init, name)));
                }
                // The one member with a default that is not zero.
                touch->set("touchType", c.string(dict_member(c, init, "touchType").is_undefined()
                                                     ? std::string{"direct"}
                                                     : dict_string(c, init, "touchType")));
                return self;
            });
        touch_ctor->retained.push_back(touch_prototype);
        touch_ctor->define("prototype", touch_prototype, script::attr_none);
        touch_proto->define("constructor", value::object(touch_ctor), script::attr_builtin);
        cx.define_builtin_global("Touch", value::object(touch_ctor));
    }

    // Converts a `sequence<Touch>` to a TouchList. It is read as an array-like
    // rather than copied out of an array, because a page may hand over anything
    // carrying a `length`.
    const auto touch_list_from = [touch_list_prototype](context & c, value sequence) {
        const value made = c.make_object();
        // Rooted across the reads below: every `lookup_property` here can run a
        // page's own getter, which can allocate and trigger a collection, and
        // the collector does not see a list held only in a C++ local.
        const context::rooted keep{c, made};
        auto * list = static_cast<script::object_object *>(made.as_heap());
        list->prototype = touch_list_prototype;
        const double length =
            sequence.is_object_like()
                ? context::to_number(c.lookup_property(sequence, std::string{"length"}))
                : 0.0;
        const std::uint32_t count = (std::isnan(length) || length <= 0.0) ? 0u
                                    : length >= 4294967295.0 ? 4294967295u
                                                             : static_cast<std::uint32_t>(length);
        for (std::uint32_t i = 0; i < count; ++i) {
            // `lookup_index`, not `lookup_property`: the commonest sequence is
            // an array literal, and an array's elements are not in its property
            // table, so `lookup_property(arr, "0")` reads undefined.
            list->set(std::to_string(i), c.lookup_index(sequence, value::number(i)));
        }
        // Read-only and not enumerable, like every interface's own attribute:
        // `for (var k in touches)` yields the indices and not `length`.
        list->define("length", value::number(count), script::attr_builtin);
        return made;
    };
    interface_of("TouchEvent", ui_prototype,
                 [ui_members, modifier_members,
                  touch_list_from](context & c, script::object_object & e, value init) {
                     ui_members(c, e, init);
                     // TouchEventInit inherits EventModifierInit, so the four
                     // modifier booleans are written here. TouchEvent declares
                     // them itself rather than inheriting them from MouseEvent.
                     modifier_members(c, e, init);
                     for (const char * name : {"touches", "targetTouches", "changedTouches"}) {
                         e.set(name, touch_list_from(c, dict_member(c, init, name)));
                     }
                 });

    const value keyboard_prototype = interface_of(
        "KeyboardEvent", ui_prototype,
        [ui_members, modifier_members](context & c, script::object_object & e, value init) {
            ui_members(c, e, init);
            modifier_members(c, e, init);
            const double key_code = dict_number(c, init, "keyCode");
            e.set("key", c.string(dict_string(c, init, "key")));
            e.set("code", c.string(dict_string(c, init, "code")));
            e.set("location", value::number(dict_number(c, init, "location")));
            e.set("repeat", value::boolean(dict_flag(c, init, "repeat")));
            e.set("isComposing", value::boolean(dict_flag(c, init, "isComposing")));
            e.set("charCode", value::number(dict_number(c, init, "charCode")));
            e.set("keyCode", value::number(key_code));
            // `which` is the key code for a keyboard event, which is what every
            // page that reads it means by it.
            e.set("which", value::number(key_code));
        });
    {
        auto * proto = static_cast<script::object_object *>(keyboard_prototype.as_heap());
        proto->set("DOM_KEY_LOCATION_STANDARD", value::number(0));
        proto->set("DOM_KEY_LOCATION_LEFT", value::number(1));
        proto->set("DOM_KEY_LOCATION_RIGHT", value::number(2));
        proto->set("DOM_KEY_LOCATION_NUMPAD", value::number(3));
        set_method(cx, *proto, "getModifierState", modifier_state, script::attr_builtin);
        // `initKeyEvent` is deliberately absent. It is a Gecko-only legacy
        // method that the DOM removed, and dom/events/KeyEvent-initKeyEvent.html
        // asserts three times that it is undefined.
    }

    interface_of("CompositionEvent", ui_prototype,
                 [ui_members](context & c, script::object_object & e, value init) {
                     ui_members(c, e, init);
                     e.set("data", c.string(dict_string(c, init, "data")));
                 });

    interface_of("InputEvent", ui_prototype,
                 [ui_members](context & c, script::object_object & e, value init) {
                     ui_members(c, e, init);
                     // `data` is a nullable string here, unlike CompositionEvent's.
                     e.set("data", dict_member(c, init, "data").is_undefined()
                                       ? value::null()
                                       : c.string(dict_string(c, init, "data")));
                     e.set("isComposing", value::boolean(dict_flag(c, init, "isComposing")));
                     e.set("inputType", c.string(dict_string(c, init, "inputType")));
                 });

    // AnimationEvent and TransitionEvent. Pages synthesise them
    // (`new AnimationEvent("webkitAnimationEnd")`, as the four
    // `webkit-*-event.html` files do) to test that prefixed and unprefixed
    // handlers are separate listeners, so the constructors are needed
    // regardless of which animation events the engine fires itself.
    //
    // Both derive from `Event`, not `UIEvent`, as CSS Animations and CSS
    // Transitions say: they carry no `view` and no `detail`. `elapsedTime` is a
    // double and `animationName`, `propertyName` and `pseudoElement` are
    // DOMStrings defaulting to "". The two dictionaries differ in one member
    // name, so they are written out rather than shared.
    interface_of("AnimationEvent", event_prototype_,
                 [](context & c, script::object_object & e, value init) {
                     e.set("animationName", c.string(dict_string(c, init, "animationName")));
                     e.set("elapsedTime", value::number(dict_number(c, init, "elapsedTime")));
                     e.set("pseudoElement", c.string(dict_string(c, init, "pseudoElement")));
                 });
    interface_of("TransitionEvent", event_prototype_,
                 [](context & c, script::object_object & e, value init) {
                     e.set("propertyName", c.string(dict_string(c, init, "propertyName")));
                     e.set("elapsedTime", value::number(dict_number(c, init, "elapsedTime")));
                     e.set("pseudoElement", c.string(dict_string(c, init, "pseudoElement")));
                 });

    // PromiseRejectionEvent (HTML, "unhandled promise rejections"): the promise
    // (a required member, so `new PromiseRejectionEvent("x")` is a TypeError)
    // and its reason.
    interface_of("PromiseRejectionEvent", event_prototype_,
                 [](context & c, script::object_object & e, value init) {
                     const value promise = dict_member(c, init, "promise");
                     if (promise.is_undefined()) {
                         c.throw_error("TypeError", "Failed to construct 'PromiseRejectionEvent': "
                                                    "the 'promise' member is required");
                         return;
                     }
                     e.set("promise", promise);
                     e.set("reason", dict_member(c, init, "reason"));
                 });

    // `MediaQueryListEvent` (CSSOM View §4.2): the list's media and whether it
    // matches now. Dispatched by media_queries.cpp's report.
    interface_of("MediaQueryListEvent", event_prototype_,
                 [](context & c, script::object_object & e, value init) {
                     e.set("media", c.string(dict_string(c, init, "media")));
                     e.set("matches", value::boolean(dict_flag(c, init, "matches")));
                 });

    // `HashChangeEvent` (HTML 7.4.6.2): two USVStrings defaulting to "".
    interface_of("HashChangeEvent", event_prototype_,
                 [](context & c, script::object_object & e, value init) {
                     e.set("oldURL", c.string(dict_string(c, init, "oldURL")));
                     e.set("newURL", c.string(dict_string(c, init, "newURL")));
                 });

    // `ToggleEvent` (HTML 6.12.2): a <details> or popover's `toggle`, from
    // one state to another; handler_attributes.cpp queues the details one.
    interface_of("ToggleEvent", event_prototype_,
                 [](context & c, script::object_object & e, value init) {
                     e.set("oldState", c.string(dict_string(c, init, "oldState")));
                     e.set("newState", c.string(dict_string(c, init, "newState")));
                 });

    // `PageTransitionEvent` (HTML 7.4.6.4): `pageshow` after the window's
    // `load` (input.cpp's dispatch), and whether the page came from a cache,
    // which here it never does.
    interface_of("PageTransitionEvent", event_prototype_,
                 [](context & c, script::object_object & e, value init) {
                     e.set("persisted", value::boolean(dict_flag(c, init, "persisted")));
                 });

    // `ErrorEvent`, which is what an uncaught exception is reported as. It is
    // here rather than beside dispatch_error because a page constructs one:
    // window-event-restored-after-throwing-onerror.html dispatches its own.
    interface_of("ErrorEvent", event_prototype_,
                 [](context & c, script::object_object & e, value init) {
                     e.set("message", c.string(dict_string(c, init, "message")));
                     e.set("filename", c.string(dict_string(c, init, "filename")));
                     e.set("lineno", value::number(dict_number(c, init, "lineno")));
                     e.set("colno", value::number(dict_number(c, init, "colno")));
                     e.set("error", dict_member(c, init, "error"));
                 });

    // `MessageEvent` (HTML 9.3.1): what postMessage fires at a window. `data`
    // is any and defaults to null, `source` a window or null, and `ports` a
    // frozen array, always empty, as this engine has no MessagePort.
    message_event_prototype_ = interface_of(
        "MessageEvent", event_prototype_, [](context & c, script::object_object & e, value init) {
            const value data = dict_member(c, init, "data");
            e.set("data", data.is_undefined() ? value::null() : data);
            e.set("origin", c.string(dict_string(c, init, "origin")));
            e.set("lastEventId", c.string(dict_string(c, init, "lastEventId")));
            const value source = dict_member(c, init, "source");
            if (!source.is_undefined() && !source.is_null() && !source.is_heap()) {
                c.throw_error("TypeError", "Failed to construct 'MessageEvent': "
                                           "member source is not a window.");
                return;
            }
            e.set("source", source.is_undefined() ? value::null() : source);
            const value ports = c.make_array();
            c.prevent_extensions(ports);
            e.set("ports", ports);
        });

    // The legacy init* methods. `initUIEvent`, `initMouseEvent` and
    // `initKeyboardEvent` predate the constructors and are still how older
    // pages fill an event in. Each takes a positional argument list and does
    // nothing while the event is being dispatched
    // (Event-init-while-dispatching.html): a listener that re-initialises the
    // event it was handed would otherwise rewrite it under the rest of the path.
    const auto init_common = [](context & c, std::span<value> a, script::object_object & o) {
        o.set("type", c.string(arg_string(c, a, 0)));
        o.set("bubbles", value::boolean(a.size() > 1 && context::truthy(a[1])));
        o.set("cancelable", value::boolean(a.size() > 2 && context::truthy(a[2])));
        o.set("defaultPrevented", value::boolean(false));
        o.set(std::string{cancel_bubble_property}, value::boolean(false));
        o.set(std::string{stop_immediate_property}, value::boolean(false));
        o.set(std::string{initialised_property}, value::boolean(true));
    };
    set_method(
        cx, *static_cast<script::object_object *>(ui_prototype.as_heap()), "initUIEvent",
        [init_common](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!self.is_object() || flag_of(c, self, dispatch_property)) {
                return value::undefined();
            }
            auto * o = static_cast<script::object_object *>(self.as_heap());
            init_common(c, a, *o);
            o->set("view", a.size() > 3 ? a[3] : value::null());
            o->set("detail", value::number(a.size() > 4 ? context::to_number(a[4]) : 0.0));
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *static_cast<script::object_object *>(mouse_prototype.as_heap()), "initMouseEvent",
        [init_common](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!self.is_object() || flag_of(c, self, dispatch_property)) {
                return value::undefined();
            }
            auto * o = static_cast<script::object_object *>(self.as_heap());
            init_common(c, a, *o);
            // The argument order is the one DOM Level 2 fixed and cannot
            // be changed: view, detail, screenX, screenY, clientX,
            // clientY, ctrl, alt, shift, meta, button, relatedTarget.
            const auto number_at = [&a](std::size_t i) {
                return value::number(i < a.size() ? context::to_number(a[i]) : 0.0);
            };
            const auto flag_at = [&a](std::size_t i) {
                return value::boolean(i < a.size() && context::truthy(a[i]));
            };
            o->set("view", a.size() > 3 ? a[3] : value::null());
            o->set("detail", number_at(4));
            o->set("screenX", number_at(5));
            o->set("screenY", number_at(6));
            o->set("clientX", number_at(7));
            o->set("clientY", number_at(8));
            o->set("ctrlKey", flag_at(9));
            o->set("altKey", flag_at(10));
            o->set("shiftKey", flag_at(11));
            o->set("metaKey", flag_at(12));
            o->set("button", number_at(13));
            o->set("relatedTarget", a.size() > 14 ? a[14] : value::null());
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *static_cast<script::object_object *>(keyboard_prototype.as_heap()),
        "initKeyboardEvent",
        [init_common](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!self.is_object() || flag_of(c, self, dispatch_property)) {
                return value::undefined();
            }
            auto * o = static_cast<script::object_object *>(self.as_heap());
            init_common(c, a, *o);
            o->set("view", a.size() > 3 ? a[3] : value::null());
            o->set("key", c.string(a.size() > 4 ? c.to_string(a[4]) : std::string{}));
            o->set("location", value::number(a.size() > 5 ? context::to_number(a[5]) : 0.0));
            o->set("code", c.string(a.size() > 6 ? c.to_string(a[6]) : std::string{}));
            o->set("repeat", value::boolean(a.size() > 7 && context::truthy(a[7])));
            return value::undefined();
        },
        script::attr_builtin);

    // EventTarget. `new EventTarget()` is a listener list with no node under it.
    //
    // The three methods live on the prototype and find their target through
    // `this`, which is what makes `class Nicer extends EventTarget` work: a
    // subclass instance inherits them and `this` is the instance. Capturing the
    // object in the closure instead would give every subclass the base's list.
    //
    // `this` is not always a standalone EventTarget. Every node wrapper
    // inherits these (`HTMLDivElement -> ... -> Node -> EventTarget`), so
    // `step_of` asks which target the receiver names rather than assuming the
    // object bucket, which no dispatch through the tree ever visits.
    auto * target_proto = cx.allocate<script::object_object>();
    // Nor is `this` always this document's. The prototypes belong to the realm
    // and are installed by the primary bindings, so a node of a document the
    // page made (`createHTMLDocument()`, `document.cloneNode(true)`) reaches
    // these lambdas, which capture the primary, and the primary's `step_of`
    // would file it as a standalone object with no capture, bubble or path.
    // Each method therefore asks which bindings own the receiver
    // (target_owner), as every Node operation does.
    set_method(
        cx, *target_proto, "addEventListener",
        [this](context & c, std::span<value> args) {
            const value self = c.current_this();
            if (!self.is_object_like()) { return value::undefined(); }
            dom_bindings & owner = target_owner(self);
            owner.add_listener(owner.make_listener(c, owner.step_of(self), args));
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *target_proto, "removeEventListener",
        [this](context & c, std::span<value> args) {
            const value self = c.current_this();
            dom_bindings & owner = target_owner(self);
            const std::string type = arg_string(c, args, 0);
            const value callback = arg(args, 1);
            // The capture flag is part of a listener's identity, and the third
            // argument may be an options object or the bare boolean. Nothing else
            // in the dictionary is read: `once`, `passive` and `signal` are on
            // AddEventListenerOptions, which removeEventListener does not take, and
            // a page detects that by handing it getters and watching which run.
            const value options = arg(args, 2);
            const bool capture = options.is_object()
                                     ? context::truthy(c.lookup_property(options, "capture"))
                                     : context::truthy(options);
            const path_step at = owner.step_of(self);
            owner.listeners_.remove(listener_target_of(at), type, callback.bits(), capture);
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *target_proto, "dispatchEvent",
        [this](context & c, std::span<value> args) {
            const value self = c.current_this();
            const value event = arg(args, 0);
            // A non-Event argument is a TypeError, not a quiet `true`.
            if (!inherits_from(event, event_prototype_)) {
                c.throw_error("TypeError", "Failed to execute 'dispatchEvent' on 'EventTarget': "
                                           "parameter 1 is not of type 'Event'.");
                return value::boolean(false);
            }
            if (!self.is_object_like()) { return value::boolean(true); }
            dom_bindings & owner = target_owner(self);
            untrust_for_dispatch(c, event);
            return value::boolean(!owner.dispatch_to(event, owner.step_of(self)));
        },
        script::attr_builtin);
    // Shaped like the Error constructor: `this` is the instance when this runs
    // through `new` or a subclass's `super()`, and a bare call makes a new
    // object.
    const value target_prototype = value::object(target_proto);
    auto * target_ctor = cx.allocate<script::native_object>(
        "EventTarget", [target_prototype](context & c, std::span<value>) {
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<script::object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = target_prototype; }
            return self;
        });
    // The prototype is held in a C++ lambda capture, which the collector cannot
    // see, so it is listed in native_object::retained as well as being the
    // constructor's traced `prototype` property.
    target_ctor->retained.push_back(target_prototype);
    target_ctor->set("prototype", target_prototype);
    target_proto->define("constructor", value::object(target_ctor), script::attr_builtin);
    cx.define_builtin_global("EventTarget", value::object(target_ctor));
    event_target_prototype_ = target_prototype;
    // The listener fence hangs off the EventTarget constructor, which is
    // reachable from a root the collector already walks:
    // `event_target_prototype_` is marked by register_roots and names its
    // constructor, whose `retained` list keeps the fence. A `value` in a C++
    // member is not a root by itself.
    install_listener_fence(cx, *target_ctor);
    // This must run after `event_target_prototype_` is set: it builds the DOM
    // interface prototypes it hangs the accessors on, and
    // `ensure_dom_interfaces` refuses until EventTarget exists.
    install_event_handler_attributes(cx);

    // window.dispatchEvent. The window is the last stop on every event path, so
    // dispatching at it runs only the window's own listeners.
    if (auto * window = window_object()) {
        const value window_dispatch = value::object(cx.allocate<script::native_object>(
            "dispatchEvent", [this](context & c, std::span<value> args) {
                const value event = arg(args, 0);
                if (!inherits_from(event, event_prototype_)) {
                    c.throw_error("TypeError", "Failed to execute 'dispatchEvent' on 'Window': "
                                               "parameter 1 is not of type 'Event'.");
                    return value::boolean(false);
                }
                untrust_for_dispatch(c, event);
                return value::boolean(!dispatch_to(event, path_step{node_id{}, listen_on::window}));
            }));
        window->set("dispatchEvent", window_dispatch);
        // The window is the global object, so dispatchEvent must also be a bare
        // global, alongside addEventListener and removeEventListener.
        cx.define_global("dispatchEvent", window_dispatch);
        // `window.event` exists and is undefined outside a dispatch, which is
        // different from not existing (event-global.html opens with
        // assert_own_property(window, "event")). dispatch_to writes it and the
        // bare global beside it, since the window and the globals table are
        // separate storage here.
        window->set("event", value::undefined());
    }
    cx.define_global("event", value::undefined());
}

} // namespace ctbrowser::shell
