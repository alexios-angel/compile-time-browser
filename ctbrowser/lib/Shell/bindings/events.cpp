// dom_bindings - input dispatch, Event objects and listener invocation.
//
// One of six files carved out of a 3,926-line bindings.cpp on 2026-08-09.
// These are all member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp, so they split across translation
// units with nothing to declare and no linkage to arrange.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <cmath>
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
// THE DISPATCH FLAG AND THE INITIALIZED FLAG, which are two different questions
// that `dispatchEvent` asks in the same breath and answers with the same
// exception. An event that is ALREADY travelling may not be dispatched again,
// and an event that has never been given a type may not be dispatched at all -
// `document.createEvent` hands back exactly that kind and `initEvent` is what
// clears it. Both are InvalidStateError, and both were silent successes here:
// dom/events/EventTarget-dispatchEvent.html spends 21 of its 25 assertions on
// the pair.
constexpr std::string_view dispatch_property = "__dispatching";
constexpr std::string_view initialised_property = "__initialised";
// Where `isTrusted` reads from. It is an accessor rather than a data property
// because the IDL says so and because a page can see the difference - see
// install_event_interfaces.
constexpr std::string_view trusted_property = "__isTrusted";
// THE IN-PASSIVE-LISTENER FLAG, which rides on the EVENT because the event is
// the only thing `preventDefault` is handed. `{passive: true}` is not a hint
// the engine may take or leave: the DOM says the canceled flag is not set while
// a passive listener is running, so a page that promises not to cancel and then
// tries is refused rather than believed.
constexpr std::string_view passive_property = "__passive";
// The propagation path, as a list, for `composedPath()`.
constexpr std::string_view path_property = "__path";

[[nodiscard]] bool flag_of(context & cx, value event, std::string_view name) {
    return context::truthy(cx.lookup_property(event, std::string{name}));
}

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

// THE ONE `isTrusted` GETTER OF THIS REALM, fetched back off Event.prototype.
//
// Every event carries it as an OWN accessor (below), and
// dom/events/Event-isTrusted.any.js compares the getters of two different
// events for identity - so a fresh native per event fails a test that a data
// property fails differently. Keeping the single copy in Event.prototype's own
// accessor table means there is no private slot to invent and nothing extra for
// the collector to be told about.
[[nodiscard]] value is_trusted_getter_of(value event_prototype) {
    if (!event_prototype.is_object()) { return value::undefined(); }
    auto * proto = static_cast<script::object_object *>(event_prototype.as_heap());
    const script::accessor_entry * entry = proto->find_accessor("isTrusted");
    return entry == nullptr ? value::undefined() : entry->getter;
}

// Everything `new Event`, `new MouseEvent`, `document.createEvent` and the
// engine's own input events have in common, written ONTO an object rather than
// into a new one.
//
// Writing onto the receiver is what makes `class SubclassedEvent extends Event`
// work: a constructor that allocates its own object leaves `super()`'s caller
// holding an instance nothing was ever written to, so every property the
// subclass inherits reads undefined. That is what
// Event-subclasses-constructors.html found, and because its assertion ran
// outside a test() the whole file reported HARNESS_ERROR rather than a failure.
//
// The METHODS are not here. They live on Event.prototype, one copy per page,
// where the specification puts them - an event used to carry seven freshly
// allocated natives of its own, which is seven allocations per mousemove.
void initialise_event(context & cx, script::object_object & event, std::string_view type,
                      bool bubbles, bool cancelable, double timestamp, value is_trusted_getter) {
    event.set("type", cx.string(std::string{type}));
    event.set("target", value::null());
    // `srcElement` is the same object under the name IE gave it, and plenty of
    // shipped code still reads it.
    event.set("srcElement", value::null());
    event.set("currentTarget", value::null());
    event.set("eventPhase", value::number(0));
    event.set("bubbles", value::boolean(bubbles));
    event.set("cancelable", value::boolean(cancelable));
    event.set("composed", value::boolean(false));
    event.set("defaultPrevented", value::boolean(false));
    event.set("timeStamp", value::number(timestamp));
    event.set(std::string{cancel_bubble_property}, value::boolean(false));
    event.set(std::string{stop_immediate_property}, value::boolean(false));
    event.set(std::string{dispatch_property}, value::boolean(false));
    // DECLARED HERE rather than created by the first passive listener that
    // runs, so the write in fire_at updates a slot instead of growing the
    // table in the middle of a dispatch.
    event.set(std::string{passive_property}, value::boolean(false));
    // NOT trusted, and NOT initialised: both are what the page-facing paths
    // start from, and each of the two callers that knows better says so.
    event.define(trusted_property, value::boolean(false), script::attr_none);
    event.define(initialised_property, value::boolean(false), script::attr_none);
    // `isTrusted` is [LegacyUnforgeable], which WebIDL defines as an OWN
    // property of every instance sharing one getter per realm rather than an
    // inherited one. Both halves are observable: getOwnPropertyDescriptor on an
    // instance must find it, and the getters of two instances must be the same
    // function.
    //
    // `is_callable()` AND NOT `is_object()`. `value::is_object()` is
    // heap_kind::object EXACTLY, and a getter is a heap_kind::native - so the
    // obvious test is false for every getter there has ever been, and this
    // silently took the fallback below and made `isTrusted` a DATA property on
    // every event in the engine. Nothing about that looks wrong from the page
    // until something asks for the descriptor. It is the same trap that
    // context::make_instance sets for a native constructor's prototype, one
    // function up, and it is worth knowing there are two of them.
    if (is_trusted_getter.is_callable()) {
        event.define_accessor("isTrusted", is_trusted_getter, value::undefined(),
                              script::attr_enumerable);
    } else {
        event.set("isTrusted", value::boolean(false));
    }
}

// ONE LISTENER, CALLED THE WAY THE CALLBACK ASKED TO BE CALLED.
//
// Two things were wrong and a page could see both.
//
// `this` WAS UNDEFINED. The specification binds it to the CURRENT TARGET, which
// is the single most useful thing a listener has: `el.addEventListener('click',
// function () { this.classList.add('on') })` is how a great deal of shipped
// code is written, and here `this` was undefined and the assignment silently
// went nowhere. dom/events/EventTarget-this-of-listener.html is six tests about
// exactly this.
//
// AN OBJECT WITH A `handleEvent` METHOD IS A LISTENER. EventListener is a
// callback INTERFACE, not a callback function: a page may register an object,
// and the method is looked up on it at DISPATCH time rather than at
// registration - EventListener-handleEvent.html registers an object whose
// `handleEvent` is a GETTER and counts how many times it runs. A function is
// never asked for one, even if it has one, which is the other half of the same
// rule and is what the last two tests in that file check.
void invoke_listener(context & cx, value callback, value receiver, value event) {
    if (callback.is_callable()) {
        (void)cx.call(callback, std::span<const value>{&event, 1}, receiver);
        return;
    }
    // `is_object_like()` for the third time in this file and for the third
    // reason: EventListener is "any object", and a page may register a Proxy
    // wrapping one - dom/events/EventListener-handleEvent-cross-realm.html
    // registers five. A callable one has already been handled above.
    if (!callback.is_object_like()) { return; }
    const value handler = cx.lookup_property(callback, "handleEvent");
    if (!handler.is_callable()) { return; }
    // THE OBJECT IS THE RECEIVER, not the target: `handleEvent` is a method of
    // the listener object and reads its own state.
    (void)cx.call(handler, std::span<const value>{&event, 1}, callback);
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
    context & cx = *cx_;
    auto * object = static_cast<script::object_object *>(event.as_heap());
    // THE TWO REFUSALS, BEFORE ANYTHING ELSE HAPPENS - concept-event-dispatch
    // is not reached at all when either flag is wrong, and a nested dispatch of
    // an event that is already travelling would otherwise reset the flags the
    // outer one is reading.
    if (flag_of(cx, event, dispatch_property)) {
        throw_dom_exception(cx, "InvalidStateError",
                            "Failed to execute 'dispatchEvent': the event is already being "
                            "dispatched.");
        return false;
    }
    // AN EVENT NOBODY INITIALISED. `document.createEvent` hands back one with no
    // type, and dispatching it is a mistake the specification names rather than
    // a dispatch to a listener called "". An object that is not one of ours has
    // no flag at all and is refused the same way, which is closer to the
    // TypeError a browser raises than the silent success this used to be.
    const value * initialised = object->find(std::string{initialised_property});
    if (initialised == nullptr || !context::truthy(*initialised)) {
        throw_dom_exception(cx, "InvalidStateError",
                            "Failed to execute 'dispatchEvent': the event has not been "
                            "initialised.");
        return false;
    }
    // BEFORE the listeners run. A handler for `input` reads the field's new
    // value, so a wrapper still holding the old one is the whole bug.
    (void)refresh_wrappers();
    const std::string type = cx.to_string(cx.lookup_property(event, "type"));
    const std::vector<path_step> path = propagation_path(at);

    const value target_object = object_of_step(cx, at);
    object->set("target", target_object);
    object->set("srcElement", target_object);
    object->set(std::string{stop_immediate_property}, value::boolean(false));
    object->set(std::string{dispatch_property}, value::boolean(true));
    ++dispatch_depth_;
    // `composedPath()` is the path AS A LIST, and it is empty outside a
    // dispatch. Built once here rather than recomputed by the method, because
    // the tree may have moved by the time a page asks.
    {
        value listed = cx.make_array();
        auto * items = static_cast<script::array_object *>(listed.as_heap());
        for (const path_step & step : path) { items->items.push_back(object_of_step(cx, step)); }
        object->set(std::string{path_property}, listed);
    }

    // `window.event`, WHICH IS SET FOR THE WHOLE DISPATCH AND RESTORED AFTER IT.
    //
    // It is the oldest event API there is and pages still reach for it - and not
    // only old ones: dom/events/Event-stopPropagation-cancel-bubbling.html
    // calls `event.stopPropagation()` with no receiver at all, which only works
    // because the bare name is a global. So it is written to BOTH the window
    // object and the globals table, which are separate storage here.
    //
    // SAVED AND RESTORED rather than cleared, because a listener may dispatch:
    // the inner dispatch must leave the outer one's event visible when it
    // returns, which is exactly what window-event-restored-after-throwing-
    // onerror.html checks by throwing from the middle of the nested one.
    //
    // THE TWO STORAGES ARE SAVED SEPARATELY, which is not tidiness: a page may
    // have a top-level `var event` of its own, and that is a GLOBAL while
    // `window.event` is an own property of the window object. Restoring one
    // from the other would silently overwrite the page's variable with
    // undefined the first time anything was dispatched.
    const context::rooted keep_event{cx, event};
    script::object_object * window = window_object();
    value outer_window_event = value::undefined();
    if (window != nullptr) {
        if (const value * held = window->find("event")) { outer_window_event = *held; }
        window->set("event", event);
    }
    const value outer_global_event =
        cx.has_global("event") ? cx.global("event") : value::undefined();
    const context::rooted keep_outer_window{cx, outer_window_event};
    const context::rooted keep_outer_global{cx, outer_global_event};
    cx.define_global("event", event);

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
    object->set(std::string{dispatch_property}, value::boolean(false));
    // AND THE PROPAGATION FLAGS ARE UNSET - concept-event-dispatch step 14, and
    // not a detail: the same event object is dispatched twice by plenty of code,
    // and an engine that left the flag set made the second dispatch reach
    // nobody. Clearing them HERE rather than on entry is what lets
    // `stopPropagation()` called BEFORE a dispatch stop that dispatch, which is
    // the other half of the same rule.
    object->set(std::string{cancel_bubble_property}, value::boolean(false));
    object->set(std::string{stop_immediate_property}, value::boolean(false));
    object->set(std::string{path_property}, cx.make_array());
    if (window != nullptr) { window->set("event", outer_window_event); }
    cx.define_global("event", outer_global_event);
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
    // THE PROTOTYPE FIRST, so a collection triggered by the writes below finds
    // an object that is already an Event - and because everything a page calls
    // on one is found through it.
    if (event_prototype_.is_object()) { event->prototype = event_prototype_; }
    initialise_event(cx, *event, type, bubbles, cancelable, now_ms_,
                     is_trusted_getter_of(event_prototype_));
    return self;
}

value dom_bindings::make_event(context & cx, std::string_view type, node_id target) {
    // AN ENGINE EVENT BUBBLES AND CAN BE CANCELLED. Every event the browser
    // generates here is one a page may refuse - a click, a key, a wheel notch -
    // and the flags decide whether the bubble pass runs at all and whether
    // preventDefault does anything, so getting them wrong is not cosmetic.
    value event = make_event_object(cx, type, true, true);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set(std::string{trusted_property}, value::boolean(true));
    // AND IT IS INITIALISED, which dispatch now insists on. `browser::tick`
    // sends `load` and `DOMContentLoaded` through here and a click goes the same
    // way, so an engine event that arrived without the flag would be refused by
    // the very check that exists to catch a page's uninitialised one.
    object->set(std::string{initialised_property}, value::boolean(true));
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
// THE MEMBERS ARE READ IN THE ORDER WebIDL CONVERTS THEM, which is alphabetical
// and is observable: EventListenerOptions-capture.html and
// AddEventListenerOptions-passive.html both pass a dictionary whose members are
// GETTERS and assert which of them ran. The same tests assert that a member
// nobody asked about - `dummy` - is never read, which is why this looks up four
// names rather than copying the object.
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
        made.capture = context::truthy(cx.lookup_property(options, "capture"));
        made.once = context::truthy(cx.lookup_property(options, "once"));
        // `passive` IS A PROMISE THE ENGINE ENFORCES, not one it takes on
        // trust: preventDefault does nothing while this listener runs. Reading
        // it is also how a page feature-DETECTS the option at all - it hands
        // addEventListener a dictionary whose members are getters and watches
        // which of them run, which is what every passive polyfill does.
        made.passive = context::truthy(cx.lookup_property(options, "passive"));
        const value signal = cx.lookup_property(options, "signal");
        if (!signal.is_undefined()) {
            // `AbortSignal signal` IS NOT NULLABLE IN THE IDL, so `{signal:
            // null}` is a TypeError rather than "no signal". A page that means
            // "no signal" leaves the member out.
            if (!signal.is_object()) {
                cx.throw_error("TypeError",
                               "Failed to execute 'addEventListener' on 'EventTarget': member "
                               "signal is not of type AbortSignal.");
                made.spent = true;
                return made;
            }
            made.abort_signal = signal;
            // AN ALREADY-ABORTED SIGNAL ADDS NOTHING. The signal's abort steps
            // are what remove a listener, and one registered after the abort
            // would never have them run - so it would live forever, which is
            // the opposite of what the page asked for.
            if (context::truthy(cx.lookup_property(signal, "aborted"))) { made.spent = true; }
        }
    } else if (args.size() > 2) {
        made.capture = context::truthy(options);
    }
    return made;
}

void dom_bindings::add_listener(listener made) {
    // A listener the options refused - a null signal, or one already aborted -
    // is not registered at all. `spent` is the flag the reap pass already reads,
    // so nothing else has to learn about the case.
    if (made.spent) { return; }
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
    // A LISTENER THAT THREW IS REPORTED TO THE PAGE, and not only to the
    // embedder.
    //
    // `note_callback_fault` at the end of a dispatch put the text in
    // `callback_error()`, which is an EMBEDDER channel: ctbrowse prints it and
    // ctdrive returns it. The page was told nothing at all - no `error` event,
    // no `window.onerror` - so a library with its own error reporting saw a
    // listener that simply stopped, and a test that counts error events counted
    // none. This is the other half of the fix wpt.md records for a throw during
    // LOAD; the same gap was still open one layer down.
    //
    // THE GUARD IS `type == "error"` AND IT IS THE WHOLE GUARD. Reporting is
    // itself a dispatch, so an `error` listener that throws is the recursion,
    // and refusing to report a fault raised while an `error` event is
    // travelling bounds it at one level with no state to keep. The dispatch
    // that swallows it still clears the VM's failure flag through
    // note_callback_fault, so nothing is left refusing to run.
    //
    // THE FAULT IS TAKEN BEFORE THE EVENT IS DISPATCHED, for the reason wpt.md
    // gives at length: every C++ entry into JavaScript declines while `failed_`
    // is set, so a report attempted with the flag still up runs no listener at
    // all and tells the page nothing twice.
    const auto report_fault = [this, type] {
        if (cx_ == nullptr || !cx_->failed() || type == "error") { return; }
        const std::string fault = std::string{type} + " listener: " + cx_->take_error();
        // The page gets it first. If nothing handled it - `preventDefault` on
        // an error event is how a page says it did - it goes to the embedder
        // as well, which is what a browser's console is for. The FIRST one is
        // kept there, exactly as note_callback_fault keeps it: a listener that
        // faults on every event has one bug, not a thousand.
        const bool handled = dispatch_error(fault);
        ++callback_faults_;
        if (!handled && callback_error_.empty()) { callback_error_ = fault; }
    };
    // Indexed rather than iterated: a listener may register another one, and
    // appending to the vector being walked invalidates an iterator. A listener
    // added during a dispatch does not run in that dispatch, which is the rule.
    const std::size_t count = listeners_.size();
    for (std::size_t i = 0; i < count && i < listeners_.size(); ++i) {
        // RE-READ THE FLAG EACH TIME. stopImmediatePropagation is defined by
        // stopping the listeners that would have run next at this very step, so
        // a check hoisted out of the loop implements the other method.
        if (flag_of(*cx_, event, stop_immediate_property)) { return; }
        listener & l = listeners_[i];
        if (l.on != step.on || l.type != type || l.capture != capturing || l.spent) { continue; }
        if (l.on == listen_on::node && l.target != step.node) { continue; }
        if (l.on == listen_on::object && l.host.bits() != step.host.bits()) { continue; }
        if (l.once) { l.spent = true; }
        // COPIED OUT BEFORE THE CALL. `l` is a reference into a vector a
        // listener can grow (addEventListener) or shrink (an AbortSignal), so
        // nothing may touch it once script is running.
        const value callback = l.callback;
        const bool passive = l.passive;
        // SET AND CLEARED rather than saved and restored: an event that is
        // already being dispatched is refused, so one can never be inside two
        // of these at once. It stays set across a NESTED dispatch on purpose -
        // the flag belongs to the event, so a listener of some other event that
        // reaches back for this one and cancels it is refused too, which is
        // what concept-event-listener-inner-invoke steps 10 and 15 say.
        auto * carrier = static_cast<script::object_object *>(event.as_heap());
        if (passive) { carrier->set(std::string{passive_property}, value::boolean(true)); }
        invoke_listener(*cx_, callback, object_of_step(*cx_, step), event);
        if (passive) { carrier->set(std::string{passive_property}, value::boolean(false)); }
        // PER LISTENER, not per dispatch. A throw from the first of three must
        // not stop the other two - which it did, because every later `call`
        // declines while the VM's failure flag is up - and each one that throws
        // is its own report.
        report_fault();
    }
    if (!capturing && !flag_of(*cx_, event, stop_immediate_property)) {
        fire_handler_property(object_of_step(*cx_, step), type, event);
        report_fault();
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
    // `is_object_like()` AND NOT `is_object()`, and this one was load-bearing:
    // `value::is_object()` is heap_kind::object EXACTLY, and THE WINDOW IS A
    // PROXY. So this returned at the door for every window step of every
    // dispatch there has ever been, and `window.onerror`, `window.onload`,
    // `window.onclick` - every handler PROPERTY on the window - has never once
    // fired. An element's worked, which is why nothing noticed: the wrapper is
    // an ordinary object and the only test covering handler properties used
    // one. Found by asserting on window.onerror rather than by a page
    // complaining, because a handler that is never called says nothing.
    if (cx_ == nullptr || !target.is_object_like()) { return; }
    const value handler = cx_->lookup_property(target, "on" + std::string{type});
    if (!handler.is_callable()) { return; }
    // `window.onerror` IS THE ONE HANDLER THAT IS NOT HANDED ITS EVENT.
    //
    // HTML's OnErrorEventHandler takes (message, filename, lineno, colno,
    // error) - five positional arguments, the first a STRING - and it is the
    // signature twenty years of shipped code is written against:
    // `window.onerror = function (msg, url, line) { report(msg) }`. Handing it
    // the event instead gives every one of them an object where a string was
    // expected, and `msg.indexOf(...)` on it throws inside the very handler
    // that exists to report a throw. dom/events/Event-dispatch-throwing.html
    // asserts `typeof e === 'string'` for exactly this reason.
    //
    // ONLY AT THE WINDOW and only for `error`: an `onerror` on an <img> is an
    // ordinary event handler and takes the event, which is the distinction the
    // specification draws with the ErrorEvent type rather than the name.
    if (type == "error" &&
        target.bits() == object_of_step(*cx_, path_step{node_id{}, listen_on::window}).bits()) {
        const value arguments[5] = {
            cx_->lookup_property(event, "message"), cx_->lookup_property(event, "filename"),
            cx_->lookup_property(event, "lineno"), cx_->lookup_property(event, "colno"),
            cx_->lookup_property(event, "error")};
        (void)cx_->call(handler, arguments, target);
        return;
    }
    (void)cx_->call(handler, std::span<const value>{&event, 1}, target);
}

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
        window->set("dispatchEvent",
                    value::object(cx.allocate<script::native_object>(
                        "dispatchEvent", [this](context & c, std::span<value> args) {
                            const value event = arg(args, 0);
                            if (!inherits_from(event, event_prototype_)) {
                                c.throw_error("TypeError",
                                              "Failed to execute 'dispatchEvent' on 'Window': "
                                              "parameter 1 is not of type 'Event'.");
                                return value::boolean(false);
                            }
                            return value::boolean(
                                !dispatch_to(event, path_step{node_id{}, listen_on::window}));
                        })));
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
