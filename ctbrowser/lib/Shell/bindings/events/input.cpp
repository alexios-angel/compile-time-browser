// dom_bindings - what the engine pushes in: focus and viewport observations,
// the key, wheel and mouse events it synthesises, uncaught-exception reports,
// and the propagation path an event travels.

#include "internal.hpp"

#include <utility>

namespace ctbrowser::shell {

using namespace detail;

namespace {

// The realm's `name` interface's prototype, on an engine-made event - the way
// a FocusEvent gets its own. A page that replaced the global keeps the
// Event.prototype it had.
void adopt_interface(context & cx, script::object_object & object, std::string_view name) {
    if (!cx.has_global(name)) { return; }
    const value ctor = cx.global(name);
    if (!ctor.is_object_like()) { return; }
    if (const value proto = cx.lookup_property(ctor, "prototype"); proto.is_object()) {
        object.prototype = proto;
    }
}

// Whether any listener of these types on the path is NOT passive - the one
// question the cancelability of a wheel or touch event turns on.
[[nodiscard]] bool blocking_listener(const std::vector<binding_detail::listener> & listeners,
                                     const std::vector<binding_detail::path_step> & path,
                                     std::initializer_list<std::string_view> types) {
    return std::ranges::any_of(path, [&](const binding_detail::path_step & step) {
        return std::ranges::any_of(listeners, [&](const binding_detail::listener & l) {
            if (l.spent || l.passive || l.on != step.on) { return false; }
            if (l.on == binding_detail::listen_on::node && l.target != step.node) { return false; }
            return std::ranges::find(types, std::string_view{l.type}) != types.end();
        });
    });
}

// The first step of an input event's path: the node, or the Document for none.
[[nodiscard]] binding_detail::path_step step_at(node_id target) {
    return target ? binding_detail::path_step{target, binding_detail::listen_on::node}
                  : binding_detail::path_step{node_id{}, binding_detail::listen_on::document};
}

} // namespace

bool dom_bindings::refresh_wrappers() {
    if (cx_ == nullptr) { return false; }
    // NOT EVERY WRAPPER, EVERY FRAME. What refresh_element writes - tagName,
    // nodeType, localName, ownerDocument - cannot change for a node, and the
    // box metrics and reflected attributes are accessors; walking the whole
    // table before each dispatch and layout was quadratic in a page that
    // makes elements as it goes (the interpolation harness makes one per
    // subtest). The two things that can be stale are the interface chain of
    // a wrapper made before EventTarget existed, which is linked once, and
    // the document's live properties - activeElement and title would go
    // stale the moment focus moved.
    ensure_dom_interfaces(*cx_);
    refresh_document();
    return std::exchange(wrote_to_control_, false);
}

// WHERE FOCUS IS, pushed in. The hook the bindings already hold is write-only:
// script can call element.focus() and the browser hears it, but nothing came
// back, so `document.activeElement` had nothing to read. Same shape as
// observe_location, and for the same reason - a value set once at install is a
// snapshot, and this one changes on every click.
void dom_bindings::observe_focus(node_id id) {
    focused_ = id;
    refresh_document();
    place_caret_on_focus(id); // an editing host gets a caret - see selection.cpp
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
    const value event = make_event(*cx_, type, target);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    // "THE END" (HTML 13.2.7) FIRES THESE TWO WITH ITS OWN FLAGS, not an
    // engine event's: DOMContentLoaded bubbles and cannot be cancelled, and
    // the window's `load` does neither (the-end.html).
    const bool window_load = !target && type == "load";
    if (!target && type == "DOMContentLoaded") { object->set("cancelable", value::boolean(false)); }
    bool result = false;
    if (window_load) {
        // AT THE WINDOW, with the legacy target override flag: the path is the
        // window alone - an event that does not bubble never reaches it from
        // the Document - and `target` reads the Document.
        object->set("bubbles", value::boolean(false));
        object->set("cancelable", value::boolean(false));
        object->define(std::string{legacy_target_override_property}, value::boolean(true),
                       script::attr_none);
        const script::context::rooted keep{*cx_, event};
        result = dispatch_to(event, path_step{node_id{}, listen_on::window});
    } else {
        result = dispatch_event(type, target, event);
    }
    // ...and then, the first time the document is shown, `pageshow`: "if the
    // Document's page showing is false, set it to true" and fire a
    // PageTransitionEvent with `persisted` false at the window. It bubbles
    // and is cancelable as every browser fires it (whatwg/html#6794). A
    // document.open() that reaches `load` again is already showing.
    script::object_object * window = window_object();
    if (window_load && window != nullptr &&
        window->find(CTBROWSER_SYMBOL_KEY("sym:ctbrowser:pageShowing")) == nullptr) {
        window->define(CTBROWSER_SYMBOL_KEY("sym:ctbrowser:pageShowing"), value::boolean(true),
                       script::attr_none);
        const value shown = make_event(*cx_, "pageshow", node_id{});
        auto * held = static_cast<script::object_object *>(shown.as_heap());
        const value ctor = cx_->global("PageTransitionEvent");
        if (const value proto =
                ctor.is_callable() ? cx_->lookup_property(ctor, "prototype") : value::undefined();
            proto.is_object()) {
            held->prototype = proto;
        }
        held->set("persisted", value::boolean(false));
        (void)dispatch_event("pageshow", node_id{}, shown);
    }
    return result;
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

bool dom_bindings::dispatch_error(std::string_view message, std::string_view script_src) {
    return dispatch_error_value(message, value::undefined(), script_src);
}

// THE VALUE MATTERS AND NOT ONLY THE TEXT. A page's own reporting is written
// against `event.error`, and so is the suite's: EventListener-handleEvent.html
// rethrows it - `throw event.error` - and asserts the identity of the object it
// gets back against the one its getter threw, which no string can answer.
bool dom_bindings::dispatch_error_value(std::string_view message, value error,
                                        std::string_view script_src) {
    if (cx_ == nullptr) { return false; }
    // ROOTED ACROSS THE ALLOCATIONS BELOW. `error` arrives in a C++ local, which
    // is not somewhere the collector looks, and building the event object
    // allocates - so an unrooted thrown object can be swept between the throw
    // and the listener that was going to read it.
    const context::rooted keep_error{*cx_, error};
    value event = make_event(*cx_, "error", node_id{});
    auto * object = static_cast<script::object_object *>(event.as_heap());
    // AN ErrorEvent, which is what makes the window's `onerror` take the five
    // arguments rather than the event (handler_attributes.cpp).
    if (const value ctor =
            cx_->has_global("ErrorEvent") ? cx_->global("ErrorEvent") : value::undefined();
        ctor.is_object_like()) {
        if (const value proto = cx_->lookup_property(ctor, "prototype"); proto.is_object()) {
            object->prototype = proto;
        }
    }
    object->set("message", cx_->string(std::string{message}));
    // The script's URL: "report an exception" names it, and an inline
    // script's is its document's (`source === location.href` in the
    // body-onerror files; compile-error-same-origin.html wants the external
    // one's, resolved). No position yet - the VM keeps none for a throw.
    std::string filename = location_href_;
    if (!script_src.empty() && !script_src.starts_with("<inline:")) {
        filename = location_href_.empty() ? std::string{script_src}
                                          : resolve(location_href_, std::string{script_src});
    }
    object->set("filename", cx_->string(filename));
    object->set("lineno", value::number(0));
    object->set("colno", value::number(0));
    object->set("error", error);
    // "IN ERROR REPORTING MODE" for the length of the dispatch: an `error`
    // listener of this window that throws is not reported here again (see
    // listeners.cpp's report_fault).
    const bool was = std::exchange(error_reporting_, true);
    const bool handled = dispatch_event("error", node_id{}, event);
    error_reporting_ = was;
    return handled;
}

// `focus`/`blur` and `focusin`/`focusout` (UI Events 5.4.4): FocusEvents whose
// `relatedTarget` is the other element of the change, the first pair not
// bubbling and the second bubbling. `dispatch()` alone made a bubbling plain
// Event with no relatedTarget, which a page cannot tell from any other.
bool dom_bindings::dispatch_focus(std::string_view type, node_id target, node_id related) {
    if (cx_ == nullptr || !target) { return false; }
    value event = make_event(*cx_, type, target);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    const bool bubbles = type == "focusin" || type == "focusout";
    object->set("bubbles", value::boolean(bubbles));
    object->set("cancelable", value::boolean(false));
    object->set("composed", value::boolean(true));
    object->set("relatedTarget", related ? wrap(*cx_, related) : value::null());
    if (const value ctor =
            cx_->has_global("FocusEvent") ? cx_->global("FocusEvent") : value::undefined();
        ctor.is_object_like()) {
        if (const value proto = cx_->lookup_property(ctor, "prototype"); proto.is_object()) {
            object->prototype = proto;
        }
    }
    return dispatch_event(type, target, event);
}

// `hashchange` at the window: a HashChangeEvent carrying both addresses,
// bubbling as HTML fires it, not cancelable.
bool dom_bindings::dispatch_hash_change(const std::string & old_url, const std::string & new_url) {
    if (cx_ == nullptr) { return false; }
    value event = make_event(*cx_, "hashchange", node_id{});
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("cancelable", value::boolean(false));
    object->set("oldURL", cx_->string(old_url));
    object->set("newURL", cx_->string(new_url));
    if (const value ctor = cx_->has_global("HashChangeEvent") ? cx_->global("HashChangeEvent")
                                                              : value::undefined();
        ctor.is_object_like()) {
        if (const value proto = cx_->lookup_property(ctor, "prototype"); proto.is_object()) {
            object->prototype = proto;
        }
    }
    return dispatch_event("hashchange", node_id{}, event);
}

bool dom_bindings::dispatch_key(std::string_view type, node_id target, const input_event & input) {
    if (cx_ == nullptr) { return false; }
    // An ACTIVATION-TRIGGERING input event (HTML 6.4.3): a keydown that is not
    // Escape notifies before it is dispatched.
    if (type == "keydown" && input.key != "Escape") { notify_activation(); }
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
//
// A WheelEvent, composed (UI Events 3.1: every mouse event a user agent
// dispatches is), and CANCELABLE ONLY WHEN SOMETHING COULD CANCEL IT - UI
// Events' "cancelability of wheel events": a user agent "should generate only
// uncancelable wheel events when it observes that there are no non-passive
// listeners for the event". The four passive-by-default targets make that the
// common case, and non-cancelable-when-passive/ asserts both halves for each.
// `mousewheel` counts, because a trusted `wheel` nobody listened for at a step
// reaches its `mousewheel` listeners there (legacy_event_type_of).
bool dom_bindings::dispatch_wheel(node_id target, const input_event & input) {
    if (cx_ == nullptr) { return false; }
    value event = make_mouse_event(*cx_, "wheel", target, input, false);
    const context::rooted keep{*cx_, event};
    auto * object = static_cast<script::object_object *>(event.as_heap());
    adopt_interface(*cx_, *object, "WheelEvent");
    object->set("cancelable",
                value::boolean(blocking_listener(
                    listeners_, propagation_path(step_at(target), true), {"wheel", "mousewheel"})));
    object->set("deltaX", value::number(input.wheel_x * 100.0));
    object->set("deltaY", value::number(-input.wheel_y * 100.0));
    object->set("deltaZ", value::number(0));
    object->set("deltaMode", value::number(0)); // 0 = pixels
    // THE LEGACY SPELLING TOO, and it is not politeness: plenty of shipped code
    // reads `wheelDelta` and it is the OPPOSITE sign again by definition, so a
    // library falling back to it inverts unless both are right.
    object->set("wheelDelta", value::number(input.wheel_y * 120.0));
    object->set("wheelDeltaX", value::number(-input.wheel_x * 120.0));
    object->set("wheelDeltaY", value::number(input.wheel_y * 120.0));
    object->set("buttons", value::number(0));
    return dispatch_event("wheel", target, event);
}

// A FINGER (Touch Events 1, and the `touch` pointer of Pointer Events 3),
// dispatched the way a mouse's press is: the pointer event first, then the
// touch event, both composed. One finger, so identifier 0 and every list holds
// the one Touch - except `touches` and `targetTouches` at `touchend`, which
// hold what is STILL on the surface, and that is nothing.
//
// `target` is where the touch STARTED, for all three: a Touch keeps the
// element it began on for its whole life, however far the finger travels -
// which is what lets a drag that leaves its element still be that element's.
//
// touchstart and touchmove are cancelable only when a non-passive listener on
// the path could cancel them, for the reason the wheel is (non-cancelable-
// when-passive/'s twenty touch files); touchend always is.
//
// `pointer` false is a finger whose pointer was CANCELLED when its drag began
// to pan: Pointer Events stops sending that pointer's events, and the touch
// events go on. Phase "cancel" is that moment - the `pointercancel` alone,
// bubbling and not cancelable.
bool dom_bindings::dispatch_touch(std::string_view phase, node_id target, float x, float y,
                                  bool pointer) {
    if (cx_ == nullptr) { return false; }
    context & cx = *cx_;
    const bool start = phase == "start";
    const bool end = phase == "end";
    const bool cancel = phase == "cancel";
    const std::string pointer_type = start    ? "pointerdown"
                                     : end    ? "pointerup"
                                     : cancel ? "pointercancel"
                                              : "pointermove";
    const std::string touch_type = start ? "touchstart" : end ? "touchend" : "touchmove";
    // Activation-triggering (HTML 6.4.3) is the LIFT for a finger: a non-mouse
    // pointerup, and touchend.
    if (end) { notify_activation(); }

    if (pointer || cancel) {
        const input_event at = input_event::mouse_move_to(x, y);
        const value made = make_mouse_event(cx, pointer_type, target, at, true);
        const context::rooted keep{cx, made};
        auto * object = static_cast<script::object_object *>(made.as_heap());
        object->set("pointerId", value::number(2)); // the mouse is 1
        object->set("pointerType", cx.string("touch"));
        object->set("buttons", value::number(end || cancel ? 0 : 1));
        object->set("pressure", value::number(end || cancel ? 0 : 0.5));
        object->set("width", value::number(1));
        object->set("height", value::number(1));
        if (cancel) { object->set("cancelable", value::boolean(false)); }
        (void)dispatch_event(pointer_type, target, made);
    }
    if (cancel) { return false; }

    value event = make_event(cx, touch_type, target);
    const context::rooted keep_event{cx, event};
    auto * object = static_cast<script::object_object *>(event.as_heap());
    adopt_interface(cx, *object, "TouchEvent");
    object->set("composed", value::boolean(true));
    object->set(
        "cancelable",
        value::boolean(end || blocking_listener(listeners_, propagation_path(step_at(target), true),
                                                {std::string_view{touch_type}})));
    object->set("detail", value::number(0));
    object->set("view", window_view_.is_heap() ? window_view_ : window_);
    for (const char * key : {"altKey", "metaKey", "ctrlKey", "shiftKey"}) {
        object->set(key, value::boolean(false));
    }

    const value touch = cx.make_object();
    const context::rooted keep_touch{cx, touch};
    auto * finger = static_cast<script::object_object *>(touch.as_heap());
    adopt_interface(cx, *finger, "Touch");
    const point scroll = viewport_scroll();
    finger->set("identifier", value::number(0));
    finger->set("target", wrap(cx, target));
    finger->set("clientX", value::number(x));
    finger->set("clientY", value::number(y));
    finger->set("pageX", value::number(x + scroll.x));
    finger->set("pageY", value::number(y + scroll.y));
    finger->set("screenX", value::number(x));
    finger->set("screenY", value::number(y));
    for (const char * key : {"radiusX", "radiusY"}) { finger->set(key, value::number(1)); }
    finger->set("rotationAngle", value::number(0));
    finger->set("force", value::number(end ? 0 : 0.5));
    finger->set("altitudeAngle", value::number(std::numbers::pi / 2));
    finger->set("azimuthAngle", value::number(0));
    finger->set("touchType", cx.string("direct"));

    // A TouchList: TouchList.prototype, the Touch at "0", and a read-only length.
    const auto list = [&](bool holds) {
        const value made = cx.make_object();
        const context::rooted keep_list{cx, made};
        auto * held = static_cast<script::object_object *>(made.as_heap());
        adopt_interface(cx, *held, "TouchList");
        if (holds) { held->set("0", touch); }
        held->define("length", value::number(holds ? 1 : 0), script::attr_builtin);
        return made;
    };
    object->set("touches", list(!end));
    object->set("targetTouches", list(!end));
    object->set("changedTouches", list(true));
    return dispatch_event(touch_type, target, event);
}

// A USER'S EDIT, told to the page (Input Events 2, HTML 4.10.5.5):
//   * `beforeinput` before the value changes - an InputEvent, composed and
//     CANCELABLE, and cancelling it keeps the edit out;
//   * `input` after it - the same InputEvent, not cancelable;
//   * a control's `input` with no inputType (a <select>'s) - a plain Event,
//     bubbling and composed;
//   * `change` once the edit is committed - a plain Event that bubbles and
//     neither cancels nor composes.
// `data` is the text inserted, null for a deletion. Answers whether a listener
// cancelled it.
bool dom_bindings::dispatch_input(std::string_view type, node_id target,
                                  std::optional<std::string_view> input_type,
                                  std::optional<std::string_view> data) {
    if (cx_ == nullptr || !target) { return false; }
    context & cx = *cx_;
    value event = make_event(cx, type, target);
    const context::rooted keep{cx, event};
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("cancelable", value::boolean(type == "beforeinput"));
    object->set("composed", value::boolean(type != "change"));
    if (input_type && type != "change") {
        adopt_interface(cx, *object, "InputEvent");
        object->set("detail", value::number(0));
        object->set("view", window_view_.is_heap() ? window_view_ : window_);
        object->set("inputType", cx.string(std::string{*input_type}));
        object->set("data", data ? cx.string(std::string{*data}) : value::null());
        object->set("isComposing", value::boolean(false));
        object->set("dataTransfer", value::null());
    }
    return dispatch_event(type, target, event);
}

// THE USER'S SELECTION IS THE PAGE'S SELECTION. A press on text collapses
// `getSelection()` there, and a drag extends it - through the Selection's own
// script surface (`collapse`, `setBaseAndExtent`), so its validity rules, its
// direction and its selectionchange are the API's and not restated here. A
// click leaves a DIRECTIONLESS collapsed selection, which is why the collapsed
// case is `collapse` rather than a setBaseAndExtent to the same point.
//
// The positions arrive as UTF-8 byte offsets into a text node's BOX, as the
// painted selection keeps them, and leave as the DOM's UTF-16 offsets into the
// node's own text. The box folded each whitespace run to one space unless it
// preserves whitespace (box_builder::collapse_whitespace), so the node's text
// is walked the same way first - "\n  Hello" shows as " Hello", and the H at
// byte 1 of the box is at offset 3 of the node - and a position just past a
// folded space lands past the whole run.
void dom_bindings::select_by_user(node_id anchor, std::size_t anchor_byte, node_id focus,
                                  std::size_t focus_byte) {
    if (cx_ == nullptr || doc_ == nullptr || !anchor || !focus) { return; }
    context & cx = *cx_;
    const auto in_realm = enter_realm(cx);
    const auto utf16 = [&](node_id id, std::size_t byte) {
        const located shown = locate(id);
        const bool folded =
            shown.f != nullptr && shown.f->box != nullptr && !shown.f->box->preformatted;
        const auto txn = doc_->read();
        const std::string_view text = txn.text(id);
        if (folded) {
            const auto is_space = [](char c) {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
            };
            std::size_t raw = 0;
            bool in_run = false;
            for (std::size_t kept = 0; raw < text.size() && kept < byte; ++raw) {
                const bool space = is_space(text[raw]);
                if (!space || !in_run) { ++kept; }
                in_run = space;
            }
            while (in_run && raw < text.size() && is_space(text[raw])) { ++raw; }
            byte = raw;
        }
        double units = 0;
        for (std::size_t i = 0; i < std::min(byte, text.size()); ++i) {
            const auto b = static_cast<unsigned char>(text[i]);
            if ((b & 0xC0u) != 0x80u) { units += b >= 0xF0u ? 2 : 1; }
        }
        return units;
    };
    // The DOCUMENT's getSelection, as place_caret_on_focus reaches it. The
    // window object does not hold the global one - that is the realm's
    // binding, which only the window view reads through to - so asking the
    // window found nothing and no press ever reached the page's Selection.
    const value get = cx.lookup_property(document_, "getSelection");
    if (!get.is_callable()) { return; }
    const value selection = cx.call(get, std::span<const value>{}, document_);
    const context::rooted keep_selection{cx, selection};
    const bool collapsed = anchor == focus && anchor_byte == focus_byte;
    const value method =
        selection.is_object()
            ? cx.lookup_property(selection, collapsed ? "collapse" : "setBaseAndExtent")
            : value::undefined();
    if (method.is_callable()) {
        std::array<value, 4> args{wrap(cx, anchor), value::number(utf16(anchor, anchor_byte)),
                                  wrap(cx, focus), value::number(utf16(focus, focus_byte))};
        const context::rooted_values keep_args{cx, args};
        (void)cx.call(method, std::span<const value>{args.data(), collapsed ? 2u : 4u}, selection);
    }
    if (cx.failed()) { (void)cx.take_error(); }
}

void dom_bindings::hold_scrollend(bool hold) {
    if (hold) {
        if (!held_scrollend_) { held_scrollend_.emplace(); }
        return;
    }
    if (!held_scrollend_) { return; }
    const std::vector<node_id> due = std::move(*held_scrollend_);
    held_scrollend_.reset();
    if (cx_ != nullptr && !due.empty()) { fire_scrollend(*cx_, due); }
}

// Where a wheel notch or a touch drag goes once no listener kept it: the
// nearest SCROLL CONTAINER on the flat-tree path from the target that can still
// move that way - which is scroll chaining, CSSOM View's "scroll an element" on
// the first box that has room - and the VIEWPORT when none can, which is the
// caller's to scroll. The root element and the body stand for the viewport and
// end the walk. Answers whether an element took it.
bool dom_bindings::wheel_scroll(node_id from, float dx, float dy) {
    if (doc_ == nullptr || !from || (dx == 0 && dy == 0)) { return false; }
    flush_layout();
    const node_id root = doc_->read().root();
    const node_id body = body_element();
    for (const path_step & step : propagation_path(step_at(from), true)) {
        if (step.on != listen_on::node) { break; }
        if (step.node == root || step.node == body) { return false; }
        const located at = locate(step.node);
        if (at.f == nullptr || at.f->box == nullptr || !at.f->box->scroll_container) { continue; }
        const rect area = layout::scrolling_area_of(*at.f);
        const rect padding = layout::padding_box_of(*at.f);
        const point now = scroll_offset_of(step.node);
        const bool room_x = (dx > 0 && now.x < area.width - padding.width) || (dx < 0 && now.x > 0);
        const bool room_y =
            (dy > 0 && now.y < area.height - padding.height) || (dy < 0 && now.y > 0);
        if (!room_x && !room_y) { continue; }
        scroll_element_to(step.node, now.x + dx, now.y + dy);
        return true;
    }
    return false;
}

// CSSOM View §13.1 "run the scroll steps", steps 4-5: after the `scroll`
// events, a `scrollend` at every target whose scroll COMPLETED - and an instant
// scroll, which is every scroll this engine makes, completes as it happens, so
// the targets are exactly the ones that just heard `scroll`. At a Document it
// bubbles, to the window; at an element it does not. Neither is cancelable.
//
// A USER'S scroll completes when the user stops (CSSOM View: "if scrolling was
// done by a user input, wait until the user input is done"), so while a finger
// is down the targets are held, once each, and fired when it lifts.
void dom_bindings::fire_scrollend(context & cx, std::span<const node_id> scrolled) {
    if (held_scrollend_) {
        for (const node_id at : scrolled) {
            if (std::ranges::find(*held_scrollend_, at) == held_scrollend_->end()) {
                held_scrollend_->push_back(at);
            }
        }
        return;
    }
    for (const node_id at : scrolled) {
        const value event = make_event(cx, "scrollend", at);
        auto * object = static_cast<script::object_object *>(event.as_heap());
        object->set("bubbles", value::boolean(!at));
        object->set("cancelable", value::boolean(false));
        (void)dispatch_event("scrollend", at, event);
    }
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
        // A scroller removed while it scrolled never finishes its scroll
        // (scrollend-event-not-fired-after-removing-scroller.tentative.html).
        if (at && !is_connected(at)) { continue; }
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
    // ...and a mousedown (its pointerdown is a mouse's, which the list names too).
    if (type == "mousedown") { notify_activation(); }
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
// own attribute or an enclosing <fieldset>'s, as the browser's is_disabled says -
// outside that fieldset's first <legend>.
bool dom_bindings::click(node_id target) {
    if (cx_ == nullptr || !target || doc_ == nullptr) { return false; }
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(target).value_or(atom{}));
        if (control_kind_of(tag, txn.attribute_value(target, atoms_->intern("type"))) !=
            control_kind::none) {
            const atom disabled = atoms_->intern("disabled");
            const atom fieldset = atoms_->intern_lower("fieldset");
            const atom legend = atoms_->intern_lower("legend");
            // ...less the one exemption HTML 4.10.18.5 makes: a control inside
            // a disabled fieldset's FIRST <legend> child is not disabled by it.
            const auto first_legend_of = [&](node_id set) {
                for (const node_id child : txn.children(set)) {
                    if (txn.tag(child).value_or(atom{}) == legend) { return child; }
                }
                return node_id{};
            };
            node_id below;
            for (node_id at = target; at; below = at, at = txn.parent(at)) {
                if (at == target && txn.has_attribute(at, disabled)) { return false; }
                if (at != target && txn.tag(at).value_or(atom{}) == fieldset &&
                    txn.has_attribute(at, disabled) && below != first_legend_of(at)) {
                    return false;
                }
            }
        }
    }
    // THE CLICK IN PROGRESS FLAG, steps 2-4 of `click()`: a click() on an
    // element whose click() is still running returns without dispatching. A
    // <label>'s activation behaviour clicks the control it labels, and a
    // control with none of its own - a <textarea> - hands that click's
    // activation straight back to the label, which would click it again for
    // ever: `<label>Notes <textarea>` hung the page on its first click.
    // ponytail: a thread-local stack of (bindings, element) stands in for the
    // flag, which only ever lives across one synchronous dispatch; a bit on
    // the element if that stack ever gets deep.
    static thread_local std::vector<std::pair<const dom_bindings *, node_id>> clicking;
    const std::pair<const dom_bindings *, node_id> self{this, target};
    if (std::ranges::find(clicking, self) != clicking.end()) { return false; }
    clicking.push_back(self);
    value event = make_mouse_event(*cx_, "click", target, input_event{}, false);
    // "with the not trusted flag set" - it came from script, whoever asked.
    static_cast<script::object_object *>(event.as_heap())
        ->set(std::string{trusted_property}, value::boolean(false));
    const bool result = dispatch_to(event, path_step{target, listen_on::node});
    clicking.pop_back();
    return result;
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
// A SHADOW ROOT'S PARENT IS ITS HOST - DOM's "get the parent" for a shadow
// root - unless the event is not `composed` AND this is the root of the
// target's own tree. The walk used to stop at the fragment either way, so a
// listener on the host never heard an event from inside its own tree:
// Event-dispatch-listener-order.window.js registers on both sides of the
// boundary and got the inside half. And it is only the TARGET'S root that
// stops a non-composed event: one at a light child slotted into a shadow tree
// travels through the slot, out of that tree to its host and on up
// (Extensions-to-Event-Interface.html).
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
        const node_id own_root = root_of_tree(txn, at.node, false);
        for (node_id walk = at.node; walk;) {
            path.push_back(path_step{walk, listen_on::node});
            // THE FLAT TREE, not the light-DOM chain: a slotted node's next step
            // is its assigned slot - DOM "get the parent" returns the assigned
            // slot for a slottable. A closed slot still routes the event, so
            // assigned_slot_of ignores mode. event-inside-slotted-node.html
            // walks [target, …, slot, slot parent, shadow root, host].
            node_id up = assigned_slot_of(walk);
            if (up) {
                walk = up;
                continue;
            }
            up = txn.parent(walk);
            if (!up) {
                const shadow_tree * tree = shadow_tree_of(walk);
                if (tree != nullptr && (composed || walk != own_root)) {
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
    // A DOCUMENT A PAGE MADE HAS NO WINDOW ABOVE IT: `new Document()`,
    // `createHTMLDocument()` and a clone of the page's document have a null
    // browsing context, so their path ends at the Document - the page's
    // window is not an ancestor of another document's nodes.
    // Event-dispatch-bubbles-{true,false}.html count exactly that.
    // A FRAME's document has one: its own window (frames.cpp).
    if (window_.is_object()) { path.push_back(path_step{node_id{}, listen_on::window}); }
    return path;
}

// What `currentTarget` reports for one step, and what an `on<type>` handler is
// looked up on. The window is the PROXY rather than the object behind it: a page
// compares `evt.currentTarget === window` by identity, and `window` is the proxy.
value dom_bindings::object_of_step(context & cx, path_step step) {
    switch (step.on) {
    case listen_on::node: return wrap(cx, step.node);
    case listen_on::document: return document_;
    case listen_on::window:
        if (window_view_.is_heap()) { return window_view_; }
        return cx.has_global("window") ? cx.global("window") : window_;
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
    if (same(window_view_)) { return path_step{node_id{}, listen_on::window}; }
    if (!window_view_.is_heap() && cx_ != nullptr && cx_->has_global("window") &&
        same(cx_->global("window"))) {
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
