#include "internal.hpp"

#include "handler_names.hpp"
#include <ctbrowser/script/compile.hpp>

namespace ctbrowser::shell {

using namespace detail;

// --- activation behaviour --------------------------------------------------
//
// WHICH ELEMENTS HAVE IT, per HTML: a link with an href, a button, an input, a
// label, and a <summary> whose parent is a <details>. `element.click()`,
// `dispatchEvent(new MouseEvent("click"))` and the engine's own mouse click all
// reach the two functions here through dispatch_to, which is the point: the
// browser's activate hook used to be called by two of the three from OUTSIDE
// the dispatch, with the toggle after the listeners, so a click listener on a
// checkbox read the old checkedness and a constructed click toggled nothing.
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
    // A `javascript:` LINK RUNS ITS SCRIPT, and that is the whole navigation:
    // HTML 7.4.2.1 evaluates the percent-decoded URL body as a classic script
    // in the document's realm, as a TASK queued from the navigate - so it is a
    // zero-delay timer here, which is what puts it after the click's own
    // listeners and microtasks. In the bindings rather than the browser's
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
        // element is connected, else `input` - bubbles, composed - and then
        // `change` - bubbles - neither cancelable. Being disabled does not
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
    // form, opening the <select> - and, for the two above, repainting the box.
    if (on_activate_) { on_activate_(target); }
}

bool dom_bindings::dispatch_event(std::string_view type, node_id target, value event) {
    (void)type; // the event carries it; initEvent can have changed it since
    if (cx_ == nullptr) { return false; }
    const script::context::rooted keep{*cx_, event}; // see fire_at
    return dispatch_to(event, target ? path_step{target, listen_on::node}
                                     : path_step{node_id{}, listen_on::document});
}

value dom_bindings::make_event_object(context & cx, std::string_view type, bool bubbles,
                                      bool cancelable) {
    auto * event = cx.allocate<script::object_object>();
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
    //
    // WHAT COMES BACK IS THE CALLEE'S RETURN VALUE IF IT WAS A BOOLEAN, and
    // undefined otherwise. A handler property's `return false` cancels the
    // event and nothing else about a return value is read, so a boolean is all
    // the fence forwards - it is not a heap value, so nothing has to be rooted
    // across the return, and it cannot be mistaken for the `[thrown]` array.
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

// COMPILED AT THE FIRST DISPATCH, NOT AT INSTALL. `run_nested` goes through
// `context::call`, and `call` refuses every closure while no program has run
// yet (`program_` is null until the page's first `execute`) - so a fence built
// during install() came back `undefined`, every listener took the unfenced
// path below, and a throw inside one reached the page's own `try`. By the
// first dispatch a program is always running.
void dom_bindings::compile_listener_fence(context & cx) {
    if (listener_fence_.is_callable() || fence_keeper_ == nullptr) { return; }
    // A `TypeError` AND NOT A STRING, because the page can see the difference:
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
    // A COMPILE FAILURE IS NOT FATAL. `invoke_listener` falls back to the
    // unfenced C++ path, which is what this file did before the fence existed:
    // every listener still runs and only the containment is lost.
    if (compiled.ok) { listener_fence_ = cx.run_nested(cx.own_program(std::move(compiled))); }
    fence_keeper_->retained.push_back(listener_fence_);
}

bool dom_bindings::invoke_listener(context & cx, value callback, value receiver, value args,
                                   value & thrown, value & returned) {
    thrown = value::undefined();
    returned = value::undefined();
    // ROOTED ACROSS THE FENCE'S COMPILE. The first listener a fresh context
    // fires compiles the fence through run_nested, which can collect - and
    // the event, the callback and the receiver live only in these C++
    // locals until `call` roots them. settle_read's event object was freed
    // exactly there once install_builtins allocated enough for the heap to
    // cross its first collection threshold inside the page's first handler
    // (image_basics' FileReader test, 2026-09-12).
    const context::rooted keep_callback{cx, callback};
    const context::rooted keep_receiver{cx, receiver};
    const context::rooted keep_args{cx, args};
    // THE PAGE'S FENCE FOR EVERY DOCUMENT OF THE REALM: a frame's document
    // and one a page made install no event interfaces of their own, and a
    // listener of theirs that threw left for the page's `try` - out of
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

// DOM "invoke" step 6's table: the prefixed spelling a trusted event of the
// unprefixed type reaches when nothing listened for the unprefixed one.
[[nodiscard]] std::string_view legacy_event_type_of(std::string_view type) {
    if (type == "animationend") { return "webkitAnimationEnd"; }
    if (type == "animationiteration") { return "webkitAnimationIteration"; }
    if (type == "animationstart") { return "webkitAnimationStart"; }
    if (type == "transitionend") { return "webkitTransitionEnd"; }
    // ...and the one the table does not list but every engine that has the
    // prefixed four also has: a trusted `wheel` reaches `mousewheel`, the
    // pre-standard name, under the same rule. The DOM's own default passive
    // value names `mousewheel` beside `wheel`, and non-cancelable-when-passive/
    // has ten files that listen for nothing else.
    if (type == "wheel") { return "mousewheel"; }
    return {};
}

void dom_bindings::fire_at(path_step step, std::string_view type, value event, bool capturing) {
    // THE EVENT IS ROOTED FOR THE CALL. An event the ENGINE made - a sheet's
    // load from browser::tick, an image's from the registry - lives only in
    // a C++ local while its listeners run, and a listener that allocates
    // enough runs the collector; the object was freed under the second
    // listener and read back as whatever took its slot (paint_timing saw an
    // element where `bubbles` should have been). A page-made event is held
    // by its register too, so this costs it nothing.
    if (cx_ == nullptr) { return; }
    const script::context::rooted keep{*cx_, event};
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
    //
    // AT THE LISTENER'S OWN GLOBAL (DOM 2.10 "inner invoke" step 10.2: "report
    // exception for listener's callback's corresponding JavaScript object's
    // associated realm's global object"): a function made in a frame reports
    // to the frame's window, whatever target it listened on and whichever
    // window's addEventListener added it. The realm is the closure's; a
    // native or a `handleEvent` object has none here, and reports where the
    // dispatch is.
    //
    // THE GUARD IS THAT GLOBAL'S "IN ERROR REPORTING MODE" (HTML 8.1.3.3),
    // set by dispatch_error_value for the length of the `error` event it
    // fires: an `error` listener that throws while its own window is
    // reporting is dropped, which bounds the recursion at one level - and one
    // that throws into ANOTHER window's report is reported there
    // (window-onerror-with-cross-frame-event-listeners-5.html). Only an
    // `error` listener: a promise job the dispatch's microtask checkpoint runs
    // is inside the flag too, and what IT throws is a report of its own.
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
        // as well, which is what a browser's console is for. The FIRST one is
        // kept there, exactly as note_callback_fault keeps it: a listener that
        // faults on every event has one bug, not a thousand.
        const bool handled = global->dispatch_error_value(fault, thrown);
        if (!handled && callback_error_.empty()) { callback_error_ = fault; }
    };
    // `window.event`, DOM 2.10 "inner invoke" steps 8-11: THE CURRENT EVENT OF
    // THE LISTENER CALLBACK'S GLOBAL - the window of the realm the function
    // was made in, not the target's and not the dispatching document's: a
    // frame's function listening in the page sets the frame's `event`, and the
    // page's own is untouched (event-global-extra.window.js, event-global-is-
    // still-set-when-reporting-exception-onerror.html). Set for the call -
    // left as it was when the step is inside a shadow tree - and put back
    // after it, AFTER the fault is reported, which step 10 does with the event
    // still current. Put back rather than cleared, because a listener may
    // dispatch (window-event-restored-after-throwing-onerror.html).
    //
    // BOTH STORAGES, SAVED SEPARATELY: the window object's own `event` and the
    // realm's global - `event.stopPropagation()` with no receiver reads the
    // bare name (Event-stopPropagation-cancel-bubbling.html) - and a page's
    // top-level `var event` is a global while `window.event` is a property.
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
    // THE INNER INVOKE (DOM 2.10 "inner invoke"), once for the event's type
    // and - when nothing at all was found for it and the event is trusted -
    // once more under the legacy type of the table in "invoke" step 6:
    // `animationend` reaches a `webkitAnimationEnd` listener on an element
    // that has no listener for the unprefixed name (EventListener-invoke-
    // legacy, the four webkit-*-event files). Answers whether anything ran.
    const auto inner_invoke = [&](std::string_view type) -> bool {
        bool found = false;
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
            if (flag_of(*cx_, event, stop_immediate_property)) { return found; }
            const auto entry = std::ranges::find_if(listeners_, [&](const listener & l) {
                return !l.spent && l.callback.bits() == identity && belongs(l);
            });
            // Gone since the copy was taken - removed by a listener that ran
            // earlier at this step, or by its AbortSignal.
            if (entry == listeners_.end()) { continue; }
            found = true;
            if (entry->handler) {
                // The event handler's listener: the handler property AS IT IS NOW,
                // at the place the handler was first set (HTML 8.1.8.1).
                value thrown = value::undefined();
                const value target = object_of_step(*cx_, step);
                const value handler = cx_->lookup_property(target, "on" + ascii_lower_copy(type));
                const context::rooted keep_handler{*cx_, handler};
                // In the passive listener flag like any listener: a passive
                // `onwheel` returning false does not cancel.
                auto * carrier = static_cast<script::object_object *>(event.as_heap());
                const bool passive = entry->passive;
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
            if (entry->once) { entry->spent = true; }
            // COPIED OUT BEFORE THE CALL. `entry` is an iterator into a vector a
            // listener can grow (addEventListener) or shrink (an AbortSignal), so
            // nothing may touch it once script is running.
            const value callback = entry->callback;
            const bool passive = entry->passive;
            // SET AND CLEARED rather than saved and restored: an event that is
            // already being dispatched is refused, so one can never be inside two
            // of these at once. It stays set across a NESTED dispatch on purpose -
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
                // PER LISTENER, not per dispatch. A throw from the first of three
                // must not stop the other two - which it did twice over, first
                // because every later `call` declines while the VM's failure flag
                // is up and then because the throw left the dispatch entirely - and
                // each one that throws is its own report.
                report_fault(threw, thrown, callback);
            });
        }
        // A HANDLER NOBODY REGISTERED A LISTENER FOR - a parsed attribute the
        // write log never saw, a name whose event type is not its lowercase
        // spelling - still runs, after the listeners, as it always did.
        // ...but NOT a body's or frameset's forwarded handler (`<body onerror>`,
        // `onload`...): that one is the WINDOW's, registered at the window step,
        // and running it here too would call it twice - and without the five
        // arguments an ErrorEvent at the window is owed
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
