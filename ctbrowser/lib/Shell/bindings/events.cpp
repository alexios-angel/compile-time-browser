// dom_bindings - input dispatch, Event objects and listener invocation.
//
// One of six files carved out of a 3,926-line bindings.cpp on 2026-08-09.
// These are all member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp, so they split across translation
// units with nothing to declare and no linkage to arrange.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <numbers>

// dom_bindings' method bodies - the API a page's script actually calls.
//
// The header lists what a page can reach; this is how each one works.

namespace ctbrowser::shell {

bool dom_bindings::refresh_wrappers() {
    if (cx_ == nullptr) { return false; }
    wrote_to_control_ = false;
    for (auto & [packed, obj] : wrappers_) {
        if (obj != nullptr) { refresh_element(*cx_, *obj, unpack(packed)); }
    }
    // The document's live properties too - this loop walks `wrappers_`, which
    // the document object is not in, so activeElement and title would go stale
    // the moment focus moved.
    refresh_document();
    return wrote_to_control_;
}

// WHERE FOCUS IS, pushed in. The hook the bindings already hold is write-only:
// script can call element.focus() and the browser hears it, but nothing came
// back, so `document.activeElement` had nothing to read. Same shape as
// observe_location, and for the same reason - a value set once at install is a
// snapshot, and this one changes on every click.
void dom_bindings::observe_focus(node_id id) {
    focused_ = id;
    refresh_document();
}

void dom_bindings::observe_viewport(int width, int height) {
    viewport_width_ = width;
    viewport_height_ = height;
}

bool dom_bindings::dispatch(std::string_view type, node_id target) {
    if (cx_ == nullptr) { return false; }
    return dispatch_event(type, target, make_event(*cx_, type, target));
}

// AT THE WINDOW, which is where an uncaught exception is reported. `node_id{}`
// is the window-and-document bucket every global listener already lives in, so
// this reaches `window.onerror` and `addEventListener("error", ...)` alike.
//
// The `error` property is undefined rather than a fabricated Error object: this
// engine's failure is a STRING by the time it gets here, and handing a page a
// synthetic exception whose stack is a lie is worse than handing it nothing.
// testharness.js reads `e.error && e.error.stack` and falls back to
// filename:lineno:colno, which is the branch this takes.
bool dom_bindings::dispatch_error(std::string_view message) {
    if (cx_ == nullptr) { return false; }
    value event = make_event(*cx_, "error", node_id{});
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("message", cx_->string(std::string{message}));
    object->set("filename", cx_->string(std::string{}));
    object->set("lineno", value::number(0));
    object->set("colno", value::number(0));
    object->set("error", value::undefined());
    return dispatch_event("error", node_id{}, event);
}

bool dom_bindings::dispatch_key(std::string_view type, node_id target, const input_event & input) {
    if (cx_ == nullptr) { return false; }
    value event = make_event(*cx_, type, target);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("code", cx_->string(input.key));
    object->set("key", cx_->string(dom_key_value(input.key, input.shift)));
    object->set("shiftKey", value::boolean(input.shift));
    object->set("ctrlKey", value::boolean(input.ctrl));
    object->set("altKey", value::boolean(false));
    object->set("metaKey", value::boolean(false));
    object->set("repeat", value::boolean(false));
    // THE LEGACY PAIR, and they are not optional in practice. Phaser's whole
    // keyboard system matches on `keyCode` - `KeyCodes.LEFT` is 37 - so without
    // these the event arrived with a correct `code` and no key ever matched:
    // every arrow key in a Phaser game did nothing at all. See dom_key_code.
    const int legacy = dom_key_code(input.key);
    object->set("keyCode", value::number(legacy));
    object->set("which", value::number(legacy));
    return dispatch_event(type, target, event);
}

// A `wheel` EVENT, which the page never used to get at all.
//
// The notch went straight to the scroller: `handle` scrolled the document and
// nothing was dispatched, so a page could not zoom, could not scroll its own
// canvas, and could not refuse the page scroll. Every 3D library on the web
// reads this one - Babylon's ArcRotateCamera has a `mousewheel` input attached
// by default - and a scene you could orbit but not zoom is how it showed up.
//
// deltaY IS IN PIXELS AND ITS SIGN IS THE OPPOSITE OF THE ENGINE'S. `wheel_y`
// here is notches with POSITIVE meaning away from the user; the DOM's `deltaY`
// is positive when the content scrolls DOWN, which is the same physical
// direction with the other sign. Getting that backwards inverts every zoom, and
// looks like a preference rather than a fault.
//
// The 100 is what a browser reports per notch in pixel mode (deltaMode 0), and
// libraries divide by it: Babylon's default wheelPrecision is 3, so a notch at
// 100 moves the camera about 33 units - which is why the number matters rather
// than being a scale nobody sees.
bool dom_bindings::dispatch_wheel(node_id target, const input_event & input) {
    if (cx_ == nullptr) { return false; }
    value event = make_event(*cx_, "wheel", target);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("clientX", value::number(input.x));
    object->set("clientY", value::number(input.y));
    object->set("pageX", value::number(input.x));
    object->set("pageY", value::number(input.y));
    object->set("offsetX", value::number(input.x));
    object->set("offsetY", value::number(input.y));
    object->set("deltaX", value::number(0));
    object->set("deltaY", value::number(-input.wheel_y * 100.0));
    object->set("deltaZ", value::number(0));
    object->set("deltaMode", value::number(0)); // 0 = pixels
    // THE LEGACY SPELLING TOO, and it is not politeness: plenty of shipped code
    // reads `wheelDelta` and it is the OPPOSITE sign again by definition, so a
    // library falling back to it inverts unless both are right.
    object->set("wheelDelta", value::number(input.wheel_y * 120.0));
    object->set("shiftKey", value::boolean(input.shift));
    object->set("ctrlKey", value::boolean(input.ctrl));
    object->set("buttons", value::number(0));
    return dispatch_event("wheel", target, event);
}

bool dom_bindings::dispatch_mouse(std::string_view type, node_id target,
                                  const input_event & input) {
    if (cx_ == nullptr) { return false; }
    // A POINTER EVENT FIRES TOO, and first.
    //
    // pointerdown/move/up are what a modern library listens for - they are the
    // one set that covers a mouse, a pen and a touch, so there is no reason to
    // register for anything else. p5.js 2.x registers ONLY for them, so a page
    // that rendered perfectly never responded to a single click, and nothing
    // anywhere said so: the listeners were installed, the events were
    // dispatched, and the two sets simply had different names.
    //
    // Both are fired, pointer first, which is the order a browser uses - a page
    // written against either one works, and one written against both sees them
    // in the right sequence.
    const auto build = [&](std::string_view kind, bool pointer) {
        value event = make_event(*cx_, kind, target);
        auto * object = static_cast<script::object_object *>(event.as_heap());
        object->set("clientX", value::number(input.x));
        object->set("clientY", value::number(input.y));
        object->set("pageX", value::number(input.x));
        object->set("pageY", value::number(input.y));
        object->set("offsetX", value::number(input.x));
        object->set("offsetY", value::number(input.y));
        // SDL numbers buttons from 1; the DOM numbers them from 0, with 2 for
        // the right button rather than 3.
        const int dom_button = input.button == 3 ? 2 : (input.button > 0 ? input.button - 1 : 0);
        object->set("button", value::number(dom_button));
        // `buttons` is a MASK of what is held, and it is not the same question
        // as `button`, which names the one that changed. p5 reads it to notice
        // a release it missed - `mouseIsPressed && e.buttons === 0` - so a
        // missing one leaves the flag stuck on forever.
        const bool down = kind == "mousedown" || kind == "pointerdown";
        object->set("buttons", value::number(down ? 1 << dom_button : 0));
        object->set("shiftKey", value::boolean(input.shift));
        object->set("ctrlKey", value::boolean(input.ctrl));
        if (pointer) {
            // One pointer, because there is one mouse. A page keyed on
            // pointerId - p5 keeps a map of active ones - needs it to be
            // stable, and needs the same id on down and up or the entry leaks.
            object->set("pointerId", value::number(1));
            object->set("pointerType", cx_->string("mouse"));
            object->set("isPrimary", value::boolean(true));
            object->set("pressure", value::number(down ? 0.5 : 0));
        }
        return event;
    };
    std::string_view pointer_type;
    if (type == "mousedown") {
        pointer_type = "pointerdown";
    } else if (type == "mouseup") {
        pointer_type = "pointerup";
    } else if (type == "mousemove") {
        pointer_type = "pointermove";
    }
    bool stopped = false;
    if (!pointer_type.empty()) {
        stopped = dispatch_event(pointer_type, target, build(pointer_type, true));
    }
    return dispatch_event(type, target, build(type, false)) || stopped;
}

// WHERE AN EVENT GOES, and it is not the node chain alone.
//
// The path is the target, then every ancestor, then the DOCUMENT and then the
// WINDOW - and the last two are the reason this exists as a function. The
// document is not in the node tree here: this tree builder makes the `<html>`
// element the document's root and there is no Document node above it, so the
// two objects a page listens on most have to be appended by hand.
//
// An EMPTY target is the document. `browser::tick` dispatches `load` and
// `DOMContentLoaded` that way and testharness registers its error handler on the
// window, so both buckets must still be reached; making the document the target
// rather than the window is what keeps `document.addEventListener(
// 'DOMContentLoaded', ...)` - the single most common listener on the web -
// firing. The deviation is that a `load` listener on the document fires too,
// where a browser fires that one at the window with the document only as the
// event's target.
std::vector<dom_bindings::path_step> dom_bindings::propagation_path(path_step at) const {
    std::vector<path_step> path;
    // A STANDALONE EventTarget IS THE WHOLE PATH. It is not in the tree, so
    // there is nothing above it to capture through or bubble to, and appending
    // the document and the window would deliver a page's private events to
    // every global listener there is.
    if (at.on == listen_on::object) { return {at}; }
    if (at.on == listen_on::node && at.node) {
        const auto txn = doc_->read();
        for (node_id walk = at.node; walk; walk = txn.parent(walk)) {
            path.push_back(path_step{walk, listen_on::node});
        }
    }
    if (at.on != listen_on::window) { path.push_back(path_step{node_id{}, listen_on::document}); }
    path.push_back(path_step{node_id{}, listen_on::window});
    return path;
}

// What `currentTarget` reports for one step, and what an `on<type>` handler is
// looked up on. The window is the PROXY rather than the object behind it: a page
// compares `evt.currentTarget === window` by identity, and `window` is the proxy.
value dom_bindings::object_of_step(context & cx, path_step step) {
    switch (step.on) {
    case listen_on::node: return wrap(cx, step.node);
    case listen_on::document: return document_;
    case listen_on::window: return cx.has_global("window") ? cx.global("window") : window_;
    case listen_on::object: return step.host;
    }
    return value::undefined();
}

namespace {

// The propagation flags, kept on the event where the page can see them through
// `cancelBubble`. Reading them back off the object rather than out of a local is
// what makes `evt.stopPropagation()` BEFORE dispatch mean something - which is
// exactly what dom/events/Event-dispatch-propagation-stopped.html does.
constexpr std::string_view stop_immediate_property = "__stopImmediate";
constexpr std::string_view cancel_bubble_property = "__cancelBubble";

[[nodiscard]] bool flag_of(context & cx, value event, std::string_view name) {
    return context::truthy(cx.lookup_property(event, std::string{name}));
}

} // namespace

// THE DISPATCH ALGORITHM: capture down the path, then bubble back up.
//
// What changed from the old three-line version, and why each half is
// load-bearing:
//
//   * `currentTarget` and `eventPhase` are set BEFORE each step's listeners run
//     and cleared after the whole dispatch. They are the two properties a
//     delegating page reads, and both read `undefined` before.
//   * the propagation flags are checked BETWEEN steps, so stopPropagation stops
//     the rest of the path, and the immediate flag is checked between listeners
//     at one step.
//   * an event whose `bubbles` is false runs the bubble pass at the TARGET only,
//     which is the whole meaning of the flag.
//   * `preventDefault` is honoured only when `cancelable`, which is the
//     difference between a cancellable click and a `load` a page cannot refuse.
bool dom_bindings::dispatch_to(value event, path_step at) {
    if (cx_ == nullptr || !event.is_object()) { return false; }
    // BEFORE the listeners run. A handler for `input` reads the field's new
    // value, so a wrapper still holding the old one is the whole bug.
    (void)refresh_wrappers();
    context & cx = *cx_;
    auto * object = static_cast<script::object_object *>(event.as_heap());
    const std::string type = cx.to_string(cx.lookup_property(event, "type"));
    const std::vector<path_step> path = propagation_path(at);

    const value target_object = object_of_step(cx, at);
    object->set("target", target_object);
    // `srcElement` is the same object under the name IE gave it, and plenty of
    // shipped code still reads it.
    object->set("srcElement", target_object);
    object->set(std::string{stop_immediate_property}, value::boolean(false));
    // THE DISPATCH FLAG, which is what makes initEvent a no-op while the event
    // is in flight and what a second, nested dispatch of the same object would
    // have to refuse.
    object->set("__dispatching", value::boolean(true));
    ++dispatch_depth_;
    // `composedPath()` is the path AS A LIST, and it is empty outside a
    // dispatch. Built once here rather than recomputed by the method, because
    // the tree may have moved by the time a page asks.
    {
        value listed = cx.make_array();
        auto * items = static_cast<script::array_object *>(listed.as_heap());
        for (const path_step & step : path) { items->items.push_back(object_of_step(cx, step)); }
        object->set("__path", listed);
        object->set("composedPath", value::object(cx.allocate<script::native_object>(
                                        "composedPath", [](context & c, std::span<value>) {
                                            const value self = c.current_this();
                                            const value held = c.lookup_property(self, "__path");
                                            return held.is_array() ? held : c.make_array();
                                        })));
    }

    const auto stopped = [&] { return flag_of(cx, event, cancel_bubble_property); };
    const auto at_target = [&](path_step step) { return step.on == at.on && step.node == at.node; };
    const auto phase = [&](path_step step, double otherwise) {
        return value::number(at_target(step) ? 2 : otherwise);
    };

    // CAPTURE: from the window down to the target. The target's own capturing
    // listeners run here, at phase AT_TARGET rather than CAPTURING_PHASE.
    for (std::size_t i = path.size(); i-- > 0;) {
        if (stopped()) { break; }
        object->set("currentTarget", object_of_step(cx, path[i]));
        object->set("eventPhase", phase(path[i], 1));
        fire_at(path[i], type, event, true);
    }
    // BUBBLE: back up. A non-bubbling event gets this pass at the target only.
    const bool bubbles = context::truthy(cx.lookup_property(event, "bubbles"));
    for (const path_step & step : path) {
        if (stopped()) { break; }
        if (!bubbles && !at_target(step)) { break; }
        object->set("currentTarget", object_of_step(cx, step));
        object->set("eventPhase", phase(step, 3));
        fire_at(step, type, event, false);
    }

    // AFTER THE DISPATCH the event is not travelling any more, and the two
    // properties that say where it is have to say so - a page keeps the object
    // and reads them later.
    object->set("currentTarget", value::null());
    object->set("eventPhase", value::number(0));
    object->set("__dispatching", value::boolean(false));
    // AND THE PROPAGATION FLAGS ARE UNSET - concept-event-dispatch step 14, and
    // not a detail: the same event object is dispatched twice by plenty of code,
    // and an engine that left the flag set made the second dispatch reach
    // nobody. Clearing them HERE rather than on entry is what lets
    // `stopPropagation()` called BEFORE a dispatch stop that dispatch, which is
    // the other half of the same rule.
    object->set(std::string{cancel_bubble_property}, value::boolean(false));
    object->set(std::string{stop_immediate_property}, value::boolean(false));
    object->set("__path", cx.make_array());
    // A `once` listener is removed AFTER the dispatch, not during it: erasing
    // from the vector being walked is how a later listener gets skipped. And not
    // after a NESTED dispatch either - a listener may dispatch, and the inner
    // compaction would shift the list the outer loop is indexing.
    --dispatch_depth_;
    reap_spent_listeners();
    // A LISTENER THAT FAULTS IS REPORTED AND THE FAULT CLEARED, exactly as for
    // a timer or an animation frame. Without this the first listener to fault
    // left the VM's failure flag set for the life of the page: every later
    // callback of any kind was refused, so the page stopped responding to
    // everything, and nothing anywhere said why.
    // ...and after an event, which is the other checkpoint a browser has: a
    // listener that resolves a promise has its handlers run before the next
    // event is dispatched, not at some later frame.
    cx.drain_microtasks();
    note_callback_fault(type);
    return prevented(event);
}

bool dom_bindings::dispatch_event(std::string_view type, node_id target, value event) {
    (void)type; // the event carries it; initEvent can have changed it since
    return dispatch_to(event, target ? path_step{target, listen_on::node}
                                     : path_step{node_id{}, listen_on::document});
}

value dom_bindings::make_event_object(context & cx, std::string_view type, bool bubbles,
                                      bool cancelable) {
    auto * event = static_cast<script::object_object *>(cx.make_object().as_heap());
    const value self = value::object(event);
    if (event_prototype_.is_object()) { event->prototype = event_prototype_; }
    event->set("type", cx.string(std::string{type}));
    event->set("target", value::null());
    event->set("srcElement", value::null());
    event->set("currentTarget", value::null());
    event->set("eventPhase", value::number(0));
    event->set("bubbles", value::boolean(bubbles));
    event->set("cancelable", value::boolean(cancelable));
    event->set("composed", value::boolean(false));
    // NOT trusted: every event a page can reach through this constructor was
    // made by the page. The engine's input events say so by overwriting it.
    event->set("isTrusted", value::boolean(false));
    event->set("timeStamp", value::number(now_ms_));
    event->set("defaultPrevented", value::boolean(false));
    event->set(std::string{cancel_bubble_property}, value::boolean(false));
    event->set(std::string{stop_immediate_property}, value::boolean(false));
    event->set("__dispatching", value::boolean(false));

    const auto method = [&](std::string name, script::native_fn fn) {
        event->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // One SHARED event object per dispatch, so preventDefault called by any
    // listener is visible to the browser and to every later listener - which
    // is what makes it mean anything at all.
    //
    // CANCELABLE OR NOTHING HAPPENS. `preventDefault` on an uncancellable event
    // is a no-op in every browser, and treating it as one is the difference
    // between a page that can refuse a click and a page that can refuse a load.
    method("preventDefault", [](context & c, std::span<value>) {
        const value target = c.current_this();
        if (target.is_object() && context::truthy(c.lookup_property(target, "cancelable"))) {
            static_cast<script::object_object *>(target.as_heap())
                ->set("defaultPrevented", value::boolean(true));
        }
        return value::undefined();
    });
    method("stopPropagation", [](context & c, std::span<value>) {
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
    method("stopImmediatePropagation", [](context & c, std::span<value>) {
        const value target = c.current_this();
        if (target.is_object()) {
            auto * o = static_cast<script::object_object *>(target.as_heap());
            o->set(std::string{cancel_bubble_property}, value::boolean(true));
            o->set(std::string{stop_immediate_property}, value::boolean(true));
        }
        return value::undefined();
    });
    // `initEvent` is how an event made by `document.createEvent` is given its
    // type - createEvent hands back an event whose type is the empty string, and
    // a page that never calls this dispatches an event named "".
    method("initEvent", [](context & c, std::span<value> a) {
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
        if (context::truthy(c.lookup_property(target, "__dispatching"))) {
            return value::undefined();
        }
        auto * o = static_cast<script::object_object *>(target.as_heap());
        o->set("type", c.string(arg_string(c, a, 0)));
        o->set("bubbles", value::boolean(a.size() > 1 && context::truthy(a[1])));
        o->set("cancelable", value::boolean(a.size() > 2 && context::truthy(a[2])));
        // AND CLEARS THE THREE FLAGS. initEvent re-initialises the event, which
        // means the canceled flag AND both propagation flags - an event that has
        // been stopped once must be usable again, and the suite dispatches the
        // same object several times to check exactly that.
        o->set("defaultPrevented", value::boolean(false));
        o->set(std::string{cancel_bubble_property}, value::boolean(false));
        o->set(std::string{stop_immediate_property}, value::boolean(false));
        return value::undefined();
    });

    // `cancelBubble` AND `returnValue` are accessors rather than properties,
    // because both are one-way: `cancelBubble = false` does NOT restart a
    // stopped propagation and `returnValue = true` does not un-cancel an event.
    // As data properties each would undo itself, and both are set by shipped
    // code that expects the specified asymmetry.
    const auto accessor = [&](std::string name, script::native_fn read, script::native_fn write) {
        event->define_accessor(
            name, value::object(cx.allocate<script::native_object>(name, std::move(read))),
            value::object(cx.allocate<script::native_object>(name, std::move(write))));
    };
    accessor(
        "cancelBubble",
        [](context & c, std::span<value>) {
            return value::boolean(
                context::truthy(c.lookup_property(c.current_this(), "__cancelBubble")));
        },
        [](context & c, std::span<value> a) {
            const value target = c.current_this();
            if (target.is_object() && !a.empty() && context::truthy(a[0])) {
                static_cast<script::object_object *>(target.as_heap())
                    ->set("__cancelBubble", value::boolean(true));
            }
            return value::undefined();
        });
    accessor(
        "returnValue",
        [](context & c, std::span<value>) {
            return value::boolean(
                !context::truthy(c.lookup_property(c.current_this(), "defaultPrevented")));
        },
        [](context & c, std::span<value> a) {
            const value target = c.current_this();
            if (target.is_object() && !a.empty() && !context::truthy(a[0]) &&
                context::truthy(c.lookup_property(target, "cancelable"))) {
                static_cast<script::object_object *>(target.as_heap())
                    ->set("defaultPrevented", value::boolean(true));
            }
            return value::undefined();
        });
    return self;
}

value dom_bindings::make_event(context & cx, std::string_view type, node_id target) {
    // AN ENGINE EVENT BUBBLES AND CAN BE CANCELLED. Every event the browser
    // generates here is one a page may refuse - a click, a key, a wheel notch -
    // and the flags decide whether the bubble pass runs at all and whether
    // preventDefault does anything, so getting them wrong is not cosmetic.
    value event = make_event_object(cx, type, true, true);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("isTrusted", value::boolean(true));
    object->set("target", wrap(cx, target));
    object->set("srcElement", wrap(cx, target));
    return event;
}

bool dom_bindings::prevented(value event) {
    if (!event.is_object()) { return false; }
    const value * slot =
        static_cast<script::object_object *>(event.as_heap())->find("defaultPrevented");
    return slot != nullptr && context::truthy(*slot);
}

// The third argument is an options object or a bare capture flag -
// `addEventListener(t, f, true)` is the old spelling and pages still use it.
//
// `passive` is accepted and ignored, which is honest: it is a promise the
// listener will not call preventDefault, and nothing here optimises on that
// promise, so honouring it would change nothing observable.
dom_bindings::listener dom_bindings::make_listener(context & cx, path_step target,
                                                   std::span<value> args) {
    listener made;
    made.target = target.node;
    made.on = target.on;
    made.host = target.host;
    made.type = arg_string(cx, args, 0);
    made.callback = arg(args, 1);
    const value options = arg(args, 2);
    if (options.is_object()) {
        made.abort_signal = cx.lookup_property(options, "signal");
        made.once = context::truthy(cx.lookup_property(options, "once"));
        made.capture = context::truthy(cx.lookup_property(options, "capture"));
    } else if (args.size() > 2) {
        made.capture = context::truthy(options);
    }
    return made;
}

void dom_bindings::add_listener(listener made) {
    // (type, callback, capture) ON ONE TARGET is a listener's identity, and the
    // DOM says a second registration of the same three does nothing at all.
    // Without this a page that registers in a function it calls twice got two
    // calls per event, and a `once` listener registered twice fired twice.
    const bool already = std::ranges::any_of(listeners_, [&](const listener & l) {
        return l.on == made.on && l.target == made.target && l.host.bits() == made.host.bits() &&
               l.type == made.type && l.capture == made.capture && !l.spent &&
               l.callback.bits() == made.callback.bits();
    });
    if (already) { return; }
    listeners_.push_back(std::move(made));
}

void dom_bindings::reap_spent_listeners() {
    if (dispatch_depth_ > 0) { return; }
    std::erase_if(listeners_, [](const listener & l) { return l.spent; });
}

void dom_bindings::fire_at(path_step step, std::string_view type, value event, bool capturing) {
    // Indexed rather than iterated: a listener may register another one, and
    // appending to the vector being walked invalidates an iterator. A listener
    // added during a dispatch does not run in that dispatch, which is the rule.
    const std::size_t count = listeners_.size();
    for (std::size_t i = 0; i < count && i < listeners_.size(); ++i) {
        // RE-READ THE FLAG EACH TIME. stopImmediatePropagation is defined by
        // stopping the listeners that would have run next at this very step, so
        // a check hoisted out of the loop implements the other method.
        if (context::truthy(cx_->lookup_property(event, "__stopImmediate"))) { return; }
        listener & l = listeners_[i];
        if (l.on != step.on || l.type != type || l.capture != capturing || l.spent) { continue; }
        if (l.on == listen_on::node && l.target != step.node) { continue; }
        if (l.on == listen_on::object && l.host.bits() != step.host.bits()) { continue; }
        if (l.once) { l.spent = true; }
        (void)cx_->call(l.callback, std::span<const value>{&event, 1});
    }
    if (!capturing && !context::truthy(cx_->lookup_property(event, "__stopImmediate"))) {
        fire_handler_property(object_of_step(*cx_, step), type, event);
    }
}

// `el.onclick = fn` - an EVENT HANDLER PROPERTY, which is the other half of the
// event API and was entirely absent. addEventListener worked; assigning a
// handler stored a function nothing ever called, so a page written the older way
// simply did nothing and said nothing about it.
//
// p5.js needs it on its own load path: `loadImage` sets `img.onload` and
// `img.onerror` and awaits a promise those two settle, and `loadBytes` does the
// same with a FileReader. It is not a legacy corner here, it is the only way
// those callbacks arrive.
//
// The DEVIATION, said plainly: the spec registers an on-handler as a listener at
// the moment it is ASSIGNED, so it runs interleaved with addEventListener ones
// in registration order. Here it runs after them, in the bubble phase, once.
// Observable only by a page that mixes both for one type and depends on the
// order - and cheap, versus a listener list that must be rewritten whenever a
// property is assigned.
void dom_bindings::fire_handler_property(value target, std::string_view type, value event) {
    if (cx_ == nullptr || !target.is_object()) { return; }
    const value handler = cx_->lookup_property(target, "on" + std::string{type});
    if (!handler.is_callable()) { return; }
    (void)cx_->call(handler, std::span<const value>{&event, 1}, target);
}

// `Event`, `CustomEvent` and `EventTarget`, as real interface objects.
//
// THREE THINGS HAVE TO BE TRUE and only one of them was:
//
//   * `new Event("x", { bubbles: true })` produces an object dispatch honours -
//     the old stub's preventDefault and stopPropagation were empty functions, so
//     an event could be constructed, dispatched and cancelled with no effect
//     whatsoever and nothing said so.
//   * `e instanceof Event` and `e.constructor === Event` - the second is what
//     testharness's assert_throws_dom and assert_class_string compare, and what
//     a page uses to tell one event from another.
//   * the phase constants are readable BOTH ways: `Event.AT_TARGET` off the
//     constructor and `e.AT_TARGET` off the instance, which is why they go on
//     the prototype as well.
void dom_bindings::install_event_interfaces(context & cx) {
    const auto phase_constants = [](auto * target) {
        target->set("NONE", value::number(0));
        target->set("CAPTURING_PHASE", value::number(1));
        target->set("AT_TARGET", value::number(2));
        target->set("BUBBLING_PHASE", value::number(3));
    };

    auto * event_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    event_prototype_ = value::object(event_proto);
    phase_constants(event_proto);

    // `{ bubbles, cancelable }` - the EventInit dictionary, and the two members
    // that change what a dispatch does.
    const auto init_flag = [](context & c, std::span<value> args, const char * name) {
        return args.size() > 1 && args[1].is_object() &&
               context::truthy(c.lookup_property(args[1], name));
    };
    auto * event_ctor = cx.allocate<script::native_object>(
        "Event", [this, init_flag](context & c, std::span<value> args) {
            return make_event_object(c, arg_string(c, args, 0), init_flag(c, args, "bubbles"),
                                     init_flag(c, args, "cancelable"));
        });
    event_ctor->set("prototype", event_prototype_);
    phase_constants(event_ctor);
    event_proto->set("constructor", value::object(event_ctor));
    cx.define_global("Event", value::object(event_ctor));

    // CustomEvent adds ONE member, `detail`, and it is the whole reason the
    // interface exists: it is how a page carries its own payload on an event.
    auto * custom_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    custom_proto->prototype = event_prototype_;
    custom_event_prototype_ = value::object(custom_proto);
    auto * custom_ctor = cx.allocate<script::native_object>(
        "CustomEvent", [this, init_flag](context & c, std::span<value> args) {
            value made = make_event_object(c, arg_string(c, args, 0), init_flag(c, args, "bubbles"),
                                           init_flag(c, args, "cancelable"));
            auto * object = static_cast<script::object_object *>(made.as_heap());
            object->prototype = custom_event_prototype_;
            object->set("detail", args.size() > 1 && args[1].is_object()
                                      ? c.lookup_property(args[1], "detail")
                                      : value::null());
            // The older spelling, which pages that predate the constructor use
            // after document.createEvent("CustomEvent").
            object->set(
                "initCustomEvent",
                value::object(cx_->allocate<script::native_object>(
                    "initCustomEvent", [](context & inner, std::span<value> a) {
                        const value self = inner.current_this();
                        if (!self.is_object()) { return value::undefined(); }
                        auto * o = static_cast<script::object_object *>(self.as_heap());
                        o->set("type", inner.string(arg_string(inner, a, 0)));
                        o->set("bubbles", value::boolean(a.size() > 1 && context::truthy(a[1])));
                        o->set("cancelable", value::boolean(a.size() > 2 && context::truthy(a[2])));
                        o->set("detail", arg(a, 3));
                        return value::undefined();
                    })));
            return made;
        });
    custom_ctor->set("prototype", custom_event_prototype_);
    custom_proto->set("constructor", value::object(custom_ctor));
    cx.define_global("CustomEvent", value::object(custom_ctor));

    // `new EventTarget()` - a listener list with no node under it.
    //
    // The three methods live on the PROTOTYPE and find their target through
    // `this`, which is what makes `class Nicer extends EventTarget` work: a
    // subclass instance inherits them and `this` is the instance. Capturing the
    // object in the closure instead would give every subclass the base's list.
    auto * target_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto target_method = [&](std::string name, script::native_fn fn) {
        target_proto->set(name,
                          value::object(cx.allocate<script::native_object>(name, std::move(fn))));
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
        // argument may be an options object or the bare boolean.
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
        if (!self.is_object() || !event.is_object()) { return value::boolean(true); }
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
    target_ctor->set("prototype", target_prototype);
    target_proto->set("constructor", value::object(target_ctor));
    cx.define_global("EventTarget", value::object(target_ctor));
    event_target_prototype_ = target_prototype;

    // `window.dispatchEvent`. The window is the LAST stop on every path, so
    // dispatching AT it runs only the window's own listeners - which is what the
    // old stub did by accident and this does on purpose.
    if (auto * window = window_object()) {
        window->set("dispatchEvent", value::object(cx.allocate<script::native_object>(
                                         "dispatchEvent", [this](context &, std::span<value> args) {
                                             const value event = arg(args, 0);
                                             if (!event.is_object()) {
                                                 return value::boolean(true);
                                             }
                                             return value::boolean(!dispatch_to(
                                                 event, path_step{node_id{}, listen_on::window}));
                                         })));
    }
}

} // namespace ctbrowser::shell
