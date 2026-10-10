//===- events/input.cpp - Engine input events and paths ---------*- C++ -*-===//
//
// What the engine pushes into dom_bindings: focus and viewport observations,
// the key, wheel, touch and mouse events it synthesises, element.click(),
// uncaught-exception reports, and the propagation path an event travels.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include <utility>

namespace ctbrowser::shell {

using namespace detail;

namespace {

/// Gives an engine-made event the prototype of the realm's `name` interface
/// (for example WheelEvent). If a page replaced that global with something
/// unusable, the event keeps the prototype it had.
void adopt_interface(context & cx, script::object_object & object, std::string_view name) {
    if (!cx.has_global(name)) { return; }
    const value ctor = cx.global(name);
    if (!ctor.is_object_like()) { return; }
    if (const value proto = cx.lookup_property(ctor, "prototype"); proto.is_object()) {
        object.prototype = proto;
    }
}

/// True if any listener of these types on the path is not passive, which is
/// what decides whether a wheel or touch event is cancelable.
[[nodiscard]] bool blocking_listener(std::span<const binding_detail::listener> listeners,
                                     const std::vector<binding_detail::path_step> & path,
                                     std::initializer_list<std::string_view> types) {
    return std::ranges::any_of(path, [&](const binding_detail::path_step & step) {
        return std::ranges::any_of(listeners, [&](const binding_detail::listener & l) {
            if (l.spent || l.passive || l.target.kind != step.on) { return false; }
            if (l.target.kind == binding_detail::listen_on::node &&
                l.target.identity != step.node.key()) {
                return false;
            }
            return std::ranges::find(types, std::string_view{l.type}) != types.end();
        });
    });
}

/// The first step of an input event's path: the node, or the Document for none.
[[nodiscard]] binding_detail::path_step step_at(node_id target) {
    return target ? binding_detail::path_step{target, binding_detail::listen_on::node}
                  : binding_detail::path_step{node_id{}, binding_detail::listen_on::document};
}

} // namespace

/// Brings wrappers up to date before a dispatch or layout, and returns (and
/// clears) whether script wrote to a form control since the last call.
bool dom_bindings::refresh_wrappers() {
    if (cx_ == nullptr) { return false; }
    // This does not walk every wrapper: what refresh_element writes (tagName,
    // nodeType, localName, ownerDocument) cannot change for a node, and the
    // box metrics and reflected attributes are accessors, so a full walk
    // would only add quadratic cost for a page that makes elements as it
    // goes. The one thing that can be stale is the interface chain of a
    // wrapper made before EventTarget existed, which is linked once.
    ensure_dom_interfaces(*cx_);
    return std::exchange(wrote_to_control_, false);
}

/// Tells the bindings which element has focus, for `document.activeElement`.
/// The focus hook the bindings hold only goes the other way (script's
/// element.focus() tells the browser). Same shape as observe_location: a value
/// set once at install would be a snapshot, and this one changes on every click.
void dom_bindings::observe_focus(node_id id) {
    focused_ = id;
    place_caret_on_focus(id); // an editing host gets a caret - see selection.cpp
}

/// Records the viewport size the page sees (innerWidth/innerHeight).
void dom_bindings::observe_viewport(int width, int height) {
    viewport_width_ = width;
    viewport_height_ = height;
}

/// Dispatches a plain engine event of `type` at `target` (the document for a
/// null target; the window for the document's `load`). Returns whether it was
/// cancelled.
bool dom_bindings::dispatch(std::string_view type, node_id target) {
    if (cx_ == nullptr) { return false; }
    // Pending frame loads settle before the window's `load`: HTML delays the
    // load event until every subresource has finished, so a `load` handler
    // sees their onload handlers as already run
    // (css/cssom/HTML{Link,Style}Element-load-event).
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
    // "The end" (HTML 13.2.7) fires these two with its own flags, not an
    // engine event's: DOMContentLoaded bubbles and cannot be cancelled, and
    // the window's `load` does neither (the-end.html).
    const bool window_load = !target && type == "load";
    if (!target && type == "DOMContentLoaded") { object->set("cancelable", value::boolean(false)); }
    bool result = false;
    if (window_load) {
        // At the window, with the legacy target override flag: the path is the
        // window alone (an event that does not bubble never reaches it from
        // the Document) and `target` reads the Document.
        object->set("bubbles", value::boolean(false));
        object->set("cancelable", value::boolean(false));
        object->define(std::string{legacy_target_override_property}, value::boolean(true),
                       script::attr_none);
        const script::context::rooted keep{*cx_, event};
        result = dispatch_to(event, path_step{node_id{}, listen_on::window});
    } else {
        result = dispatch_event(type, target, event);
    }
    // Then, the first time the document is shown, `pageshow`: "if the
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

/// Reports an uncaught exception as an `error` event at the window. `node_id{}`
/// is the window-and-document bucket every global listener already lives in,
/// so this reaches `window.onerror` and `addEventListener("error", ...)` alike.
///
/// The `error` property is whatever the throw left behind, and `undefined` when
/// there was nothing to leave: a VM fault (the allocation ceiling, the call
/// stack ceiling) is a failure that was never an exception, and a synthetic
/// Error whose stack is a lie is worse than nothing. testharness.js reads
/// `e.error && e.error.stack` and falls back to filename:lineno:colno, so both
/// shapes are ones it understands. Returns whether a listener handled it.
bool dom_bindings::dispatch_error(std::string_view message) {
    return dispatch_error_value(message, value::undefined());
}

/// As above, naming the script the error came from (`script_src`).
bool dom_bindings::dispatch_error(std::string_view message, std::string_view script_src) {
    return dispatch_error_value(message, value::undefined(), script_src);
}

/// Reports an uncaught exception carrying the thrown value itself as
/// `event.error`, not only its text. Pages and tests depend on its identity:
/// EventListener-handleEvent.html rethrows `event.error` and compares it with
/// the object its getter threw.
bool dom_bindings::dispatch_error_value(std::string_view message, value error,
                                        std::string_view script_src) {
    if (cx_ == nullptr) { return false; }
    // Rooted across the allocations below: `error` arrives in a C++ local,
    // which the collector does not see, and building the event object
    // allocates, so an unrooted thrown object could be swept before the
    // listener reads it.
    const context::rooted keep_error{*cx_, error};
    value event = make_event(*cx_, "error", node_id{});
    auto * object = static_cast<script::object_object *>(event.as_heap());
    // An ErrorEvent, which is what makes the window's `onerror` take the five
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
    // one's, resolved). No position yet: the VM keeps none for a throw.
    std::string filename = location_href_;
    if (!script_src.empty() && !script_src.starts_with("<inline:")) {
        filename = location_href_.empty() ? std::string{script_src}
                                          : resolve(location_href_, std::string{script_src});
    }
    object->set("filename", cx_->string(filename));
    object->set("lineno", value::number(0));
    object->set("colno", value::number(0));
    object->set("error", error);
    // "In error reporting mode" for the length of the dispatch: an `error`
    // listener of this window that throws is not reported here again (see
    // listeners.cpp's report_fault).
    const bool was = std::exchange(error_reporting_, true);
    const bool handled = dispatch_event("error", node_id{}, event);
    error_reporting_ = was;
    return handled;
}

/// Dispatches `focus`/`blur` or `focusin`/`focusout` (UI Events 5.4.4):
/// FocusEvents whose `relatedTarget` is the other element of the change, the
/// first pair not bubbling and the second bubbling.
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

/// Dispatches `hashchange` at the window: a HashChangeEvent carrying both
/// addresses, bubbling as HTML fires it, not cancelable.
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

/// Dispatches a `keydown` or `keyup` KeyboardEvent for `input` at `target`.
bool dom_bindings::dispatch_key(std::string_view type, node_id target, const input_event & input) {
    if (cx_ == nullptr) { return false; }
    // An activation-triggering input event (HTML 6.4.3): a keydown that is not
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
    // Legacy `keyCode`/`which` are required in practice: Phaser's keyboard input
    // matches on keyCode (LEFT is 37). See dom_key_code.
    const int legacy = dom_key_code(input.key);
    object->set("keyCode", value::number(legacy));
    object->set("which", value::number(legacy));
    return dispatch_event(type, target, event);
}

/// Dispatches `keypress`, UI Events' legacy event for a key that produces a
/// character value: after its keydown and before the text's `beforeinput`, at
/// the same target, and cancelling it keeps the character out. `key` is the
/// character itself and keyCode, charCode and which its code point, as every
/// browser reports; `code` is not known by the time the text arrives.
bool dom_bindings::dispatch_keypress(node_id target, std::string_view character) {
    if (cx_ == nullptr || character.empty()) { return false; }
    value event = make_event(*cx_, "keypress", target);
    const context::rooted keep{*cx_, event};
    auto * object = static_cast<script::object_object *>(event.as_heap());
    std::size_t at = 0;
    const value point = value::number(static_cast<double>(decode_utf8(character, at)));
    object->set("key", cx_->string(std::string{character}));
    object->set("code", cx_->string(std::string{}));
    for (const char * flag : {"shiftKey", "ctrlKey", "altKey", "metaKey", "repeat"}) {
        object->set(flag, value::boolean(false));
    }
    object->set("keyCode", point);
    object->set("charCode", point);
    object->set("which", point);
    return dispatch_event("keypress", target, event);
}

/// Dispatches a `wheel` event before the default scroll, so a page can zoom,
/// scroll its own canvas, or cancel the page scroll (3D libraries such as
/// Babylon.js zoom on it).
///
/// deltaY is in pixels and its sign is the opposite of the engine's:
/// `wheel_y` is notches, positive meaning away from the user, while the DOM's
/// `deltaY` is positive when the content scrolls down. Getting that backwards
/// inverts every zoom.
///
/// The 100 is what a browser reports per notch in pixel mode (deltaMode 0),
/// and libraries divide by it: Babylon's default wheelPrecision is 3, so a
/// notch moves the camera about 33 units.
///
/// It is a WheelEvent, composed (UI Events 3.1: every mouse event a user agent
/// dispatches is), and cancelable only when something could cancel it, per UI
/// Events' "cancelability of wheel events": a user agent "should generate only
/// uncancelable wheel events when it observes that there are no non-passive
/// listeners for the event". The four passive-by-default targets make that the
/// common case, and non-cancelable-when-passive/ asserts both halves for each.
/// `mousewheel` counts, because a trusted `wheel` nobody listened for at a step
/// reaches its `mousewheel` listeners there (legacy_event_type_of).
bool dom_bindings::dispatch_wheel(node_id target, const input_event & input) {
    if (cx_ == nullptr) { return false; }
    value event = make_mouse_event(*cx_, "wheel", target, input, false);
    const context::rooted keep{*cx_, event};
    auto * object = static_cast<script::object_object *>(event.as_heap());
    adopt_interface(*cx_, *object, "WheelEvent");
    object->set("cancelable", value::boolean(blocking_listener(
                                  listeners_.entries(), propagation_path(step_at(target), true),
                                  {"wheel", "mousewheel"})));
    object->set("deltaX", value::number(input.wheel_x * 100.0));
    object->set("deltaY", value::number(-input.wheel_y * 100.0));
    object->set("deltaZ", value::number(0));
    object->set("deltaMode", value::number(0)); // 0 = pixels
    // The legacy `wheelDelta` too, which shipped code still reads. Its sign is
    // the opposite of deltaY's by definition (120 per notch).
    object->set("wheelDelta", value::number(input.wheel_y * 120.0));
    object->set("wheelDeltaX", value::number(-input.wheel_x * 120.0));
    object->set("wheelDeltaY", value::number(input.wheel_y * 120.0));
    object->set("buttons", value::number(0));
    return dispatch_event("wheel", target, event);
}

/// Dispatches a finger (Touch Events 1, and the `touch` pointer of Pointer
/// Events 3) the way a mouse press is dispatched: the pointer event first,
/// then the touch event, both composed. There is one finger, so identifier 0,
/// and every list holds the one Touch, except `touches` and `targetTouches` at
/// `touchend`, which hold what is still on the surface: nothing.
///
/// `target` is where the touch started, for all three phases: a Touch keeps
/// the element it began on for its whole life, however far the finger moves,
/// so a drag that leaves its element still belongs to that element.
///
/// touchstart and touchmove are cancelable only when a non-passive listener on
/// the path could cancel them, as for the wheel; touchend always is.
///
/// `pointer` false is a finger whose pointer was cancelled when its drag began
/// to pan: Pointer Events stops sending that pointer's events, and the touch
/// events go on. Phase "cancel" is that moment: the `pointercancel` alone,
/// bubbling and not cancelable.
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
    // Activation-triggering (HTML 6.4.3) is the lift for a finger: a non-mouse
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
    object->set("cancelable",
                value::boolean(end || blocking_listener(listeners_.entries(),
                                                        propagation_path(step_at(target), true),
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

/// Tells the page about a user's edit (Input Events 2, HTML 4.10.5.5):
///   * `beforeinput` before the value changes: an InputEvent, composed and
///     cancelable, and cancelling it keeps the edit out;
///   * `input` after it: the same InputEvent, not cancelable;
///   * a control's `input` with no inputType (a <select>'s): a plain Event,
///     bubbling and composed;
///   * `change` once the edit is committed: a plain Event that bubbles and
///     neither cancels nor composes.
/// `data` is the text inserted, null for a deletion. Returns whether a
/// listener cancelled it.
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

/// Applies the user's mouse selection to the page's Selection. A press on text
/// collapses `getSelection()` there, and a drag extends it, through the
/// Selection's own script surface (`collapse`, `setBaseAndExtent`), so its
/// validity rules, direction and selectionchange are the API's and not
/// restated here. A click leaves a directionless collapsed selection, which is
/// why the collapsed case is `collapse` rather than a setBaseAndExtent to the
/// same point.
///
/// The positions arrive as UTF-8 byte offsets into a text node's box, as the
/// painted selection keeps them, and leave as the DOM's UTF-16 offsets into
/// the node's own text. The box folded each whitespace run to one space unless
/// it preserves whitespace (box_builder::collapse_whitespace), so the node's
/// text is walked the same way first ("\n  Hello" shows as " Hello", and the
/// H at byte 1 of the box is at offset 3 of the node), and a position just
/// past a folded space lands past the whole run.
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
    // Ask the document for getSelection, as place_caret_on_focus does. The
    // window object does not hold the global one (that is the realm's
    // binding, which only the window view reads through to), so asking the
    // window would find nothing.
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

/// While `hold` is true (a finger is down), scrollend events are held, once
/// per target; releasing fires the held ones (see fire_scrollend).
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

/// Scrolls for a wheel notch or touch drag that no listener cancelled: the
/// nearest scroll container on the flat-tree path from the target that can
/// still move that way (scroll chaining, CSSOM View's "scroll an element" on
/// the first box that has room), or the viewport when none can, which is the
/// caller's to scroll. The root element and the body stand for the viewport
/// and end the walk. Returns whether an element took it.
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

/// CSSOM View §13.1 "run the scroll steps", steps 4-5: after the `scroll`
/// events, a `scrollend` at every target whose scroll completed. An instant
/// scroll, which is every scroll this engine makes, completes as it happens, so
/// the targets are exactly the ones that just heard `scroll`. At a Document it
/// bubbles, to the window; at an element it does not. Neither is cancelable.
///
/// A user's scroll completes when the user stops (CSSOM View: "if scrolling
/// was done by a user input, wait until the user input is done"), so while a
/// finger is down the targets are held, once each, and fired when it lifts.
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
        // A scroller removed while it scrolled never finishes its scroll
        // (scrollend-event-not-fired-after-removing-scroller.tentative.html).
        if (at && !is_connected(at)) { continue; }
        const value event = make_event(cx, "scrollend", at);
        auto * object = static_cast<script::object_object *>(event.as_heap());
        object->set("bubbles", value::boolean(!at));
        object->set("cancelable", value::boolean(false));
        (void)dispatch_event("scrollend", at, event);
    }
}

/// Makes a trusted MouseEvent (or PointerEvent when `pointer`) of `type` at
/// `target`, filled in from `input`.
value dom_bindings::make_mouse_event(context & cx, std::string_view type, node_id target,
                                     const input_event & input, bool pointer) {
    value event = make_event(cx, type, target);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    // A MouseEvent, not a plain Event: `instanceof MouseEvent` is how a page
    // tells the two apart, and it is how dispatch decides whether a `click` has
    // activation behaviour at all - dom/events/Event-dispatch-click.html's
    // "basic with wrong event class" sends a `new Event("click")` at a checkbox
    // and expects it not to toggle.
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
    // `buttons` is a mask of what is held, unlike `button`, which names the
    // one that changed. p5 reads it to notice a release it missed
    // (`mouseIsPressed && e.buttons === 0`), so without it the flag sticks.
    const bool down = type == "mousedown" || type == "pointerdown";
    object->set("buttons", value::number(down ? 1 << dom_button : 0));
    object->set("shiftKey", value::boolean(input.shift));
    object->set("ctrlKey", value::boolean(input.ctrl));
    if (pointer) {
        // One pointer, because there is one mouse. A page keyed on
        // pointerId (p5 keeps a map of active ones) needs it to be
        // stable, and needs the same id on down and up or the entry leaks.
        object->set("pointerId", value::number(1));
        object->set("pointerType", cx.string("mouse"));
        object->set("isPrimary", value::boolean(true));
        object->set("pressure", value::number(down ? 0.5 : 0));
    }
    return event;
}

/// Dispatches a mouse event of `type` for `input` at `target`, preceded by its
/// pointer event. Returns whether either was cancelled.
bool dom_bindings::dispatch_mouse(std::string_view type, node_id target,
                                  const input_event & input) {
    if (cx_ == nullptr) { return false; }
    // A mousedown is activation-triggering too (HTML 6.4.3 lists a mouse's
    // pointerdown).
    if (type == "mousedown") { notify_activation(); }
    // The matching pointer event fires first, as in browsers; many libraries
    // (p5.js 2.x) listen only for pointerdown/move/up.
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

/// `element.click()` (HTML "click()" method): dispatches one untrusted
/// MouseEvent down the same path an engine click takes, so dispatch_to owns
/// activation behaviour for both. Libraries rely on it (p5's save() clicks an
/// <a download>).
///
/// A disabled form control gets nothing, not even the event (step 1 of the
/// method), which is how click() differs from dispatchEvent(): the latter
/// reaches a disabled checkbox and toggles it, as Event-dispatch-click.html
/// asserts. `disabled` here is the control's own attribute or an enclosing
/// <fieldset>'s, as the browser's is_disabled says, outside that fieldset's
/// first <legend>.
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
            // The one exemption HTML 4.10.18.5 makes: a control inside a
            // disabled fieldset's first <legend> child is not disabled by it.
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
    // The click in progress flag, steps 2-4 of `click()`: a click() on an
    // element whose click() is still running returns without dispatching. A
    // <label>'s activation behaviour clicks the control it labels, and a
    // control with none of its own (a <textarea>) hands that click's
    // activation straight back to the label, which would otherwise click it
    // again forever (`<label>Notes <textarea>`).
    // ponytail: a thread-local stack of (bindings, element) stands in for the
    // flag, which only ever lives across one synchronous dispatch; a bit on
    // the element if that stack ever gets deep.
    static thread_local std::vector<std::pair<const dom_bindings *, node_id>> clicking;
    const std::pair<const dom_bindings *, node_id> self{this, target};
    if (std::ranges::find(clicking, self) != clicking.end()) { return false; }
    clicking.push_back(self);
    value event = make_mouse_event(*cx_, "click", target, input_event{}, false);
    // "With the not trusted flag set": it came from script, whoever asked.
    static_cast<script::object_object *>(event.as_heap())
        ->set(std::string{trusted_property}, value::boolean(false));
    const bool result = dispatch_to(event, path_step{target, listen_on::node});
    clicking.pop_back();
    return result;
}

/// True if `target` is in this document's tree (through shadow hosts).
bool dom_bindings::is_connected(node_id target) const {
    if (!target || doc_ == nullptr) { return false; }
    const auto txn = doc_->read();
    const node_id top = root_of_tree(txn, target, true);
    // The same test propagation_path makes: a parsed page's root is the <html>
    // element, and a created document still has its Document node.
    return top == txn.root() || txn.kind(top).value_or(node_kind::element) == node_kind::document;
}

/// Builds the propagation path of an event dispatched at `at`: the target,
/// then every ancestor, then the document and then the window. A parsed page
/// has no Document node (its tree root is <html>), so the document and window
/// steps are appended by hand. A document a page created does have a Document
/// node, and the root test below accepts either.
///
/// An empty target is the document. `browser::tick` dispatches `load` and
/// `DOMContentLoaded` that way, and testharness registers its error handler on
/// the window, so both buckets must be reached; making the document the target
/// keeps `document.addEventListener('DOMContentLoaded', ...)` firing. The
/// deviation is that a `load` listener on the document fires too, where a
/// browser fires that one at the window with the document only as the event's
/// target.
///
/// A shadow root's parent is its host (DOM "get the parent"), unless the event
/// is not composed and this is the root of the target's own tree. Only the
/// target's own root stops a non-composed event: one at a light child slotted
/// into a shadow tree travels through the slot, out of that tree to its host
/// and on up (Extensions-to-Event-Interface.html). A slotted node's next step
/// is its slot.
///
/// A detached tree ends at its own root: the document and window are appended
/// only when the walk reaches the document.
std::vector<dom_bindings::path_step> dom_bindings::propagation_path(path_step at,
                                                                    bool composed) const {
    std::vector<path_step> path;
    // A standalone EventTarget is the whole path. It is not in the tree, so
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
            // The flat tree, not the light-DOM chain: a slotted node's next step
            // is its assigned slot; DOM "get the parent" returns the assigned
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
                    // Same test as element/internal.hpp's is_document_root: a
                    // parsed page's root is the <html> element, and a created
                    // document still has its Document node.
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
    // A document a page made has no window above it: `new Document()`,
    // `createHTMLDocument()` and a clone of the page's document have a null
    // browsing context, so their path ends at the Document
    // (Event-dispatch-bubbles-{true,false}.html). A frame's document has one:
    // its own window (frames.cpp).
    if (window_.is_object()) { path.push_back(path_step{node_id{}, listen_on::window}); }
    return path;
}

/// Returns what `currentTarget` reports for one step, and what an `on<type>`
/// handler is looked up on. The window is the proxy rather than the object
/// behind it: a page compares `evt.currentTarget === window` by identity, and
/// `window` is the proxy.
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

/// Returns the path step a script object names as an EventTarget. The window
/// is recognised both as the proxy and as the object behind it, because
/// object_of_step hands out the proxy and a page can reach either; the
/// document likewise. Any other node maps to its own node step, and anything
/// else is a standalone object.
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
