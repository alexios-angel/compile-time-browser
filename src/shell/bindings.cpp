#include <ctbrowser/shell/bindings.hpp>

// dom_bindings' method bodies - the API a page's script actually calls.
//
// The header lists what a page can reach; this is how each one works.

namespace ctbrowser::shell {

void dom_bindings::observe_resources(asset_registry & assets, image_store & images) {
    assets_ = &assets;
    images_ = &images;
}

namespace {

// The parts of a URL, as `location` reports them.
//
// href alone is not enough for a library: `location.search` is where a page
// reads its own query string and `location.pathname` is what a router matches
// on, and both are read WITHOUT a guard - the idiom is
// `location.search.substring(1)`, so an absent one is not a missing feature but
// a TypeError on the first line of the library's setup.
//
// Parsed rather than tracked, because href is the one thing the browser
// actually knows and keeping seven fields in step with it by hand is how they
// drift apart.
struct url_parts {
    std::string protocol; // "file:" - WITH the colon, as the DOM reports it
    std::string host;     // hostname:port
    std::string hostname;
    std::string port;
    std::string pathname;
    std::string search; // "?a=1", empty when there is none - not "?"
    std::string hash;   // "#x"
    std::string origin;
};

[[nodiscard]] url_parts split_url(std::string_view href) {
    url_parts out;
    std::string_view rest = href;
    if (const std::size_t scheme = rest.find("://"); scheme != std::string_view::npos) {
        out.protocol = std::string{rest.substr(0, scheme)} + ":";
        rest = rest.substr(scheme + 3);
        const std::size_t authority = rest.find_first_of("/?#");
        const std::string_view host = rest.substr(0, authority);
        out.host = std::string{host};
        if (const std::size_t colon = host.rfind(':'); colon != std::string_view::npos) {
            out.hostname = std::string{host.substr(0, colon)};
            out.port = std::string{host.substr(colon + 1)};
        } else {
            out.hostname = std::string{host};
        }
        out.origin = out.protocol + "//" + out.host;
        rest = authority == std::string_view::npos ? std::string_view{} : rest.substr(authority);
    } else if (const std::size_t colon = rest.find(':');
               colon != std::string_view::npos && rest.substr(colon).starts_with(":/")) {
        // `file:/path` - a scheme with no authority at all.
        out.protocol = std::string{rest.substr(0, colon)} + ":";
        out.origin = "null";
        rest = rest.substr(colon + 1);
    }
    if (const std::size_t at = rest.find('#'); at != std::string_view::npos) {
        out.hash = std::string{rest.substr(at)};
        rest = rest.substr(0, at);
    }
    if (const std::size_t at = rest.find('?'); at != std::string_view::npos) {
        out.search = std::string{rest.substr(at)};
        rest = rest.substr(0, at);
    }
    out.pathname = std::string{rest};
    // A URL with no path still HAS one, and a page joining onto it produces
    // "//thing" rather than "/thing" when it is empty.
    if (out.pathname.empty() && !out.host.empty()) { out.pathname = "/"; }
    return out;
}

} // namespace

// Every part of the URL, derived from href. Called wherever href is set, so
// the two cannot disagree.
void dom_bindings::write_location_parts(context & cx, script::object_object & loc) {
    const url_parts parts = split_url(location_href_);
    loc.set("protocol", cx.string(parts.protocol));
    loc.set("host", cx.string(parts.host));
    loc.set("hostname", cx.string(parts.hostname));
    loc.set("port", cx.string(parts.port));
    loc.set("pathname", cx.string(parts.pathname));
    loc.set("search", cx.string(parts.search));
    loc.set("origin", cx.string(parts.origin.empty() ? "null" : parts.origin));
}

void dom_bindings::observe_location(std::string href, std::string hash) {
    location_href_ = std::move(href);
    location_hash_ = std::move(hash);
    // WRITE THEM THROUGH. `href` was set once when the object was built,
    // which made it a snapshot: a page reading location.href after
    // following a link got whatever was true at page load, forever.
    if (cx_ != nullptr && location_.is_object()) {
        auto * loc = static_cast<script::object_object *>(location_.as_heap());
        loc->set("href", cx_->string(location_href_));
        loc->set("hash", cx_->string(location_hash_));
        write_location_parts(*cx_, *loc);
    }
}

void dom_bindings::set_alert_hook(std::function<void(const std::string &)> hook) {
    on_alert_ = std::move(hook);
}

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

void dom_bindings::register_roots(context & cx) {
    cx.set_external_roots([this](const context::root_visitor & mark) {
        for (const listener & l : listeners_) {
            mark(l.callback);
            mark(l.abort_signal);
        }
        for (const timer & t : timers_) { mark(t.callback); }
        // A QUEUED FETCH holds the only reference to the promise a page is
        // waiting on, and to the signal that may cancel it. Neither is reachable
        // from anywhere else between the call and the turn that settles it.
        for (const pending_fetch & waiting : fetches_) {
            mark(waiting.promise);
            mark(waiting.signal);
        }
        // A QUEUED IMAGE LOAD holds the only reference to the wrapper whose
        // onload will run and to decode()'s promise.
        for (const pending_image & waiting : image_loads_) {
            mark(waiting.target);
            mark(waiting.promise);
        }
        for (const value & callback : animation_callbacks_) { mark(callback); }
        for (const auto & [packed, obj] : wrappers_) {
            if (obj != nullptr) { mark(value::object(obj)); }
        }
        // Blob.prototype is held here as well as on the global, and the global
        // is what keeps it alive - but a page can delete a global, and a Blob
        // whose prototype was collected stops being `instanceof Blob`.
        mark(blob_prototype_);
        mark(location_);
        mark(document_);
        mark(window_);
    });
}

void dom_bindings::install(context & cx) {
    cx_ = &cx;
    register_roots(cx);
    install_console(cx);
    location_ = make_location(cx);
    install_document(cx);
    install_window(cx);
    install_timers(cx);
    install_resources(cx);
    install_navigation(cx);
}

bool dom_bindings::dispatch(std::string_view type, node_id target) {
    if (cx_ == nullptr) { return false; }
    return dispatch_event(type, target, make_event(*cx_, type, target));
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
    return dispatch_event(type, target, event);
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

std::size_t dom_bindings::run_due_callbacks() {
    if (cx_ == nullptr) { return 0; }
    std::size_t ran = 0;
    // FETCHES FIRST, so a handler waiting on one runs in the same turn as the
    // timers rather than a turn behind them. Copied before running, because a
    // handler resolved by one may start another.
    if (!fetches_.empty()) {
        std::vector<pending_fetch> due;
        due.swap(fetches_);
        for (const pending_fetch & waiting : due) {
            settle_fetch(*cx_, waiting);
            note_callback_fault("fetch");
            ++ran;
        }
    }
    // IMAGE LOADS with them, and for the same reason: p5's loadImage awaits a
    // fetch and then awaits an image load, so a turn that ran one but not the
    // other would need two ticks per image instead of one.
    if (!image_loads_.empty()) {
        std::vector<pending_image> due;
        due.swap(image_loads_);
        for (const pending_image & waiting : due) {
            settle_image(*cx_, waiting);
            note_callback_fault("image load");
            ++ran;
        }
    }
    // Copied before running: a callback may add or cancel timers, and
    // iterating the live list while it does is how a timer that
    // re-registers itself becomes an infinite loop inside one tick.
    std::vector<timer> due;
    for (timer & t : timers_) {
        if (!t.cancelled && t.due_ms <= now_ms_) { due.push_back(t); }
    }
    for (const timer & t : due) {
        const auto still = std::ranges::find_if(
            timers_, [&](const timer & x) { return x.id == t.id && !x.cancelled; });
        if (still == timers_.end()) { continue; }
        if (still->repeating) {
            still->due_ms = now_ms_ + still->interval_ms;
        } else {
            still->cancelled = true;
        }
        (void)cx_->call(t.callback, {});
        note_callback_fault("setTimeout");
        ++ran;
    }
    std::erase_if(timers_, [](const timer & t) { return t.cancelled; });
    // AFTER THE TIMERS, BEFORE THE ANIMATION FRAMES. That is where a browser
    // puts its microtask checkpoint, and the ordering is observable: a promise
    // resolved by a timer must have run its handlers before the frame that
    // follows draws.
    cx_->drain_microtasks();

    std::vector<value> frame_callbacks;
    frame_callbacks.swap(animation_callbacks_);
    for (const value & cb : frame_callbacks) {
        const value ms = value::number(now_ms_);
        (void)cx_->call(cb, std::span<const value>{&ms, 1});
        note_callback_fault("requestAnimationFrame");
        ++ran;
    }
    cx_->drain_microtasks();
    note_callback_fault("microtask");
    return ran;
}

// A FAULT IN A CALLBACK IS REPORTED AND CLEARED, not left set.
//
// `context::run` clears the failure flag on entry, so a fault in a <script> is
// reported once and forgotten. `call` has no such entry point, so a fault in
// the first animation frame stayed set for the life of the page: every later
// callback was refused and the page silently stopped moving. p5.js drives its
// entire draw loop through requestAnimationFrame, so that is one faulting frame
// between a sketch that runs and a sketch that renders one frame and dies with
// no message.
//
// Clearing matches a browser, where an exception in one callback cancels
// neither the rest of the queue nor the next frame.
void dom_bindings::note_callback_fault(std::string_view source) {
    if (cx_ == nullptr || !cx_->failed()) { return; }
    const std::string message = std::string{source} + " callback: " + cx_->take_error();
    // The FIRST one is kept: a loop that faults every frame would otherwise
    // replace the original diagnosis with the thousandth copy of it.
    if (callback_error_.empty()) { callback_error_ = message; }
    ++callback_faults_;
}

double dom_bindings::next_callback_ms() const {
    if (!animation_callbacks_.empty()) { return 0; }
    double soonest = std::numeric_limits<double>::infinity();
    for (const timer & t : timers_) {
        if (t.cancelled) { continue; }
        soonest = std::min(soonest, std::max(0.0, t.due_ms - now_ms_));
    }
    return soonest;
}

std::size_t dom_bindings::pending_animation_frames() const noexcept {
    return animation_callbacks_.size();
}

const std::vector<std::string> & dom_bindings::console_output() const noexcept {
    return console_;
}

value dom_bindings::wrap(context & cx, node_id id) {
    if (!id) { return value::null(); }
    if (const auto it = wrappers_.find(pack(id)); it != wrappers_.end()) {
        refresh_element(cx, *it->second, id);
        return value::object(it->second);
    }
    auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
    value wrapper = value::object(obj);
    obj->set(std::string{handle_property}, value::number(static_cast<double>(pack(id))));
    install_element_methods(cx, *obj);
    install_element_views(cx, *obj, id);
    refresh_element(cx, *obj, id);
    wrappers_.emplace(pack(id), obj);
    return wrapper;
}

std::uint64_t dom_bindings::pack(node_id id) {
    return (static_cast<std::uint64_t>(id.generation) << 32) | id.slot;
}

node_id dom_bindings::unpack(std::uint64_t bits) {
    node_id id;
    id.slot = static_cast<std::uint32_t>(bits & 0xFFFFFFFFu);
    id.generation = static_cast<std::uint32_t>(bits >> 32);
    return id;
}

node_id dom_bindings::receiver(context & cx) {
    const value self = cx.current_this();
    if (!self.is_object()) { return node_id{}; }
    auto * obj = static_cast<script::object_object *>(self.as_heap());
    const value * slot = obj->find(std::string{handle_property});
    if (slot == nullptr) { return node_id{}; }
    return unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
}

void dom_bindings::refresh_element(context & cx, script::object_object & obj, node_id id) {
    const auto txn = doc_->read();
    // `tagName` is UPPERCASE for an HTML element, and lowercase was a silent
    // wrong answer: p5.js branches on `elt.tagName === 'INPUT'` and on
    // `child.tagName === param` in its XML module, so every such comparison was
    // false and the code behind it never ran. An SVG element keeps its own case -
    // tagName is the qualified name, and only HTML uppercases it.
    {
        std::string tag_name{atoms_->text(txn.tag(id).value_or(atom{}))};
        if (txn.element_ns(id) == node_ns::html) {
            for (char & c : tag_name) {
                if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
            }
        }
        obj.set("tagName", cx.string(tag_name));
    }
    // `id`, `className`, `width` and `height` are NOT set here: they are
    // accessors over the attributes, installed once in install_element_views.
    // As data properties they were write-only in the wrong direction - a page
    // assigning `el.id = 'x'` changed the wrapper and nothing else, and the
    // next refresh put the old value back. p5.js names its canvas and sizes it
    // that way, so both writes vanished.
    const std::string_view tag_text = atoms_->text(txn.tag(id).value_or(atom{}));

    refresh_control(cx, obj, txn, id, tag_text);

    const rect box = box_of(id);
    obj.set("offsetLeft", value::number(static_cast<double>(box.x)));
    obj.set("offsetTop", value::number(static_cast<double>(box.y)));
    obj.set("offsetWidth", value::number(static_cast<double>(box.width)));
    obj.set("offsetHeight", value::number(static_cast<double>(box.height)));
}

namespace {

// `backgroundColor` -> `background-color`. The IDL name and the CSS name are
// different spellings of the same property, and the attribute the style engine
// parses wants the CSS one.
std::string css_property_name(std::string_view idl) {
    std::string out;
    for (const char c : idl) {
        if (c >= 'A' && c <= 'Z') {
            out += '-';
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += c;
        }
    }
    return out;
}

// The declarations an object holds, as a `style` attribute. Serialising the
// whole object on every write is what keeps the two representations from
// drifting: there is one source of truth, the object, and the attribute is
// derived from it.
std::string style_attribute(script::object_object & held, context & cx) {
    std::string out;
    for (const auto & [name, v] : held.props) {
        if (v.is_nullish()) { continue; }
        // `setProperty` and friends live on the same object, and a CSS value is
        // never a function - without this the methods serialise themselves into
        // the attribute as `set-property: function;`.
        if (v.is_callable()) { continue; }
        const std::string text = cx.to_string(v);
        // Assigning "" REMOVES a declaration, which is how a page turns one
        // off - emitting `display: ;` instead would leave the old value in
        // place as far as the parser is concerned.
        if (text.empty()) { continue; }
        out += css_property_name(name);
        out += ": ";
        out += text;
        out += "; ";
    }
    return out;
}

// The declarations already in a `style` attribute, so a write through
// `el.style` extends what the author wrote rather than replacing it. The whole
// object is re-serialised on every write, so anything not read back here is
// LOST on the first assignment - which silently deleted the width and height an
// element was sized by.
void seed_declarations(script::object_object & held, context & cx, std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t colon = text.find(':', i);
        if (colon == std::string_view::npos) { break; }
        std::size_t end = text.find(';', colon);
        if (end == std::string_view::npos) { end = text.size(); }
        const auto trim = [](std::string_view piece) {
            const std::size_t first = piece.find_first_not_of(" \t\n\r\f");
            if (first == std::string_view::npos) { return std::string_view{}; }
            return piece.substr(first, piece.find_last_not_of(" \t\n\r\f") - first + 1);
        };
        const std::string_view name = trim(text.substr(i, colon - i));
        const std::string_view v = trim(text.substr(colon + 1, end - colon - 1));
        if (!name.empty()) { held.set(std::string{name}, cx.string(std::string{v})); }
        i = end + 1;
    }
}

// The tokens of a `class` attribute. Whitespace-separated, and a class list
// operation is defined in terms of them rather than of the string.
std::vector<std::string> class_tokens(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = text.find_first_not_of(" \t\n\r\f", i);
        if (start == std::string_view::npos) { break; }
        const std::size_t end = text.find_first_of(" \t\n\r\f", start);
        out.emplace_back(text.substr(start, end == std::string_view::npos ? end : end - start));
        i = end == std::string_view::npos ? text.size() : end;
    }
    return out;
}

} // namespace

void dom_bindings::install_element_views(context & cx, script::object_object & obj, node_id id) {
    // --- element.style
    //
    // A PROXY, because a style object has no fixed set of properties: a page
    // may write any CSS property and the write has to reach the document. The
    // proxy's target holds the declarations and the `set` trap re-serialises it
    // into the element's `style` attribute - which the style engine already
    // parses, so there is no second representation to keep in step.
    //
    // BOTH traps canonicalise the name, because `backgroundColor` and
    // `background-color` are two spellings of ONE property. Storing them as
    // written put both in the attribute and made a read miss a write.
    auto * held = static_cast<script::object_object *>(cx.make_object().as_heap());
    {
        const auto txn = doc_->read();
        seed_declarations(*held, cx, txn.attribute_value(id, atoms_->intern("style")));
    }
    const value target = value::object(held);
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto trap = [&](std::string name, script::native_fn fn) {
        handler->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    trap("get", [](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
        auto * store = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        // The raw name first: that is where setProperty and getPropertyValue
        // live, and canonicalising them turns them into `set-property`.
        if (const value * found = store->find(name)) { return *found; }
        const value * found = store->find(css_property_name(name));
        return found == nullptr ? value::undefined() : *found;
    });
    trap("set", [this, id](context & c, std::span<value> args) {
        if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
        auto * store = static_cast<script::object_object *>(args[0].as_heap());
        store->set(css_property_name(c.to_string(args[1])), args[2]);
        (void)doc_->set_attribute(id, atoms_->intern("style"), style_attribute(*store, c));
        mutated();
        return value::boolean(true);
    });
    const value style_view =
        value::object(cx.allocate<script::proxy_object>(target, value::object(handler)));

    // `setProperty` / `getPropertyValue` / `removeProperty` take the CSS
    // spelling rather than the IDL one, so they are the only way to reach a
    // custom property (`--x`) - which no identifier can name.
    const auto declaration_method = [&](std::string name, script::native_fn fn) {
        held->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    declaration_method("setProperty", [this, id, held](context & c, std::span<value> args) {
        held->set(arg_string(c, args, 0), args.size() > 1 ? args[1] : value::undefined());
        (void)doc_->set_attribute(id, atoms_->intern("style"), style_attribute(*held, c));
        mutated();
        return value::undefined();
    });
    declaration_method("removeProperty", [this, id, held](context & c, std::span<value> args) {
        held->erase(arg_string(c, args, 0));
        (void)doc_->set_attribute(id, atoms_->intern("style"), style_attribute(*held, c));
        mutated();
        return value::undefined();
    });
    declaration_method("getPropertyValue", [held](context & c, std::span<value> args) {
        const value * found = held->find(arg_string(c, args, 0));
        return found == nullptr ? c.string("") : c.string(c.to_string(*found));
    });
    obj.set("style", style_view);

    // --- the reflected attributes
    //
    // `id`, `className`, `width` and `height` are IDL attributes that REFLECT
    // content attributes: reading one reads the attribute and writing one
    // writes it. As plain data properties they only ever went one way - the
    // refresh copied the attribute onto the wrapper, and a page's assignment
    // changed the wrapper alone and was overwritten by the next refresh.
    //
    // Nothing said so. p5.js gives its canvas an id and a size that way and
    // both writes disappeared, leaving a nameless 300x150 canvas that no
    // amount of drawing could make the right size.
    const auto reflect_string = [&](std::string property, std::string attribute) {
        obj.define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [this, id, attribute](context & c, std::span<value>) {
                    const auto txn = doc_->read();
                    return c.string(
                        std::string{txn.attribute_value(id, atoms_->intern(attribute))});
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, id, attribute](context & c, std::span<value> a) {
                    (void)doc_->set_attribute(id, atoms_->intern(attribute), arg_string(c, a, 0));
                    mutated();
                    return value::undefined();
                })));
    };
    reflect_string("id", "id");
    reflect_string("className", "class");
    // THE REST OF THE ORDINARY IDL ATTRIBUTES, and leaving them out was the same
    // one-way bug as `id` used to be: `link.href = url` set a property on the
    // wrapper that no attribute, no layout and no default action ever read.
    //
    // That is what made p5's export do nothing. downloadFile builds `<a href
    // download>` entirely from script, so BOTH attributes were invisible - the
    // click found an anchor with no href and no download and treated it as a
    // click on nothing.
    //
    // Named rather than generic, because reflection is per element in the spec
    // and a made-up `el.foo = 1` must NOT become an attribute. These are the ones
    // a page assigns.
    for (const auto & [property, attribute] :
         std::initializer_list<std::pair<const char *, const char *>>{
             {"href", "href"},
             {"download", "download"},
             {"target", "target"},
             {"rel", "rel"},
             {"alt", "alt"},
             {"title", "title"},
             {"name", "name"},
             {"placeholder", "placeholder"},
             {"htmlFor", "for"}}) {
        reflect_string(property, attribute);
    }

    // `innerHTML` and `textContent` are ACCESSORS over the tree, not properties
    // on the wrapper. As properties, assigning markup stored a string, built no
    // nodes, rendered nothing and reported nothing - and reading one back gave
    // whatever the page last wrote rather than what the DOM actually holds.
    const auto tree_property = [&](std::string property, script::native_fn read,
                                   script::native_fn write) {
        obj.define_accessor(
            property, value::object(cx.allocate<script::native_object>(property, std::move(read))),
            value::object(cx.allocate<script::native_object>(property, std::move(write))));
    };
    tree_property(
        "innerHTML", [this, id](context & c, std::span<value>) { return c.string(inner_html(id)); },
        [this, id](context & c, std::span<value> a) {
            set_inner_html(id, arg_string(c, a, 0));
            return value::undefined();
        });
    tree_property(
        "textContent",
        [this, id](context & c, std::span<value>) { return c.string(text_content(id)); },
        [this, id](context & c, std::span<value> a) {
            // Text, never markup: that is the whole point of the property, and
            // the reason a page reaches for it instead of innerHTML.
            set_text(id, arg_string(c, a, 0));
            return value::undefined();
        });

    // `value` and `checked` ARE ACCESSORS, on a control.
    //
    // They were data properties written by refresh_control on whatever tick it
    // next ran. A page that creates a control and reads it back in the same
    // statement - `createInput('hello').value()`, which is p5's own DOM library
    // - therefore read the property as it was before the value existed. The
    // header's note that "the VM has no property accessors" was true when it
    // was written and is not any more.
    //
    // refresh_control still runs: it writes BACK a property assignment into the
    // control, which is how `input.value = ''` clears a field. These make the
    // READ live, which is the half that could not be done before.
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        const std::string_view type = txn.attribute_value(id, atoms_->intern("type"));
        if (control_kind_of(tag, type) != control_kind::none) {
            obj.define_accessor("value",
                                value::object(cx.allocate<script::native_object>(
                                    "value",
                                    [this, id](context & c, std::span<value>) {
                                        const auto read = doc_->read();
                                        return c.string(forms_->state_of(read, *atoms_, id).value);
                                    })),
                                value::object(cx.allocate<script::native_object>(
                                    "value", [this, id](context & c, std::span<value> a) {
                                        const auto read = doc_->read();
                                        control_state & control =
                                            forms_->state_of(read, *atoms_, id);
                                        control.value = arg_string(c, a, 0);
                                        control.caret = control.value.size();
                                        control.selection = control.caret;
                                        // An assignment DIRTIES the control, so the `value`
                                        // attribute stops being the answer - otherwise setting
                                        // it to "" would be undone by the next read.
                                        control.value_edited = true;
                                        // The browser has to learn a control changed, or the
                                        // paint is stale until something else marks it.
                                        wrote_to_control_ = true;
                                        mutated();
                                        return value::undefined();
                                    })));
            obj.define_accessor("checked",
                                value::object(cx.allocate<script::native_object>(
                                    "checked",
                                    [this, id](context &, std::span<value>) {
                                        const auto read = doc_->read();
                                        return value::boolean(
                                            forms_->state_of(read, *atoms_, id).checked);
                                    })),
                                value::object(cx.allocate<script::native_object>(
                                    "checked", [this, id](context &, std::span<value> a) {
                                        const auto read = doc_->read();
                                        forms_->state_of(read, *atoms_, id).checked =
                                            !a.empty() && context::truthy(a[0]);
                                        wrote_to_control_ = true;
                                        mutated();
                                        return value::undefined();
                                    })));
        }
    }

    // `parentNode` and `children` are ACCESSORS because the tree moves. A
    // wrapper built when an element was detached and refreshed later would hand
    // back the parent it had at wrapping time, which for an element p5 creates
    // and then appends is null forever.
    const auto navigate = [&](std::string property, script::native_fn fn) {
        obj.define_accessor(
            property, value::object(cx.allocate<script::native_object>(property, std::move(fn))),
            value::undefined());
    };
    navigate("parentNode", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        return wrap(c, txn.parent(id));
    });
    navigate("parentElement", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        return wrap(c, txn.parent(id));
    });
    // `childNodes` is EVERY child, text nodes included; `children` is the
    // elements only. Both exist because they answer different questions, and a
    // page that wants the text nodes has no other way to reach them.
    navigate("childNodes", [this, id](context & c, std::span<value>) {
        value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        const auto txn = doc_->read();
        for (const node_id child : txn.children(id)) { items->items.push_back(wrap(c, child)); }
        return list;
    });
    navigate("children", [this, id](context & c, std::span<value>) {
        value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        const auto txn = doc_->read();
        for (const node_id child : txn.children(id)) {
            if (txn.tag(child).has_value()) { items->items.push_back(wrap(c, child)); }
        }
        return list;
    });

    // `width` and `height` are numbers, and on a <canvas> they are the size of
    // its PIXEL BUFFER rather than of its laid-out box - `canvas.width / 2` is
    // the first line of most canvas pages. Assigning one resizes the surface,
    // which is what the spec means by a canvas being reset by the assignment.
    const auto reflect_size = [&](std::string property, double fallback) {
        obj.define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [this, id, property, fallback](context &, std::span<value>) {
                    const auto txn = doc_->read();
                    const std::string_view text = txn.attribute_value(id, atoms_->intern(property));
                    double parsed = 0;
                    bool any = false;
                    for (const char c : text) {
                        if (c < '0' || c > '9') { break; }
                        parsed = parsed * 10 + (c - '0');
                        any = true;
                    }
                    return value::number(any ? parsed : fallback);
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, id, property](context &, std::span<value> a) {
                    const double want = arg_number(a, 0);
                    (void)doc_->set_attribute(id, atoms_->intern(property),
                                              std::to_string(static_cast<long long>(want)));
                    // The SURFACE follows, or the canvas keeps drawing into a
                    // buffer of the size it was created at and everything past
                    // that edge is silently discarded.
                    if (canvases_ != nullptr) {
                        const auto txn = doc_->read();
                        const auto number = [&](std::string_view name, int missing) {
                            const std::string_view text =
                                txn.attribute_value(id, atoms_->intern(name));
                            int out = 0;
                            bool any = false;
                            for (const char c : text) {
                                if (c < '0' || c > '9') { break; }
                                out = out * 10 + (c - '0');
                                any = true;
                            }
                            return any ? out : missing;
                        };
                        canvases_->resize(id, number("width", 300), number("height", 150));
                    }
                    mutated();
                    return value::undefined();
                })));
    };
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        if (tag == "canvas") {
            // The HTML defaults, which a page that omits the attributes relies on.
            reflect_size("width", 300);
            reflect_size("height", 150);
        } else if (tag == "img") {
            install_image_views(cx, obj, id);
        } else if (txn.has_attribute(id, atoms_->intern("width")) ||
                   txn.has_attribute(id, atoms_->intern("height"))) {
            reflect_size("width", 0);
            reflect_size("height", 0);
        }
    }

    // --- element.classList
    //
    // Every operation reads the attribute, edits the token list and writes it
    // back, so nothing is cached and a class added by the parser, by
    // setAttribute or by the style engine is seen by all of them.
    auto * list = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto tokens_now = [this, id] {
        const auto txn = doc_->read();
        return class_tokens(txn.attribute_value(id, atoms_->intern("class")));
    };
    const auto write_tokens = [this, id](const std::vector<std::string> & tokens) {
        std::string text;
        for (const std::string & token : tokens) {
            if (!text.empty()) { text += ' '; }
            text += token;
        }
        (void)doc_->set_attribute(id, atoms_->intern("class"), text);
        mutated();
    };
    const auto list_method = [&](std::string name, script::native_fn fn) {
        list->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    list_method("add", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        for (const value & v : args) {
            const std::string token = c.to_string(v);
            if (token.empty()) { continue; }
            if (std::find(tokens.begin(), tokens.end(), token) == tokens.end()) {
                tokens.push_back(token);
            }
        }
        write_tokens(tokens);
        return value::undefined();
    });
    list_method("remove", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        for (const value & v : args) {
            const std::string token = c.to_string(v);
            std::erase(tokens, token);
        }
        write_tokens(tokens);
        return value::undefined();
    });
    list_method("contains", [tokens_now](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = tokens_now();
        return value::boolean(std::find(tokens.begin(), tokens.end(), arg_string(c, args, 0)) !=
                              tokens.end());
    });
    list_method("toggle", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        const std::string token = arg_string(c, args, 0);
        // The two-argument form FORCES a state rather than flipping it -
        // `classList.toggle("on", isOn)` is the idiom, and treating the second
        // argument as absent turns it into a flip that is right half the time.
        const bool present = std::find(tokens.begin(), tokens.end(), token) != tokens.end();
        const bool want = args.size() > 1 ? context::truthy(args[1]) : !present;
        if (want && !present) { tokens.push_back(token); }
        if (!want && present) { std::erase(tokens, token); }
        write_tokens(tokens);
        return value::boolean(want);
    });
    list_method("item", [tokens_now](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = tokens_now();
        const auto i = static_cast<std::ptrdiff_t>(
            context::to_number(args.empty() ? value::undefined() : args[0]));
        if (i < 0 || static_cast<std::size_t>(i) >= tokens.size()) { return value::null(); }
        return c.string(tokens[static_cast<std::size_t>(i)]);
    });
    // An ACCESSOR, not a number: the count changes whenever the attribute does,
    // and a data property would report whatever it was when the element was
    // first wrapped.
    list->define_accessor("length",
                          value::object(cx.allocate<script::native_object>(
                              "length",
                              [tokens_now](context &, std::span<value>) {
                                  return value::number(static_cast<double>(tokens_now().size()));
                              })),
                          value::undefined());
    obj.set("classList", value::object(list));
}

rect dom_bindings::box_of(node_id id) const {
    if (fragments_ == nullptr) { return rect{}; }
    const auto find = [&](auto && self, const layout::fragment & f, float dx, float dy) -> rect {
        const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
        if (f.source == id) { return box; }
        for (const auto & child : f.children) {
            if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
        }
        return rect{};
    };
    return find(find, *fragments_, 0, 0);
}

void dom_bindings::install_element_methods(context & cx, script::object_object & obj) {
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };

    // `element.click()` - CLICKING WITHOUT A MOUSE.
    //
    // It was absent, and that is how p5's save() reaches the outside world:
    // downloadFile makes an <a href download>, calls click() on it, and revokes
    // the URL on the next line. So the whole export path was one missing method
    // wide, and the failure was that nothing happened - no error, no file.
    //
    // Both halves, in the right order: the event first, through the ordinary
    // capture-and-bubble dispatch, and the DEFAULT ACTION after it unless a
    // listener called preventDefault. A click() that only dispatched would leave
    // `link.click()` doing nothing and `checkbox.click()` not checking anything.
    method("click", [this](context & c, std::span<value> args) {
        (void)args;
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        if (!dispatch_event("click", id, make_event(c, "click", id)) && on_activate_) {
            on_activate_(id);
        }
        return value::undefined();
    });

    method("setAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        (void)doc_->set_attribute(id, atoms_->intern_lower(arg_string(c, args, 0)),
                                  arg_string(c, args, 1));
        mutated();
        return value::undefined();
    });
    method("getAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const auto txn = doc_->read();
        // PRESENT-BUT-EMPTY is not absent. `<details open>`, `<input
        // disabled>` and `<option selected>` all have an empty value, and
        // returning null for them made every boolean attribute unreadable
        // from script - the one shape of attribute that is only ever tested
        // for presence.
        const atom name = atoms_->intern_lower(arg_string(c, args, 0));
        if (!txn.has_attribute(id, name)) { return value::null(); }
        return c.string(std::string{txn.attribute_value(id, name)});
    });
    method("setText", [this](context & c, std::span<value> args) {
        set_text(id_or_nothing(c), arg_string(c, args, 0));
        return value::undefined();
    });
    method("getText", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        return id ? c.string(text_of(id)) : c.string(std::string{});
    });
    method("addClass", [this](context & c, std::span<value> args) {
        edit_classes(receiver(c), arg_string(c, args, 0), true);
        return value::undefined();
    });
    method("removeClass", [this](context & c, std::span<value> args) {
        edit_classes(receiver(c), arg_string(c, args, 0), false);
        return value::undefined();
    });
    method("hasClass", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        const std::string want = arg_string(c, args, 0);
        for (const std::string_view cls : split(txn.attribute_value(id, atoms_->intern("class")))) {
            if (cls == want) { return value::boolean(true); }
        }
        return value::boolean(false);
    });
    // `insertAdjacentHTML(position, markup)` - a fragment parse at one of four
    // places relative to this element. The parser and the copy are the same
    // ones innerHTML uses; only where the nodes land differs.
    method("insertAdjacentHTML", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self || atoms_ == nullptr) { return value::undefined(); }
        std::string where = arg_string(c, args, 0);
        for (char & ch : where) { ch = static_cast<char>(std::tolower(ch)); }
        const std::string markup = arg_string(c, args, 1);

        // Parsed into a scratch document, as innerHTML does and for the same
        // reason: tree_builder::parse replaces the root it is handed.
        document scratch{*atoms_};
        (void)parse_html(scratch, markup);
        const auto from = scratch.read();
        node_id body{};
        const auto find_body = [&](auto && walk, node_id at) -> void {
            if (!body && from.tag(at).value_or(atom{}) == atoms_->intern_lower("body")) {
                body = at;
            }
            for (const node_id child : from.children(at)) { walk(walk, child); }
        };
        find_body(find_body, from.root());
        if (!body) { return value::undefined(); }

        const auto txn = doc_->read();
        const node_id parent = txn.parent(self);
        // `beforebegin` and `afterend` need a PARENT to be inserted into, and an
        // element that has none simply ignores them - which is what a browser
        // does rather than throwing.
        for (const node_id child : from.children(body)) {
            if (where == "afterbegin") {
                // Reversed, because each new node goes in front of the last -
                // otherwise a two-node fragment arrives back to front.
                const std::span<const node_id> existing = txn.children(self);
                const node_id first = existing.empty() ? node_id{} : existing.front();
                const node_id made = copy_subtree(from, child, self);
                if (first) { (void)doc_->insert_before(self, made, first); }
            } else if (where == "beforebegin" && parent) {
                const node_id made = copy_subtree(from, child, parent);
                (void)doc_->insert_before(parent, made, self);
            } else if (where == "afterend" && parent) {
                (void)copy_subtree(from, child, parent);
            } else {
                // beforeend, and the fallback: append inside.
                (void)copy_subtree(from, child, self);
            }
        }
        mutated();
        return value::undefined();
    });

    // TREE NAVIGATION. appendChild and removeChild could already change the
    // tree; nothing could WALK it, so an element could not reach its own
    // parent. `this.elt.parentNode.removeChild(this.elt)` is the ordinary way
    // to take an element out of the page - it is how p5.js discards the default
    // canvas when a sketch calls createCanvas - and with parentNode undefined
    // the removal threw inside a callback and the discarded canvas stayed in
    // the document, laid out and painted, underneath the real one.
    method("remove", [this](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (self) {
            (void)doc_->remove_child(self);
            mutated();
        }
        return value::undefined();
    });
    method("insertBefore", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        const node_id before = handle_of(arg(args, 1));
        if (parent && child) {
            // A null reference node means "at the end", which is what makes
            // `insertBefore(node, null)` a documented spelling of appendChild.
            if (before) {
                (void)doc_->insert_before(parent, child, before);
            } else {
                (void)doc_->append_child(parent, child);
            }
            mutated();
        }
        return arg(args, 0);
    });
    // WHERE THE ELEMENT IS ON SCREEN. A page turns a pointer event's viewport
    // coordinates into coordinates within an element by subtracting this - p5's
    // getMouseInfo does exactly that to compute mouseX/mouseY - so without it
    // every mouse listener throws on its first event. The listeners were
    // installed and the events were dispatched; the conversion in between is
    // what was missing, and it made the whole input surface look absent.
    // `querySelector` ON AN ELEMENT, searching its own subtree. The document
    // had both and an element had neither, so the ordinary "find something
    // inside this" - which is what a library does with a container it owns -
    // threw. p5.js's describe() builds an offscreen tree and queries it.
    method("querySelector", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = query(arg_string(c, args, 0), receiver(c));
        return found.empty() ? value::null() : wrap(c, found.front());
    });
    method("querySelectorAll", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = query(arg_string(c, args, 0), receiver(c));
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const node_id node : found) { items->items.push_back(wrap(c, node)); }
        return out;
    });
    method("getBoundingClientRect", [this](context & c, std::span<value>) {
        const rect box = box_of(receiver(c));
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        const auto set = [&](const char * name, float v) {
            out->set(name, value::number(static_cast<double>(v)));
        };
        set("x", box.x);
        set("y", box.y);
        set("left", box.x);
        set("top", box.y);
        set("width", box.width);
        set("height", box.height);
        // right and bottom are DERIVED, and pages read them directly rather
        // than adding the width themselves.
        set("right", box.x + box.width);
        set("bottom", box.y + box.height);
        return value::object(out);
    });
    method("appendChild", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        if (parent && child) {
            (void)doc_->append_child(parent, child);
            mutated();
        }
        return arg(args, 0);
    });
    method("removeChild", [this](context &, std::span<value> args) {
        const node_id child = handle_of(arg(args, 0));
        if (child) {
            (void)doc_->remove_child(child);
            mutated();
        }
        return arg(args, 0);
    });
    method("addEventListener", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (id) { listeners_.push_back(make_listener(c, id, args)); }
        return value::undefined();
    });
    // The other half. The window and the document could both remove a listener
    // and an element could not, so a page that tidied up after itself - which
    // p5's Element does when it is removed - threw instead.
    method("removeEventListener", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string type = arg_string(c, args, 0);
        const value callback = arg(args, 1);
        std::erase_if(listeners_, [&](const listener & l) {
            return l.target == id && l.type == type && l.callback.bits() == callback.bits();
        });
        return value::undefined();
    });

    // --- form controls -------------------------------------------------
    method("getValue", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return c.string(std::string{}); }
        const auto txn = doc_->read();
        return c.string(forms_->state_of(txn, *atoms_, id).value);
    });
    method("setValue", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        control_state & control = forms_->state_of(txn, *atoms_, id);
        control.value = arg_string(c, args, 0);
        control.caret = control.value.size();
        control.selection = control.caret;
        control.value_edited = true;
        mutated();
        return value::undefined();
    });
    method("isChecked", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(forms_->state_of(txn, *atoms_, id).checked);
    });
    method("setChecked", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        forms_->state_of(txn, *atoms_, id).checked = context::truthy(arg(args, 0));
        mutated();
        return value::undefined();
    });
    method("focus", [this](context & c, std::span<value>) {
        if (const node_id id = receiver(c); id && on_focus_) { on_focus_(id); }
        return value::undefined();
    });
    method("blur", [this](context &, std::span<value>) {
        if (on_focus_) { on_focus_(node_id{}); }
        return value::undefined();
    });

    // --- canvas --------------------------------------------------------
    method("getContext", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        // "2d" ONLY. Returning an object for "webgl" that cannot draw is
        // worse than returning null: a page feature-detects with exactly
        // this call and would take the WebGL path into a dead end.
        if (!id || arg_string(c, args, 0) != "2d") { return value::null(); }
        return canvas_context_object(c, id);
    });

    // `canvas.toDataURL()` and `canvas.toBlob()` - READING A CANVAS BACK OUT.
    //
    // Both mean PNG: that is what p5's save() asks for, and encode_png writes one
    // with no compression library (see shell/images.hpp). A `type` argument
    // naming anything else still gets PNG rather than a lie about the format -
    // the data URL says image/png, so a page that reads it back is not misled.
    const auto canvas_bytes = [this](context & c) -> std::vector<std::byte> {
        const node_id id = receiver(c);
        if (!id || canvases_ == nullptr) { return {}; }
        // context_for, not pixels_of: a canvas nobody asked getContext of has no
        // surface yet, and a browser still gives you a transparent PNG of the
        // right size rather than nothing. An empty answer here would look like a
        // broken encoder.
        const auto number = [&](std::string_view name, int fallback) {
            const auto txn = doc_->read();
            const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
            int out = 0;
            bool any = false;
            for (const char digit : text) {
                if (digit < '0' || digit > '9') { break; }
                out = out * 10 + (digit - '0');
                any = true;
            }
            return any ? out : fallback;
        };
        (void)canvases_->context_for(id, number("width", 300), number("height", 150));
        const std::shared_ptr<const paint::bitmap> pixels = canvases_->pixels_of(id);
        return pixels ? encode_png(*pixels) : std::vector<std::byte>{};
    };
    method("toDataURL", [canvas_bytes](context & c, std::span<value>) {
        const std::vector<std::byte> png = canvas_bytes(c);
        std::string binary;
        binary.reserve(png.size());
        for (const std::byte b : png) { binary += static_cast<char>(b); }
        // Through the standard library's own btoa, so ONE base64 encoder decides
        // what this means here.
        const value encoder = c.global("btoa");
        if (!encoder.is_callable()) { return c.string("data:image/png;base64,"); }
        const value text = c.string(binary);
        const value args[1] = {text};
        return c.string("data:image/png;base64," + c.to_string(c.call(encoder, args)));
    });
    method("toBlob", [this, canvas_bytes](context & c, std::span<value> args) {
        const value callback = arg(args, 0);
        if (!callback.is_callable()) { return value::undefined(); }
        std::vector<std::byte> png = canvas_bytes(c);
        auto * blob = static_cast<script::object_object *>(c.make_object().as_heap());
        value bytes = c.make_array();
        auto * out = static_cast<script::array_object *>(bytes.as_heap());
        out->elements = script::element_kind::u8;
        out->items.reserve(png.size());
        for (const std::byte b : png) {
            out->items.push_back(value::number(static_cast<double>(static_cast<unsigned char>(b))));
        }
        blob->set("size", value::number(static_cast<double>(png.size())));
        blob->set("type", c.string("image/png"));
        blob->set("__bytes", bytes);
        if (blob_prototype_.is_object()) {
            blob->prototype = blob_prototype_; // so `x instanceof Blob` is true
        }
        // QUEUED, not called: toBlob is asynchronous, and a page that wraps it in
        // a promise - which is what p5's p5.Image.toBlob does - depends on the
        // callback landing after the call returns.
        const value blob_value = value::object(blob);
        c.queue_microtask(callback, std::vector<value>{blob_value});
        return value::undefined();
    });
}

std::shared_ptr<const paint::bitmap> dom_bindings::image_argument(value v) {
    if (images_ == nullptr) { return nullptr; }
    if (v.is_number()) { return images_->at(static_cast<int>(context::to_number(v))); }
    if (!v.is_object()) { return nullptr; }
    auto * wrapper = static_cast<script::object_object *>(v.as_heap());
    const value * handle = wrapper->find(handle_property);
    if (handle == nullptr || assets_ == nullptr) { return nullptr; }
    const node_id id = unpack(static_cast<std::uint64_t>(context::to_number(*handle)));
    const auto txn = doc_->read();
    // A CANVAS IS AN IMAGE SOURCE. `drawImage(otherCanvas, ...)` is how a page
    // composites one surface onto another, and it is what p5's `image(g, ...)`
    // does with a createGraphics - so an offscreen buffer drew nothing at all,
    // silently, because a canvas has no `src` to load and this returned null.
    if (canvases_ != nullptr && atoms_->text(txn.tag(id).value_or(atom{})) == "canvas") {
        return canvases_->pixels_of(id);
    }
    const std::string_view src = txn.attribute_value(id, atoms_->intern("src"));
    return src.empty() ? nullptr : images_->load(*assets_, src);
}

// An `<img>`, which `new Image()` also is.
//
// THE DECISION THAT MADE THE REST FALL OUT: `new Image()` builds a real detached
// <img> element rather than a parallel Image type. So `src` is already a
// reflected attribute, `image_argument` already resolves it, drawImage already
// accepts it, appendChild already displays it, and the CSS box already sizes it.
// A separate object would have needed every one of those taught about it.
//
// What an <img> needs beyond a plain element is the LOADING surface: a src whose
// assignment starts work, a size that falls back to the decoded pixels, and the
// two ways a page learns the load finished.
void dom_bindings::install_image_views(context & cx, script::object_object & obj, node_id id) {
    auto * wrapper = &obj;
    // `naturalWidth` is the DECODED size and `width` is the attribute when there
    // is one, the decoded size otherwise. p5's loadImage reads `img.width`
    // straight after the load to size its own canvas, so answering 0 there gave
    // a zero-by-zero p5.Image that drew nothing.
    const auto decoded = [this, id]() -> std::shared_ptr<const paint::bitmap> {
        if (images_ == nullptr || assets_ == nullptr) { return nullptr; }
        const auto txn = doc_->read();
        const std::string_view src = txn.attribute_value(id, atoms_->intern("src"));
        return src.empty() ? nullptr : images_->load(*assets_, src);
    };
    const auto size_view = [&](std::string property, bool natural_only, bool horizontal) {
        obj.define_accessor(property,
                            value::object(cx.allocate<script::native_object>(
                                property,
                                [this, id, property, natural_only, horizontal,
                                 decoded](context &, std::span<value>) {
                                    if (!natural_only) {
                                        const auto txn = doc_->read();
                                        const std::string_view text =
                                            txn.attribute_value(id, atoms_->intern(property));
                                        double parsed = 0;
                                        bool any = false;
                                        for (const char c : text) {
                                            if (c < '0' || c > '9') { break; }
                                            parsed = parsed * 10 + (c - '0');
                                            any = true;
                                        }
                                        if (any) { return value::number(parsed); }
                                    }
                                    const std::shared_ptr<const paint::bitmap> image = decoded();
                                    if (!image) { return value::number(0); }
                                    return value::number(static_cast<double>(
                                        horizontal ? image->width : image->height));
                                })),
                            value::object(cx.allocate<script::native_object>(
                                property, [this, id, property](context & c, std::span<value> a) {
                                    (void)doc_->set_attribute(
                                        id, atoms_->intern(property),
                                        std::to_string(static_cast<long long>(arg_number(a, 0))));
                                    mutated();
                                    (void)c;
                                    return value::undefined();
                                })));
    };
    size_view("width", false, true);
    size_view("height", false, false);
    size_view("naturalWidth", true, true);
    size_view("naturalHeight", true, false);

    // `src` REFLECTS, and assigning it STARTS A LOAD. The generic reflection two
    // screens up would have given the first half only: the attribute changed and
    // nothing ever told the page the pixels had arrived.
    obj.define_accessor(
        "src",
        value::object(cx.allocate<script::native_object>(
            "src",
            [this, id](context & c, std::span<value>) {
                const auto txn = doc_->read();
                return c.string(std::string{txn.attribute_value(id, atoms_->intern("src"))});
            })),
        value::object(cx.allocate<script::native_object>(
            "src", [this, id, wrapper](context & c, std::span<value> a) {
                const std::string url = arg_string(c, a, 0);
                (void)doc_->set_attribute(id, atoms_->intern("src"), url);
                mutated();
                begin_image_load(value::object(wrapper), id, url, value::undefined());
                return value::undefined();
            })));

    // `complete` is what a page checks before waiting: p5 and many others do
    // `if (img.complete) use(img); else img.onload = ...`, and a `complete` that
    // is always false makes the second branch the only one, forever.
    obj.define_accessor("complete",
                        value::object(cx.allocate<script::native_object>(
                            "complete",
                            [decoded](context &, std::span<value>) {
                                return value::boolean(decoded() != nullptr);
                            })),
                        value::undefined());

    // `decode()` - the promise-shaped way to wait, and the one that does not
    // need a handler property. Already decoded resolves on the next turn rather
    // than immediately, because a promise that settles inside the call it was
    // created by is not one.
    obj.set("decode",
            value::object(cx.allocate<script::native_object>(
                "decode", [this, id, wrapper](context & c, std::span<value>) {
                    const value promise = c.make_pending_promise();
                    const auto txn = doc_->read();
                    begin_image_load(value::object(wrapper), id,
                                     std::string{txn.attribute_value(id, atoms_->intern("src"))},
                                     promise);
                    return promise;
                })));
}

void dom_bindings::begin_image_load(value target, node_id id, std::string url, value promise) {
    image_loads_.push_back(pending_image{target, id, std::move(url), promise});
}

void dom_bindings::settle_image(context & cx, const pending_image & waiting) {
    // The decode happens HERE, on the turn that announces it, and is cached by
    // name in image_store - which is what makes p5's loadImage work at all: it
    // revokes the object URL inside onload, BEFORE drawing the image. The bytes
    // are gone from the registry by then, and the cached bitmap is the browser's
    // "already decoded" state made real.
    const std::shared_ptr<const paint::bitmap> image =
        waiting.url.empty() || images_ == nullptr || assets_ == nullptr
            ? nullptr
            : images_->load(*assets_, waiting.url);
    const bool ok = image != nullptr;

    auto * event = static_cast<script::object_object *>(cx.make_object().as_heap());
    event->set("type", cx.string(ok ? "load" : "error"));
    event->set("target", waiting.target);
    if (!ok) { event->set("message", cx.string("could not load " + waiting.url)); }
    const value as_event = value::object(event);

    if (waiting.promise.is_object()) {
        // decode() rejects with an Error rather than an event, which is what its
        // caller catches.
        value outcome = waiting.target;
        if (!ok) {
            // The ERROR OBJECT, not make_rejection's rejected promise: settling
            // one promise with another gave `undefined` to the catch branch,
            // which is a message about nothing.
            auto * failure = static_cast<script::object_object *>(cx.make_object().as_heap());
            failure->set("name", cx.string("EncodingError"));
            failure->set("message", cx.string("could not decode " + waiting.url));
            outcome = value::object(failure);
        }
        cx.settle_promise(waiting.promise, outcome, !ok);
    }
    fire_at(waiting.id, ok ? "load" : "error", as_event, false);
    // A LOADED IMAGE CHANGES WHAT IS DRAWN. Without this an <img> appended
    // before its bytes arrived kept its empty box until something else happened
    // to invalidate the page.
    if (ok) { mutated(); }
}

value dom_bindings::canvas_context_object(context & cx, node_id id) {
    const auto txn = doc_->read();
    const auto attribute = [&](std::string_view name, int fallback) {
        const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
        if (text.empty()) { return fallback; }
        int value = 0;
        for (const char c : text) {
            if (c < '0' || c > '9') { break; }
            value = value * 10 + (c - '0');
        }
        return value == 0 ? fallback : value;
    };
    canvas_context * canvas =
        canvases_->context_for(id, attribute("width", 300), attribute("height", 150));
    if (canvas == nullptr) { return value::null(); }

    auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
    const value self = value::object(obj);
    obj->set("canvas", wrap(cx, id));
    const auto method = [&](std::string name, script::native_fn fn) {
        obj->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // fillStyle and strokeStyle are PROPERTIES that the drawing calls read
    // back, which is the real canvas idiom - `ctx.fillStyle = 'red'` then
    // `ctx.fillRect(...)`. Reading them at draw time rather than at
    // assignment is what makes that work.
    obj->set("fillStyle", cx.string("#000000"));
    obj->set("strokeStyle", cx.string("#000000"));
    obj->set("lineWidth", value::number(1));
    obj->set("globalAlpha", value::number(1));
    obj->set("font", cx.string("10px sans-serif"));
    obj->set("textAlign", cx.string("start"));
    obj->set("textBaseline", cx.string("alphabetic"));

    const auto sync = [canvas](context & c) {
        const value self_value = c.current_this();
        if (!self_value.is_object()) { return; }
        auto * o = static_cast<script::object_object *>(self_value.as_heap());
        // The spec strings are remembered alongside the parsed values, and only
        // when the parse SUCCEEDED - an unreadable colour leaves both halves
        // alone, so what restore() puts back always matches what is drawn.
        if (const value * v = o->find("fillStyle")) {
            std::string spec = c.to_string(*v);
            if (const auto parsed = paint::parse_color(spec)) {
                canvas->fill_style = *parsed;
                canvas->fill_spec = std::move(spec);
            }
        }
        if (const value * v = o->find("strokeStyle")) {
            std::string spec = c.to_string(*v);
            if (const auto parsed = paint::parse_color(spec)) {
                canvas->stroke_style = *parsed;
                canvas->stroke_spec = std::move(spec);
            }
        }
        if (const value * v = o->find("lineWidth")) {
            canvas->line_width = static_cast<float>(context::to_number(*v));
        }
        if (const value * v = o->find("globalAlpha")) {
            canvas->global_alpha = static_cast<float>(context::to_number(*v));
        }
        if (const value * v = o->find("font")) {
            std::string font = c.to_string(*v);
            canvas->font_size = font_size_from(font);
            font_face_from(font, canvas->font_family, canvas->font_bold, canvas->font_italic);
            canvas->font_spec = std::move(font);
        }
        // Kept as the spec's strings rather than parsed here: an unknown value
        // has to behave as the default, and the drawing code is the one place
        // that knows what the default means for each of them.
        if (const value * v = o->find("textAlign")) { canvas->text_align = c.to_string(*v); }
        if (const value * v = o->find("textBaseline")) { canvas->text_baseline = c.to_string(*v); }
    };

    // A drawing call does NOT report a document mutation. The canvas's
    // pixels are shared with the display list, so nothing needs re-recording
    // - the browser notices the canvas's revision moved and re-rasters. An
    // animation that marked the document dirty would re-run style and layout
    // sixty times a second for a page that did not change.
    const auto draws = [sync](auto body) {
        return [sync, body](context & c, std::span<value> args) {
            sync(c);
            body(c, args);
            return value::undefined();
        };
    };

    method("fillRect", draws([canvas](context &, std::span<value> a) {
               canvas->fill_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("clearRect", draws([canvas](context &, std::span<value> a) {
               canvas->clear_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("strokeRect", draws([canvas](context &, std::span<value> a) {
               canvas->stroke_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("beginPath", draws([canvas](context &, std::span<value>) { canvas->begin_path(); }));
    method("closePath", draws([canvas](context &, std::span<value>) { canvas->close_path(); }));
    method("moveTo", draws([canvas](context &, std::span<value> a) {
               canvas->move_to(number(a, 0), number(a, 1));
           }));
    method("lineTo", draws([canvas](context &, std::span<value> a) {
               canvas->line_to(number(a, 0), number(a, 1));
           }));
    method("rect", draws([canvas](context &, std::span<value> a) {
               canvas->rect_path(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("arc", draws([canvas](context & c, std::span<value> a) {
               canvas->arc(number(a, 0), number(a, 1), number(a, 2), number(a, 3), number(a, 4),
                           a.size() > 5 && context::truthy(a[5]));
               (void)c;
           }));
    // drawImage(image, dx, dy [, dw, dh]) and the source-rect form. `image`
    // is either a loadImage() handle or an <img> element wrapper - the same
    // two things a page can hold, and both have to work.
    method("drawImage", draws([this, canvas](context &, std::span<value> a) {
               const std::shared_ptr<const paint::bitmap> source = image_argument(arg(a, 0));
               if (!source) { return; }
               const auto natural_w = static_cast<float>(source->width);
               const auto natural_h = static_cast<float>(source->height);
               if (a.size() >= 9) {
                   // The nine-argument form takes a rectangle OUT of the source.
                   canvas->draw_image_region(*source, number(a, 1), number(a, 2), number(a, 3),
                                             number(a, 4), number(a, 5), number(a, 6), number(a, 7),
                                             number(a, 8));
                   return;
               }
               const float w = a.size() >= 4 ? number(a, 3) : natural_w;
               const float h = a.size() >= 5 ? number(a, 4) : natural_h;
               canvas->draw_image(*source, number(a, 1), number(a, 2), w, h);
           }));
    // `fill(path)` and `stroke(path)` REPLAY a Path2D rather than using the
    // context's own current path. p5.js draws every shape that way: it builds
    // one Path2D per shape with a visitor and hands it to the context, so a
    // fill() that ignored its argument drew whatever happened to be left in the
    // context - usually nothing.
    //
    // Replaying replaces the current path, which is what the spec's "these
    // methods do not affect the current default path" amounts to here: nothing
    // in this engine reads the path back afterwards.
    const auto replay = [canvas](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_object()) { return; }
        const value commands = c.lookup_property(a[0], std::string{path_commands_property});
        if (!commands.is_array()) { return; }
        canvas->begin_path();
        for (const value & step : static_cast<script::array_object *>(commands.as_heap())->items) {
            if (!step.is_array()) { continue; }
            const auto & parts = static_cast<script::array_object *>(step.as_heap())->items;
            if (parts.empty()) { continue; }
            const std::string verb = c.to_string(parts[0]);
            const auto n = [&](std::size_t i) {
                return i < parts.size() ? static_cast<float>(context::to_number(parts[i])) : 0.0f;
            };
            if (verb == "M") {
                canvas->move_to(n(1), n(2));
            } else if (verb == "L") {
                canvas->line_to(n(1), n(2));
            } else if (verb == "Q") {
                canvas->quadratic_curve_to(n(1), n(2), n(3), n(4));
            } else if (verb == "C") {
                canvas->bezier_curve_to(n(1), n(2), n(3), n(4), n(5), n(6));
            } else if (verb == "R") {
                canvas->rect_path(n(1), n(2), n(3), n(4));
            } else if (verb == "A") {
                canvas->arc(n(1), n(2), n(3), n(4), n(5), n(6) != 0);
            } else if (verb == "E") {
                canvas->ellipse(n(1), n(2), n(3), n(4), n(5), n(6), n(7), n(8) != 0);
            } else if (verb == "Z") {
                canvas->close_path();
            }
        }
    };
    // `clip()` takes the same two forms as fill: a Path2D and/or a rule.
    method("clip", draws([canvas, replay](context & c, std::span<value> a) {
               replay(c, a);
               auto rule = canvas_context::fill_rule::nonzero;
               for (const value & v : a) {
                   if (v.is_string() && c.to_string(v) == "evenodd") {
                       rule = canvas_context::fill_rule::even_odd;
                   }
               }
               canvas->clip(rule);
           }));
    method("fill", draws([canvas, replay](context & c, std::span<value> a) {
               replay(c, a);
               // The rule is the last argument in both forms - `fill(rule)` and
               // `fill(path, rule)` - so it is looked for rather than counted.
               auto rule = canvas_context::fill_rule::nonzero;
               for (const value & v : a) {
                   if (v.is_string() && c.to_string(v) == "evenodd") {
                       rule = canvas_context::fill_rule::even_odd;
                   }
               }
               canvas->fill(rule);
           }));
    method("stroke", draws([canvas, replay](context & c, std::span<value> a) {
               replay(c, a);
               canvas->stroke();
           }));
    method("save", draws([canvas](context &, std::span<value>) { canvas->save(); }));
    // restore() has to write the state BACK TO THE JAVASCRIPT OBJECT, not just
    // pop the C++ stack. Everything on that stack except the transform is also
    // a property script can assign - fillStyle, strokeStyle, lineWidth,
    // globalAlpha, font - and sync() copies those onto the context before every
    // call. A restore that only popped was therefore undone by the very next
    // draw, and the transform appeared to be the only thing save() protected
    // for exactly that reason: it is the one piece of state with no property
    // behind it.
    //
    // Ordering is what makes this correct: draws() runs sync() FIRST, so any
    // assignment made since the last call is folded in before restore() pops
    // over it - which is what the spec means by restoring the state as of the
    // matching save().
    method("restore", draws([canvas](context & c, std::span<value>) {
               canvas->restore();
               const value self_value = c.current_this();
               if (!self_value.is_object()) { return; }
               auto * o = static_cast<script::object_object *>(self_value.as_heap());
               o->set("fillStyle", c.string(canvas->fill_spec));
               o->set("strokeStyle", c.string(canvas->stroke_spec));
               o->set("font", c.string(canvas->font_spec));
               o->set("textAlign", c.string(canvas->text_align));
               o->set("textBaseline", c.string(canvas->text_baseline));
               o->set("lineWidth", value::number(canvas->line_width));
               o->set("globalAlpha", value::number(canvas->global_alpha));
           }));
    method("translate", draws([canvas](context &, std::span<value> a) {
               canvas->translate(number(a, 0), number(a, 1));
           }));
    method("scale", draws([canvas](context &, std::span<value> a) {
               canvas->scale(number(a, 0), number(a, 1));
           }));
    method("rotate",
           draws([canvas](context &, std::span<value> a) { canvas->rotate(number(a, 0)); }));
    method("ellipse", draws([canvas](context &, std::span<value> a) {
               canvas->ellipse(number(a, 0), number(a, 1), number(a, 2), number(a, 3), number(a, 4),
                               number(a, 5), number(a, 6), a.size() > 7 && context::truthy(a[7]));
           }));
    method("resetTransform",
           draws([canvas](context &, std::span<value>) { canvas->reset_transform(); }));
    // `setTransform` REPLACES the matrix and `transform` composes with it. The
    // six numbers are the 2x3 in the spec's order - a, b, c, d, e, f - and a
    // single argument is a matrix-like object, which is the form
    // `ctx.setTransform(ctx.getTransform())` takes.
    const auto matrix_argument = [](context & c, std::span<value> a) {
        if (!a.empty() && a[0].is_object()) {
            const auto component = [&](const char * name, float fallback) {
                const value v = c.lookup_property(a[0], name);
                return v.is_undefined() ? fallback : static_cast<float>(context::to_number(v));
            };
            return transform{component("a", 1), component("b", 0), component("c", 0),
                             component("d", 1), component("e", 0), component("f", 0)};
        }
        return transform{number(a, 0), number(a, 1), number(a, 2),
                         number(a, 3), number(a, 4), number(a, 5)};
    };
    method("setTransform", draws([canvas, matrix_argument](context & c, std::span<value> a) {
               // No arguments is the identity, which is what resetTransform is.
               canvas->set_transform(a.empty() ? transform{} : matrix_argument(c, a));
           }));
    method("transform", draws([canvas, matrix_argument](context & c, std::span<value> a) {
               canvas->multiply_transform(matrix_argument(c, a));
           }));
    // Not wrapped in draws(): reading the matrix changes no pixels. It does
    // still have to be the LIVE one, so a library that reads it back after its
    // own translate() sees the translate.
    method("getTransform", [canvas](context & c, std::span<value>) {
        const transform & t = canvas->current_transform();
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        out->set("a", value::number(t.a));
        out->set("b", value::number(t.b));
        out->set("c", value::number(t.c));
        out->set("d", value::number(t.d));
        out->set("e", value::number(t.e));
        out->set("f", value::number(t.f));
        return value::object(out);
    });
    method("fillText", draws([canvas](context & c, std::span<value> a) {
               canvas->fill_text(a.empty() ? std::string{} : c.to_string(a[0]), number(a, 1),
                                 number(a, 2));
           }));
    // NOT wrapped in draws() - it changes no pixels - but it must still SYNC,
    // and it did not. It is the one method that read the canvas's font state
    // without refreshing it first, so `ctx.font = '20px X'; ctx.measureText(s)`
    // measured in whatever font the last DRAWING call happened to leave behind.
    // --- the pixels, as bytes
    //
    // getImageData/putImageData are how a page reads what it drew and writes
    // back something it computed - every filter, every colour pick, every
    // `pixels[]` loop in p5.js. The buffer is RGBA bytes in that order, which
    // is NOT the engine's packed ARGB, so both directions unpack rather than
    // memcpy: getting that wrong swaps red and blue and looks almost right.
    const auto image_data = [](context & c, int width, int height) {
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        out->set("width", value::number(width));
        out->set("height", value::number(height));
        value bytes = c.make_array();
        auto * store = static_cast<script::array_object *>(bytes.as_heap());
        // A REAL Uint8ClampedArray, so a page's `data[i] = 300` clamps to 255
        // the way it does in a browser rather than storing 300.
        store->elements = script::element_kind::u8_clamped;
        store->items.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4,
                            value::number(0));
        out->set("data", bytes);
        return std::pair{out, store};
    };
    method("createImageData", [image_data](context & c, std::span<value> a) {
        // `createImageData(other)` takes its SIZE from the other one and is
        // still blank, which is what makes it the way to get a scratch buffer.
        int width = static_cast<int>(arg_number(a, 0));
        int height = static_cast<int>(arg_number(a, 1));
        if (!a.empty() && a[0].is_object()) {
            width = static_cast<int>(context::to_number(c.lookup_property(a[0], "width")));
            height = static_cast<int>(context::to_number(c.lookup_property(a[0], "height")));
        }
        return value::object(image_data(c, std::max(0, width), std::max(0, height)).first);
    });
    method("getImageData", [canvas, image_data](context & c, std::span<value> a) {
        const int x = static_cast<int>(arg_number(a, 0));
        const int y = static_cast<int>(arg_number(a, 1));
        const int width = std::max(0, static_cast<int>(arg_number(a, 2)));
        const int height = std::max(0, static_cast<int>(arg_number(a, 3)));
        auto [out, store] = image_data(c, width, height);
        const auto & surface = canvas->surface();
        if (!surface) { return value::object(out); }
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                const color pixel{surface->at(x + column, y + row)};
                const auto at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(column)) *
                                4;
                store->items[at] = value::number(pixel.red());
                store->items[at + 1] = value::number(pixel.green());
                store->items[at + 2] = value::number(pixel.blue());
                store->items[at + 3] = value::number(pixel.alpha());
            }
        }
        return value::object(out);
    });
    method("putImageData", draws([canvas](context & c, std::span<value> a) {
               if (a.empty() || !a[0].is_object()) { return; }
               const value source = c.lookup_property(a[0], "data");
               if (!source.is_array()) { return; }
               const auto & bytes = static_cast<script::array_object *>(source.as_heap())->items;
               const int width =
                   static_cast<int>(context::to_number(c.lookup_property(a[0], "width")));
               const int height =
                   static_cast<int>(context::to_number(c.lookup_property(a[0], "height")));
               const int dx = static_cast<int>(arg_number(a, 1));
               const int dy = static_cast<int>(arg_number(a, 2));
               // Written straight into the surface: putImageData is NOT
               // affected by the transform, the clip or globalAlpha. That is
               // the spec and it is the reason it exists - a page computing
               // pixels wants those pixels, not those pixels composited.
               const auto & surface = canvas->surface();
               if (!surface) { return; }
               for (int row = 0; row < height; ++row) {
                   for (int column = 0; column < width; ++column) {
                       const auto at =
                           (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(column)) *
                           4;
                       if (at + 3 >= bytes.size()) { continue; }
                       const auto channel = [&](std::size_t offset) {
                           return static_cast<std::uint8_t>(
                               std::clamp(context::to_number(bytes[at + offset]), 0.0, 255.0));
                       };
                       const int px = dx + column;
                       const int py = dy + row;
                       if (px < 0 || py < 0 || px >= surface->width || py >= surface->height) {
                           continue;
                       }
                       surface->pixels[static_cast<std::size_t>(py) *
                                           static_cast<std::size_t>(surface->width) +
                                       static_cast<std::size_t>(px)] =
                           color::rgba(channel(0), channel(1), channel(2), channel(3)).argb;
                   }
               }
           }));
    method("measureText", [canvas, sync](context & c, std::span<value> a) {
        sync(c);
        auto * metrics = static_cast<script::object_object *>(c.make_object().as_heap());
        const std::string text = a.empty() ? std::string{} : c.to_string(a[0]);
        // Through the canvas, so the object that measures is the object that
        // draws - see docs/raster.md. Measuring with font8x8 while fillText
        // drew with a real face is exactly how text ends up where it was not.
        const double width = static_cast<double>(canvas->measure_text(text));
        metrics->set("width", value::number(width));
        // THE BOUNDING BOX, not just the advance. A `width` on its own is not
        // enough for a library that positions text itself: p5.js measures a
        // line as `actualBoundingBoxLeft + actualBoundingBoxRight`, which was
        // undefined + undefined = NaN, and every width it derived from that -
        // textWidth, line wrapping, text bounds - was NaN too.
        //
        // The box is measured from the ALIGNMENT POINT, so which side of it the
        // run falls on depends on textAlign. That is what keeps left+right
        // equal to the width whatever the alignment is.
        double left = 0;
        if (canvas->text_align == "center") {
            left = width / 2;
        } else if (canvas->text_align == "right" || canvas->text_align == "end") {
            left = width;
        }
        metrics->set("actualBoundingBoxLeft", value::number(left));
        metrics->set("actualBoundingBoxRight", value::number(width - left));
        // The font's metrics stand in for the glyphs' own extents. They are an
        // over-estimate for a run with no ascenders or descenders, which is the
        // safe direction: text laid out to these bounds never overlaps.
        const auto & backend = canvas->fonts();
        const double ascent = static_cast<double>(backend.ascent(
            canvas->font_size, canvas->font_family, canvas->font_bold, canvas->font_italic));
        const double descent = static_cast<double>(backend.descent(
            canvas->font_size, canvas->font_family, canvas->font_bold, canvas->font_italic));
        metrics->set("actualBoundingBoxAscent", value::number(ascent));
        metrics->set("actualBoundingBoxDescent", value::number(descent));
        metrics->set("fontBoundingBoxAscent", value::number(ascent));
        metrics->set("fontBoundingBoxDescent", value::number(descent));
        metrics->set("emHeightAscent", value::number(ascent));
        metrics->set("emHeightDescent", value::number(descent));
        metrics->set("alphabeticBaseline", value::number(0));
        return value::object(metrics);
    });
    return self;
}

float dom_bindings::number(std::span<value> args, std::size_t i) {
    return i < args.size() ? static_cast<float>(context::to_number(args[i])) : 0.0f;
}

float dom_bindings::font_size_from(std::string_view font) {
    const std::size_t px = font.find("px");
    if (px == std::string_view::npos) { return 10; }
    std::size_t start = px;
    while (start > 0 && font[start - 1] >= '0' && font[start - 1] <= '9') { --start; }
    float size = 0;
    for (std::size_t i = start; i < px; ++i) {
        size = size * 10 + static_cast<float>(font[i] - '0');
    }
    return size > 0 ? size : 10;
}

void dom_bindings::font_face_from(std::string_view font, std::string & family, bool & bold,
                                  bool & italic) {
    family.clear();
    bold = false;
    italic = false;
    const std::size_t px = font.find("px");
    if (px == std::string_view::npos) { return; }

    // Before the size: the style and weight keywords.
    const std::string_view before = font.substr(0, px);
    bold = before.find("bold") != std::string_view::npos;
    italic = before.find("italic") != std::string_view::npos ||
             before.find("oblique") != std::string_view::npos;

    // After it: the family list. The FIRST entry, which is what the rest of the
    // engine resolves too (layout takes the first name of the list as well).
    std::string_view rest = font.substr(px + 2);
    if (const std::size_t comma = rest.find(','); comma != std::string_view::npos) {
        rest = rest.substr(0, comma);
    }
    const std::size_t first = rest.find_first_not_of(" \t'\"");
    if (first == std::string_view::npos) { return; }
    const std::size_t last = rest.find_last_not_of(" \t'\"");
    family = rest.substr(first, last - first + 1);
}

node_id dom_bindings::handle_of(value v) {
    if (!v.is_object()) { return node_id{}; }
    auto * obj = static_cast<script::object_object *>(v.as_heap());
    const value * slot = obj->find(std::string{handle_property});
    return slot == nullptr ? node_id{}
                           : unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
}

std::string dom_bindings::text_of(node_id id) const {
    const auto txn = doc_->read();
    std::string out;
    const auto walk = [&](auto && self, node_id at) -> void {
        out += txn.text(at);
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, id);
    return out;
}

void dom_bindings::set_text(node_id id, std::string text) {
    if (!id) { return; }
    // Copied before removing: children() is a view onto the live child
    // list, and removing while iterating it is a use-after-free waiting for
    // the second child.
    std::vector<node_id> existing;
    {
        const auto txn = doc_->read();
        for (const node_id child : txn.children(id)) { existing.push_back(child); }
    }
    for (const node_id child : existing) { (void)doc_->remove_child(child); }
    if (const node_id created = doc_->create_text(text)) { (void)doc_->append_child(id, created); }
    mutated();
}

void dom_bindings::edit_classes(node_id id, const std::string & name, bool add) {
    if (!id || name.empty()) { return; }
    const auto txn = doc_->read();
    std::vector<std::string> classes;
    for (const std::string_view cls : split(txn.attribute_value(id, atoms_->intern("class")))) {
        if (cls != name) { classes.emplace_back(cls); }
    }
    if (add) { classes.push_back(name); }
    std::string joined;
    for (const std::string & cls : classes) {
        if (!joined.empty()) { joined += ' '; }
        joined += cls;
    }
    (void)doc_->set_attribute(id, atoms_->intern("class"), joined);
    mutated();
}

std::vector<std::string_view> dom_bindings::split(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && text[at] == ' ') { ++at; }
        const std::size_t start = at;
        while (at < text.size() && text[at] != ' ') { ++at; }
        if (at > start) { out.push_back(text.substr(start, at - start)); }
    }
    return out;
}

void dom_bindings::mutated() {
    if (on_mutation_) { on_mutation_(); }
}

void dom_bindings::install_console(context & cx) {
    auto * console = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto log = [this](context & c, std::span<value> args) {
        std::string line;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) { line += ' '; }
            line += c.to_string(args[i]);
        }
        console_.push_back(std::move(line));
        return value::undefined();
    };
    // EVERY name a page calls, all writing to the one list. `console.debug` was
    // absent, and a missing console method is worse than a silent one: p5's own
    // error REPORTER calls it, so a library that had something to say about a
    // failed load threw a second error on top of the first and the real message
    // was never printed. A page's diagnostics must not be able to fail.
    //
    // The grouping and timing ones exist and do nothing, deliberately: a page
    // calls them for a console a test has no way to look at, and throwing is the
    // only outcome that would change what the page does.
    for (const char * name : {"log", "warn", "error", "debug", "info", "trace", "dir"}) {
        console->set(name, value::object(cx.allocate<script::native_object>(name, log)));
    }
    const auto ignore = [](context &, std::span<value>) { return value::undefined(); };
    for (const char * name :
         {"group", "groupEnd", "groupCollapsed", "table", "time", "timeEnd", "assert", "count"}) {
        console->set(name, value::object(cx.allocate<script::native_object>(name, ignore)));
    }
    cx.define_global("console", value::object(console));
}

void dom_bindings::install_document(context & cx) {
    auto * doc = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto method = [&](std::string name, script::native_fn fn) {
        doc->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };

    method("getElementById", [this](context & c, std::span<value> args) {
        return wrap(c, find_by_id(arg_string(c, args, 0)));
    });
    method("createElement", [this](context & c, std::span<value> args) {
        // NOT a mutation: a created element is detached and changes nothing
        // on screen until it is appended.
        return wrap(c, doc_->create_element(atoms_->intern_lower(arg_string(c, args, 0))));
    });
    method("createTextNode", [this](context & c, std::span<value> args) {
        return wrap(c, doc_->create_text(arg_string(c, args, 0)));
    });
    method("addEventListener", [this](context & c, std::span<value> args) {
        listeners_.push_back(make_listener(c, node_id{}, args));
        return value::undefined();
    });
    method("getElementsByTagName", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = all_by_tag(arg_string(c, args, 0));
        auto * list = static_cast<script::object_object *>(c.make_object().as_heap());
        // An ARRAY-SHAPED object: the VM has no Array, so a live collection is
        // indices plus a length, which is what `for (i = 0; i < n; i++)` - the
        // way every page walks one - actually reads.
        for (std::size_t i = 0; i < found.size(); ++i) {
            list->set(std::to_string(i), wrap(c, found[i]));
        }
        list->set("length", value::number(static_cast<double>(found.size())));
        return value::object(list);
    });

    method("querySelector", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = query(arg_string(c, args, 0));
        return found.empty() ? value::null() : wrap(c, found.front());
    });
    method("querySelectorAll", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = query(arg_string(c, args, 0));
        // An ARRAY, not a NodeList: everything a page does with one - index it,
        // read length, walk it - an array already does, and p5 spreads the
        // result into an array anyway.
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const node_id node : found) { items->items.push_back(wrap(c, node)); }
        return out;
    });
    method("hasFocus", [](context &, std::span<value>) {
        // There is one window and a page in it is the thing being looked at.
        // A page asks this to decide whether to keep animating; answering
        // false would make every sketch stop.
        return value::boolean(true);
    });

    // 'complete' BY THE TIME SCRIPT RUNS, which is this engine's model: a page
    // is parsed, its resources are resolved, and only then does anything
    // execute. A library that branches on this - p5.js starts immediately when
    // it reads 'complete' and waits for a `load` event otherwise - takes the
    // branch that matches what actually happened.
    doc->set("readyState", cx.string("complete"));
    // `document.cookie`, IN MEMORY AND FOR THIS PAGE ONLY.
    //
    // An accessor rather than a string, because the API is not a string: READING
    // gives every pair, and WRITING sets ONE of them - `document.cookie = "a=1"`
    // adds to what is there rather than replacing it. A plain property gets that
    // backwards, and a page that stores two things loses the first.
    //
    // Absent, it was worse than wrong: a library reads it unguarded, and
    // `undefined.split(';')` is a TypeError on the first line of its setup.
    //
    // TODO: persist per origin once there IS an origin to scope a jar to - the
    // same reasoning localStorage is written down with, and the same answer: a
    // test that leaves state behind fails the next run for reasons that have
    // nothing to do with the code.
    doc->define_accessor(
        "cookie",
        value::object(cx.allocate<script::native_object>("cookie",
                                                         [this](context & c, std::span<value>) {
                                                             std::string out;
                                                             for (const auto & [name, item] :
                                                                  cookies_) {
                                                                 if (!out.empty()) { out += "; "; }
                                                                 out += name + "=" + item;
                                                             }
                                                             return c.string(out);
                                                         })),
        value::object(cx.allocate<script::native_object>("cookie", [this](context & c,
                                                                          std::span<value> a) {
            const std::string written = arg_string(c, a, 0);
            // Everything after the first `;` is attributes - path, expires,
            // SameSite - and none of them mean anything without an origin
            // or a clock to expire against.
            const std::string pair = written.substr(0, written.find(';'));
            const std::size_t equals = pair.find('=');
            if (equals == std::string::npos) { return value::undefined(); }
            const auto trim = [](std::string_view piece) {
                const std::size_t first = piece.find_first_not_of(" \t");
                if (first == std::string_view::npos) { return std::string{}; }
                return std::string{piece.substr(first, piece.find_last_not_of(" \t") - first + 1)};
            };
            const std::string name = trim(pair.substr(0, equals));
            const std::string item = trim(pair.substr(equals + 1));
            for (auto & [key, held] : cookies_) {
                if (key == name) {
                    held = item;
                    return value::undefined();
                }
            }
            cookies_.emplace_back(name, item);
            return value::undefined();
        })));

    doc->set("body", wrap(cx, find_by_tag("body")));
    doc->set("documentElement", wrap(cx, find_by_tag("html")));
    document_ = value::object(doc);
    cx.define_global("document", document_);
    refresh_document();
}

// The document's own live properties. `body` and `documentElement` can be set
// once because the node never changes; these cannot - a title is rewritten by
// script and the focused element changes on every click - so they are pushed
// again whenever the wrappers are, exactly as location.href is.
void dom_bindings::refresh_document() {
    auto * doc = document_object();
    if (doc == nullptr || cx_ == nullptr) { return; }
    doc->set("title", cx_->string(text_of(find_by_tag("title"))));
    doc->set("activeElement", wrap(*cx_, focused_));
}

void dom_bindings::install_navigation(context & cx) {
    cx.define_native("alert", [this](context & c, std::span<value> args) {
        const std::string message = arg_string(c, args, 0);
        if (on_alert_) { on_alert_(message); }
        return value::undefined();
    });
    cx.define_global("location", location_);
    // `document.location` and `window.location` are the SAME object as the
    // global one, not three copies - a page reads whichever it learned, and
    // they have to agree.
    if (auto * doc = document_object()) { doc->set("location", location_); }
    if (auto * window = window_object()) { window->set("location", location_); }
}

value dom_bindings::make_location(context & cx) {
    auto * loc = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto method = [&](std::string name, script::native_fn fn) {
        loc->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    method("reload", [this](context &, std::span<value>) {
        reload_requested_ = true;
        return value::undefined();
    });
    method("toString", [this](context & c, std::span<value>) { return c.string(location_href_); });
    loc->set("href", cx.string(location_href_));
    loc->set("hash", cx.string(location_hash_));
    write_location_parts(cx, *loc);
    return value::object(loc);
}

script::object_object * dom_bindings::document_object() {
    return document_.is_object() ? static_cast<script::object_object *>(document_.as_heap())
                                 : nullptr;
}

script::object_object * dom_bindings::window_object() {
    return window_.is_object() ? static_cast<script::object_object *>(window_.as_heap()) : nullptr;
}

void dom_bindings::install_window(context & cx) {
    auto * window = static_cast<script::object_object *>(cx.make_object().as_heap());
    window->set("innerWidth", value::number(viewport_width_));
    window->set("innerHeight", value::number(viewport_height_));
    window->set("devicePixelRatio", value::number(1));
    window->set("addEventListener",
                value::object(cx.allocate<script::native_object>(
                    "addEventListener", [this](context & c, std::span<value> args) {
                        listeners_.push_back(make_listener(c, node_id{}, args));
                        return value::undefined();
                    })));
    window->set("removeEventListener",
                value::object(cx.allocate<script::native_object>(
                    "removeEventListener", [this](context & c, std::span<value> args) {
                        const std::string type = arg_string(c, args, 0);
                        const value callback = arg(args, 1);
                        std::erase_if(listeners_, [&](const listener & l) {
                            return !l.target && l.type == type &&
                                   l.callback.bits() == callback.bits();
                        });
                        return value::undefined();
                    })));
    // `localStorage`, IN MEMORY AND FOR THIS PAGE ONLY. 38 uses in p5.js, which
    // reads it before it draws anything.
    //
    // TODO: persist per origin once there IS an origin to scope a store to.
    // Not persisted, deliberately: a test that leaves state behind fails the
    // next run for reasons that have nothing to do with the code, and this
    // engine has no origin to scope a store to anyway. A page gets a working
    // store that starts empty every time, which is what the API promises minus
    // the durability.
    const auto storage = [&](const char * name) {
        auto * store = static_cast<script::object_object *>(cx.make_object().as_heap());
        auto * items = static_cast<script::object_object *>(cx.make_object().as_heap());
        const value backing = value::object(items);
        store->set("__items", backing);
        const auto method = [&](std::string method_name, script::native_fn fn) {
            store->set(method_name, value::object(cx.allocate<script::native_object>(
                                        method_name, std::move(fn))));
        };
        method("getItem", [items](context & c, std::span<value> args) {
            value * held = items->find(arg_string(c, args, 0));
            // A MISS IS null, not undefined - pages branch on `=== null`.
            return held == nullptr ? value::null() : *held;
        });
        method("setItem", [items](context & c, std::span<value> args) {
            items->set(arg_string(c, args, 0), c.string(arg_string(c, args, 1)));
            return value::undefined();
        });
        method("removeItem", [items](context & c, std::span<value> args) {
            (void)items->erase(arg_string(c, args, 0));
            return value::undefined();
        });
        method("clear", [items](context &, std::span<value>) {
            items->props.clear();
            items->index.clear();
            return value::undefined();
        });
        method("key", [items](context & c, std::span<value> args) {
            const auto at =
                static_cast<std::size_t>(std::max(0.0, script::context::to_number(arg(args, 0))));
            return at < items->props.size() ? c.string(items->props[at].first) : value::null();
        });
        store->define_accessor("length",
                               value::object(cx.allocate<script::native_object>(
                                   "length",
                                   [items](context &, std::span<value>) {
                                       return value::number(
                                           static_cast<double>(items->props.size()));
                                   })),
                               value::undefined());
        window->set(name, value::object(store));
        cx.define_global(name, value::object(store));
    };
    storage("localStorage");
    storage("sessionStorage");

    // `new AbortController()`. p5.js makes one in its constructor and passes
    // its signal to every listener it installs, so that removing a sketch can
    // remove them all at once.
    //
    // The signal is carried and honoured by removeEventListener via `abort`;
    // TODO: aborting should cancel an in-flight fetch once fetch stops blocking
    // the frame. Today there is nothing in flight to cancel.
    // what is NOT modelled is aborting an in-flight fetch, because a fetch here
    // does not overlap with anything. A page that aborts one gets a request
    // that already finished, which is a difference worth knowing about.
    cx.define_native("AbortController", [this](context & c, std::span<value>) {
        auto * controller = static_cast<script::object_object *>(c.make_object().as_heap());
        auto * signal = static_cast<script::object_object *>(c.make_object().as_heap());
        signal->set("aborted", value::boolean(false));
        signal->set("reason", value::undefined());
        const value signal_value = value::object(signal);
        controller->set("signal", signal_value);
        controller->set("abort",
                        value::object(c.allocate<script::native_object>(
                            "abort", [this, signal_value](context & inner, std::span<value> args) {
                                auto * s =
                                    static_cast<script::object_object *>(signal_value.as_heap());
                                s->set("aborted", value::boolean(true));
                                s->set("reason", arg(args, 0));
                                // Every listener registered with this signal goes.
                                std::erase_if(listeners_, [&](const listener & l) {
                                    return l.abort_signal.is_heap() &&
                                           l.abort_signal.as_heap() == signal_value.as_heap();
                                });
                                (void)inner;
                                return value::undefined();
                            })));
        return value::object(controller);
    });

    // `new Event(type)` and `window.dispatchEvent(event)`.
    //
    // p5.js finishes starting by announcing itself - `window.dispatchEvent(new
    // Event('p5Ready'))` - and a page that listens for that is how anything
    // else on the page knows p5 is ready. Both were absent, so the library
    // could not complete its own bootstrap.
    //
    // Dispatch here is FLAT: a window event runs the window's listeners for
    // that type, in registration order. There is no capture phase and no
    // bubbling, because a window event has nowhere to bubble from.
    // `Blob`, `URL.createObjectURL` and base64.
    //
    // ONE PIECE OF MACHINERY SERVING TWO FEATURES. `loadImage` fetches bytes,
    // wraps them in a Blob, makes an object URL and points an Image at it; and
    // `save()` wraps bytes in a Blob, makes an object URL and clicks an
    // `<a download>`. Neither works without this and both work with it.
    //
    // AN OBJECT URL IS AN ASSET. Rather than a private table that only images
    // consult, `createObjectURL` registers the bytes in the asset registry under
    // a synthetic `blob:` name - so every path that already resolves a URL
    // resolves this one: `<img src>`, `fetch()`, and p5's loaders. One mechanism
    // instead of three, and nothing had to learn a new kind of URL.
    // A PROTOTYPE, so `x instanceof Blob` is true. p5's downloadFile branches on
    // exactly that - `if (!(saveData instanceof Blob)) saveData = new Blob([data])`
    // - and got the wrong branch, wrapping a Blob in another Blob and copying
    // every byte of an exported image for nothing.
    auto * blob_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    blob_prototype_ = value::object(blob_proto);
    const value blob_prototype = blob_prototype_;
    auto * blob_ctor = cx.allocate<script::native_object>(
        "Blob", [blob_prototype](context & c, std::span<value> a) {
            auto * blob = static_cast<script::object_object *>(c.make_object().as_heap());
            blob->prototype = blob_prototype;
            // `new Blob([parts], { type })`. A part is a string or something with
            // bytes - a typed array, another Blob - which is what a page actually
            // passes: `new Blob([data], { type: contentType })`.
            value bytes = c.make_array();
            auto * out = static_cast<script::array_object *>(bytes.as_heap());
            out->elements = script::element_kind::u8;
            if (!a.empty() && a[0].is_array()) {
                for (const value & part :
                     static_cast<script::array_object *>(a[0].as_heap())->items) {
                    if (part.is_string()) {
                        for (const char ch : c.to_string(part)) {
                            out->items.push_back(
                                value::number(static_cast<double>(static_cast<unsigned char>(ch))));
                        }
                        continue;
                    }
                    // A typed array is bytes already; a Blob carries them in the
                    // same slot this one does.
                    value inner = part;
                    if (part.is_object()) { inner = c.lookup_property(part, "__bytes"); }
                    if (inner.is_array()) {
                        for (const value & b :
                             static_cast<script::array_object *>(inner.as_heap())->items) {
                            out->items.push_back(b);
                        }
                    }
                }
            }
            std::string type;
            if (a.size() > 1 && a[1].is_object()) {
                const value given = c.lookup_property(a[1], "type");
                if (!given.is_undefined()) { type = c.to_string(given); }
            }
            blob->set("size", value::number(static_cast<double>(out->items.size())));
            blob->set("type", c.string(type));
            blob->set("__bytes", bytes);
            return value::object(blob);
        });
    blob_ctor->set("prototype", blob_prototype_);
    cx.define_global("Blob", value::object(blob_ctor));

    {
        auto * url = static_cast<script::object_object *>(cx.make_object().as_heap());
        const auto url_method = [&](std::string name, script::native_fn fn) {
            url->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
        };
        url_method("createObjectURL", [this](context & c, std::span<value> a) {
            if (assets_ == nullptr || a.empty() || !a[0].is_object()) { return c.string(""); }
            const value held = c.lookup_property(a[0], "__bytes");
            std::vector<std::byte> bytes;
            if (held.is_array()) {
                for (const value & b : static_cast<script::array_object *>(held.as_heap())->items) {
                    bytes.push_back(static_cast<std::byte>(
                        static_cast<unsigned char>(std::clamp(context::to_number(b), 0.0, 255.0))));
                }
            }
            // Counted rather than random: `Math.random` is seeded here so a
            // golden can exist, and a URL that changed between runs would defeat
            // that for any page that prints one.
            const std::string name = "blob:ctbrowser/" + std::to_string(++next_object_url_);
            assets_->add(name, std::move(bytes));
            return c.string(name);
        });
        url_method("revokeObjectURL", [this](context & c, std::span<value> a) {
            // Replaced with nothing rather than erased: the registry has no
            // remove, and an empty entry is indistinguishable from a missing one
            // to every reader. A page that revokes and then loads gets the 404
            // it should.
            //
            // An image ALREADY DECODED from this URL survives, because
            // image_store caches by name. That is not an accident to be tidied
            // up - it is the browser's own rule, that revoking frees the bytes
            // and not the decoded image, and p5's loadImage depends on it: it
            // revokes inside onload and draws the image on the next line.
            if (assets_ != nullptr) { assets_->add(arg_string(c, a, 0), {}); }
            return value::undefined();
        });
        cx.define_global("URL", value::object(url));
    }

    // `new Image()`, which is a detached <img>.
    //
    // See install_image_views for why that is the whole implementation: every
    // path that already handles an <img> - src reflection, image_argument,
    // drawImage, appendChild, the CSS box - handles this one with no changes.
    // `new Image(w, h)` sets the presentational size, which p5 uses when it
    // copies a region out of a canvas.
    cx.define_native("Image", [this](context & c, std::span<value> a) {
        const value wrapper = wrap(c, doc_->create_element(atoms_->intern("img")));
        if (!wrapper.is_object()) { return wrapper; }
        for (const auto & [index, name] :
             std::initializer_list<std::pair<std::size_t, const char *>>{{0, "width"},
                                                                         {1, "height"}}) {
            if (a.size() > index) { c.store_property(wrapper, name, a[index]); }
        }
        return wrapper;
    });

    // `new Path2D()` - a path recorded now and drawn later.
    //
    // Every 2D shape p5.js draws goes through one: a visitor walks the shape
    // and emits path verbs, and the renderer hands the result to fill() or
    // stroke(). So this is not an optional corner of the canvas API here, it is
    // the whole 2D drawing path.
    //
    // The verbs are kept in a script array (see path_commands_property) rather
    // than a C++ type: nothing but the canvas replay reads them, the GC already
    // traces arrays, and `new Path2D(other)` is then a copy of one vector.
    cx.define_native("Path2D", [](context & c, std::span<value> args) {
        auto * path = static_cast<script::object_object *>(c.make_object().as_heap());
        value commands = c.make_array();
        auto * steps = static_cast<script::array_object *>(commands.as_heap());
        // `new Path2D(other)` starts as a copy. p5 makes separate fill and
        // stroke paths from one built path exactly this way.
        if (!args.empty() && args[0].is_object()) {
            const value source = c.lookup_property(args[0], std::string{path_commands_property});
            if (source.is_array()) {
                steps->items = static_cast<script::array_object *>(source.as_heap())->items;
            }
        }
        path->set(std::string{path_commands_property}, commands);
        // Each verb records the letter and its numbers, which is the same shape
        // the canvas replay reads back.
        const auto record = [&](std::string name, std::string letter, std::size_t count) {
            path->set(name, value::object(c.allocate<script::native_object>(
                                name, [steps, letter, count](context & inner, std::span<value> a) {
                                    value step = inner.make_array();
                                    auto * parts =
                                        static_cast<script::array_object *>(step.as_heap());
                                    parts->items.push_back(inner.string(letter));
                                    for (std::size_t i = 0; i < count; ++i) {
                                        parts->items.push_back(value::number(arg_number(a, i)));
                                    }
                                    steps->items.push_back(step);
                                    return value::undefined();
                                })));
        };
        record("moveTo", "M", 2);
        record("lineTo", "L", 2);
        record("quadraticCurveTo", "Q", 4);
        record("bezierCurveTo", "C", 6);
        record("rect", "R", 4);
        record("arc", "A", 6);
        record("ellipse", "E", 8);
        record("closePath", "Z", 0);
        // `addPath(other, matrix)` APPLIES THE MATRIX. The verbs are copied
        // with their coordinates already transformed, which is what makes a
        // path built once reusable at several places - p5 passes one when it
        // clips, so this and clip() are the same feature arriving.
        //
        // A point-valued operand transforms exactly. An arc's or an ellipse's
        // RADII do not: a matrix with a skew turns a circle into an ellipse at
        // an angle, which these verbs cannot express. The centre is placed
        // correctly and the radii take the matrix's scale, which is right for
        // the translate/scale/rotate a page actually passes, and is written
        // down here rather than discovered.
        path->set("addPath",
                  value::object(c.allocate<script::native_object>(
                      "addPath", [steps](context & inner, std::span<value> a) {
                          if (a.empty() || !a[0].is_object()) { return value::undefined(); }
                          const value source =
                              inner.lookup_property(a[0], std::string{path_commands_property});
                          if (!source.is_array()) { return value::undefined(); }
                          const auto & from =
                              static_cast<script::array_object *>(source.as_heap())->items;
                          if (a.size() < 2 || !a[1].is_object()) {
                              steps->items.insert(steps->items.end(), from.begin(), from.end());
                              return value::undefined();
                          }
                          const auto part = [&](const char * name, double fallback) {
                              const value v = inner.lookup_property(a[1], name);
                              return v.is_undefined() ? fallback : context::to_number(v);
                          };
                          const double ma = part("a", 1), mb = part("b", 0);
                          const double mc = part("c", 0), md = part("d", 1);
                          const double me = part("e", 0), mf = part("f", 0);
                          // The scale each axis picks up, for the
                          // radius-valued operands.
                          const double sx = std::sqrt(ma * ma + mb * mb);
                          const double sy = std::sqrt(mc * mc + md * md);
                          for (const value & step : from) {
                              if (!step.is_array()) { continue; }
                              const auto & parts =
                                  static_cast<script::array_object *>(step.as_heap())->items;
                              if (parts.empty()) { continue; }
                              value moved = inner.make_array();
                              auto * out = static_cast<script::array_object *>(moved.as_heap());
                              const std::string verb = inner.to_string(parts[0]);
                              out->items.push_back(parts[0]);
                              const auto number = [&](std::size_t i) {
                                  return i < parts.size() ? context::to_number(parts[i]) : 0.0;
                              };
                              const auto push_point = [&](std::size_t i) {
                                  const double x = number(i);
                                  const double y = number(i + 1);
                                  out->items.push_back(value::number(ma * x + mc * y + me));
                                  out->items.push_back(value::number(mb * x + md * y + mf));
                              };
                              if (verb == "M" || verb == "L") {
                                  push_point(1);
                              } else if (verb == "Q") {
                                  push_point(1);
                                  push_point(3);
                              } else if (verb == "C") {
                                  push_point(1);
                                  push_point(3);
                                  push_point(5);
                              } else if (verb == "R") {
                                  push_point(1);
                                  out->items.push_back(value::number(number(3) * sx));
                                  out->items.push_back(value::number(number(4) * sy));
                              } else if (verb == "A") {
                                  push_point(1);
                                  out->items.push_back(value::number(number(3) * sx));
                                  for (std::size_t i = 4; i < parts.size(); ++i) {
                                      out->items.push_back(parts[i]);
                                  }
                              } else if (verb == "E") {
                                  push_point(1);
                                  out->items.push_back(value::number(number(3) * sx));
                                  out->items.push_back(value::number(number(4) * sy));
                                  for (std::size_t i = 5; i < parts.size(); ++i) {
                                      out->items.push_back(parts[i]);
                                  }
                              } else {
                                  for (std::size_t i = 1; i < parts.size(); ++i) {
                                      out->items.push_back(parts[i]);
                                  }
                              }
                              steps->items.push_back(moved);
                          }
                          return value::undefined();
                      })));
        return value::object(path);
    });

    // `new ImageData(w, h)` or `new ImageData(data, w, h)`. A page builds one
    // to hand to putImageData, and a filter that returns a fresh ImageData
    // rather than mutating in place - which is p5.js's own convention - needs
    // to be able to make one.
    cx.define_native("ImageData", [](context & c, std::span<value> args) {
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        // The array form comes FIRST, and the size arguments shift along - the
        // two overloads are told apart by whether argument 0 is a buffer.
        const bool given = !args.empty() && args[0].is_array();
        const int width = static_cast<int>(arg_number(args, given ? 1 : 0));
        int height = static_cast<int>(arg_number(args, given ? 2 : 1));
        value bytes = given ? args[0] : c.make_array();
        auto * store = static_cast<script::array_object *>(bytes.as_heap());
        if (given) {
            // Height is optional when the data is given: it follows from the
            // length, because the buffer is four bytes a pixel by definition.
            if (height <= 0 && width > 0) {
                height =
                    static_cast<int>(store->items.size() / (static_cast<std::size_t>(width) * 4));
            }
        } else {
            store->elements = script::element_kind::u8_clamped;
            store->items.assign(static_cast<std::size_t>(std::max(0, width)) *
                                    static_cast<std::size_t>(std::max(0, height)) * 4,
                                value::number(0));
        }
        out->set("width", value::number(std::max(0, width)));
        out->set("height", value::number(std::max(0, height)));
        out->set("data", bytes);
        return value::object(out);
    });

    cx.define_native("Event", [](context & c, std::span<value> args) {
        auto * event = static_cast<script::object_object *>(c.make_object().as_heap());
        event->set("type", c.string(arg_string(c, args, 0)));
        event->set("bubbles", value::boolean(false));
        event->set("cancelable", value::boolean(false));
        event->set("defaultPrevented", value::boolean(false));
        event->set("target", value::null());
        const auto no_op = [&](const char * name) {
            event->set(name,
                       value::object(c.allocate<script::native_object>(
                           name, [](context &, std::span<value>) { return value::undefined(); })));
        };
        no_op("preventDefault");
        no_op("stopPropagation");
        no_op("stopImmediatePropagation");
        return value::object(event);
    });
    window->set("dispatchEvent", value::object(cx.allocate<script::native_object>(
                                     "dispatchEvent", [this](context & c, std::span<value> args) {
                                         const value event = arg(args, 0);
                                         const std::string type =
                                             event.is_object()
                                                 ? c.to_string(c.lookup_property(event, "type"))
                                                 : c.to_string(event);
                                         // COPIED before dispatch: a listener may add or remove
                                         // one, and appending to the vector being walked
                                         // invalidates it.
                                         std::vector<value> callbacks;
                                         for (const listener & l : listeners_) {
                                             if (!l.target && l.type == type) {
                                                 callbacks.push_back(l.callback);
                                             }
                                         }
                                         const value one[1] = {event};
                                         for (const value & callback : callbacks) {
                                             if (callback.is_callable()) {
                                                 (void)c.call(callback, one);
                                             }
                                         }
                                         return value::boolean(true);
                                     })));

    // `navigator`. A page reads it to decide what it is running in, and
    // `navigator.userAgent.replace(...)` on an absent navigator is undefined
    // twice over before anything notices.
    //
    // The string names this engine rather than imitating a browser. A page that
    // sniffs for Chrome will not find it, which is correct: this is not Chrome,
    // and a page taking a Chrome-only path here would be worse served by a lie.
    {
        auto * navigator = static_cast<script::object_object *>(cx.make_object().as_heap());
        navigator->set("userAgent", cx.string("Mozilla/5.0 (compatible; ctbrowser)"));
        navigator->set("appVersion", cx.string("5.0 (compatible; ctbrowser)"));
        navigator->set("platform", cx.string("ctbrowser"));
        navigator->set("vendor", cx.string(""));
        navigator->set("language", cx.string("en-US"));
        navigator->set("onLine", value::boolean(network_allowed_));
        navigator->set("maxTouchPoints", value::number(0));
        navigator->set("hardwareConcurrency", value::number(1));
        auto * languages = static_cast<script::array_object *>(cx.make_array().as_heap());
        languages->items.push_back(cx.string("en-US"));
        navigator->set("languages", value::object(languages));
        // mediaDevices and getUserMedia are ABSENT rather than stubbed: a page
        // feature-detects them, and a stub that exists but cannot deliver a
        // stream fails later and worse than one that was never there.
        window->set("navigator", value::object(navigator));
        cx.define_global("navigator", value::object(navigator));
    }

    // `screen`. One window, and it is the viewport.
    {
        auto * screen = static_cast<script::object_object *>(cx.make_object().as_heap());
        screen->set("width", value::number(viewport_width_));
        screen->set("height", value::number(viewport_height_));
        screen->set("availWidth", value::number(viewport_width_));
        screen->set("availHeight", value::number(viewport_height_));
        screen->set("colorDepth", value::number(24));
        screen->set("pixelDepth", value::number(24));
        window->set("screen", value::object(screen));
        cx.define_global("screen", value::object(screen));
    }

    auto * performance = static_cast<script::object_object *>(cx.make_object().as_heap());
    performance->set(
        "now", value::object(cx.allocate<script::native_object>(
                   "now", [this](context &, std::span<value>) { return value::number(now_ms_); })));
    window->set("performance", value::object(performance));
    window_ = value::object(window);

    // WINDOW IS THE GLOBAL OBJECT, through a proxy rather than a copy.
    //
    // `globals_` stays the single storage and the window forwards to it, so the
    // two cannot drift - which a second table synchronised at some cadence
    // always eventually does. A miss on the window's own properties reads a
    // global, and a write to a name the window does not already own DEFINES
    // one.
    //
    // Both directions are load-bearing for p5.js. It calls
    // `window.requestAnimationFrame(...)`, which is an ordinary global here; and
    // in global mode it assigns `window.ellipse = ...` for every one of its
    // ~200 drawing functions, which the sketch then calls as bare `ellipse(...)`
    // - so a write that did not reach the globals would leave every one of them
    // undefined at the call site.
    //
    // The other direction matters just as much: `_globalInit` reads
    // `window.setup` to decide whether the sketch is in global mode, and a
    // sketch writes `function setup() {}` at its top level, which is a global.
    auto * window_handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    const value window_target = window_;
    const auto window_trap = [&](std::string name, script::native_fn fn) {
        window_handler->set(name,
                            value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    window_trap("get", [](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
        auto * target = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        if (target->find(name) != nullptr || target->find_accessor(name) != nullptr) {
            return c.lookup_property(args[0], name);
        }
        return c.global(name);
    });
    window_trap("set", [](context & c, std::span<value> args) {
        if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
        auto * target = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        // An own property of the window keeps its own storage - `innerWidth` is
        // refreshed on the object every frame, and routing it to the globals
        // would leave a read seeing the stale one.
        if (target->find(name) != nullptr || target->find_accessor(name) != nullptr) {
            c.store_property(args[0], name, args[2]);
        } else {
            c.define_global(name, args[2]);
        }
        return value::boolean(true);
    });
    window_trap("has", [](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::boolean(false); }
        auto * target = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        return value::boolean(target->find(name) != nullptr ||
                              target->find_accessor(name) != nullptr || c.has_global(name));
    });
    const value window_view = value::object(
        cx.allocate<script::proxy_object>(window_target, value::object(window_handler)));
    cx.define_global("window", window_view);
    cx.define_global("globalThis", window_view);
    cx.define_global("performance", value::object(performance));
}

void dom_bindings::install_resources(context & cx) {
    cx.define_native("loadImage", [this](context & c, std::span<value> args) {
        if (assets_ == nullptr || images_ == nullptr) { return value::number(-1); }
        return value::number(images_->handle_for(*assets_, arg_string(c, args, 0)));
    });
    cx.define_native("imageWidth", [this](context &, std::span<value> args) {
        const auto image =
            images_ != nullptr ? images_->at(static_cast<int>(arg_number(args, 0))) : nullptr;
        return value::number(image ? image->width : 0);
    });
    cx.define_native("imageHeight", [this](context &, std::span<value> args) {
        const auto image =
            images_ != nullptr ? images_->at(static_cast<int>(arg_number(args, 0))) : nullptr;
        return value::number(image ? image->height : 0);
    });

    // fetch(url) -> a settled Response. The registry is consulted FIRST, so
    // a page that ships its resources never opens a socket and a test run
    // is reproducible; a miss goes to the network when the application
    // allows it.
    // `new Request(url, init)`.
    //
    // p5.js does not call fetch with a string. Every loader builds a Request
    // first - `new Request(path, { method: 'GET', mode: 'cors' })` - and hands
    // that to fetch, so an absent Request constructor made loadImage, loadModel,
    // loadShader and loadBytes throw on their first line. The failure named
    // `Request`, which is not obviously about loading an image.
    //
    // The fields are the ones a caller reads back. `method` and `mode` are
    // recorded rather than honoured: there is one transport here and it does a
    // GET, so a POST would be a wrong answer either way - and a page that sets
    // one can at least see what it set.
    cx.define_native("Request", [](context & c, std::span<value> a) {
        auto * request = static_cast<script::object_object *>(c.make_object().as_heap());
        request->set("url", c.string(a.empty() ? std::string{} : c.to_string(a[0])));
        std::string method = "GET";
        std::string mode = "cors";
        if (a.size() > 1 && a[1].is_object()) {
            const value given_method = c.lookup_property(a[1], "method");
            const value given_mode = c.lookup_property(a[1], "mode");
            if (!given_method.is_undefined()) { method = c.to_string(given_method); }
            if (!given_mode.is_undefined()) { mode = c.to_string(given_mode); }
            const value headers = c.lookup_property(a[1], "headers");
            if (!headers.is_undefined()) { request->set("headers", headers); }
            const value body = c.lookup_property(a[1], "body");
            if (!body.is_undefined()) { request->set("body", body); }
        }
        request->set("method", c.string(method));
        request->set("mode", c.string(mode));
        return value::object(request);
    });

    cx.define_native("fetch", [this](context & c, std::span<value> args) {
        // A REQUEST OR A STRING. `fetch(request)` is the form every p5 loader
        // uses, and to_string on an object would have produced "[object Object]"
        // and looked for an asset by that name - a 404 that says nothing about
        // why.
        value target = arg(args, 0);
        if (target.is_object()) {
            const value from_request = c.lookup_property(target, "url");
            if (!from_request.is_undefined()) { target = from_request; }
        }
        const std::string url = c.to_string(target);
        // QUEUED, not done. The promise is pending and the event loop settles
        // it, so a page's other work happens while the request is outstanding -
        // which is the whole point of the API and was not observable before.
        value signal = value::undefined();
        if (arg(args, 1).is_object()) { signal = c.lookup_property(arg(args, 1), "signal"); }
        const value promise = c.make_pending_promise();
        if (promise.is_undefined()) {
            // No promise machinery installed - a bare VM with no standard
            // library. Do it the old way rather than returning nothing.
            return fetch_now(c, url);
        }
        fetches_.push_back(pending_fetch{promise, url, signal});
        return promise;
    });
}

// One queued fetch, resolved. The body work is fetch_now's, which still does
// the deciding - asset registry, then a file next to the page, then the network -
// so there is one answer to "where does a url come from" rather than two.
void dom_bindings::settle_fetch(context & cx, const pending_fetch & waiting) {
    // ABORTED BEFORE IT RAN. The signal is the only reason a queued fetch does
    // not happen, and it is now possible to observe: the request is outstanding
    // for at least one turn, so a page has somewhere to call abort() from.
    if (waiting.signal.is_object() &&
        context::truthy(cx.lookup_property(waiting.signal, "aborted"))) {
        auto * error = static_cast<script::object_object *>(cx.make_object().as_heap());
        error->set("name", cx.string("AbortError"));
        error->set("message", cx.string("fetch of " + waiting.url + " was aborted"));
        cx.settle_promise(waiting.promise, value::object(error), true);
        return;
    }
    // fetch_now hands back a SETTLED promise, so its outcome is adopted rather
    // than wrapped again.
    const value done = fetch_now(cx, waiting.url);
    bool rejected = false;
    value with = done;
    if (done.is_object()) {
        auto * obj = static_cast<script::object_object *>(done.as_heap());
        if (obj->find("__settled") != nullptr) {
            value * held = obj->find("__value");
            value * state = obj->find("__rejected");
            with = held == nullptr ? value::undefined() : *held;
            rejected = state != nullptr && context::truthy(*state);
        }
    }
    cx.settle_promise(waiting.promise, with, rejected);
}

value dom_bindings::fetch_now(context & cx, const std::string & url) {
    std::vector<std::byte> body;
    int status = 200;
    std::string type;
    std::string failure;

    if (assets_ != nullptr && assets_->contains(url)) {
        const std::span<const std::byte> baked = assets_->find(url);
        body.assign(baked.begin(), baked.end());
    } else if (url.find("://") == std::string::npos && assets_ != nullptr) {
        // A relative url is a file next to the page, which is what a
        // page-local `fetch("data.json")` means.
        body = assets_->load(url);
        if (body.empty()) {
            status = 404;
            failure = "no such resource: " + url;
        }
    } else if (network_allowed_) {
        const http_response response = http_get(url);
        status = response.status;
        type = response.content_type;
        body = std::move(response.body);
        if (!response.completed()) { failure = response.error; }
    } else {
        failure = "network access is off and " + url + " was not baked in";
    }

    if (!failure.empty()) {
        // A network failure REJECTS, which is what a page's catch branch is
        // written for. A 404 does not - it is a Response with ok false.
        return make_rejection(cx, failure);
    }
    return make_response(cx, url, status, type, std::move(body));
}

value dom_bindings::make_rejection(context & cx, const std::string & message) {
    auto * error = static_cast<script::object_object *>(cx.make_object().as_heap());
    error->set("message", cx.string(message));
    error->set("name", cx.string("TypeError"));
    return cx.make_promise(value::object(error), true);
}

void dom_bindings::install_timers(context & cx) {
    cx.define_native("setTimeout", [this](context &, std::span<value> args) {
        return value::number(add_timer(arg(args, 0), arg_number(args, 1), false));
    });
    cx.define_native("setInterval", [this](context &, std::span<value> args) {
        return value::number(add_timer(arg(args, 0), arg_number(args, 1), true));
    });
    const auto cancel = [this](context &, std::span<value> args) {
        const auto id = static_cast<std::uint32_t>(arg_number(args, 0));
        for (timer & t : timers_) {
            if (t.id == id) { t.cancelled = true; }
        }
        return value::undefined();
    };
    cx.define_native("clearTimeout", cancel);
    cx.define_native("clearInterval", cancel);
    cx.define_native("requestAnimationFrame", [this](context &, std::span<value> args) {
        animation_callbacks_.push_back(arg(args, 0));
        return value::number(++next_timer_id_);
    });
}

std::uint32_t dom_bindings::add_timer(value callback, double delay_ms, bool repeating) {
    const std::uint32_t id = ++next_timer_id_;
    timers_.push_back(timer{id, callback, now_ms_ + std::max(0.0, delay_ms),
                            std::max(0.0, delay_ms), repeating, false});
    return id;
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

node_id dom_bindings::copy_subtree(const read_txn & from, node_id node, node_id parent) {
    node_id made;
    if (from.kind(node).value_or(node_kind::element) == node_kind::text) {
        made = doc_->create_text(from.text(node));
    } else {
        made = doc_->create_element(from.tag(node).value_or(atom{}), from.element_ns(node));
        for (const attribute & a : from.attributes(node)) {
            (void)doc_->set_attribute(made, a.name, a.value);
        }
    }
    (void)doc_->append_child(parent, made);
    for (const node_id child : from.children(node)) { copy_subtree(from, child, made); }
    return made;
}

// PARSE THE MARKUP, do not store it.
//
// A fragment goes through the same WHATWG tokenizer and tree builder the page
// did - the alternative is a second, worse parser for the commonest way a page
// builds content. `tree_builder::parse` replaces the document's root, so it
// runs against a SCRATCH document; that document shares this one's atom table,
// so copying across needs no name remapping.
void dom_bindings::set_inner_html(node_id target, std::string_view markup) {
    if (!target || atoms_ == nullptr) { return; }
    {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(target);
        const std::vector<node_id> existing{kids.begin(), kids.end()};
        for (const node_id child : existing) { (void)doc_->remove_child(child); }
    }
    document scratch{*atoms_};
    (void)parse_html(scratch, markup);
    const auto from = scratch.read();
    // The builder always makes html/body; the fragment's nodes are body's
    // children. Anything that landed in head - a <style>, a <title> - is not
    // what `el.innerHTML = ...` means and is left behind.
    node_id body{};
    const auto find_body = [&](auto && self, node_id at) -> void {
        if (!body && from.tag(at).value_or(atom{}) == atoms_->intern_lower("body")) { body = at; }
        for (const node_id child : from.children(at)) { self(self, child); }
    };
    find_body(find_body, from.root());
    if (!body) { return; }
    for (const node_id child : from.children(body)) { copy_subtree(from, child, target); }
    mutated();
}

// Read back as markup. A serialiser rather than the original text: the DOM is
// the truth, and a page that appended a node after setting innerHTML expects to
// see it.
std::string dom_bindings::inner_html(node_id target) const {
    const auto txn = doc_->read();
    std::string out;
    const auto write = [&](auto && self, node_id node) -> void {
        if (txn.kind(node).value_or(node_kind::element) == node_kind::text) {
            out += txn.text(node);
            return;
        }
        const std::string_view tag = atoms_->text(txn.tag(node).value_or(atom{}));
        out += "<";
        out += tag;
        for (const attribute & a : txn.attributes(node)) {
            out += " ";
            out += atoms_->text(a.name);
            out += "=\"";
            out += a.value;
            out += "\"";
        }
        out += ">";
        if (ctbrowser::html::is_void_element(tag)) { return; }
        for (const node_id child : txn.children(node)) { self(self, child); }
        out += "</";
        out += tag;
        out += ">";
    };
    for (const node_id child : txn.children(target)) { write(write, child); }
    return out;
}

// Every text node under the element, concatenated - which is what
// `textContent` is, and what makes it the safe way to read a label.
std::string dom_bindings::text_content(node_id target) const {
    const auto txn = doc_->read();
    std::string out;
    const auto walk = [&](auto && self, node_id node) -> void {
        if (txn.kind(node).value_or(node_kind::element) == node_kind::text) {
            out += txn.text(node);
        }
        for (const node_id child : txn.children(node)) { self(self, child); }
    };
    for (const node_id child : txn.children(target)) { walk(walk, child); }
    return out;
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

// The wrapper for a node IF ONE EXISTS. Deliberately does not create one: this
// is on the dispatch path for every event, and making a wrapper per node per
// event would build the whole document's worth of them for a mousemove.
value dom_bindings::value_of_wrapper(node_id id) const {
    if (!id) { return value::undefined(); }
    const auto it = wrappers_.find(pack(id));
    return it == wrappers_.end() || it->second == nullptr ? value::undefined()
                                                          : value::object(it->second);
}

node_id dom_bindings::find_by_id(const std::string & want) {
    const auto txn = doc_->read();
    const atom key = atoms_->intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.attribute_value(at, key) == want) { found = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// `querySelector` / `querySelectorAll`, for COMPOUND selectors.
//
// A compound selector is a tag and any number of `#id` and `.class` parts -
// `canvas`, `#sketch`, `.row.selected`, `div#main` - and a comma-separated
// list of them. That covers what p5.js needs on its load path
// (`document.querySelectorAll('script')`) and what `select()` is used for in
// practice.
//
// TODO: support combinators by reaching the style engine's real matcher. It
// needs element_facts and an ancestor_filter, so either those move somewhere
// both callers can use or the engine grows a `matches(node, selector)` entry
// point that builds them itself.
// COMBINATORS ARE NOT SUPPORTED: `div p`, `ul > li`, `a + b` all match
// nothing. The style engine has a real matcher for those, but it is built
// around the cascade - element facts, an ancestor bloom filter, a rule index -
// and reaching it from here would mean exposing all of that. Saying so is
// better than a half-matcher that silently gets descendants wrong.
std::vector<node_id> dom_bindings::query(std::string_view selector, node_id within) {
    struct compound {
        std::string tag;
        std::string id;
        std::vector<std::string> classes;
    };
    std::vector<compound> wanted;
    for (std::size_t at = 0; at <= selector.size();) {
        const std::size_t comma = selector.find(',', at);
        std::string_view one = selector.substr(
            at, comma == std::string_view::npos ? std::string_view::npos : comma - at);
        at = comma == std::string_view::npos ? selector.size() + 1 : comma + 1;
        while (!one.empty() && one.front() == ' ') { one.remove_prefix(1); }
        while (!one.empty() && one.back() == ' ') { one.remove_suffix(1); }
        if (one.empty() || one.find(' ') != std::string_view::npos ||
            one.find('>') != std::string_view::npos) {
            continue; // a combinator: not supported, matches nothing
        }
        compound part;
        std::size_t i = 0;
        while (i < one.size() && one[i] != '#' && one[i] != '.') { ++i; }
        part.tag = std::string{one.substr(0, i)};
        while (i < one.size()) {
            const char kind = one[i++];
            const std::size_t start = i;
            while (i < one.size() && one[i] != '#' && one[i] != '.') { ++i; }
            std::string name{one.substr(start, i - start)};
            if (kind == '#') {
                part.id = std::move(name);
            } else {
                part.classes.push_back(std::move(name));
            }
        }
        wanted.push_back(std::move(part));
    }
    if (wanted.empty()) { return {}; }

    const auto txn = doc_->read();
    const atom id_attribute = atoms_->intern("id");
    const atom class_attribute = atoms_->intern("class");
    std::vector<node_id> found;
    const auto fits = [&](node_id node, const compound & part) {
        const auto tagged = txn.tag(node);
        if (!tagged) { return false; }
        if (!part.tag.empty() && part.tag != "*" && *tagged != atoms_->intern_lower(part.tag)) {
            return false;
        }
        if (!part.id.empty() && txn.attribute_value(node, id_attribute) != part.id) {
            return false;
        }
        if (!part.classes.empty()) {
            const std::string_view list = txn.attribute_value(node, class_attribute);
            for (const std::string & want : part.classes) {
                bool present = false;
                for (std::size_t from = 0; from < list.size();) {
                    const std::size_t end = list.find(' ', from);
                    const std::string_view one =
                        list.substr(from, end == std::string_view::npos ? end : end - from);
                    if (one == want) { present = true; }
                    if (end == std::string_view::npos) { break; }
                    from = end + 1;
                }
                if (!present) { return false; }
            }
        }
        return true;
    };
    const auto walk = [&](auto && self, node_id at, bool include) -> void {
        if (include) {
            for (const compound & part : wanted) {
                if (fits(at, part)) {
                    found.push_back(at);
                    break;
                }
            }
        }
        for (const node_id child : txn.children(at)) { self(self, child, true); }
    };
    // A search rooted at an ELEMENT looks at its descendants, not itself.
    walk(walk, within ? within : txn.root(), false);
    return found;
}

std::vector<node_id> dom_bindings::all_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    // "*" is every ELEMENT, which is how a page asks for the whole document.
    const bool every = tag == "*";
    const atom want = every ? atom{} : atoms_->intern_lower(tag);
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (const auto tagged = txn.tag(at); tagged && (every || *tagged == want)) {
            found.push_back(at);
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

node_id dom_bindings::find_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    const atom want = atoms_->intern_lower(tag);
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.tag(at).value_or(atom{}) == want) { found = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

} // namespace ctbrowser::shell
