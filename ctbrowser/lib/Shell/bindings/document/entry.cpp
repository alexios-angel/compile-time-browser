// dom_bindings - the entry points: install(), the GC roots, location and
// navigation, and the small mutators the rest of the bindings share.
//
// One of eight files carved out of a 3,071-line bindings/document.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the name productions every one of
// them needs are in internal.hpp beside this. Nothing about the public header
// changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// Every part of the URL, derived from href. Called wherever href is set, so
// the two cannot disagree.
void dom_bindings::write_location_parts(context & cx, script::object_object & loc) {
    const location_url parts = location_parts(location_href_);
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
    // AND THE DOCUMENT'S THREE NAMES FOR THE SAME STRING, for the same reason:
    // `document.URL` set once at install is a snapshot, and a page that follows
    // a link and then reads it gets the address it started at.
    if (cx_ != nullptr) {
        if (auto * doc = document_object()) {
            for (const char * name : {"URL", "documentURI", "baseURI"}) {
                doc->set(name, cx_->string(location_href_));
            }
        }
    }
}

// THE REALM HAS ONE EXTERNAL-ROOTS CALLBACK. `set_external_roots` REPLACES
// rather than appends, so a second dom_bindings registering its own would
// silently unhook the primary's and the page's own document would be swept on
// the next collection. The primary therefore walks itself and then every
// document it has made; a secondary never registers.
void dom_bindings::register_roots(context & cx) {
    cx.set_external_roots([this](const context::root_visitor & mark) { mark_roots(mark); });
}

dom_bindings::~dom_bindings() = default;

void dom_bindings::mark_roots(const context::root_visitor & mark) const {
    for (const listener & l : listeners_) {
        mark(l.callback);
        mark(l.abort_signal);
        // A STANDALONE EventTarget can be reachable from nowhere else: a
        // page may `new EventTarget()`, register on it and drop the
        // variable, and the listener is then the only reference there is.
        mark(l.host);
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
    // A QUEUED READ holds the only reference to the reader whose onload will
    // run and to the blob it is reading.
    for (const pending_read & waiting : reads_) {
        mark(waiting.reader);
        mark(waiting.blob);
    }
    for (const value & callback : animation_callbacks_) { mark(callback); }
    for (const auto & [packed, obj] : wrappers_) {
        if (obj != nullptr) { mark(value::object(obj)); }
    }
    // Blob.prototype is held here as well as on the global, and the global
    // is what keeps it alive - but a page can delete a global, and a Blob
    // whose prototype was collected stops being `instanceof Blob`.
    // A WebGL context object is reachable only from here once the page has
    // dropped its variable, and getContext must still hand back the same one.
    for (const auto & [packed, obj] : webgl_objects_) {
        if (obj != nullptr) { mark(value::object(obj)); }
    }
    mark(blob_prototype_);
    // Event.prototype and CustomEvent.prototype, for the same reason
    // Blob.prototype is here: they are held on a global a page can delete,
    // and an event whose prototype was collected stops being an Event.
    mark(event_prototype_);
    mark(custom_event_prototype_);
    mark(event_target_prototype_);
    // DOMException.prototype, for the same reason: `assert_throws_dom`
    // requires `e.constructor === DOMException`, and a prototype the
    // collector could not see would break that on the first sweep.
    mark(dom_exception_prototype_);
    mark(css_interface_);
    mark(location_);
    mark(document_);
    // The proxy traces its own target, so this is belt and braces - but the
    // two are set in two statements and a collection between them would
    // otherwise sweep the object the proxy is about to point at.
    mark(document_target_);
    mark(window_);
    // AND EVERY DOCUMENT THIS ONE MADE, recursively - a document made by a
    // document made by the page is still reachable only from here.
    for (const auto & made : secondary_documents_) { made->mark_roots(mark); }
}

void dom_bindings::install(context & cx) {
    cx_ = &cx;
    register_roots(cx);
    install_console(cx);
    location_ = make_location(cx);
    install_document(cx);
    install_window(cx);
    // AFTER install_window, which is where `window_` is assigned: this puts
    // `dispatchEvent` on the window object.
    install_event_interfaces(cx);
    // AFTER install_window: it defines the `window` proxy whose handler falls
    // back to the globals, which is what makes one bare global answer both
    // `getComputedStyle(el)` and `window.getComputedStyle(el)`.
    install_computed_style(cx);
    // BEFORE anything that may throw one.
    install_dom_exception(cx);
    install_css_interface(cx);
    install_mutation_observer(cx);
    // AFTER install_css_interface, because the sheet objects throw through
    // `dom_exception_prototype_` and hang their state off `document_`.
    install_style_sheets(cx);
    install_timers(cx);
    install_resources(cx);
    install_navigation(cx);
    // AND THE INTERFACE OBJECTS, LAST AND EAGERLY.
    //
    // `ensure_dom_interfaces` is a no-op until `event_target_prototype_` exists,
    // which is why it cannot run earlier than this; everything else about it is
    // lazy, and lazily is not good enough. The ninety interface objects are
    // GLOBALS - `Text`, `Comment`, `HTMLDivElement`, `NodeList` - and they were
    // defined on the first `wrap()`, so a page whose first statement was
    // `new Text("x")` or `x instanceof HTMLDivElement` asked about a name that
    // did not exist yet and got a TypeError or `false`. A page that had touched
    // one element first got the right answer. That is the shape of defect that
    // reads as flakiness, and it was the same one behind `createHTMLDocument`
    // and `make_live_collection` earlier on this branch.
    ensure_dom_interfaces(cx);
}

// THE HANDLE HAS TO BE ONE OF OURS, and that is what the second half of this
// checks. `pack(node_id)` is a slot and a generation into ONE slab, so the
// same number names a different node in a different document - and there are
// two documents now. Reading a foreign wrapper's handle would hand back
// whatever node happens to sit in that slot HERE, which is exactly the silent
// wrong answer `createHTMLDocument` was refused over.
//
// The test is exact and costs one lookup: a wrapper is ours if and only if OUR
// table maps its key to THAT object. Nothing is ever erased from `wrappers_`,
// so a node that has been removed from the tree still answers - which is what
// a page holding a detached element needs.
//
// It also refuses a handle a page FABRICATED. `{__node: 5}` used to name node
// 5; it now names nothing, which is what it always meant.
node_id dom_bindings::handle_of(value v) {
    if (!v.is_object()) { return node_id{}; }
    auto * obj = static_cast<script::object_object *>(v.as_heap());
    const value * slot = obj->find(std::string{handle_property});
    if (slot == nullptr) { return node_id{}; }
    const auto packed = static_cast<std::uint64_t>(context::to_number(*slot));
    const auto held = wrappers_.find(packed);
    if (held == wrappers_.end() || held->second != obj) { return node_id{}; }
    return unpack(packed);
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
    // THE MUTATION OBSERVERS FIRST, and from here rather than from each of the 23
    // natives that change the document: this is the funnel they all already go
    // through. It costs one branch on a page that never made an observer.
    record_mutations();
    // An `<iframe>` can only appear, change its `src` or leave through a
    // mutation, so this is where the reconcile is told there is something to
    // look at. The walk itself is not done here: it needs the script context
    // and it must not run inside a native that is halfway through a tree edit.
    frames_dirty_ = true;
    if (on_mutation_) { on_mutation_(); }
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
    if (auto * doc = document_object()) {
        doc->set("location", location_);
        // `document.URL`, `documentURI` and `baseURI` are the same string as
        // location.href and are set beside it so they cannot drift. Three
        // spellings because three specifications named it: URL is HTML's,
        // documentURI is the DOM's, and baseURI is what a relative link is
        // resolved against - which is the document's URL here, there being no
        // <base> support to move it.
        for (const char * name : {"URL", "documentURI", "baseURI"}) {
            doc->set(name, cx.string(location_href_));
        }
        // `document.defaultView` IS THE WINDOW, and it has to be the PROXY -
        // the same object `window` and `self` name - rather than the object
        // behind it. A page compares the two by identity, and a second window
        // object that answered the same questions would still fail
        // `document.defaultView === window`.
        //
        // MEASURED, and it is worth the note because the failure was nowhere
        // near the cause. `assert_throws_dom` takes an optional DOMException
        // CONSTRUCTOR as its second argument so a test can say which global the
        // exception must have come from, and `Document-createElementNS.html`
        // passes `doc.defaultView.DOMException` for every one of its throwing
        // cases. With defaultView undefined that argument was undefined, so
        // testharness took its no-constructor branch, treated the undefined as
        // the FUNCTION to call, and reported 110 assertions as "threw an object
        // that is not a DOMException" - a message about the exception, from a
        // test that had not yet called anything.
        //
        // Set here rather than in install_document because the window proxy
        // does not exist until install_window has run, and install_navigation
        // is the first thing after it that already reaches for both.
        doc->set("defaultView", cx.global("window"));
    }
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

// THE TARGET, not the proxy. Everything in these bindings that writes a
// property on the document goes through here, and a write to the proxy would
// be a write to a `set` trap that does not exist. See the two members.
script::object_object * dom_bindings::document_object() {
    return document_target_.is_object()
               ? static_cast<script::object_object *>(document_target_.as_heap())
               : nullptr;
}

script::object_object * dom_bindings::window_object() {
    return window_.is_object() ? static_cast<script::object_object *>(window_.as_heap()) : nullptr;
}

} // namespace ctbrowser::shell
