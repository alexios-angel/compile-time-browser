// dom_bindings - the event interfaces a page can name: Event.prototype and its
// methods, the constructor hierarchy from Event down to PointerEvent, the
// legacy init* methods, and EventTarget.
//
// One of three files carved out of a 1,647-line bindings/events.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// Does `self`'s prototype chain reach `wanted`?
//
// THIS IS THE "WAS I CALLED WITH `new`?" TEST, and it has to be a prototype
// walk rather than a bare is_object(). Every event constructor here initialises
// its RECEIVER, so `new MouseEvent(...)` arrives with an instance whose chain
// already reaches Event.prototype - and so does `super(...)` from a page's own
// `class X extends Event`, which is the whole reason for the shape. A plain
// call arrives with `this` undefined, which is a TypeError; the specification
// says an interface object is not callable.
[[nodiscard]] bool inherits_from(value self, value wanted) {
    if (!self.is_object() || !wanted.is_object()) { return false; }
    value link = static_cast<script::object_object *>(self.as_heap())->prototype;
    for (int depth = 0; depth < 64 && link.is_object(); ++depth) {
        if (link.as_heap() == wanted.as_heap()) { return true; }
        link = static_cast<script::object_object *>(link.as_heap())->prototype;
    }
    return false;
}

// One member of an event's init dictionary.
//
// A MISSING DICTIONARY, `undefined` AND `null` ARE THE SAME ANSWER. WebIDL
// converts all three to the dictionary with every member defaulted, and
// Event-subclasses-constructors.html tests each of the three separately for
// every interface - `new MouseEvent("type", null)` beside `new
// MouseEvent("type")` - which is 18 of its assertions.
[[nodiscard]] value dict_member(context & cx, value init, const char * name) {
    if (!init.is_object_like()) { return value::undefined(); }
    return cx.lookup_property(init, std::string{name});
}
[[nodiscard]] bool dict_flag(context & cx, value init, const char * name) {
    return context::truthy(dict_member(cx, init, name));
}
// A `long`/`double` member. NaN is 0 rather than NaN: WebIDL's integer
// conversions send it there and the suite's default-value cases compare against
// 0 with assert_equals, which NaN fails against itself.
[[nodiscard]] double dict_number(context & cx, value init, const char * name) {
    const value held = dict_member(cx, init, name);
    if (held.is_undefined()) { return 0.0; }
    const double number = context::to_number(held);
    return std::isnan(number) ? 0.0 : number;
}
[[nodiscard]] std::string dict_string(context & cx, value init, const char * name) {
    const value held = dict_member(cx, init, name);
    return held.is_undefined() ? std::string{} : cx.to_string(held);
}
// A nullable interface member - `relatedTarget`, `view`. Absent is `null` and
// not `undefined`, which the suite compares for with assert_equals.
[[nodiscard]] value dict_object(context & cx, value init, const char * name) {
    const value held = dict_member(cx, init, name);
    return held.is_undefined() ? value::null() : held;
}

} // namespace

// THE EVENT INTERFACES, as a hierarchy rather than as one class wearing every
// hat.
//
// There used to be two: `Event` and `CustomEvent`. Everything else - `UIEvent`,
// `MouseEvent`, `KeyboardEvent`, `WheelEvent`, `FocusEvent`, `CompositionEvent`,
// `InputEvent`, `PointerEvent` - was undefined, so `new MouseEvent('click')`
// threw and `document.createEvent('MouseEvent')` produced a plain Event. That is
// not a corner: a page that wants to synthesise a click has no other way to
// build one, and 42 assertions in dom/events fail on the constructor alone
// before they get to what they were testing.
//
// FOUR THINGS HAVE TO BE TRUE OF EACH and none of them was:
//
//   * `new X(type, init)` produces an object dispatch honours, with every member
//     of X's init dictionary AND of every dictionary it inherits.
//   * `e instanceof X` and `e instanceof Event` are both true, which means a
//     real prototype chain rather than a marker object.
//   * `e.constructor === X` and `e.constructor.name === "X"` - the second is
//     what Event-init-while-dispatching.html walks the prototype chain reading.
//   * a page can EXTEND one: `class MyEvent extends MouseEvent`. That is why
//     every constructor here initialises `this` instead of allocating.
void dom_bindings::install_event_interfaces(context & cx) {
    const auto phase_constants = [](auto * target) {
        target->set("NONE", value::number(0));
        target->set("CAPTURING_PHASE", value::number(1));
        target->set("AT_TARGET", value::number(2));
        target->set("BUBBLING_PHASE", value::number(3));
    };
    const auto method_on = [&cx](script::object_object * target, const char * name,
                                 script::native_fn fn) {
        target->define(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))),
                       script::attr_builtin);
    };

    // --- Event.prototype: everything an event DOES ------------------------
    //
    // On the prototype rather than on each event, which is where the
    // specification puts it and is also seven fewer allocations per event.
    auto * event_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    event_prototype_ = value::object(event_proto);
    phase_constants(event_proto);

    // One SHARED event object per dispatch, so preventDefault called by any
    // listener is visible to the browser and to every later listener - which
    // is what makes it mean anything at all.
    //
    // CANCELABLE OR NOTHING HAPPENS. `preventDefault` on an uncancellable event
    // is a no-op in every browser, and treating it as one is the difference
    // between a page that can refuse a click and a page that can refuse a load.
    //
    // AND NOT FROM A PASSIVE LISTENER, which is the same rule arriving from the
    // other side: `{passive: true}` is a promise not to cancel, and the DOM
    // enforces it rather than trusting it. Both refusals are silent, because
    // both are what a browser does.
    method_on(event_proto, "preventDefault", [](context & c, std::span<value>) {
        const value target = c.current_this();
        if (target.is_object() && context::truthy(c.lookup_property(target, "cancelable")) &&
            !flag_of(c, target, passive_property)) {
            static_cast<script::object_object *>(target.as_heap())
                ->set("defaultPrevented", value::boolean(true));
        }
        return value::undefined();
    });
    method_on(event_proto, "stopPropagation", [](context & c, std::span<value>) {
        const value target = c.current_this();
        if (target.is_object()) {
            static_cast<script::object_object *>(target.as_heap())
                ->set(std::string{cancel_bubble_property}, value::boolean(true));
        }
        return value::undefined();
    });
    // The IMMEDIATE flag stops the listeners at THIS step as well as the rest
    // of the path, which is the only difference between the two and the reason
    // both exist.
    method_on(event_proto, "stopImmediatePropagation", [](context & c, std::span<value>) {
        const value target = c.current_this();
        if (target.is_object()) {
            auto * o = static_cast<script::object_object *>(target.as_heap());
            o->set(std::string{cancel_bubble_property}, value::boolean(true));
            o->set(std::string{stop_immediate_property}, value::boolean(true));
        }
        return value::undefined();
    });
    // The path the CURRENT dispatch built, or an empty list outside one.
    method_on(event_proto, "composedPath", [](context & c, std::span<value>) {
        const value held = c.lookup_property(c.current_this(), std::string{path_property});
        return held.is_array() ? held : c.make_array();
    });
    // `initEvent` is how an event made by `document.createEvent` is given its
    // type - createEvent hands back an event whose type is the empty string, and
    // a page that never calls this dispatches an event named "".
    method_on(event_proto, "initEvent", [](context & c, std::span<value> a) {
        const value target = c.current_this();
        if (!target.is_object()) { return value::undefined(); }
        // THE TYPE IS MANDATORY. `initEvent()` with no argument is a TypeError
        // in every browser, and answering it with an event named "undefined"
        // would be a dispatch to nobody that looks like it worked.
        if (a.empty()) {
            c.throw_error("TypeError",
                          "initEvent requires at least 1 argument, but only 0 were passed");
            return value::undefined();
        }
        // DOES NOTHING WHILE THE EVENT IS BEING DISPATCHED. The specification
        // returns early on the dispatch flag, and it is not a corner: a listener
        // that re-initialises the event it was handed would otherwise rename it
        // and reset its flags underneath the rest of the path.
        if (flag_of(c, target, dispatch_property)) { return value::undefined(); }
        auto * o = static_cast<script::object_object *>(target.as_heap());
        o->set("type", c.string(arg_string(c, a, 0)));
        o->set("bubbles", value::boolean(a.size() > 1 && context::truthy(a[1])));
        o->set("cancelable", value::boolean(a.size() > 2 && context::truthy(a[2])));
        // AND CLEARS THE FLAGS. initEvent re-initialises the event, which means
        // the canceled flag, BOTH propagation flags, the target and the trusted
        // flag - an event that has been stopped once must be usable again, and
        // the suite dispatches the same object several times to check exactly
        // that. It also SETS the initialized flag, which is the only way an
        // event from document.createEvent ever becomes dispatchable.
        o->set("defaultPrevented", value::boolean(false));
        o->set(std::string{cancel_bubble_property}, value::boolean(false));
        o->set(std::string{stop_immediate_property}, value::boolean(false));
        o->set("target", value::null());
        o->set("srcElement", value::null());
        o->set(std::string{trusted_property}, value::boolean(false));
        o->set(std::string{initialised_property}, value::boolean(true));
        return value::undefined();
    });

    // `cancelBubble` AND `returnValue` are accessors rather than properties,
    // because both are one-way: `cancelBubble = false` does NOT restart a
    // stopped propagation and `returnValue = true` does not un-cancel an event.
    // As data properties each would undo itself, and both are set by shipped
    // code that expects the specified asymmetry.
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
            // `returnValue = false` IS preventDefault UNDER ANOTHER NAME, so it
            // is refused in a passive listener for the same reason - the suite
            // tests the two spellings separately because an engine that guarded
            // only the obvious one leaves the other as a way through.
            if (target.is_object() && !a.empty() && !context::truthy(a[0]) &&
                context::truthy(c.lookup_property(target, "cancelable")) &&
                !flag_of(c, target, passive_property)) {
                static_cast<script::object_object *>(target.as_heap())
                    ->set("defaultPrevented", value::boolean(true));
            }
            return value::undefined();
        });
    // THE ONE `isTrusted` GETTER. It is installed here so that every instance
    // can take a copy of it as an OWN accessor - see initialise_event - and so
    // that there is exactly one per page.
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

    // --- the constructors -------------------------------------------------
    //
    // `member_writer` is one interface's share of the init dictionary. A
    // subclass's runs its parent's FIRST, so the chain reads in specification
    // order and a member that appears on both - `relatedTarget` is on
    // FocusEventInit and MouseEventInit alike - is written by the more derived
    // one last.
    using member_writer = std::function<void(context &, script::object_object &, value)>;

    const auto constructor_for = [this, &cx](const char * name, value prototype_value,
                                             member_writer write_members) {
        auto * ctor = cx.allocate<script::native_object>(
            name,
            [this, name, prototype_value, write_members](context & c,
                                                         std::span<value> args) -> value {
                const value self = c.current_this();
                // AN INTERFACE OBJECT IS NOT CALLABLE. `Event("x")` without
                // `new` is a TypeError, and this is how it is told apart: `new`
                // and `super()` both arrive with an object receiver and a plain
                // call arrives with none at all.
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
                // THE TYPE IS CONVERTED BEFORE ANY DICTIONARY MEMBER IS READ,
                // and a `toString` that throws must reach the page - which is
                // one of Event-constructors.any.js's assertions.
                const std::string type = c.to_string(args[0]);
                const value init = arg(args, 1);
                // ONE MEMBER AT A TIME, IN ORDER, AND NOT AS CALL ARGUMENTS.
                // Every one of these may be a getter with side effects, and the
                // suite asserts which ran and in what order - so the sequence
                // cannot be left to the unspecified evaluation order of a
                // function call's arguments.
                const bool bubbles = dict_flag(c, init, "bubbles");
                const bool cancelable = dict_flag(c, init, "cancelable");
                const bool composed = dict_flag(c, init, "composed");
                auto * event = static_cast<script::object_object *>(self.as_heap());
                // THE PROTOTYPE IS FILLED IN HERE AND ONLY IF IT IS MISSING.
                // `context::make_instance` gives a NATIVE constructor's fresh
                // instance no prototype at all - it reads one off a closure and
                // off an ordinary object and a native is neither - so `new
                // Event(...)` arrives bare. `super()` from a page's `class X
                // extends Event` arrives with X.prototype already in place, and
                // overwriting that would flatten the subclass back to an Event.
                if (!event->prototype.is_object()) { event->prototype = prototype_value; }
                initialise_event(c, *event, type, bubbles, cancelable, now_ms_,
                                 is_trusted_getter_of(event_prototype_));
                event->set("composed", value::boolean(composed));
                // CONSTRUCTED IS INITIALISED. It is the difference between this
                // and document.createEvent, and it is what dispatchEvent asks.
                event->set(std::string{initialised_property}, value::boolean(true));
                write_members(c, *event, init);
                return self;
            });
        // THE PROTOTYPE IS IN A C++ LAMBDA CAPTURE, which is invisible to the
        // collector - see native_object::retained. It is also the constructor's
        // `prototype` property, whose table IS traced; both are recorded because
        // the two facts are independent and only one of them is obvious.
        ctor->retained.push_back(prototype_value);
        // Non-writable, non-configurable, non-enumerable, like every interface
        // object's `prototype` (WebIDL 3.6).
        ctor->define("prototype", prototype_value, script::attr_none);
        static_cast<script::object_object *>(prototype_value.as_heap())
            ->define("constructor", value::object(ctor), script::attr_builtin);
        cx.define_global(name, value::object(ctor));
        return ctor;
    };
    const auto interface_of = [&cx, &constructor_for](const char * name, value parent_prototype,
                                                      member_writer write_members) {
        auto * proto = static_cast<script::object_object *>(cx.make_object().as_heap());
        proto->prototype = parent_prototype;
        const value prototype_value = value::object(proto);
        (void)constructor_for(name, prototype_value, std::move(write_members));
        return prototype_value;
    };

    const member_writer no_members = [](context &, script::object_object &, value) {};

    // `Event` itself, on the prototype built above rather than a fresh one.
    phase_constants(constructor_for("Event", event_prototype_, no_members));

    // --- CustomEvent: one member, and it is the whole point ---------------
    custom_event_prototype_ = interface_of("CustomEvent", event_prototype_,
                                           [](context & c, script::object_object & e, value init) {
                                               e.set("detail", dict_object(c, init, "detail"));
                                           });
    // The older spelling, which pages that predate the constructor use after
    // document.createEvent("CustomEvent"). On the PROTOTYPE, so an event from
    // createEvent has it too - it used to be installed by the constructor only,
    // so the one path that actually needs it was the one path without it.
    method_on(static_cast<script::object_object *>(custom_event_prototype_.as_heap()),
              "initCustomEvent", [](context & c, std::span<value> a) {
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
              });

    // --- UIEvent, and everything below it ---------------------------------
    const member_writer ui_members = [](context & c, script::object_object & e, value init) {
        // `view` IS A Window OR NULL AND NOTHING ELSE. `new UIEvent("x", {view:
        // 7})` is a TypeError, which the suite tests directly - a nullable
        // interface member is not "anything, or null".
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
            // same points in document space - the same numbers here, because a
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
    // `getModifierState('Shift')` - the general form of the four booleans, and
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
    method_on(static_cast<script::object_object *>(mouse_prototype.as_heap()), "getModifierState",
              modifier_state);

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

    interface_of("PointerEvent", mouse_prototype,
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
                     e.set("tangentialPressure",
                           value::number(dict_number(c, init, "tangentialPressure")));
                     e.set("tiltX", value::number(dict_number(c, init, "tiltX")));
                     e.set("tiltY", value::number(dict_number(c, init, "tiltY")));
                     e.set("twist", value::number(dict_number(c, init, "twist")));
                     e.set("pointerType", c.string(dict_string(c, init, "pointerType")));
                     e.set("isPrimary", value::boolean(dict_flag(c, init, "isPrimary")));
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
        method_on(proto, "getModifierState", modifier_state);
        // `initKeyEvent` is DELIBERATELY ABSENT. It is a Gecko-only legacy
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
                     // `data` is a NULLABLE string here, unlike CompositionEvent's.
                     e.set("data", dict_member(c, init, "data").is_undefined()
                                       ? value::null()
                                       : c.string(dict_string(c, init, "data")));
                     e.set("isComposing", value::boolean(dict_flag(c, init, "isComposing")));
                     e.set("inputType", c.string(dict_string(c, init, "inputType")));
                 });

    // `ErrorEvent`, which is what an uncaught exception is reported as. It is
    // here rather than beside dispatch_error because a page CONSTRUCTS one:
    // window-event-restored-after-throwing-onerror.html dispatches its own.
    interface_of("ErrorEvent", event_prototype_,
                 [](context & c, script::object_object & e, value init) {
                     e.set("message", c.string(dict_string(c, init, "message")));
                     e.set("filename", c.string(dict_string(c, init, "filename")));
                     e.set("lineno", value::number(dict_number(c, init, "lineno")));
                     e.set("colno", value::number(dict_number(c, init, "colno")));
                     e.set("error", dict_member(c, init, "error"));
                 });

    // --- the legacy init* methods -----------------------------------------
    //
    // `initUIEvent`, `initMouseEvent` and `initKeyboardEvent` predate the
    // constructors and are still how a page written before 2015 fills one in.
    // Each is the same shape: a positional argument list, and NOTHING AT ALL
    // while the event is being dispatched - which is the half
    // Event-init-while-dispatching.html is about, and which is not decorative.
    // A listener that re-initialises the event it was handed would otherwise
    // rewrite it underneath the rest of the path.
    const auto init_common = [](context & c, std::span<value> a, script::object_object & o) {
        o.set("type", c.string(arg_string(c, a, 0)));
        o.set("bubbles", value::boolean(a.size() > 1 && context::truthy(a[1])));
        o.set("cancelable", value::boolean(a.size() > 2 && context::truthy(a[2])));
        o.set("defaultPrevented", value::boolean(false));
        o.set(std::string{cancel_bubble_property}, value::boolean(false));
        o.set(std::string{stop_immediate_property}, value::boolean(false));
        o.set(std::string{initialised_property}, value::boolean(true));
    };
    method_on(static_cast<script::object_object *>(ui_prototype.as_heap()), "initUIEvent",
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
              });
    method_on(static_cast<script::object_object *>(mouse_prototype.as_heap()), "initMouseEvent",
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
              });
    method_on(static_cast<script::object_object *>(keyboard_prototype.as_heap()),
              "initKeyboardEvent", [init_common](context & c, std::span<value> a) {
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
              });

    // --- EventTarget -------------------------------------------------------
    //
    // `new EventTarget()` - a listener list with no node under it.
    //
    // The three methods live on the PROTOTYPE and find their target through
    // `this`, which is what makes `class Nicer extends EventTarget` work: a
    // subclass instance inherits them and `this` is the instance. Capturing the
    // object in the closure instead would give every subclass the base's list.
    auto * target_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto target_method = [&](const char * name, script::native_fn fn) {
        method_on(target_proto, name, std::move(fn));
    };
    target_method("addEventListener", [this](context & c, std::span<value> args) {
        const value self = c.current_this();
        if (!self.is_object()) { return value::undefined(); }
        add_listener(make_listener(c, path_step{node_id{}, listen_on::object, self}, args));
        return value::undefined();
    });
    target_method("removeEventListener", [this](context & c, std::span<value> args) {
        const value self = c.current_this();
        const std::string type = arg_string(c, args, 0);
        const value callback = arg(args, 1);
        // The capture flag is part of a listener's identity, and the third
        // argument may be an options object or the bare boolean. NOTHING ELSE
        // in the dictionary is read - `once`, `passive` and `signal` are on
        // AddEventListenerOptions, which removeEventListener does not take, and
        // a page detects that by handing it getters and watching which run.
        const value options = arg(args, 2);
        const bool capture = options.is_object()
                                 ? context::truthy(c.lookup_property(options, "capture"))
                                 : context::truthy(options);
        std::erase_if(listeners_, [&](const listener & l) {
            return l.on == listen_on::object && l.host.bits() == self.bits() && l.type == type &&
                   l.capture == capture && l.callback.bits() == callback.bits();
        });
        return value::undefined();
    });
    target_method("dispatchEvent", [this](context & c, std::span<value> args) {
        const value self = c.current_this();
        const value event = arg(args, 0);
        // NOT AN EVENT IS A TypeError, not a quiet `true`. `dispatchEvent(null)`
        // used to report that nothing cancelled the event it was never given.
        if (!inherits_from(event, event_prototype_)) {
            c.throw_error("TypeError", "Failed to execute 'dispatchEvent' on 'EventTarget': "
                                       "parameter 1 is not of type 'Event'.");
            return value::boolean(false);
        }
        if (!self.is_object()) { return value::boolean(true); }
        return value::boolean(!dispatch_to(event, path_step{node_id{}, listen_on::object, self}));
    });
    // The Error constructor's shape, and for the same reason: `this` is the
    // instance when this runs through `new` or through a subclass's `super()`,
    // and a bare call still has to produce something.
    const value target_prototype = value::object(target_proto);
    auto * target_ctor = cx.allocate<script::native_object>(
        "EventTarget", [target_prototype](context & c, std::span<value>) {
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<script::object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = target_prototype; }
            return self;
        });
    // THE PROTOTYPE IS IN A C++ LAMBDA CAPTURE, which is invisible to the
    // collector - see native_object::retained. It is also the constructor's
    // `prototype` property, and the property table IS traced; both are recorded
    // because the two facts are independent and only one of them is obvious.
    target_ctor->retained.push_back(target_prototype);
    target_ctor->set("prototype", target_prototype);
    target_proto->define("constructor", value::object(target_ctor), script::attr_builtin);
    cx.define_global("EventTarget", value::object(target_ctor));
    event_target_prototype_ = target_prototype;

    // `window.dispatchEvent`. The window is the LAST stop on every path, so
    // dispatching AT it runs only the window's own listeners - which is what the
    // old stub did by accident and this does on purpose.
    if (auto * window = window_object()) {
        const value window_dispatch = value::object(cx.allocate<script::native_object>(
            "dispatchEvent", [this](context & c, std::span<value> args) {
                const value event = arg(args, 0);
                if (!inherits_from(event, event_prototype_)) {
                    c.throw_error("TypeError", "Failed to execute 'dispatchEvent' on 'Window': "
                                               "parameter 1 is not of type 'Event'.");
                    return value::boolean(false);
                }
                return value::boolean(!dispatch_to(event, path_step{node_id{}, listen_on::window}));
            }));
        window->set("dispatchEvent", window_dispatch);
        // AND THE BARE NAME, which was the missing third of the set. The window
        // is the global object, so `addEventListener`, `removeEventListener` and
        // `dispatchEvent` are all three ordinary globals; the first two were
        // defined as such and this one was not, so a page written the way the
        // specification's own examples are written -
        //
        //     addEventListener("wheel", handler);
        //     dispatchEvent(new Event("wheel"));
        //
        // got a listener registered and then a TypeError on the very next line.
        // non-cancelable-when-passive/synthetic-events-cancelable.html is
        // twelve subtests of exactly that pair and never reached an assertion.
        cx.define_global("dispatchEvent", window_dispatch);
        // `window.event` EXISTS AND IS UNDEFINED outside a dispatch, which is a
        // different thing from not existing: event-global.html opens with
        // assert_own_property(window, "event"). dispatch_to writes it, and
        // writes the bare global beside it - the window and the globals table
        // are separate storage here and both spellings are used in the wild.
        window->set("event", value::undefined());
    }
    cx.define_global("event", value::undefined());
}

} // namespace ctbrowser::shell
