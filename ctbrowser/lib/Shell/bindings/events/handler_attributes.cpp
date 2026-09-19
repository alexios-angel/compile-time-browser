#include "internal.hpp"

#include "handler_names.hpp"
#include <ctbrowser/script/compile.hpp>

namespace ctbrowser::shell {

using namespace detail;

// The event type an `on<name>` handler is for: the name without `on`, in the
// four prefixed families' camelCase spelling (`onwebkitanimationend` is for
// `webkitAnimationEnd`).
[[nodiscard]] std::string event_type_of_handler(std::string_view name) {
    const std::string_view bare = name.substr(2);
    for (const std::string_view legacy : {"webkitAnimationEnd", "webkitAnimationIteration",
                                          "webkitAnimationStart", "webkitTransitionEnd"}) {
        if (ascii_iequals(bare, legacy)) { return std::string{legacy}; }
    }
    return ascii_lower_copy(bare);
}

bool dom_bindings::has_handler_listener(path_step at, std::string_view type) const {
    for (const listener & l : listeners_) {
        if (!l.handler || l.on != at.on || l.type != type) { continue; }
        if (l.on == listen_on::node && l.target != at.node) { continue; }
        if (l.on == listen_on::object && l.host.bits() != at.host.bits()) { continue; }
        return true;
    }
    return false;
}

void dom_bindings::activate_event_handler(context & cx, path_step at, std::string_view type) {
    if (has_handler_listener(at, type)) { return; }
    listener made;
    made.target = at.node;
    made.on = at.on;
    made.host = at.host;
    made.type = std::string{type};
    made.handler = true;
    // An identity of its own, traced through the listener list.
    made.callback = value::object(cx.allocate<script::native_object>(
        "event handler", [](context &, std::span<value>) { return value::undefined(); }));
    listeners_.push_back(std::move(made));
}

void dom_bindings::deactivate_event_handler(path_step at, std::string_view type) {
    std::erase_if(listeners_, [&](const listener & l) {
        if (!l.handler || l.on != at.on || l.type != type) { return false; }
        if (l.on == listen_on::node && l.target != at.node) { return false; }
        if (l.on == listen_on::object && l.host.bits() != at.host.bits()) { return false; }
        return true;
    });
}

namespace {

// THE EVENT HANDLER IDL ATTRIBUTES, HTML 8.1.7.2, as one list per mixin.
//
// A REVERSAL, and it is worth naming because the opposite rule is written down
// in element/methods.cpp: that file installs a handler property only for
// "exactly the events this engine can dispatch, and no more", on the grounds
// that a detection answering yes for an event that never fires is worse than
// one answering no. That reasoning holds for a page choosing between two
// spellings of the same event - which is the case it was written for,
// `onwheel` against `onmousewheel` - and does not hold for the attribute's mere
// existence: HTML requires all of these on every one of these objects,
// `'onanimationend' in el` is true in every browser, and a page that branches
// on it is depending on animations working rather than on the property. What
// the engine does not dispatch is a gap in the DISPATCH, and hiding the
// attribute does not close it.
//
// Those 19 names stay OWN null data properties on the wrapper, which shadow
// the accessor here - so for them `el.onclick = ""` still reads "" until that
// list moves. `onscroll` is what the unit tests use for that reason.
//
// Four files in dom/events ask for the ones no engine event uses:
// Body-FrameSet-Event-Handlers.html wants `onscroll` and `onresize`, and the
// four `webkit-*-event.html` files want the animation and transition pairs
// prefixed and unprefixed at once. `ontouchstart` is deliberately NOT here: it
// is the name a touch capability check asks about, and this engine has none.
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

// WindowEventHandlers - the ones that are the WINDOW's business rather than an
// element's, and the reason `Body-FrameSet-Event-Handlers.html` exists at all:
// HTML also puts these on `<body>` and `<frameset>`, where they FORWARD. See
// install_event_handler_attributes for what is and is not wired here.
constexpr std::string_view window_event_handlers[] = {
    "onafterprint",       "onbeforeprint", "onbeforeunload",       "onhashchange",
    "onlanguagechange",   "onmessage",     "onmessageerror",       "onoffline",
    "ononline",           "onpagehide",    "onpageshow",           "onpopstate",
    "onrejectionhandled", "onstorage",     "onunhandledrejection", "onunload"};

// WHERE A HANDLER IS ACTUALLY KEPT. Three slots per name and each answers a
// different question, which is why one would not do:
//
//   * `__on<name>` - what the IDL attribute was ASSIGNED. Its PRESENCE is the
//     state, not its value: `el.onclick = null` is a null that has been set,
//     and HTML says an assignment deactivates the content attribute's handler,
//     so a present null must not fall back to the markup.
//   * `__onsrc<name>` - the content attribute's text as it was when it was last
//     compiled, so a changed attribute recompiles and an unchanged one does not.
//   * `__onfn<name>`  - the function that text compiled to. `el.onclick ===
//     el.onclick` has to hold, and it would not if each read compiled again.
//
// All three are `attr_none`, so a page enumerating the object never sees them.
[[nodiscard]] std::string assigned_slot(const std::string & name) {
    return "__on" + name;
}
[[nodiscard]] std::string source_slot(const std::string & name) {
    return "__onsrc" + name;
}
[[nodiscard]] std::string compiled_slot(const std::string & name) {
    return "__onfn" + name;
}

} // namespace

// `onclick="doThing()"` AS A FUNCTION, which is the half of the event handler
// attributes that is not a property at all.
//
// HTML calls what the markup leaves behind an "internal raw uncompiled
// handler": the attribute's value is a function BODY, and it becomes a function
// the first time anything asks for the handler. The parameter is named `event`,
// which is what makes `onclick="alert(event.type)"` work.
//
// NOT CACHED PAST A CHANGE. The specification compiles once, at the moment the
// attribute is set, because the attribute-change steps are a hook it has and
// this does not - so the source text is kept beside the function and a
// different text compiles again. That is one string compare on a read whose
// slot is unset, and it is the difference between `setAttribute` taking effect
// and not.
//
// A HANDLER THAT WILL NOT COMPILE IS NULL, which HTML says in as many words:
// the uncompiled handler is discarded and the attribute reads null, rather than
// the page taking a SyntaxError from a read.

value dom_bindings::compile_handler_attribute(context & cx, value self, const std::string & name) {
    const node_id id = handle_of(self);
    if (!id || doc_ == nullptr || atoms_ == nullptr) { return value::undefined(); }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    std::string source;
    {
        const auto txn = doc_->read();
        const atom key = atoms_->intern_lower(name);
        if (!txn.has_attribute(id, key)) {
            // The attribute has GONE, and so must anything compiled from it -
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
    // THE SCOPE CHAIN, HTML 8.1.8.1 "getting the current value of the event
    // handler" step 10: the realm's global, then the element's node document,
    // then its form owner when it has one, then the element itself - each an
    // object environment, which is what `with` makes. That is why
    // `onclick="remove()"` reaches `window.remove` and not the element's
    // method (Element.prototype[@@unscopables] vetoes it, install_node_methods)
    // while `onclick="title"` reads the document's. The two outer objects are
    // PARAMETERS rather than free names so a frame document's element gets its
    // own document, not the page's global one.
    //
    // ponytail: every free identifier in the body now costs a `has` trap on
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
    // THE `with` BLOCKS ENCLOSE THE FUNCTION, they are not inside it: the
    // scope chain is the function's OUTER environment (step 10 builds it
    // before step 11 makes the function), so a parameter or local of the
    // handler shadows the element's properties and not the other way round.
    // Inside the body they shadowed `event` itself - `<body onerror>` read
    // `this.event`, the window's current event, where HTML hands the handler
    // the message string. A Window's `onerror` takes the five parameters
    // (event, source, lineno, colno, error) - step 11's one special case.
    const bool window_error =
        name == "onerror" && forwards_to_window(name) && body_or_frameset_of(self);
    // NAMED AFTER THE ATTRIBUTE: step 11's source text is `function
    // onclick(event) {\n<body>\n}`, which is what `handler.toString()` and
    // `handler.name` answer (event-handler-sourcetext.html). ponytail: a
    // named function expression binds its name inside the body, which the
    // specification's function does not; `onclick="onclick = f"` would write
    // that binding (silently, in sloppy code) rather than the element's.
    const bool identifier = std::ranges::all_of(name, [](const char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
               ch == '_' || ch == '$';
    });
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

// The body or frameset element `self` wraps, or none.
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

// A forwarded CONTENT attribute reaches the window from here, lazily: there is
// no attribute-change hook, so the element's `on<name>` text is compared with
// what was last compiled from it on every read of the handler - through the
// element or through the window - and a changed text is compiled onto the
// window's slot, a removed one clears it. The element that last supplied the
// window's handler is remembered so its removal is seen from the window side
// too, and every body or frameset a script has a wrapper for - a detached
// `createElement("body")` included - is checked when the window's handler is
// read, since that is where its content attribute lands. A real hook in
// setAttribute would replace all of this.
void dom_bindings::refresh_forwarded_handler(context & cx, node_id element,
                                             const std::string & name) {
    auto * window = window_object();
    if (window == nullptr || !element) { return; }
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

// THE GETTER. Null when unset, and "unset" is the ABSENCE of the assigned slot
// rather than a null in it - see assigned_slot.
value dom_bindings::event_handler_get(context & cx, value self, const std::string & name) {
    if (!self.is_object()) { return value::null(); }
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
    // THE BARE GLOBAL SPELLING, and only on the window. `window` IS the global
    // object in HTML, so `onerror = report` and `window.onerror = report` are
    // the same write - but the globals table and the window object are separate
    // storage here, and defining these accessors made the window's own property
    // win a lookup that used to fall through to the globals. A page that writes
    // the bare form is not doing anything unusual and would silently stop being
    // heard.
    if (object == window_object() && cx.has_global(name)) {
        if (const value held = cx.global(name); held.is_callable()) { return held; }
    }
    return value::null();
}

// THE SETTER, and the whole of it is one WebIDL annotation.
//
// `EventHandler` is `[LegacyTreatNonObjectAsNull] callback EventHandlerNonNull?`,
// which says: anything that is not an Object becomes null. So `el.onclick = ""`
// and `el.onclick = 42` both STORE NULL and both read back null - not the empty
// string, which is what a plain data property answered and what twelve of
// Body-FrameSet-Event-Handlers.html's assertions are about.
//
// An object that is not callable is stored as it is, which is the same
// annotation read the other way: only a non-Object is nulled. It throws when it
// is eventually invoked, which is where the specification puts the failure.
void dom_bindings::event_handler_set(context & cx, value self, const std::string & name,
                                     value given) {
    if (!self.is_object()) { return; }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    // `body.onload = f` IS `window.onload = f` - see forwards_to_window.
    if (forwards_to_window(name) && window_object() != nullptr && body_or_frameset_of(self)) {
        object = window_object();
    }
    const value stored = given.is_object_like() ? given : value::null();
    object->define(assigned_slot(name), stored, script::attr_none);
    // AND THE ASSIGNMENT DISCARDS THE CONTENT ATTRIBUTE'S HANDLER - HTML's
    // "deactivate an event handler". Dropping the compiled copy is what makes a
    // later read see the assignment rather than the markup.
    (void)object->erase(source_slot(name));
    (void)object->erase(compiled_slot(name));
    if (object == window_object()) { cx.define_global(name, stored); }
    // ...AND ITS LISTENER IS REGISTERED NOW, OR DROPPED: the handler's place
    // among addEventListener's listeners is where it was first set
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

// THE ATTRIBUTE WRITES THE DOCUMENT LOGGED since the last mutation: an
// `on*` content attribute activates its handler's listener the moment it is
// set (HTML 8.1.8.1 - the attribute change steps), and deactivates it when
// it goes with no IDL handler assigned in its place.
void dom_bindings::settle_attribute_writes(const std::vector<document::write_note> & writes) {
    if (cx_ == nullptr || doc_ == nullptr) { return; }
    for (const document::write_note & note : writes) {
        if (note.kind != document::write_note::edit::attribute || !note.node) { continue; }
        const std::string_view name = atoms_->text(note.name);
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
        const value self = wrap(*cx_, note.node);
        path_step at{note.node, listen_on::node};
        auto * object = static_cast<script::object_object *>(self.as_heap());
        if (forwards_to_window(name) && window_object() != nullptr && body_or_frameset_of(self)) {
            at = path_step{node_id{}, listen_on::window};
            object = window_object();
        }
        const std::string type = event_type_of_handler(name);
        if (present) {
            // THE ATTRIBUTE'S VALUE IS THE HANDLER NOW (8.1.8.1's attribute
            // change steps): an IDL assignment before it - `el.onclick = null`
            // - no longer shadows the markup, and the same text set again
            // compiles again (event-handler-removal.window.js).
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

// EVERY ONE OF THEM, ON EVERY OBJECT HTML PUTS THEM ON.
//
// ONE PAIR OF NATIVES PER NAME, shared by all four hosts: the accessors find
// their object through `this`, so there is no reason for HTMLElement.prototype
// and Document.prototype to hold different functions for `onclick`.
//
// ON THE WINDOW OBJECT ITSELF rather than on Window.prototype, and this one is
// load-bearing: the window is reached through a Proxy whose `set` trap writes a
// GLOBAL for any name the target does not already own. An accessor one link up
// the prototype chain is not owned, so `window.onload = fn` would define a
// global and never run the setter.
//
// WHAT IS NOT HERE, said plainly. HTML forwards six of these from `<body>` and
// `<frameset>` to the window - `onblur`, `onerror`, `onfocus`, `onload`,
// `onresize`, `onscroll` are the WINDOW's when read or written through a body
// element - and that is not wired, because forwarding without the other half
// is worse than neither. The other half is the attribute-change hook: setting
// the CONTENT attribute on a body element has to compile the handler onto the
// window there and then, and the only place that can happen is `setAttribute`,
// which is another file. With forwarding and no hook, `body.onblur` reads the
// window's slot, which the previous case left explicitly null, and the
// reflection case that passes today would fail instead.
void dom_bindings::install_event_handler_attributes(context & cx) {
    // The interface prototypes are built on the first `wrap()`, which is after
    // this - so ask for them now. It is a no-op if they already exist and it is
    // what makes `interface_prototype` answer at all this early.
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
    if (auto * window = window_object()) { hosts.push_back(window); }
    const auto install = [&](std::string_view attribute, std::span<script::object_object *> where) {
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
    // WindowEventHandlers is the window's alone here - see the note above on
    // what forwarding would need.
    if (auto * window = window_object()) {
        for (const std::string_view attribute : window_event_handlers) {
            install(attribute, std::span<script::object_object *>{&window, 1});
        }
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
    // THE NAME IS ALL-LOWERCASE AND THE TYPE IS NOT.
    //
    // Every event handler IDL attribute HTML defines is lowercase, and the type
    // it listens for is whatever the specification that named the event chose -
    // which for the prefixed CSS ones is camelCase. `onwebkitanimationend` has
    // an "event handler event type" of `webkitAnimationEnd`, so building the
    // property name by concatenation found nothing for the four legacy families
    // and would have gone on finding nothing. Folding also takes away an
    // invention: `el.onMyEvent` was a handler here for a `MyEvent` dispatch and
    // is not one in any browser, because only the attributes the IDL declares
    // are handlers at all.
    const value handler = cx_->lookup_property(target, "on" + ascii_lower_copy(type));
    if (!handler.is_callable()) { return answer(false); }
    // WHAT THE HANDLER RETURNS IS PART OF WHAT IT DID - HTML, "processing the
    // return value". A handler that returns exactly `false` CANCELS the event,
    // which is `<a onclick="return false">` and twenty years of code written
    // against it; the window's `onerror` is the one inversion, where `true`
    // means "I reported it" and cancels instead.
    //
    // EXACTLY `false`, not merely falsy. `return 0` and `return ""` do not
    // cancel in any browser - the return type is `any` and the rule names the
    // boolean, so truthiness is the wrong question to ask here.
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
        const bool threw = invoke_listener(*cx_, handler, target, arguments, caught, returned);
        process_return(/*cancel_on_true*/ true);
        return answer(threw);
    }
    const bool threw = invoke_listener(*cx_, handler, target, event, caught, returned);
    process_return(/*cancel_on_true*/ false);
    return answer(threw);
}

} // namespace ctbrowser::shell
