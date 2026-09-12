// dom_bindings - what the engine pushes in: focus and viewport observations,
// the key, wheel and mouse events it synthesises, uncaught-exception reports,
// and the propagation path an event travels.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

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
    // THE SUBRESOURCES SETTLE BEFORE THE WINDOW'S `load`. HTML delays the
    // document's load event until every sheet, script and frame has finished,
    // so a `<link onload>` or `<style onload>` counted in a `window` load
    // handler has already run - css/cssom/HTML{Link,Style}Element-load-event
    // count exactly that. They were queued beside the timers, which run AFTER
    // this dispatch, so every count read zero.
    if (!target && type == "load" && !frame_loads_.empty()) {
        std::vector<pending_frame> due;
        due.swap(frame_loads_);
        for (const pending_frame & waiting : due) {
            settle_frame(*cx_, waiting);
            note_callback_fault("frame load");
        }
    }
    return dispatch_event(type, target, make_event(*cx_, type, target));
}

// AT THE WINDOW, which is where an uncaught exception is reported. `node_id{}`
// is the window-and-document bucket every global listener already lives in, so
// this reaches `window.onerror` and `addEventListener("error", ...)` alike.
//
// The `error` property is whatever the throw left behind, and `undefined` when
// there was nothing to leave: a VM fault - the allocation ceiling, the call
// stack ceiling - is a failure that was never an exception, and a synthetic
// Error whose stack is a lie is worse than nothing. testharness.js reads
// `e.error && e.error.stack` and falls back to filename:lineno:colno, so both
// shapes are ones it understands.
bool dom_bindings::dispatch_error(std::string_view message) {
    return dispatch_error_value(message, value::undefined());
}

// THE VALUE MATTERS AND NOT ONLY THE TEXT. A page's own reporting is written
// against `event.error`, and so is the suite's: EventListener-handleEvent.html
// rethrows it - `throw event.error` - and asserts the identity of the object it
// gets back against the one its getter threw, which no string can answer.
bool dom_bindings::dispatch_error_value(std::string_view message, value error) {
    if (cx_ == nullptr) { return false; }
    // ROOTED ACROSS THE ALLOCATIONS BELOW. `error` arrives in a C++ local, which
    // is not somewhere the collector looks, and building the event object
    // allocates - so an unrooted thrown object can be swept between the throw
    // and the listener that was going to read it.
    const context::rooted keep_error{*cx_, error};
    value event = make_event(*cx_, "error", node_id{});
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("message", cx_->string(std::string{message}));
    object->set("filename", cx_->string(std::string{}));
    object->set("lineno", value::number(0));
    object->set("colno", value::number(0));
    object->set("error", error);
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

value dom_bindings::make_mouse_event(context & cx, std::string_view type, node_id target,
                                     const input_event & input, bool pointer) {
    value event = make_event(cx, type, target);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    // A MouseEvent, not a plain Event: `instanceof MouseEvent` is how a page
    // tells the two apart, and it is how dispatch decides whether a `click` has
    // activation behaviour at all - dom/events/Event-dispatch-click.html's
    // "basic with wrong event class" sends a `new Event("click")` at a checkbox
    // and expects it NOT to toggle.
    const value prototype = pointer ? pointer_event_prototype_ : mouse_event_prototype_;
    if (prototype.is_object()) { object->prototype = prototype; }
    // UI Events 3.1: the mouse events a user agent dispatches are composed.
    object->set("composed", value::boolean(true));
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
    const bool down = type == "mousedown" || type == "pointerdown";
    object->set("buttons", value::number(down ? 1 << dom_button : 0));
    object->set("shiftKey", value::boolean(input.shift));
    object->set("ctrlKey", value::boolean(input.ctrl));
    if (pointer) {
        // One pointer, because there is one mouse. A page keyed on
        // pointerId - p5 keeps a map of active ones - needs it to be
        // stable, and needs the same id on down and up or the entry leaks.
        object->set("pointerId", value::number(1));
        object->set("pointerType", cx.string("mouse"));
        object->set("isPrimary", value::boolean(true));
        object->set("pressure", value::number(down ? 0.5 : 0));
    }
    return event;
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
        stopped = dispatch_event(pointer_type, target,
                                 make_mouse_event(*cx_, pointer_type, target, input, true));
    }
    return dispatch_event(type, target, make_mouse_event(*cx_, type, target, input, false)) ||
           stopped;
}

// `element.click()` - CLICKING WITHOUT A MOUSE, HTML 3.2.6.
//
// It was absent, and that is how p5's save() reaches the outside world:
// downloadFile makes an <a href download>, calls click() on it, and revokes the
// URL on the next line. So the whole export path was one missing method wide,
// and the failure was that nothing happened - no error, no file.
//
// It used to dispatch a plain Event and then call the browser's activate hook
// itself - so a checkbox's listeners saw the OLD checkedness, a detached
// checkbox fired `change`, and `dispatchEvent(new MouseEvent("click"))` toggled
// nothing at all. Now it is one synthetic MouseEvent down the path an engine
// click takes, and dispatch_to owns the activation behaviour for both.
//
// A DISABLED FORM CONTROL GETS NOTHING - not even the event. That is step 1 of
// the method and it is the difference between `click()` and `dispatchEvent`:
// the latter reaches a disabled checkbox and toggles it, which
// Event-dispatch-click.html asserts for both. `disabled` here is the control's
// own attribute or an enclosing <fieldset>'s, as the browser's is_disabled says.
bool dom_bindings::click(node_id target) {
    if (cx_ == nullptr || !target || doc_ == nullptr) { return false; }
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(target).value_or(atom{}));
        if (control_kind_of(tag, txn.attribute_value(target, atoms_->intern("type"))) !=
            control_kind::none) {
            const atom disabled = atoms_->intern("disabled");
            const atom fieldset = atoms_->intern_lower("fieldset");
            for (node_id at = target; at; at = txn.parent(at)) {
                if ((at == target || txn.tag(at).value_or(atom{}) == fieldset) &&
                    txn.has_attribute(at, disabled)) {
                    return false;
                }
            }
        }
    }
    value event = make_mouse_event(*cx_, "click", target, input_event{}, false);
    // "with the not trusted flag set" - it came from script, whoever asked.
    static_cast<script::object_object *>(event.as_heap())
        ->set(std::string{trusted_property}, value::boolean(false));
    return dispatch_to(event, path_step{target, listen_on::node});
}

bool dom_bindings::is_connected(node_id target) const {
    if (!target || doc_ == nullptr) { return false; }
    const auto txn = doc_->read();
    const node_id top = root_of_tree(txn, target, true);
    // The same test propagation_path makes: a parsed page's root is the <html>
    // element, and a created document still has its Document node.
    return top == txn.root() || txn.kind(top).value_or(node_kind::element) == node_kind::document;
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
//
// A SHADOW ROOT'S PARENT IS ITS HOST when the event is `composed` - DOM's "get
// the parent" for a shadow root - and nothing at all when it is not. The walk
// used to stop at the fragment either way, so a listener on the host never
// heard an event from inside its own tree: Event-dispatch-listener-order.
// window.js registers on both sides of the boundary and got the inside half.
//
// AND A DETACHED TREE ENDS AT ITS OWN ROOT. The document and the window were
// appended for every node, so `div.dispatchEvent(...)` on an element nobody had
// inserted reached every global listener on the page. The DOM's chain runs
// parent to parent and the document's parent is the window; a root that is not
// the document has no parent, and the two globals are not on the path.
std::vector<dom_bindings::path_step> dom_bindings::propagation_path(path_step at,
                                                                    bool composed) const {
    std::vector<path_step> path;
    // A STANDALONE EventTarget IS THE WHOLE PATH. It is not in the tree, so
    // there is nothing above it to capture through or bubble to, and appending
    // the document and the window would deliver a page's private events to
    // every global listener there is.
    if (at.on == listen_on::object) { return {at}; }
    bool connected = true;
    if (at.on == listen_on::node && at.node) {
        const auto txn = doc_->read();
        for (node_id walk = at.node; walk;) {
            path.push_back(path_step{walk, listen_on::node});
            node_id up = txn.parent(walk);
            if (!up) {
                const shadow_tree * tree = shadow_tree_of(walk);
                if (tree != nullptr && composed) {
                    up = tree->host;
                } else {
                    // The test element/internal.hpp's is_document_root makes:
                    // a parsed page's root is the <html> element, and a
                    // created document still has its Document node.
                    connected = tree == nullptr && (walk == txn.root() ||
                                                    txn.kind(walk).value_or(node_kind::element) ==
                                                        node_kind::document);
                }
            }
            walk = up;
        }
    }
    if (!connected) { return path; }
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

// The window is recognised as the PROXY and as the object behind it, because
// `object_of_step` hands out the proxy and a page can reach either; the
// document likewise. A wrapper carries its own copies of the three methods, so
// an ELEMENT was fine before this; a node kind whose wrapper never got them
// fell through to the prototype's and its listeners went into the object
// bucket - registered, never called, and nothing anywhere said so.
dom_bindings::path_step dom_bindings::step_of(value self) {
    const auto same = [&](value other) {
        return self.is_heap() && other.is_heap() && self.bits() == other.bits();
    };
    if (cx_ != nullptr && cx_->has_global("window") && same(cx_->global("window"))) {
        return path_step{node_id{}, listen_on::window};
    }
    if (same(window_)) { return path_step{node_id{}, listen_on::window}; }
    if (same(document_) || same(document_target_)) {
        return path_step{node_id{}, listen_on::document};
    }
    if (const node_id id = handle_of(self)) { return path_step{id, listen_on::node}; }
    return path_step{node_id{}, listen_on::object, self};
}

} // namespace ctbrowser::shell
