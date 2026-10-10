//===- events/listeners.cpp - EventTarget listeners -------------*- C++ -*-===//
//
// Listener registration and invocation for dom_bindings: activation behaviour
// (what a click on a link, button or checkbox does), engine-made events,
// addEventListener's options and default passive value, the listener "fence"
// that contains a throwing listener, and DOM "inner invoke" (fire_at).
//
//===----------------------------------------------------------------------===//
#include "internal.hpp"

#include "handler_names.hpp"
#include <ctbrowser/script/compile.hpp>

namespace ctbrowser::shell {

using namespace detail;

// Activation behaviour.
//
// Which elements have activation behaviour, per HTML: a link with an href, a
// button, an input, a label, and a <summary> whose parent is a <details>.
// element.click(), a dispatched MouseEvent("click") and a real mouse click all
// reach these two functions through dispatch_to, so the toggle happens before
// the listeners run and they see the new state.

/// True if `node` has activation behaviour (see above), plus <select>, whose
/// popup the engine opens from the same hook.
bool dom_bindings::has_activation_behavior(const read_txn & txn, node_id node) const {
    const std::string_view tag = atoms_->text(txn.tag(node).value_or(atom{}));
    if (tag == "a" || tag == "area") { return txn.has_attribute(node, atoms_->intern("href")); }
    if (tag == "summary") {
        const node_id parent = txn.parent(node);
        return parent && atoms_->text(txn.tag(parent).value_or(atom{})) == "details";
    }
    // An <input type=hidden> is no activation target: its state has no input
    // activation behaviour, and every engine lets a click on one reach the
    // <label> around it (clicking-noninteractive-unlabelable-content.html).
    if (tag == "input") {
        return !ascii_iequals(
            trim(txn.attribute_value(node, atoms_->intern("type")), html_whitespace), "hidden");
    }
    // A <select> has none in HTML. It is here because the engine opens its
    // popup from the same hook, and a click that reached no activation target
    // would never open one.
    return tag == "button" || tag == "label" || tag == "select";
}

/// Runs `target`'s activation behaviour: a `javascript:` link's script, a
/// checkbox or radio's `input` and `change` events, and then the browser's
/// own part through on_activate_.
void dom_bindings::run_activation_behavior(context & cx, node_id target) {
    control_kind kind = control_kind::none;
    std::string javascript_url;
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(target).value_or(atom{}));
        kind = control_kind_of(tag, txn.attribute_value(target, atoms_->intern("type")));
        if (tag == "a" || tag == "area") {
            const std::string_view href = txn.attribute_value(target, atoms_->intern("href"));
            if (ascii_istarts_with(href, "javascript:")) {
                javascript_url = std::string{href.substr(std::string_view{"javascript:"}.size())};
            }
        }
    }
    // A `javascript:` link runs its script, and that is the whole navigation:
    // HTML 7.4.2.1 evaluates the percent-decoded URL body as a classic script
    // in the document's realm, as a task queued from the navigate. So it is a
    // zero-delay timer here, which puts it after the click's own listeners and
    // microtasks. It is in the bindings rather than the browser's
    // follow_link because the bindings own the context; the browser would
    // have to re-enter `run` from inside the dispatch that is running now.
    // dom/events/Event-dispatch-click's "pick the first with activation
    // behavior <a href>" is two nested `javascript:` anchors and expects the
    // inner one's script, once.
    if (!javascript_url.empty()) {
        script::program compiled = script::compiler::compile(
            "return (function () {\n" + percent_decode(javascript_url) + "\n});");
        if (compiled.ok) {
            const value body = cx.run_nested(cx.own_program(std::move(compiled)));
            if (body.is_callable()) { (void)add_timer(body, 0, false); }
        }
        return;
    }
    if (kind == control_kind::checkbox || kind == control_kind::radio) {
        // HTML's input activation behaviour for the two: nothing unless the
        // element is connected, else `input` (bubbles, composed) and then
        // `change` (bubbles), neither cancelable. Being disabled does not
        // matter: the specification excepts exactly these two from its
        // mutability check, and Event-dispatch-click.html's "disabling checkbox
        // in onclick listener shouldn't suppress input" is that sentence as a
        // test. The toggle itself happened before the listeners ran.
        if (is_connected(target)) {
            const auto fire = [&](std::string_view name, bool composed) {
                value event = make_event(cx, name, target);
                auto * object = static_cast<script::object_object *>(event.as_heap());
                object->set("cancelable", value::boolean(false));
                object->set("composed", value::boolean(composed));
                (void)dispatch_event(name, target, event);
            };
            fire("input", true);
            fire("change", false);
        }
    }
    // The rest is the browser's: following the link, toggling the <details>,
    // clicking the control the <label> labels, submitting or resetting the
    // form, opening the <select>, and, for the two above, repainting the box.
    if (on_activate_) { on_activate_(target); }
}

/// Dispatches `event` at `target`, or at the document when `target` is null.
/// Returns whether the event was cancelled.
bool dom_bindings::dispatch_event(std::string_view type, node_id target, value event) {
    (void)type; // the event carries it; initEvent can have changed it since
    if (cx_ == nullptr) { return false; }
    const script::context::rooted keep{*cx_, event}; // see fire_at
    return dispatch_to(event, target ? path_step{target, listen_on::node}
                                     : path_step{node_id{}, listen_on::document});
}

/// Allocates a plain, untrusted, uninitialised Event with the given flags.
value dom_bindings::make_event_object(context & cx, std::string_view type, bool bubbles,
                                      bool cancelable) {
    auto * event = cx.allocate<script::object_object>();
    const value self = value::object(event);
    // Set the prototype first, so a collection triggered by the writes below
    // finds an object that is already an Event.
    if (event_prototype_.is_object()) { event->prototype = event_prototype_; }
    initialise_event(cx, *event, type, bubbles, cancelable, now_ms_,
                     is_trusted_getter_of(event_prototype_));
    return self;
}

/// Makes a trusted engine event of `type` aimed at `target`. It bubbles and is
/// cancelable; callers that need otherwise reset the flags.
value dom_bindings::make_event(context & cx, std::string_view type, node_id target) {
    // An engine event bubbles and can be cancelled: every event the browser
    // generates here (a click, a key, a wheel notch) is one a page may refuse.
    value event = make_event_object(cx, type, true, true);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set(std::string{trusted_property}, value::boolean(true));
    // It is also initialised, which dispatch requires. `browser::tick` sends
    // `load` and `DOMContentLoaded` through here, as does a click.
    object->set(std::string{initialised_property}, value::boolean(true));
    object->set("target", wrap(cx, target));
    object->set("srcElement", wrap(cx, target));
    return event;
}

/// True if `event` has had preventDefault() take effect.
bool dom_bindings::prevented(value event) {
    if (!event.is_object()) { return false; }
    const value * slot =
        static_cast<script::object_object *>(event.as_heap())->find("defaultPrevented");
    return slot != nullptr && context::truthy(*slot);
}

/// Builds a listener from addEventListener's arguments (type, callback,
/// options). The third argument is an options object or a bare capture flag;
/// `addEventListener(t, f, true)` is the old spelling and pages still use it.
///
/// The members are read in the order WebIDL converts them, which is
/// alphabetical and observable: EventListenerOptions-capture.html and
/// AddEventListenerOptions-passive.html pass a dictionary whose members are
/// getters and assert which ran. The same tests assert that an unknown member
/// (`dummy`) is never read, which is why this looks up four names rather than
/// copying the object. A listener that must not be added is returned `spent`.
dom_bindings::listener dom_bindings::make_listener(context & cx, path_step target,
                                                   std::span<value> args) {
    listener made;
    made.target = listener_target_of(target);
    made.payload.host = target.host;
    made.type = arg_string(cx, args, 0);
    made.payload.callback = arg(args, 1);
    const value options = arg(args, 2);
    if (options.is_object()) {
        made.capture = context::truthy(cx.lookup_property(options, "capture"));
        made.once = context::truthy(cx.lookup_property(options, "once"));
        // `passive` is enforced: preventDefault does nothing while this
        // listener runs. Reading it is also how a page feature-detects the
        // option: it passes a dictionary whose members are getters and watches
        // which run, as passive polyfills do.
        //
        // An absent member is not `false` but the default passive value. The
        // IDL gives `passive` no default so the DOM can compute one from the
        // type and the target, and `{passive: undefined}` is the same as
        // leaving it out, because WebIDL treats an undefined member as not
        // present (passive-by-default.html tests both).
        const value asked = cx.lookup_property(options, "passive");
        made.passive = asked.is_undefined() ? default_passive_value(made.type, target)
                                            : context::truthy(asked);
        const value signal = cx.lookup_property(options, "signal");
        if (!signal.is_undefined()) {
            // `AbortSignal signal` is not nullable in the IDL, so
            // `{signal: null}` is a TypeError rather than "no signal". A page
            // that means "no signal" leaves the member out.
            if (!signal.is_object()) {
                cx.throw_error("TypeError",
                               "Failed to execute 'addEventListener' on 'EventTarget': member "
                               "signal is not of type AbortSignal.");
                made.spent = true;
                return made;
            }
            made.payload.abort_signal = signal;
            // An already-aborted signal adds nothing. The signal's abort steps
            // are what remove a listener, and they would never run for one
            // registered after the abort, so it would live forever.
            if (context::truthy(cx.lookup_property(signal, "aborted"))) { made.spent = true; }
        }
    } else {
        // The bare capture flag, or no third argument at all. Either way the
        // dictionary's other members are at their defaults, and `passive`'s
        // default is computed, so the commonest spelling on the web,
        // `addEventListener(type, fn)`, is where the rule matters most.
        if (args.size() > 2) { made.capture = context::truthy(options); }
        made.passive = default_passive_value(made.type, target);
    }
    // A null or absent callback registers nothing. The DOM returns before the
    // listener is built rather than storing one that can never be invoked;
    // since a listener object without a callable `handleEvent` is a TypeError,
    // a stored `null` would report one on every dispatch.
    if (made.payload.callback.is_nullish()) { made.spent = true; }
    return made;
}

/// https://dom.spec.whatwg.org/#default-passive-value: a `touchstart`,
/// `touchmove`, `wheel` or `mousewheel` listener on the window, the document,
/// the document element or the body is passive unless the page said
/// `{passive: false}`. Anywhere else, and for any other type, it is not. Those
/// listeners are how a page blocks a scroll, and a browser cannot start
/// scrolling until it knows whether one will, so the platform made them
/// passive by default.
///
/// `find_by_tag` walks the tree, so the cheap type test comes first.
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

/// Registers `made`, keyed by its callback's identity.
void dom_bindings::add_listener(listener made) {
    made.callback_id = made.payload.callback.bits();
    (void)listeners_.add(std::move(made));
}

/// Drops spent listeners, but only outside a dispatch, where no snapshot of
/// the list is in use.
void dom_bindings::reap_spent_listeners() {
    if (dispatch_depth_ > 0) { return; }
    listeners_.reap_spent();
}

/// Installs the native half of the listener fence, which is compiled once per
/// page.
///
/// See the note on `listener_fence_` in the header: the VM unwinds a throw to
/// the innermost `try` on the whole stack, so the only thing that can stop one
/// at the dispatch is a `try` inside the callee. The fence is that `try`, and
/// it also performs WebIDL's "call a user object's operation", because every
/// step of that algorithm that can throw has to be inside it:
///
///   * the `handleEvent` get, which a page may make an accessor;
///     EventListener-handleEvent.html registers one whose getter throws and
///     then asserts on the identity of the object it threw;
///   * the TypeError for a listener object whose `handleEvent` is not
///     callable, which the same file tests with `null` and with `42`.
///
/// `typeof callback === "function"` alone decides which arm to take: a
/// function is never asked for a `handleEvent`, even when it has one. `this`
/// is the current target for a function and the listener object for a
/// `handleEvent`, which reads its own state.
///
/// `keeper` (the EventTarget constructor) keeps both halves alive.
void dom_bindings::install_listener_fence(context & cx, script::native_object & keeper) {
    // `invoke(fn, receiver, args)` is the trip back into C++, so the call itself
    // is still made by the engine. `args` is the event, or an array of
    // arguments for the one handler that is not handed one: `window.onerror`
    // takes (message, filename, lineno, colno, error). The common case passes
    // one value rather than an array, to avoid an allocation per mousemove.
    //
    // It returns the callee's return value if that was a boolean, and
    // undefined otherwise. A handler property's `return false` cancels the
    // event and nothing else about a return value is read; a boolean is not a
    // heap value, so nothing has to be rooted across the return, and it
    // cannot be mistaken for the `[thrown]` array.
    auto * runner = cx.allocate<script::native_object>(
        "invokeEventListener", [](context & c, std::span<value> a) {
            if (a.size() < 3 || !a[0].is_callable()) { return value::undefined(); }
            value out = value::undefined();
            if (a[2].is_array()) {
                const std::vector<value> spread =
                    static_cast<script::array_object *>(a[2].as_heap())->items;
                out = c.call(a[0], spread, a[1]);
            } else {
                const value one = a[2];
                out = c.call(a[0], std::span<const value>{&one, 1}, a[1]);
            }
            return out.is_boolean() ? out : value::undefined();
        });
    listener_invoke_ = value::object(runner);
    keeper.retained.push_back(listener_invoke_);
    fence_keeper_ = &keeper;
}

/// Compiles the JavaScript half of the listener fence, once. This happens at
/// the first dispatch rather than at install: `context::call` refuses every
/// closure until the page's first program has run (`program_` is null until
/// then), and by the first dispatch one has.
void dom_bindings::compile_listener_fence(context & cx) {
    if (listener_fence_.is_callable() || fence_keeper_ == nullptr) { return; }
    // A `TypeError`, not a string, because the page can see the difference:
    // EventListener-handleEvent.html checks the reported value with
    // `promise_rejects_js(t, TypeError, ...)`.
    script::program compiled = script::compiler::compile(
        "return (function (invoke, callback, receiver, args) {\n"
        "    try {\n"
        "        if (typeof callback === \"function\") {\n"
        "            return invoke(callback, receiver, args);\n"
        "        }\n"
        "        var method = callback.handleEvent;\n"
        "        if (typeof method !== \"function\") {\n"
        "            throw new TypeError(\"Failed to invoke an EventListener: the object's \"\n"
        "                + \"'handleEvent' property is not a function.\");\n"
        "        }\n"
        "        return invoke(method, callback, args);\n"
        "    } catch (thrown) {\n"
        "        return [thrown];\n"
        "    }\n"
        "});\n");
    // A compile failure is not fatal: `invoke_listener` falls back to calling
    // listeners directly, so every listener still runs and only the
    // containment is lost.
    if (compiled.ok) { listener_fence_ = cx.run_nested(cx.own_program(std::move(compiled))); }
    fence_keeper_->retained.push_back(listener_fence_);
}

/// Calls one listener (a function or a `handleEvent` object) through the fence.
/// Returns true if it threw, with the thrown value in `thrown`; otherwise
/// `returned` holds its boolean return value, or undefined.
bool dom_bindings::invoke_listener(context & cx, value callback, value receiver, value args,
                                   value & thrown, value & returned) {
    thrown = value::undefined();
    returned = value::undefined();
    // Rooted across the fence's compile: the first listener a context fires
    // compiles the fence through run_nested, which can trigger a collection,
    // and until `call` roots them the event, callback and receiver live only
    // in these C++ locals.
    const context::rooted keep_callback{cx, callback};
    const context::rooted keep_receiver{cx, receiver};
    const context::rooted keep_args{cx, args};
    // Use the page's fence for every document of the realm: a frame's document
    // and one a page made install no event interfaces of their own, so without
    // this a listener of theirs that threw would escape
    // `frames[1].document.dispatchEvent(...)` itself
    // (window-onerror-with-cross-frame-event-listeners-1.html).
    dom_bindings & fenced = fence_keeper_ != nullptr ? *this : primary();
    fenced.compile_listener_fence(cx);
    if (fenced.listener_fence_.is_callable() && fenced.listener_invoke_.is_callable()) {
        const value passed[4] = {fenced.listener_invoke_, callback, receiver, args};
        const value answer = cx.call(fenced.listener_fence_, passed);
        if (!answer.is_array()) {
            returned = answer;
            return false;
        }
        // The thrown value comes back in a one-element array rather than as
        // itself, because `undefined` and `null` are both values a page can
        // throw and neither can be told from "nothing was thrown" on its own.
        const auto & items = static_cast<script::array_object *>(answer.as_heap())->items;
        thrown = items.empty() ? value::undefined() : items.front();
        return true;
    }
    // Fallback when the fence failed to compile: call the listener directly. A
    // throw from here is not contained and reaches whatever `try` the page was in.
    if (callback.is_callable()) {
        if (args.is_array()) {
            const std::vector<value> spread =
                static_cast<script::array_object *>(args.as_heap())->items;
            returned = cx.call(callback, spread, receiver);
        } else {
            returned = cx.call(callback, std::span<const value>{&args, 1}, receiver);
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

/// DOM "invoke" step 6's table: the prefixed spelling a trusted event of the
/// unprefixed type reaches when nothing listened for the unprefixed one.
[[nodiscard]] std::string_view legacy_event_type_of(std::string_view type) {
    if (type == "animationend") { return "webkitAnimationEnd"; }
    if (type == "animationiteration") { return "webkitAnimationIteration"; }
    if (type == "animationstart") { return "webkitAnimationStart"; }
    if (type == "transitionend") { return "webkitTransitionEnd"; }
    // Also one the table does not list but every engine with the prefixed
    // four has: a trusted `wheel` reaches `mousewheel`, the
    // pre-standard name, under the same rule. The DOM's own default passive
    // value names `mousewheel` beside `wheel`, and non-cancelable-when-passive/
    // has ten files that listen for nothing else.
    if (type == "wheel") { return "mousewheel"; }
    return {};
}

/// Runs the listeners at one step of an event's path, for one phase (DOM
/// "inner invoke"), and reports any that throw.
void dom_bindings::fire_at(path_step step, std::string_view type, value event, bool capturing) {
    // Root the event for the call. An event the engine made (a stylesheet's
    // load from browser::tick, an image's) lives only in a C++ local while its
    // listeners run, and a listener that allocates enough can trigger a
    // collection that would free it. A page-made event is already rooted, so
    // this costs it nothing.
    if (cx_ == nullptr) { return; }
    const script::context::rooted keep{*cx_, event};
    // A listener that threw is reported to the page (an `error` event or
    // window.onerror), not only to the embedder through callback_error().
    //
    // Reporting a fault is itself a dispatch of an `error` event, so an `error`
    // listener that throws could recurse. The guard below (see the paragraph
    // on "in error reporting mode") bounds that at one level.
    //
    // The fault is taken (clearing `failed_`) before the error event is
    // dispatched: every C++ entry into JavaScript declines while `failed_` is
    // set, so a report made with the flag still up would run no listener. See
    // ctbrowser/docs/wpt.md.
    //
    // There are two ways to fail, and only one is a throw. The fence hands
    // back the value a listener threw, and the VM's failure flag is never
    // raised for it; a VM fault (the allocation ceiling, the call stack
    // ceiling) is a failure that was never an exception, unwinds nothing, and
    // is visible only as `failed()`. Both are reported and both clear what
    // they read, because a flag left up refuses every later callback.
    //
    // The fault is reported at the listener's own global (DOM 2.10 "inner
    // invoke" step 10.2: "report exception for listener's callback's
    // corresponding JavaScript object's associated realm's global object"):
    // a function made in a frame reports
    // to the frame's window, whatever target it listened on and whichever
    // window's addEventListener added it. The realm is the closure's; a
    // native or a `handleEvent` object has none here, and reports where the
    // dispatch is.
    //
    // The recursion guard is that global's "in error reporting mode" (HTML
    // 8.1.3.3), set by dispatch_error_value for the length of the `error`
    // event it fires: an `error` listener that throws while its own window is
    // reporting is dropped, which bounds the recursion at one level, and one
    // that throws into another window's report is reported there
    // (window-onerror-with-cross-frame-event-listeners-5.html). Only an
    // `error` listener is dropped: a promise job the dispatch's microtask
    // checkpoint runs is inside the flag too, and what it throws is a report
    // of its own.
    const auto report_fault = [this, type](bool threw, value thrown, value callback) {
        if (cx_ == nullptr) { return; }
        const bool faulted = cx_->failed();
        if (!threw && !faulted) { return; }
        dom_bindings * global = &global_of(callback, *this);
        if (type == "error" && global->error_reporting_) {
            // Reporting is itself a dispatch, so this is the recursion. The
            // flag still has to go.
            if (faulted) { (void)cx_->take_error(); }
            return;
        }
        const context::rooted keep_thrown{*cx_, thrown};
        const std::string fault =
            std::string{type} + " listener: " +
            (faulted ? cx_->take_error() : "uncaught " + describe_thrown(*cx_, thrown));
        // The page gets it first. If nothing handled it - `preventDefault` on
        // an error event is how a page says it did - it goes to the embedder
        // as well, which is what a browser's console is for. The first one is
        // kept there, exactly as note_callback_fault keeps it: a listener that
        // faults on every event has one bug, not a thousand.
        const bool handled = global->dispatch_error_value(fault, thrown);
        if (!handled && callback_error_.empty()) { callback_error_ = fault; }
    };
    // `window.event`, DOM 2.10 "inner invoke" steps 8-11: the current event of
    // the listener callback's global, meaning the window of the realm the
    // function was made in, not the target's or the dispatching document's: a
    // frame's function listening in the page sets the frame's `event`, and the
    // page's own is untouched (event-global-extra.window.js, event-global-is-
    // still-set-when-reporting-exception-onerror.html). It is set for the call
    // (left as it was when the step is inside a shadow tree) and put back
    // after it, after the fault is reported, which step 10 does with the event
    // still current. It is put back rather than cleared, because a listener
    // may itself dispatch (window-event-restored-after-throwing-onerror.html).
    //
    // Both storages are saved separately: the window object's own `event` and
    // the realm's global (`event.stopPropagation()` with no receiver reads the
    // bare name, Event-stopPropagation-cancel-bubbling.html), because a
    // page's top-level `var event` is a global while `window.event` is a
    // property.
    const auto invoking = [&](value callback, const auto & call) {
        context & cx = *cx_;
        dom_bindings & global = global_of(callback, *this);
        script::realm_record & realm = global.realm_ != nullptr ? *global.realm_ : cx.main_realm();
        script::object_object * window = global.window_object();
        value saved_window = value::undefined();
        value saved_global = value::undefined();
        {
            const context::realm_scope in_realm{cx, realm};
            if (window != nullptr) {
                if (const value * held = window->find("event")) { saved_window = *held; }
            }
            if (cx.has_global("event")) { saved_global = cx.global("event"); }
            if (!step_in_shadow_) {
                if (window != nullptr) { window->set("event", event); }
                cx.define_global("event", event);
            }
        }
        const context::rooted keep_window{cx, saved_window};
        const context::rooted keep_global{cx, saved_global};
        call();
        const context::realm_scope in_realm{cx, realm};
        if (window != nullptr) { window->set("event", saved_window); }
        cx.define_global("event", saved_global);
    };
    // Inner invoke (DOM 2.10), run once for the event's type and, when nothing
    // listened for it and the event is trusted, once more under the legacy
    // name from "invoke" step 6 (`animationend` -> `webkitAnimationEnd`;
    // EventListener-invoke-legacy, the four webkit-*-event files). Returns
    // whether anything ran.
    //
    // The listener list is snapshotted by identity before any listener runs,
    // as the DOM's "clone of the event listener list" requires: a listener
    // added during dispatch does not run, and one removed during it is skipped
    // because the re-find below misses it. (Walking the live vector by index
    // would skip the listener after a removed one, since removal shifts the
    // entries.) Identities rather than values, because a `value` in a plain
    // vector is invisible to the collector; the re-find reads the callback
    // back out of `listeners_`, which is traced.
    const auto inner_invoke = [&](std::string_view type) -> bool {
        bool found = false;
        const listener_target at = listener_target_of(step);
        const auto queued = listeners_.snapshot(at, type, capturing);
        for (const std::uint64_t identity : queued) {
            // Re-read the flag each time: stopImmediatePropagation stops the
            // listeners that would have run next at this very step.
            if (flag_of(*cx_, event, stop_immediate_property)) { return found; }
            const auto * entry = listeners_.begin(at, type, identity, capturing);
            // Gone since the copy was taken: removed by a listener that ran
            // earlier at this step, or by its AbortSignal.
            if (entry == nullptr) { continue; }
            found = true;
            // No row reference survives a property lookup or callback: either
            // can register or remove listeners and invalidate the vector.
            const bool passive = entry->passive;
            if (entry->handler) {
                // The event handler's listener: the handler property as it is now,
                // at the place the handler was first set (HTML 8.1.8.1).
                value thrown = value::undefined();
                const value target = object_of_step(*cx_, step);
                const value handler = cx_->lookup_property(target, "on" + ascii_lower_copy(type));
                const context::rooted keep_handler{*cx_, handler};
                // In the passive listener flag like any listener: a passive
                // `onwheel` returning false does not cancel.
                auto * carrier = static_cast<script::object_object *>(event.as_heap());
                if (passive) { carrier->set(std::string{passive_property}, value::boolean(true)); }
                invoking(handler, [&] {
                    const bool threw = fire_handler_property(target, type, event, &thrown);
                    if (passive) {
                        carrier->set(std::string{passive_property}, value::boolean(false));
                    }
                    report_fault(threw, thrown, handler);
                });
                continue;
            }
            // Copied out before the call: `entry` points into a vector a
            // listener can grow (addEventListener) or shrink (an AbortSignal),
            // so nothing may touch it once script is running.
            const value callback = entry->payload.callback;
            // Set and cleared rather than saved and restored: an event that is
            // already being dispatched is refused, so one can never be inside two
            // of these at once. It stays set across a nested dispatch on purpose:
            // the flag belongs to the event, so a listener of some other event that
            // reaches back for this one and cancels it is refused too, which is
            // what concept-event-listener-inner-invoke steps 10 and 15 say.
            auto * carrier = static_cast<script::object_object *>(event.as_heap());
            if (passive) { carrier->set(std::string{passive_property}, value::boolean(true)); }
            value thrown = value::undefined();
            value returned = value::undefined();
            invoking(callback, [&] {
                const bool threw = invoke_listener(*cx_, callback, object_of_step(*cx_, step),
                                                   event, thrown, returned);
                if (passive) { carrier->set(std::string{passive_property}, value::boolean(false)); }
                // Report per listener, not per dispatch: a throw from one listener
                // must not stop the rest, and each throw gets its own report.
                report_fault(threw, thrown, callback);
            });
        }
        // An `on<type>` handler with no registered listener entry (a parsed
        // attribute the write log never saw, or a name whose event type is not
        // its lowercase spelling) still runs, after the listeners. A
        // <body>/<frameset> handler that forwards to the window is skipped: it
        // runs at the window step, with the five ErrorEvent arguments
        // (body-element-synthetic-errorevent.html).
        const bool forwarded = step.on == listen_on::node &&
                               forwards_to_window("on" + ascii_lower_copy(type)) &&
                               body_or_frameset_of(object_of_step(*cx_, step));
        if (!capturing && !flag_of(*cx_, event, stop_immediate_property) &&
            !has_handler_listener(step, type) && !forwarded) {
            value thrown = value::undefined();
            const value target = object_of_step(*cx_, step);
            const value handler = cx_->lookup_property(target, "on" + ascii_lower_copy(type));
            const context::rooted keep_handler{*cx_, handler};
            bool threw = false;
            invoking(handler, [&] {
                threw = fire_handler_property(target, type, event, &thrown);
                report_fault(threw, thrown, handler);
            });
            found = found || threw ||
                    cx_->lookup_property(object_of_step(*cx_, step), "on" + ascii_lower_copy(type))
                        .is_callable();
        }
        return found;
    };
    if (inner_invoke(type)) { return; }
    const std::string_view legacy = legacy_event_type_of(type);
    if (legacy.empty() || !flag_of(*cx_, event, trusted_property)) { return; }
    auto * carrier = static_cast<script::object_object *>(event.as_heap());
    carrier->set("type", cx_->string(std::string{legacy}));
    (void)inner_invoke(legacy);
    carrier->set("type", cx_->string(std::string{type}));
}

} // namespace ctbrowser::shell
