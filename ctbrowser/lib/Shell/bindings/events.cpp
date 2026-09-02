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

bool dom_bindings::dispatch_event(std::string_view type, node_id target, value event) {
    // BEFORE the listeners run. A handler for `input` reads the field's new
    // value, so a wrapper still holding the old one is the whole bug.
    (void)refresh_wrappers();
    const auto txn = doc_->read();
    // CAPTURE THEN BUBBLE, which is what the two phases mean.
    //
    // A capturing listener sees the event on the way DOWN - before the target
    // does - and that is the entire reason to pass `{ capture: true }`: it is
    // how a page intercepts an event before whatever it is aimed at handles it.
    // Firing everything in one bubbling pass ran them in the opposite order.
    std::vector<node_id> chain;
    for (node_id at = target; at; at = txn.parent(at)) { chain.push_back(at); }
    fire_global(type, event, true);
    for (std::size_t i = chain.size(); i-- > 0;) { fire_at(chain[i], type, event, true); }
    for (const node_id at : chain) { fire_at(at, type, event, false); }
    fire_global(type, event, false);
    // A `once` listener is removed AFTER the dispatch, not during it: erasing
    // from the vector being walked is how a later listener gets skipped.
    std::erase_if(listeners_, [](const listener & l) { return l.spent; });
    // A LISTENER THAT FAULTS IS REPORTED AND THE FAULT CLEARED, exactly as for
    // a timer or an animation frame. Without this the first listener to fault
    // left the VM's failure flag set for the life of the page: every later
    // callback of any kind was refused, so the page stopped responding to
    // everything, and nothing anywhere said why.
    // ...and after an event, which is the other checkpoint a browser has: a
    // listener that resolves a promise has its handlers run before the next
    // event is dispatched, not at some later frame.
    if (cx_ != nullptr) { cx_->drain_microtasks(); }
    note_callback_fault(type);
    return prevented(event);
}

value dom_bindings::make_event(context & cx, std::string_view type, node_id target) {
    auto * event = static_cast<script::object_object *>(cx.make_object().as_heap());
    event->set("type", cx.string(std::string{type}));
    event->set("target", wrap(cx, target));
    event->set("defaultPrevented", value::boolean(false));
    // One SHARED event object per dispatch, so preventDefault called by any
    // listener is visible to the browser and to every later listener - which
    // is what makes it mean anything at all.
    event->set("preventDefault", value::object(cx.allocate<script::native_object>(
                                     "preventDefault", [](context & c, std::span<value>) {
                                         const value self = c.current_this();
                                         if (self.is_object()) {
                                             static_cast<script::object_object *>(self.as_heap())
                                                 ->set("defaultPrevented", value::boolean(true));
                                         }
                                         return value::undefined();
                                     })));
    return value::object(event);
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
dom_bindings::listener dom_bindings::make_listener(context & cx, node_id target,
                                                   std::span<value> args) {
    listener made;
    made.target = target;
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

void dom_bindings::fire_at(node_id target, std::string_view type, value event, bool capturing) {
    // Indexed rather than iterated: a listener may register another one, and
    // appending to the vector being walked invalidates an iterator. A listener
    // added during a dispatch does not run in that dispatch, which is the rule.
    const std::size_t count = listeners_.size();
    for (std::size_t i = 0; i < count && i < listeners_.size(); ++i) {
        listener & l = listeners_[i];
        if (l.target != target || l.type != type || l.capture != capturing || l.spent) { continue; }
        if (l.once) { l.spent = true; }
        (void)cx_->call(l.callback, std::span<const value>{&event, 1});
    }
    if (!capturing) { fire_handler_property(value_of_wrapper(target), type, event); }
}

void dom_bindings::fire_global(std::string_view type, value event, bool capturing) {
    const std::size_t count = listeners_.size();
    for (std::size_t i = 0; i < count && i < listeners_.size(); ++i) {
        listener & l = listeners_[i];
        if (l.target || l.type != type || l.capture != capturing || l.spent) { continue; }
        if (l.once) { l.spent = true; }
        (void)cx_->call(l.callback, std::span<const value>{&event, 1});
    }
    if (!capturing) { fire_handler_property(window_, type, event); }
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

} // namespace ctbrowser::shell
