//===- events/handler_attributes.cpp - on<event> handlers -------*- C++ -*-===//
//
// HTML event handlers (HTML 8.1.8): the `on<name>` IDL attributes on elements,
// documents and windows, compiling an `onclick="..."` content attribute into a
// function, registering a handler's listener entry at the point it is set,
// and calling a handler with HTML's argument and return-value rules.
//
//===----------------------------------------------------------------------===//
#include "internal.hpp"

#include "handler_names.hpp"
#include <ctbrowser/script/compile.hpp>

namespace ctbrowser::shell {

using namespace detail;

/// Returns the event type an `on<name>` handler is for: the name without `on`,
/// lowercased, except for the four prefixed families' camelCase spelling
/// (`onwebkitanimationend` is for `webkitAnimationEnd`).
[[nodiscard]] std::string event_type_of_handler(std::string_view name) {
    const std::string_view bare = name.substr(2);
    for (const std::string_view legacy : {"webkitAnimationEnd", "webkitAnimationIteration",
                                          "webkitAnimationStart", "webkitTransitionEnd"}) {
        if (ascii_iequals(bare, legacy)) { return std::string{legacy}; }
    }
    return ascii_lower_copy(bare);
}

/// True if the handler for `type` at `at` already has a listener entry.
bool dom_bindings::has_handler_listener(path_step at, std::string_view type) const {
    return listeners_.has_handler(listener_target_of(at), type);
}

/// HTML "activate an event handler": adds the handler's placeholder listener
/// entry, so the handler runs at the position among the listeners where it was
/// first set. Does nothing if it already has one.
void dom_bindings::activate_event_handler(context & cx, path_step at, std::string_view type) {
    if (has_handler_listener(at, type)) { return; }
    listener made;
    made.target = listener_target_of(at);
    made.payload.host = at.host;
    made.type = std::string{type};
    made.handler = true;
    // "Add an event listener" computes the default passive value for it like
    // any other (HTML 8.1.8.1 adds it without saying): `document.body.onwheel`
    // is as passive as an addEventListener("wheel") there.
    made.passive = default_passive_value(made.type, at);
    // A do-nothing native that gives the entry an identity of its own; the
    // listener list traces it. Dispatch calls the handler property instead.
    made.payload.callback = value::object(cx.allocate<script::native_object>(
        "event handler", [](context &, std::span<value>) { return value::undefined(); }));
    add_listener(std::move(made));
}

/// HTML "deactivate an event handler": removes the handler's listener entry.
void dom_bindings::deactivate_event_handler(path_step at, std::string_view type) {
    listeners_.remove_handler(listener_target_of(at), type);
}

namespace {

/// The GlobalEventHandlers IDL attributes (HTML 8.1.7.2).
///
/// HTML requires all of these on every one of these objects, whether or not
/// this engine ever dispatches the event: `'onanimationend' in el` is true in
/// every browser, and hiding the property would not make the event fire.
/// `ontouchstart` is deliberately left out: it is the name a touch capability
/// check asks about, and this engine has no touch input.
constexpr std::string_view global_event_handlers[] = {"onabort",
                                                      "onanimationcancel",
                                                      "onanimationend",
                                                      "onanimationiteration",
                                                      "onanimationstart",
                                                      "onauxclick",
                                                      "onbeforeinput",
                                                      "onbeforematch",
                                                      "onbeforetoggle",
                                                      "onblur",
                                                      "oncancel",
                                                      "oncanplay",
                                                      "oncanplaythrough",
                                                      "onchange",
                                                      "onclick",
                                                      "onclose",
                                                      "oncommand",
                                                      "oncontextlost",
                                                      "oncontextmenu",
                                                      "oncontextrestored",
                                                      "oncopy",
                                                      "oncuechange",
                                                      "oncut",
                                                      "ondblclick",
                                                      "ondrag",
                                                      "ondragend",
                                                      "ondragenter",
                                                      "ondragleave",
                                                      "ondragover",
                                                      "ondragstart",
                                                      "ondrop",
                                                      "ondurationchange",
                                                      "onemptied",
                                                      "onended",
                                                      "onerror",
                                                      "onfocus",
                                                      "onformdata",
                                                      "ongotpointercapture",
                                                      "oninput",
                                                      "oninvalid",
                                                      "onkeydown",
                                                      "onkeypress",
                                                      "onkeyup",
                                                      "onload",
                                                      "onloadeddata",
                                                      "onloadedmetadata",
                                                      "onloadstart",
                                                      "onlostpointercapture",
                                                      "onmousedown",
                                                      "onmouseenter",
                                                      "onmouseleave",
                                                      "onmousemove",
                                                      "onmouseout",
                                                      "onmouseover",
                                                      "onmouseup",
                                                      "onpaste",
                                                      "onpause",
                                                      "onplay",
                                                      "onplaying",
                                                      "onpointercancel",
                                                      "onpointerdown",
                                                      "onpointerenter",
                                                      "onpointerleave",
                                                      "onpointermove",
                                                      "onpointerout",
                                                      "onpointerover",
                                                      "onpointerup",
                                                      "onprogress",
                                                      "onratechange",
                                                      "onreset",
                                                      "onresize",
                                                      "onscroll",
                                                      "onscrollend",
                                                      "onsecuritypolicyviolation",
                                                      "onseeked",
                                                      "onseeking",
                                                      "onselect",
                                                      "onslotchange",
                                                      "onstalled",
                                                      "onsubmit",
                                                      "onsuspend",
                                                      "ontimeupdate",
                                                      "ontoggle",
                                                      "ontransitioncancel",
                                                      "ontransitionend",
                                                      "ontransitionrun",
                                                      "ontransitionstart",
                                                      "onvolumechange",
                                                      "onwaiting",
                                                      "onwebkitanimationend",
                                                      "onwebkitanimationiteration",
                                                      "onwebkitanimationstart",
                                                      "onwebkittransitionend",
                                                      "onwheel"};

// Where a handler is actually kept. There are three hidden slots per name,
// each answering a different question:
//
//   * `__on<name>` - what the IDL attribute was assigned. Its presence is the
//     state, not its value: `el.onclick = null` is a null that has been set,
//     and HTML says an assignment deactivates the content attribute's handler,
//     so a present null must not fall back to the markup.
//   * `__onsrc<name>` - the content attribute's text as it was when it was last
//     compiled, so a changed attribute recompiles and an unchanged one does not.
//   * `__onfn<name>`  - the function that text compiled to. `el.onclick ===
//     el.onclick` has to hold, and it would not if each read compiled again.
//
// All three are `attr_none`, so a page enumerating the object never sees them.

/// The slot holding what the IDL attribute was assigned.
[[nodiscard]] std::string assigned_slot(const std::string & name) {
    return "__on" + name;
}
/// The slot holding the content attribute text last compiled.
[[nodiscard]] std::string source_slot(const std::string & name) {
    return "__onsrc" + name;
}
/// The slot holding the function that text compiled to.
[[nodiscard]] std::string compiled_slot(const std::string & name) {
    return "__onfn" + name;
}

} // namespace

/// Compiles the content attribute `name` of `self` (`onclick="doThing()"`) into
/// its handler function, or returns undefined when there is no attribute.
///
/// HTML calls what the markup leaves behind an "internal raw uncompiled
/// handler": the attribute's value is a function body, and it becomes a
/// function the first time anything asks for the handler. The parameter is
/// named `event`, which is what makes `onclick="alert(event.type)"` work.
///
/// The result is cached only while the attribute text is unchanged. The
/// specification compiles when the attribute is set; this engine has no
/// attribute-change hook here, so the source text is kept beside the function
/// and a different text compiles again (one string compare per read).
///
/// A handler that does not compile is null, as HTML says: the uncompiled
/// handler is discarded and the attribute reads null, rather than the page
/// getting a SyntaxError from a read.
value dom_bindings::compile_handler_attribute(context & cx, value self, const std::string & name) {
    const node_id id = handle_of(self);
    if (!id || doc_ == nullptr || atoms_ == nullptr) { return value::undefined(); }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    std::string source;
    {
        const auto txn = doc_->read();
        const atom key = atoms_->intern_lower(name);
        if (!txn.has_attribute(id, key)) {
            // The attribute is gone, so anything compiled from it goes too;
            // `Body-FrameSet-Event-Handlers.html` calls removeAttribute at the
            // end of every reflection case and the next case reads null.
            (void)object->erase(source_slot(name));
            (void)object->erase(compiled_slot(name));
            return value::undefined();
        }
        source = std::string{txn.attribute_value(id, key)};
    }
    if (const value * had = object->find(source_slot(name));
        had != nullptr && had->is_string() && cx.to_string(*had) == source) {
        const value * made = object->find(compiled_slot(name));
        return made == nullptr ? value::undefined() : *made;
    }
    // The scope chain, HTML 8.1.8.1 "getting the current value of the event
    // handler" step 10: the realm's global, then the element's node document,
    // then its form owner when it has one, then the element itself - each an
    // object environment, which is what `with` makes. That is why
    // `onclick="remove()"` reaches `window.remove` and not the element's
    // method (Element.prototype[@@unscopables] vetoes it, install_node_methods)
    // while `onclick="title"` reads the document's. The two outer objects are
    // parameters rather than free names so a frame document's element gets its
    // own document, not the page's global one.
    //
    // ponytail: every free identifier in the body costs a `has` trap on
    // the document proxy, which walks the tree for named items; an id/name
    // index on the document is the upgrade if a handler-heavy page shows it.
    value form = value::null();
    {
        const auto txn = doc_->read();
        const std::string_view local = txn.local_name(id);
        if (txn.element_ns(id) == node_ns::html &&
            (local == "button" || local == "fieldset" || local == "input" || local == "object" ||
             local == "output" || local == "select" || local == "textarea")) {
            form = cx.lookup_property(self, "form");
        }
    }
    const bool with_form = form.is_object_like();
    // The `with` blocks enclose the function rather than sitting inside it:
    // the scope chain is the function's outer environment (step 10 builds it
    // before step 11 makes the function), so a parameter or local of the
    // handler shadows the element's properties and not the other way round.
    // (Inside the body, the `with` blocks would shadow `event` with the
    // window's current event.) A Window's `onerror` takes the five parameters
    // (event, source, lineno, colno, error), step 11's one special case.
    const bool window_error =
        name == "onerror" && forwards_to_window(name) && body_or_frameset_of(self);
    // Named after the attribute: step 11's source text is `function
    // onclick(event) {\n<body>\n}`, which is what `handler.toString()` and
    // `handler.name` answer (event-handler-sourcetext.html). ponytail: a
    // named function expression binds its name inside the body, which the
    // specification's function does not; `onclick="onclick = f"` would write
    // that binding (silently, in sloppy code) rather than the element's.
    const bool identifier = std::ranges::all_of(name, [](const char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
               ch == '_' || ch == '$';
    });
    // Compiled in the element's document's realm, so a frame's `onload="run()"`
    // calls the frame's `run` whoever reads the handler first.
    const auto in_realm = enter_realm(cx);
    script::program compiled = script::compiler::compile(
        std::string{"return (function (document, form) { with (document) { "} +
        (with_form ? "with (form) { " : "") + "with (this) { return function " +
        (identifier ? name : std::string{}) + "(" +
        (window_error ? "event, source, lineno, colno, error" : "event") + ") {\n" + source +
        "\n}; }" + (with_form ? " }" : "") + " } });");
    value made = value::undefined();
    if (compiled.ok) {
        const value outer = cx.run_nested(cx.own_program(std::move(compiled)));
        const value scope[] = {document_, form};
        // `this` of the outer call is the element: the innermost object
        // environment of the chain.
        if (outer.is_callable()) { made = cx.call(outer, scope, self); }
    } else {
        // Step 11's "if body is not parsable... report the exception": the
        // window hears the SyntaxError, as it hears a script's, and the
        // handler is null (compile-error-in-attribute.html).
        (void)dispatch_error("uncaught SyntaxError: " + compiled.error);
    }
    object->define(source_slot(name), cx.string(source), script::attr_none);
    object->define(compiled_slot(name), made, script::attr_none);
    return made;
}

/// Returns the body or frameset element `self` wraps, or a null id.
node_id dom_bindings::body_or_frameset_of(value self) {
    const node_id id = handle_of(self);
    if (!id || doc_ == nullptr) { return {}; }
    const auto txn = doc_->read();
    const atom tag = txn.tag(id).value_or(atom{});
    const bool is =
        txn.element_ns(id) == node_ns::html &&
        (tag == atoms_->intern_lower("body") || tag == atoms_->intern_lower("frameset"));
    return is ? id : node_id{};
}

/// True for a forwarded handler of a body or frameset whose document has no
/// window (a DOMParser's, createHTMLDocument's): "determining the target of an
/// event handler" (8.1.8.1) finds no target, so the getter answers null and the
/// setter does nothing. Asked of the element's own document's bindings, since
/// the accessors are this document's and the element may be another's.
bool dom_bindings::windowless_forward(value self, std::string_view name) {
    if (!forwards_to_window(name)) { return false; }
    dom_bindings & owner = target_owner(self);
    return owner.window_object() == nullptr && owner.body_or_frameset_of(self);
}

/// Copies a body/frameset's forwarded content attribute onto the window's
/// handler. This happens lazily: there is
// no attribute-change hook, so the element's `on<name>` text is compared with
/// what was last compiled from it on every read of the handler (through the
/// element or through the window); a changed text is compiled onto the
/// window's slot, a removed one clears it. The element that last supplied the
/// window's handler is remembered so its removal is seen from the window side
/// too, and every body or frameset a script has a wrapper for (a detached
/// `createElement("body")` included) is checked when the window's handler is
/// read, since that is where its content attribute lands. A real hook in
/// setAttribute would replace all of this.
void dom_bindings::refresh_forwarded_handler(context & cx, node_id element,
                                             const std::string & name) {
    auto * window = window_object();
    if (window == nullptr || !element) { return; }
    const auto in_realm = enter_realm(cx);
    const value self = wrap(cx, element);
    if (!self.is_object()) { return; }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    const value * before = object->find(source_slot(name));
    const bool had = before != nullptr;
    const std::string was = had && before->is_string() ? cx.to_string(*before) : "";
    const value made = compile_handler_attribute(cx, self, name);
    const value * after = object->find(source_slot(name));
    const auto supplier = forwarded_from_.find(name);
    if (after == nullptr) {
        // Gone. If it was this element's text the window was running, the
        // window's handler goes with it ("deactivate an event handler").
        if (had && supplier != forwarded_from_.end() && supplier->second == element) {
            window->define(assigned_slot(name), value::null(), script::attr_none);
            cx.define_global(name, value::null());
            forwarded_from_.erase(supplier);
        }
        return;
    }
    if (had && cx.to_string(*after) == was) { return; } // unchanged since last seen
    const value handler = made.is_callable() ? made : value::null();
    window->define(assigned_slot(name), handler, script::attr_none);
    cx.define_global(name, handler);
    forwarded_from_[name] = element;
}

/// The `on<name>` getter. Null when unset, where "unset" is the absence of the
/// assigned slot rather than a null in it (see assigned_slot).
value dom_bindings::event_handler_get(context & cx, value self, const std::string & name) {
    if (!self.is_object()) { return value::null(); }
    if (windowless_forward(self, name)) { return value::null(); }
    const auto in_realm = enter_realm(cx);
    auto * object = static_cast<script::object_object *>(self.as_heap());
    if (forwards_to_window(name) && window_object() != nullptr) {
        if (object == window_object()) {
            // The bodies a script can have written to: the document's own,
            // and every wrapped one. The wrapped list is rebuilt only when a
            // wrapper has been made since it was last built.
            if (wrappers_.size() != bodies_scanned_at_) {
                bodies_scanned_at_ = wrappers_.size();
                bodies_.clear();
                const auto txn = doc_->read();
                const atom body = atoms_->intern_lower("body");
                const atom frameset = atoms_->intern_lower("frameset");
                for (const auto & [packed, wrapper] : wrappers_) {
                    const node_id node = unpack(packed);
                    if (!txn.contains(node)) { continue; }
                    const atom tag = txn.tag(node).value_or(atom{});
                    if ((tag == body || tag == frameset) && txn.element_ns(node) == node_ns::html) {
                        bodies_.push_back(node);
                    }
                }
            }
            if (const node_id body = find_by_tag("body");
                body && std::ranges::find(bodies_, body) == bodies_.end()) {
                refresh_forwarded_handler(cx, body, name);
            }
            for (const node_id body : bodies_) { refresh_forwarded_handler(cx, body, name); }
        } else if (const node_id element = body_or_frameset_of(self)) {
            refresh_forwarded_handler(cx, element, name);
            object = window_object();
        }
    }
    if (const value * held = object->find(assigned_slot(name))) { return *held; }
    if (const value made = compile_handler_attribute(cx, self, name); made.is_callable()) {
        return made;
    }
    // On the window only: the globals table and the window object are separate
    // storage here, so `onerror = f` (a bare global) is read back from the
    // globals when the window slot is empty.
    if (object == window_object() && cx.has_global(name)) {
        if (const value held = cx.global(name); held.is_callable()) { return held; }
    }
    return value::null();
}

/// The `on<name>` setter, which follows one WebIDL annotation.
///
/// `EventHandler` is `[LegacyTreatNonObjectAsNull] callback
/// EventHandlerNonNull?`: anything that is not an Object becomes null. So
/// `el.onclick = ""` and `el.onclick = 42` both store null and read back null
/// (Body-FrameSet-Event-Handlers.html checks this).
///
/// An object that is not callable is stored as it is, since only a non-Object
/// is nulled. It throws when it is eventually invoked, which is where the
/// specification puts the failure.
void dom_bindings::event_handler_set(context & cx, value self, const std::string & name,
                                     value given) {
    if (!self.is_object() || windowless_forward(self, name)) { return; }
    const auto in_realm = enter_realm(cx);
    auto * object = static_cast<script::object_object *>(self.as_heap());
    // `body.onload = f` is `window.onload = f`; see forwards_to_window.
    if (forwards_to_window(name) && window_object() != nullptr && body_or_frameset_of(self)) {
        object = window_object();
    }
    const value stored = given.is_object_like() ? given : value::null();
    object->define(assigned_slot(name), stored, script::attr_none);
    // The assignment also discards the content attribute's handler. Dropping
    // the compiled copy is what makes a later read see the assignment rather
    // than the markup.
    (void)object->erase(source_slot(name));
    (void)object->erase(compiled_slot(name));
    if (object == window_object()) { cx.define_global(name, stored); }
    // The handler's listener entry is registered now, or dropped for null:
    // its place among addEventListener's listeners is where it was first set
    // (event-handler-spec-example.window.js).
    dom_bindings & owner = target_owner(self);
    const path_step at = owner.step_of(object == window_object() ? value::object(object) : self);
    const std::string type = event_type_of_handler(name);
    if (stored.is_null()) {
        owner.deactivate_event_handler(at, type);
    } else {
        owner.activate_event_handler(cx, at, type);
    }
}

/// Processes the attribute writes the document logged since the last mutation.
/// An `on*` content attribute activates its handler's listener the moment it is
/// set (HTML 8.1.8.1, the attribute change steps), and deactivates it when it
/// goes with no IDL handler assigned in its place. A <details> whose `open`
/// came or went queues its toggle event (4.11.1).
void dom_bindings::settle_attribute_writes(const std::vector<document::write_note> & writes) {
    if (cx_ == nullptr || doc_ == nullptr) { return; }
    for (const document::write_note & note : writes) {
        if (note.kind != document::write_note::edit::attribute || !note.node) { continue; }
        const std::string_view name = atoms_->text(note.name);
        // ponytail: a removal does not say its namespace, so a namespaced
        // attribute qualified `open` toggles too.
        if (name == "open" && note.existed == note.removal) {
            const auto txn = doc_->read();
            if (txn.contains(note.node) && txn.element_ns(note.node) == node_ns::html &&
                txn.local_name(note.node) == "details") {
                const attribute * held = txn.find_attribute(note.node, note.name);
                if (held == nullptr || !held->ns) {
                    queue_details_toggle(note.node, note.existed, !note.removal);
                }
            }
            continue;
        }
        if (name.size() < 3 || name[0] != 'o' || name[1] != 'n') { continue; }
        bool present = false;
        {
            const auto txn = doc_->read();
            if (!txn.contains(note.node) ||
                txn.kind(note.node).value_or(node_kind::element) != node_kind::element) {
                continue;
            }
            present = txn.has_attribute(note.node, note.name);
        }
        // A body/frameset's window-forwarded handler belongs to the window step.
        const value self = unwrap_legacy_proxy(wrap(*cx_, note.node));
        path_step at{note.node, listen_on::node};
        auto * object = static_cast<script::object_object *>(self.as_heap());
        if (forwards_to_window(name) && window_object() != nullptr && body_or_frameset_of(self)) {
            at = path_step{node_id{}, listen_on::window};
            object = window_object();
        }
        const std::string type = event_type_of_handler(name);
        if (present) {
            // The attribute's value is now the handler (8.1.8.1's attribute
            // change steps): an earlier IDL assignment such as
            // `el.onclick = null` no longer shadows the markup, and the same
            // text set again compiles again (event-handler-removal.window.js).
            (void)object->erase(assigned_slot(std::string{name}));
            (void)object->erase(source_slot(std::string{name}));
            (void)object->erase(compiled_slot(std::string{name}));
            activate_event_handler(*cx_, at, type);
        } else if (const value * assigned = object->find(assigned_slot(std::string{name}));
                   assigned == nullptr || assigned->is_null()) {
            deactivate_event_handler(at, type);
        }
    }
}

/// "Queue a details toggle event task" (HTML 4.11.1): a ToggleEvent at the
/// element in a later task, and only one per burst. A toggle still queued is
/// cancelled and its old state carried into the new one, so open-then-close in
/// one script fires once, "closed" to "closed". It is queued on the primary
/// document's timers, which are the ones the tick drains.
void dom_bindings::queue_details_toggle(node_id details, bool was_open, bool open) {
    dom_bindings & top = primary();
    const auto held = std::ranges::find(details_toggles_, details, &details_toggle::details);
    if (held != details_toggles_.end()) {
        was_open = held->was_open;
        top.timer_schedule_.cancel(held->task);
        details_toggles_.erase(held);
    }
    const auto state = [](bool is_open) { return is_open ? "open" : "closed"; };
    const std::uint32_t task = top.add_timer(
        native(*cx_, "details toggle",
               [this, details, old_state = state(was_open),
                new_state = state(open)](context & c, std::span<value>) {
                   std::erase_if(details_toggles_, [details](const details_toggle & d) {
                       return d.details == details;
                   });
                   const value event = make_event(c, "toggle", details);
                   auto * object = static_cast<script::object_object *>(event.as_heap());
                   object->set("bubbles", value::boolean(false));
                   object->set("cancelable", value::boolean(false));
                   object->set("oldState", c.string(old_state));
                   object->set("newState", c.string(new_state));
                   if (const value ctor = c.has_global("ToggleEvent") ? c.global("ToggleEvent")
                                                                      : value::undefined();
                       ctor.is_object_like()) {
                       if (const value proto = c.lookup_property(ctor, "prototype");
                           proto.is_object()) {
                           object->prototype = proto;
                       }
                   }
                   (void)dispatch_event("toggle", details, event);
                   return value::undefined();
               }),
        0, false);
    details_toggles_.push_back(details_toggle{details, task, was_open});
}

/// Installs every event handler IDL attribute on every object HTML puts it on
/// (and, first, the element reflection accessors that need the same
/// interface prototypes).
///
/// There is one pair of natives per name, shared by all hosts: the accessors
/// find their object through `this`, so HTMLElement.prototype and
/// Document.prototype need not hold different functions for `onclick`.
///
/// The window's accessors go on the window object itself rather than on
/// Window.prototype. The window is reached through a Proxy whose `set` trap
/// writes a global for any name the target does not already own, so with an
/// inherited accessor `window.onload = fn` would define a global and never run
/// the setter.
///
/// <body> and <frameset> also get the window's own: both interfaces include
/// WindowEventHandlers, and there those (like the six window-reflecting names
/// they inherit from HTMLElement) read and write the window's handler
/// (forwards_to_window; refresh_forwarded_handler carries a content attribute
/// across).
void dom_bindings::install_event_handler_attributes(context & cx) {
    // The interface prototypes are normally built on the first `wrap()`, which
    // comes after this, so build them now. It is a no-op if they already exist,
    // and without it `interface_prototype` would answer nothing this early.
    ensure_dom_interfaces(cx);
    install_frame_accessors(cx); // the prototypes exist now, and this is the first to need one
    install_element_reflection(cx);
    install_double_reflection(cx);
    install_option_reflection(cx);
    install_form_owner(cx);
    std::vector<script::object_object *> hosts;
    for (const std::string_view interface : {"HTMLElement", "SVGElement", "Document"}) {
        if (const value proto = interface_prototype(interface); proto.is_object()) {
            hosts.push_back(static_cast<script::object_object *>(proto.as_heap()));
        }
    }
    const auto install = [&](std::string_view attribute, std::span<script::object_object *> where) {
        install_handler_accessor(cx, attribute, where);
    };
    for (const std::string_view attribute : global_event_handlers) { install(attribute, hosts); }
    // The Document's own (HTML 3.1.3 and the pointer lock, fullscreen and
    // selection specifications): null on the prototype until assigned.
    if (const value proto = interface_prototype("Document"); proto.is_object()) {
        auto * document_proto = static_cast<script::object_object *>(proto.as_heap());
        for (const std::string_view attribute :
             {"onreadystatechange", "onvisibilitychange", "onselectionchange", "onfullscreenchange",
              "onfullscreenerror", "onpointerlockchange", "onpointerlockerror"}) {
            install(attribute, std::span<script::object_object *>{&document_proto, 1});
        }
    }
    std::vector<script::object_object *> window_hosts;
    for (const std::string_view interface : {"HTMLBodyElement", "HTMLFrameSetElement"}) {
        if (const value proto = interface_prototype(interface); proto.is_object()) {
            window_hosts.push_back(static_cast<script::object_object *>(proto.as_heap()));
        }
    }
    for (const std::string_view attribute : window_event_handlers) {
        install(attribute, window_hosts);
    }
    if (auto * window = window_object()) { install_window_handlers(cx, *window); }
}

/// Defines one `on<name>` accessor pair over this document's handler storage on
/// each object in `where`.
void dom_bindings::install_handler_accessor(context & cx, std::string_view attribute,
                                            std::span<script::object_object *> where) {
    const std::string name{attribute};
    const value getter = value::object(
        cx.allocate<script::native_object>(name, [this, name](context & c, std::span<value>) {
            return event_handler_get(c, c.current_this(), name);
        }));
    const value setter = value::object(
        cx.allocate<script::native_object>(name, [this, name](context & c, std::span<value> a) {
            event_handler_set(c, c.current_this(), name, a.empty() ? value::undefined() : a[0]);
            return value::undefined();
        }));
    for (script::object_object * host : where) { host->define_accessor(name, getter, setter); }
}

/// Installs a window's handlers (GlobalEventHandlers and WindowEventHandlers)
/// over this document's storage: the page's window from install, a frame's from
/// install_frame_realm, so `<body onload>` in a frame is the frame window's.
void dom_bindings::install_window_handlers(context & cx, script::object_object & window) {
    script::object_object * host = &window;
    for (const std::string_view attribute : global_event_handlers) {
        install_handler_accessor(cx, attribute, std::span<script::object_object *>{&host, 1});
    }
    for (const std::string_view attribute : window_event_handlers) {
        install_handler_accessor(cx, attribute, std::span<script::object_object *>{&host, 1});
    }
}

/// Calls the `on<type>` handler property of `target` for `event`, applying
/// HTML's return-value rule. Returns true if the handler threw; the thrown
/// value goes to `*thrown`.
///
/// A handler normally runs as an ordinary listener at the position where it
/// was first set (activate_event_handler). This function is also the fallback
/// for a handler that never got a listener, such as a parsed attribute the
/// write log missed; dispatch runs that one after the listeners, in the bubble
/// phase (see listeners.cpp).
bool dom_bindings::fire_handler_property(value target, std::string_view type, value event,
                                         value * thrown) {
    value caught = value::undefined();
    const auto answer = [&](bool threw) {
        if (thrown != nullptr) { *thrown = caught; }
        return threw;
    };
    // is_object_like(), not is_object(): the window is a Proxy, which
    // is_object() rejects, and its handlers must still fire.
    if (cx_ == nullptr || !target.is_object_like()) { return answer(false); }
    // Handler attribute names are all lowercase even when the event type is not
    // (`onwebkitanimationend` handles `webkitAnimationEnd`), so the type is
    // lowercased before lookup. Only IDL-declared names are handlers, so
    // `el.onMyEvent` is not one for a `MyEvent` dispatch.
    const value handler = cx_->lookup_property(target, "on" + ascii_lower_copy(type));
    if (!handler.is_callable()) { return answer(false); }
    // HTML "processing the return value": a handler that returns exactly
    // `false` cancels the event (`<a onclick="return false">`). The window's
    // `onerror` is the one inversion, where `true` means "I reported it" and
    // cancels instead.
    //
    // Exactly `false`, not merely falsy: `return 0` and `return ""` do not
    // cancel in any browser, because the rule names the boolean.
    value returned = value::undefined();
    const auto process_return = [this, event, &returned](bool cancel_on_true) {
        if (!returned.is_boolean() || returned.as_boolean() != cancel_on_true) { return; }
        // "Cancel the event" is the DOM's set-the-canceled-flag, so it obeys
        // the same two refusals `preventDefault` does: an uncancellable event
        // cannot be cancelled and a passive listener may not try.
        if (!context::truthy(cx_->lookup_property(event, "cancelable")) ||
            flag_of(*cx_, event, passive_property)) {
            return;
        }
        static_cast<script::object_object *>(event.as_heap())
            ->set("defaultPrevented", value::boolean(true));
    };
    // `window.onerror` is the one handler that is not handed its event.
    //
    // HTML's OnErrorEventHandler takes (message, filename, lineno, colno,
    // error), five positional arguments with the first a string, and shipped
    // code is written against that signature:
    // `window.onerror = function (msg, url, line) { report(msg) }`.
    // dom/events/Event-dispatch-throwing.html asserts `typeof e === 'string'`.
    //
    // Only at the window, only for `error`, and only for an ErrorEvent (HTML
    // 8.1.8.1 step 4): an `onerror` on an <img> is an ordinary event handler
    // and takes the event, and so is the window's for a plain
    // `new Event("error")` a page dispatched - it gets that event, and it is
    // `false`, not `true`, that cancels it (window-synthetic-event.html).
    const auto is_error_event = [this, event] {
        const value ctor =
            cx_->has_global("ErrorEvent") ? cx_->global("ErrorEvent") : value::undefined();
        const value proto =
            ctor.is_object_like() ? cx_->lookup_property(ctor, "prototype") : value::undefined();
        if (!proto.is_object() || !event.is_object()) { return false; }
        for (value at = static_cast<script::object_object *>(event.as_heap())->prototype;
             at.is_object(); at = static_cast<script::object_object *>(at.as_heap())->prototype) {
            if (at.bits() == proto.bits()) { return true; }
        }
        return false;
    };
    if (type == "error" &&
        target.bits() == object_of_step(*cx_, path_step{node_id{}, listen_on::window}).bits() &&
        is_error_event()) {
        // The five arguments go in an array, which the fence spreads, so the
        // handler still runs behind the same `try` every listener does.
        // `window.onerror` is the handler a page is most likely to throw out
        // of, and
        // window-event-restored-after-throwing-onerror.html throws out of it on
        // purpose.
        const value arguments = cx_->make_array();
        auto * items = static_cast<script::array_object *>(arguments.as_heap());
        items->items.push_back(cx_->lookup_property(event, "message"));
        items->items.push_back(cx_->lookup_property(event, "filename"));
        items->items.push_back(cx_->lookup_property(event, "lineno"));
        items->items.push_back(cx_->lookup_property(event, "colno"));
        items->items.push_back(cx_->lookup_property(event, "error"));
        const bool threw = invoke_listener(*cx_, handler, target, arguments, caught, returned);
        process_return(/*cancel_on_true*/ true);
        return answer(threw);
    }
    const bool threw = invoke_listener(*cx_, handler, target, event, caught, returned);
    process_return(/*cancel_on_true*/ false);
    return answer(threw);
}

} // namespace ctbrowser::shell
