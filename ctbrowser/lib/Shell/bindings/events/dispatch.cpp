// dom_bindings - the dispatch algorithm: capture down the path and bubble back
// up, the listener list and its options, the event object itself, and the
// `on<type>` handler properties.
//
// One of three files carved out of a 1,647-line bindings/events.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

#include "internal.hpp"

#include <ctbrowser/script/compile.hpp>

namespace ctbrowser::shell {

using namespace detail;

namespace {

// WHAT A THROWN VALUE IS CALLED, for the text side of a report.
//
// `window.onerror` takes a STRING first - twenty years of shipped code reads it
// as one, and Event-dispatch-throwing.html asserts `typeof e === "string"` - so
// something has to flatten the value. An Error's `name` and `message` are data
// properties on every Error this engine makes, which is why they are read
// instead of calling `toString`: a page's own thrown object may have a
// `toString` that throws, and a reporter that faults is the one thing worse
// than a fault nobody reports. (`context::describe_thrown` is the same shape
// and is private to the VM.)
[[nodiscard]] std::string describe_thrown(context & cx, value thrown) {
    if (!thrown.is_object()) { return cx.to_string(thrown); }
    const value name = cx.lookup_property(thrown, "name");
    const value message = cx.lookup_property(thrown, "message");
    if (name.is_undefined() && message.is_undefined()) { return "an exception"; }
    std::string text = name.is_undefined() ? std::string{"Error"} : cx.to_string(name);
    const std::string body = message.is_undefined() ? std::string{} : cx.to_string(message);
    if (!body.empty()) { text += ": " + body; }
    return text;
}

} // namespace

namespace detail {

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

} // namespace detail

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
        //
        // AN ABSENT MEMBER IS NOT `false`, it is the DEFAULT PASSIVE VALUE. The
        // IDL gives `passive` no default precisely so the DOM can compute one
        // from the type and the target, and `{passive: undefined}` is the same
        // as leaving it out - WebIDL treats a member whose value is undefined
        // as not present, which passive-by-default.html tests separately from
        // omitting it for all 40 of its combinations.
        const value asked = cx.lookup_property(options, "passive");
        made.passive = asked.is_undefined() ? default_passive_value(made.type, target)
                                            : context::truthy(asked);
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
    } else {
        // The bare capture flag, or no third argument at all. Either way the
        // dictionary's other members are at their defaults - and `passive`'s
        // default is computed, so the commonest spelling on the web,
        // `addEventListener(type, fn)`, is where the rule matters most.
        if (args.size() > 2) { made.capture = context::truthy(options); }
        made.passive = default_passive_value(made.type, target);
    }
    // A NULL OR ABSENT CALLBACK REGISTERS NOTHING. The DOM returns before the
    // listener is built rather than storing one that can never be invoked, and
    // it matters now that a listener object without a callable `handleEvent` is
    // a TypeError: a stored `null` would report one on every dispatch.
    if (made.callback.is_nullish()) { made.spent = true; }
    return made;
}

// https://dom.spec.whatwg.org/#default-passive-value - the four SCROLL-BLOCKING
// types, on the four targets a page scrolls through.
//
// The rule reads as one sentence and is worth stating as one: a `touchstart`,
// `touchmove`, `wheel` or `mousewheel` listener on the WINDOW, the DOCUMENT, the
// DOCUMENT ELEMENT or the BODY is passive unless the page said `{passive:
// false}`. Anywhere else, and for any other type, only what was asked for
// counts. It exists because those four listeners on those four targets are how
// a page blocks a scroll, and a browser cannot start scrolling until it knows
// whether one of them will - so the platform changed the default rather than
// wait.
//
// `find_by_tag` walks the tree, so the type test comes FIRST: four string
// comparisons decide it for every listener a page registers that is not one of
// these, which is nearly all of them.
bool dom_bindings::default_passive_value(std::string_view type, const path_step & target) {
    if (type != "touchstart" && type != "touchmove" && type != "wheel" && type != "mousewheel") {
        return false;
    }
    switch (target.on) {
    case listen_on::window:
    case listen_on::document: return true;
    case listen_on::node:
        // The document element and the body, by tag rather than by position:
        // `documentElement` here is `find_by_tag("html")` for the same reason
        // the document's own property is, so the two answers agree.
        return target.node &&
               (target.node == find_by_tag("html") || target.node == find_by_tag("body"));
    // A standalone EventTarget is not in any document, so no scroll depends on
    // it: generic-events-stay-cancelable.html is that case exactly, and a
    // passive default there would make an event it dispatches uncancellable.
    case listen_on::object: return false;
    }
    return false;
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

// THE FENCE, COMPILED ONCE PER PAGE.
//
// See the note on `listener_fence_` in the header: the VM unwinds a throw to
// the innermost `try` on the whole stack, so the only thing that can stop one
// at the dispatch is a `try` INSIDE the callee. This is that `try`, and it
// carries WebIDL's "call a user object's operation" with it, because every
// step of that algorithm that can throw has to be on this side of the fence:
//
//   * the `handleEvent` GET, which a page may make an accessor -
//     EventListener-handleEvent.html registers one whose getter throws and then
//     asserts on the identity of the object it threw;
//   * the TypeError for a listener object whose `handleEvent` is not callable,
//     which the same file tests with `null` and with `42`.
//
// Both used to be C++ - a `cx.failed()` check after the lookup and a
// `throw_error` after it - and both were wrong for the same reason: the
// lookup's throw had already left for the page's `try`, so `failed()` was
// clear, and the TypeError this file then raised replaced the page's exception
// with one of ours.
//
// `typeof callback === "function"` IS THE WHOLE TEST for which arm to take. A
// function is never asked for a `handleEvent`, even when it has one - that is
// the other half of the same clause and is what the last two tests of that file
// check. `this` is the CURRENT TARGET for a function and the listener object
// for a `handleEvent`, which reads its own state.
void dom_bindings::install_listener_fence(context & cx, script::native_object & keeper) {
    // `invoke(fn, receiver, args)` - the trip back into C++, so that the CALL
    // itself is still ours. `args` is the event, or an ARRAY of arguments for
    // the one handler that is not handed one: `window.onerror` takes (message,
    // filename, lineno, colno, error). One value in the common case rather than
    // an array, because an array per mousemove is an allocation per mousemove.
    auto * runner = cx.allocate<script::native_object>(
        "invokeEventListener", [](context & c, std::span<value> a) {
            if (a.size() < 3 || !a[0].is_callable()) { return value::undefined(); }
            if (a[2].is_array()) {
                const std::vector<value> spread =
                    static_cast<script::array_object *>(a[2].as_heap())->items;
                (void)c.call(a[0], spread, a[1]);
                return value::undefined();
            }
            const value one = a[2];
            (void)c.call(a[0], std::span<const value>{&one, 1}, a[1]);
            return value::undefined();
        });
    listener_invoke_ = value::object(runner);
    // ROOTED ACROSS THE COMPILE. `run_nested` below runs JavaScript, which takes
    // a safepoint, and a native held only in a C++ member is not something the
    // collector can see - see native_object::retained.
    const context::rooted keep_runner{cx, listener_invoke_};
    // A `TypeError` AND NOT A STRING, because the page can see the difference:
    // EventListener-handleEvent.html checks the reported value with
    // `promise_rejects_js(t, TypeError, ...)`.
    script::program compiled = script::compiler::compile(
        "return (function (invoke, callback, receiver, args) {\n"
        "    try {\n"
        "        if (typeof callback === \"function\") {\n"
        "            invoke(callback, receiver, args);\n"
        "        } else {\n"
        "            var method = callback.handleEvent;\n"
        "            if (typeof method !== \"function\") {\n"
        "                throw new TypeError(\"Failed to invoke an EventListener: the object's \"\n"
        "                    + \"'handleEvent' property is not a function.\");\n"
        "            }\n"
        "            invoke(method, callback, args);\n"
        "        }\n"
        "    } catch (thrown) {\n"
        "        return [thrown];\n"
        "    }\n"
        "    return null;\n"
        "});\n");
    // A COMPILE FAILURE IS NOT FATAL. `invoke_listener` falls back to the
    // unfenced C++ path, which is what this file did before the fence existed:
    // every listener still runs and only the containment is lost.
    if (compiled.ok) { listener_fence_ = cx.run_nested(cx.own_program(std::move(compiled))); }
    keeper.retained.push_back(listener_invoke_);
    keeper.retained.push_back(listener_fence_);
}

bool dom_bindings::invoke_listener(context & cx, value callback, value receiver, value args,
                                   value & thrown) {
    thrown = value::undefined();
    if (listener_fence_.is_callable() && listener_invoke_.is_callable()) {
        const value passed[4] = {listener_invoke_, callback, receiver, args};
        const value answer = cx.call(listener_fence_, passed);
        if (!answer.is_array()) { return false; }
        // THE THROWN VALUE COMES BACK IN A ONE-ELEMENT ARRAY rather than as
        // itself, because `undefined` and `null` are both values a page can
        // throw and neither can be told from "nothing was thrown" on its own.
        const auto & items = static_cast<script::array_object *>(answer.as_heap())->items;
        thrown = items.empty() ? value::undefined() : items.front();
        return true;
    }
    // WITHOUT THE FENCE: the shape this had before it existed, minus the
    // containment. A throw from here reaches whatever `try` the page was inside.
    if (callback.is_callable()) {
        if (args.is_array()) {
            const std::vector<value> spread =
                static_cast<script::array_object *>(args.as_heap())->items;
            (void)cx.call(callback, spread, receiver);
        } else {
            (void)cx.call(callback, std::span<const value>{&args, 1}, receiver);
        }
        return false;
    }
    if (!callback.is_object_like()) { return false; }
    const value handler = cx.lookup_property(callback, "handleEvent");
    if (cx.failed()) { return false; }
    if (!handler.is_callable()) {
        cx.throw_error("TypeError", "Failed to invoke an EventListener: the object's "
                                    "'handleEvent' property is not a function.");
        return false;
    }
    (void)cx.call(handler, std::span<const value>{&args, 1}, callback);
    return false;
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
    //
    // TWO WAYS TO FAIL AND ONLY ONE OF THEM IS A THROW. The fence hands back the
    // value a listener threw and the VM's failure flag is never raised for it;
    // a VM FAULT - the allocation ceiling, the call stack ceiling - is a failure
    // that was never an exception, unwinds nothing, and is still only visible
    // as `failed()`. Both are reported and both clear what they read, because a
    // flag left up refuses every later callback of any kind.
    const auto report_fault = [this, type](bool threw, value thrown) {
        if (cx_ == nullptr) { return; }
        const bool faulted = cx_->failed();
        if (!threw && !faulted) { return; }
        if (type == "error") {
            // Reporting is itself a dispatch, so this is the recursion; bounded
            // at one level with no state to keep. The flag still has to go.
            if (faulted) { (void)cx_->take_error(); }
            return;
        }
        const context::rooted keep_thrown{*cx_, thrown};
        const std::string fault =
            std::string{type} + " listener: " +
            (faulted ? cx_->take_error() : "uncaught " + describe_thrown(*cx_, thrown));
        // The page gets it first. If nothing handled it - `preventDefault` on
        // an error event is how a page says it did - it goes to the embedder
        // as well, which is what a browser's console is for. The FIRST one is
        // kept there, exactly as note_callback_fault keeps it: a listener that
        // faults on every event has one bug, not a thousand.
        const bool handled = dispatch_error_value(fault, thrown);
        ++callback_faults_;
        if (!handled && callback_error_.empty()) { callback_error_ = fault; }
    };
    // THE LIST IS COPIED BEFORE ANY OF IT RUNS, which is what the DOM says and
    // is not the same thing as walking it carefully.
    //
    // concept-event-listener-inner-invoke opens with "let listeners be a clone
    // of the object's event listener list", and then two rules fall out of the
    // clone rather than being written anywhere: a listener REGISTERED during
    // this dispatch is not in the copy and does not run, and a listener REMOVED
    // during it is in the copy and is skipped by its `removed` flag.
    //
    // Walking the live vector by index cannot do the second half here, because
    // `removeEventListener` is `std::erase_if` in five places across four files
    // - it MOVES every entry after the one it takes, so the index the loop is
    // holding now names the NEXT listener and that one is silently skipped. A
    // handler that removes itself is not exotic; it is the shape of every
    // "run this once" listener written before `{once: true}` existed, and
    // Event-dispatch-handlers-changed.html removes one at each of eight targets.
    //
    // What is copied is IDENTITY, not the listener: a `value` in a plain vector
    // is invisible to the collector, and a callback dropped by the removal that
    // happened mid-dispatch could be swept while this loop still held it. The
    // re-find below reads the callback back out of `listeners_`, which IS
    // traced, and finding nothing is exactly the `removed` check.
    std::vector<std::uint64_t> queued;
    const auto belongs = [&](const listener & l) {
        if (l.on != step.on || l.type != type || l.capture != capturing) { return false; }
        if (l.on == listen_on::node && l.target != step.node) { return false; }
        if (l.on == listen_on::object && l.host.bits() != step.host.bits()) { return false; }
        return true;
    };
    for (const listener & l : listeners_) {
        if (!l.spent && belongs(l)) { queued.push_back(l.callback.bits()); }
    }
    for (const std::uint64_t identity : queued) {
        // RE-READ THE FLAG EACH TIME. stopImmediatePropagation is defined by
        // stopping the listeners that would have run next at this very step, so
        // a check hoisted out of the loop implements the other method.
        if (flag_of(*cx_, event, stop_immediate_property)) { return; }
        const auto found = std::ranges::find_if(listeners_, [&](const listener & l) {
            return !l.spent && l.callback.bits() == identity && belongs(l);
        });
        // Gone since the copy was taken - removed by a listener that ran
        // earlier at this step, or by its AbortSignal.
        if (found == listeners_.end()) { continue; }
        if (found->once) { found->spent = true; }
        // COPIED OUT BEFORE THE CALL. `found` is an iterator into a vector a
        // listener can grow (addEventListener) or shrink (an AbortSignal), so
        // nothing may touch it once script is running.
        const value callback = found->callback;
        const bool passive = found->passive;
        // SET AND CLEARED rather than saved and restored: an event that is
        // already being dispatched is refused, so one can never be inside two
        // of these at once. It stays set across a NESTED dispatch on purpose -
        // the flag belongs to the event, so a listener of some other event that
        // reaches back for this one and cancels it is refused too, which is
        // what concept-event-listener-inner-invoke steps 10 and 15 say.
        auto * carrier = static_cast<script::object_object *>(event.as_heap());
        if (passive) { carrier->set(std::string{passive_property}, value::boolean(true)); }
        value thrown = value::undefined();
        const bool threw =
            invoke_listener(*cx_, callback, object_of_step(*cx_, step), event, thrown);
        if (passive) { carrier->set(std::string{passive_property}, value::boolean(false)); }
        // PER LISTENER, not per dispatch. A throw from the first of three must
        // not stop the other two - which it did twice over, first because every
        // later `call` declines while the VM's failure flag is up and then
        // because the throw left the dispatch entirely - and each one that
        // throws is its own report.
        report_fault(threw, thrown);
    }
    if (!capturing && !flag_of(*cx_, event, stop_immediate_property)) {
        value thrown = value::undefined();
        const bool threw = fire_handler_property(object_of_step(*cx_, step), type, event, &thrown);
        report_fault(threw, thrown);
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
bool dom_bindings::fire_handler_property(value target, std::string_view type, value event,
                                         value * thrown) {
    value caught = value::undefined();
    const auto answer = [&](bool threw) {
        if (thrown != nullptr) { *thrown = caught; }
        return threw;
    };
    // `is_object_like()` AND NOT `is_object()`, and this one was load-bearing:
    // `value::is_object()` is heap_kind::object EXACTLY, and THE WINDOW IS A
    // PROXY. So this returned at the door for every window step of every
    // dispatch there has ever been, and `window.onerror`, `window.onload`,
    // `window.onclick` - every handler PROPERTY on the window - has never once
    // fired. An element's worked, which is why nothing noticed: the wrapper is
    // an ordinary object and the only test covering handler properties used
    // one. Found by asserting on window.onerror rather than by a page
    // complaining, because a handler that is never called says nothing.
    if (cx_ == nullptr || !target.is_object_like()) { return answer(false); }
    const value handler = cx_->lookup_property(target, "on" + std::string{type});
    if (!handler.is_callable()) { return answer(false); }
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
        // AN ARRAY, because that is what the fence spreads. The five arguments
        // are built into one so that the handler still runs behind the same
        // `try` every listener does - `window.onerror` is exactly the handler a
        // page is most likely to throw out of, and
        // window-event-restored-after-throwing-onerror.html throws out of it on
        // purpose.
        const value arguments = cx_->make_array();
        auto * items = static_cast<script::array_object *>(arguments.as_heap());
        items->items.push_back(cx_->lookup_property(event, "message"));
        items->items.push_back(cx_->lookup_property(event, "filename"));
        items->items.push_back(cx_->lookup_property(event, "lineno"));
        items->items.push_back(cx_->lookup_property(event, "colno"));
        items->items.push_back(cx_->lookup_property(event, "error"));
        return answer(invoke_listener(*cx_, handler, target, arguments, caught));
    }
    return answer(invoke_listener(*cx_, handler, target, event, caught));
}

} // namespace ctbrowser::shell
