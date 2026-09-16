// dom_bindings - the entry points: install(), the GC roots, location and
// navigation, and the small mutators the rest of the bindings share.

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
    // THE INDICATED ELEMENT, HTML 7.4.6.3: the fragment names an id - as
    // written, then percent-decoded - and that element is what `:target`
    // matches. Kept as a state bit on the selector engine, exactly as `:focus`
    // is, so a query and a sheet both see it; the browser re-resolves on the
    // mutation hook, and a frame document's own engine answers its queries.
    node_id fresh;
    if (location_hash_.size() > 1) {
        const std::string_view fragment = std::string_view{location_hash_}.substr(1);
        fresh = find_by_id(std::string{fragment});
        if (!fresh && fragment.find('%') != std::string_view::npos) {
            fresh = find_by_id(percent_decode(fragment));
        }
    }
    if (fresh == target_element_) { return; }
    style::engine & states = selector_engine();
    (void)states.set_state(target_element_, style::state_target, false);
    (void)states.set_state(fresh, style::state_target, true);
    target_element_ = fresh;
    if (on_mutation_) { on_mutation_(); }
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
    for (const auto & [packed, obj] : adopted_away_) {
        if (obj != nullptr) { mark(value::object(obj)); }
    }
    for (const auto & [name, held] : named_collections_) { mark(held); }
    for (const auto & [packed, held] : attr_objects_) {
        for (const auto & [key, obj] : held) {
            if (obj != nullptr) { mark(value::object(obj)); }
        }
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
    install_range(cx);
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
    // AFTER the interface table: `document.getSelection` goes on
    // Document.prototype, which does not exist until ensure_dom_interfaces
    // has built it.
    install_selection(cx);
}

// THE HANDLE HAS TO BE ONE OF OURS, and that is what the second half of this
// checks. `node_id.key()` is a slot and a generation into ONE slab, so the
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
    // CharacterData: `textContent = x` on a Text, Comment, CDATA section or PI
    // sets ITS data - "replace data" - and a DocumentType ignores it. Only an
    // element or a fragment replaces its children.
    switch (doc_->read().kind(id).value_or(node_kind::element)) {
    case node_kind::text:
    case node_kind::comment:
    case node_kind::cdata_section:
    case node_kind::processing_instruction:
        (void)doc_->set_text(id, text);
        mutated();
        return;
    case node_kind::document_type: return;
    case node_kind::document:
    case node_kind::element:
    case node_kind::document_fragment: break;
    }
    // Copied before removing: children() is a view onto the live child
    // list, and removing while iterating it is a use-after-free waiting for
    // the second child.
    std::vector<node_id> existing;
    {
        const auto txn = doc_->read();
        for (const node_id child : txn.children(id)) { existing.push_back(child); }
    }
    for (const node_id child : existing) { (void)doc_->remove_child(child); }
    // "String replace all", DOM 4.4: the Text node is made ONLY IF the string
    // is not empty. `el.textContent = ""` leaves no child, and
    // `Node-textContent.html` asserts `firstChild` is null afterwards.
    if (!text.empty()) {
        if (const node_id created = doc_->create_text(text)) {
            (void)doc_->append_child(id, created);
        }
    }
    mutated();
}

void dom_bindings::mutated() {
    // THE MUTATION OBSERVERS FIRST, and from here rather than from each of the 23
    // natives that change the document: this is the funnel they all already go
    // through. It costs one branch on a page that never made an observer.
    record_mutations();
    // A "replace all" note is for the mutation it preceded and no other.
    replace_all_.reset();
    // An `<iframe>` can only appear, change its `src` or leave through a
    // mutation, so this is where the reconcile is told there is something to
    // look at. The walk itself is not done here: it needs the script context
    // and it must not run inside a native that is halfway through a tree edit.
    frames_dirty_ = true;
    if (on_mutation_) { on_mutation_(); }
    moved_by_mutation_.clear();
    // THE TWO THAT RUN SCRIPT, last: an inserted <script>'s post-connection
    // steps, then the custom element reactions - a connectedCallback may mutate
    // again and arrive back here - and everything above it is bookkeeping.
    run_inserted_scripts();
    react_custom_elements();
}

// HTML 4.12.1 "prepare the script element", for a script a page inserted:
// connected, with a src or non-empty text, and not `already started` - which
// is what leaving the list means. THE BATCH IS TAKEN FIRST: every script the
// mutation made ready leaves the list before any of them runs, so a script
// that gives a later one its text arrives back here from that insertion and
// finds only the later one - which runs nested, ahead of the rest of the
// batch, and that is the order the post-connection steps have.
// ponytail: creation order, not tree order - the corpus inserts in the order
// it creates. Sort by tree position if a page ever depends on it.
void dom_bindings::run_inserted_scripts() {
    if (unstarted_scripts_.empty() || cx_ == nullptr || secondary_ || moving_) { return; }
    context & cx = *cx_;
    const atom src_name = atoms_->intern("src");
    const atom type_name = atoms_->intern("type");
    struct prepared {
        node_id id;
        std::string source;
        std::string src;
        std::string type;
    };
    std::vector<prepared> batch;
    {
        const auto txn = doc_->read();
        for (std::size_t i = 0; i < unstarted_scripts_.size();) {
            const node_id id = unstarted_scripts_[i];
            if (!txn.kind(id).has_value()) {
                unstarted_scripts_.erase(unstarted_scripts_.begin() +
                                         static_cast<std::ptrdiff_t>(i));
                continue;
            }
            prepared script{
                id,
                {},
                std::string{txn.attribute_value(id, src_name)},
                ascii_lower_copy(trim(txn.attribute_value(id, type_name), html_whitespace))};
            // "Child text content": the Text children only, not a comment's.
            for (const node_id child : txn.children(id)) {
                if (is_text_kind(txn.kind(child).value_or(node_kind::comment))) {
                    script.source += txn.text(child);
                }
            }
            if ((script.src.empty() && script.source.empty()) ||
                root_of_tree(txn, id, true) != txn.root()) {
                ++i;
                continue;
            }
            // Started, whatever happens next: a script that fails to parse,
            // or whose src is missing, does not run again when its children
            // change.
            unstarted_scripts_.erase(unstarted_scripts_.begin() + static_cast<std::ptrdiff_t>(i));
            batch.push_back(std::move(script));
        }
    }
    for (auto & [id, source, src, type] : batch) {
        // AN EARLIER SCRIPT OF THE BATCH MAY HAVE REMOVED THIS ONE, and a
        // script that is not connected when its turn comes does not run - it
        // was never started, so it stays on the list for a later insertion
        // (later-script-removed-by-earlier-script.html).
        {
            const auto txn = doc_->read();
            if (root_of_tree(txn, id, true) != txn.root()) {
                unstarted_scripts_.push_back(id);
                continue;
            }
        }
        // A classic script only. A data block (`type="text/plain"`) runs
        // nothing; a module's loader is the browser's and is not reached from
        // a mutation.
        if (!type.empty() && type != "text/javascript" && type != "application/javascript" &&
            type != "module") {
            continue;
        }
        if (type == "module") { continue; }
        if (!src.empty()) {
            const std::vector<std::byte> bytes =
                assets_ == nullptr ? std::vector<std::byte>{} : assets_->load(src);
            announce_load(id, !bytes.empty());
            if (bytes.empty()) { continue; }
            source.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        }
        // `document.currentScript` is this element while it runs - and while
        // its parse error is reported - and what it was, the outer script when
        // there is one, afterwards.
        value outer = value::null();
        if (auto * doc = document_object()) {
            if (const value * had = doc->find("currentScript"); had != nullptr) { outer = *had; }
        }
        // POST-CONNECTION ORDER, DOM "insert" step 7.7: the post-connection
        // steps run per inserted node in tree order, and an <iframe>'s are
        // what give it a document. So a frame that PRECEDES this script has
        // one by the time the script runs, and a frame that FOLLOWS it does
        // not yet - `iframe.contentWindow` is null from inside the script
        // (Node-appendChild-script-and-iframe.html). The frames before are
        // loaded here and the lazy reconcile is held off while the script
        // runs; a mutation the script makes re-arms it.
        // ponytail: tree order stands in for insertion order, so a frame an
        // EARLIER mutation inserted later in the tree is held off too; keep
        // the batch's own nodes if a page ever shows the difference.
        {
            std::vector<std::pair<node_id, std::string>> preceding;
            {
                const auto txn = doc_->read();
                const atom iframe_tag = atoms_->intern_lower("iframe");
                bool passed = false;
                const auto walk = [&](auto && self, node_id at) -> void {
                    if (passed) { return; }
                    if (at == id) {
                        passed = true;
                        return;
                    }
                    if (txn.tag(at).value_or(atom{}) == iframe_tag &&
                        txn.element_ns(at) == node_ns::html) {
                        const bool loaded = std::ranges::any_of(
                            frames_, [at](const auto & entry) { return entry.element == at; });
                        if (!loaded) {
                            preceding.emplace_back(at,
                                                   std::string{txn.attribute_value(at, src_name)});
                        }
                    }
                    for (const node_id child : txn.children(at)) { self(self, child); }
                };
                walk(walk, txn.root());
            }
            for (const auto & [frame, src] : preceding) {
                frames_.push_back(frame_entry{frame, src, load_frame(cx, frame, src)});
            }
        }
        const bool frames_were_dirty = frames_dirty_;
        frames_dirty_ = false;
        set_current_script(id);
        script::program compiled = script::compiler::compile(source);
        if (!compiled.ok) {
            (void)dispatch_error(compiled.error);
            if (auto * doc = document_object()) { doc->set("currentScript", outer); }
            frames_dirty_ = frames_dirty_ || frames_were_dirty;
            continue;
        }
        const script::program & kept = cx.own_program(std::move(compiled));
        auto * entry = cx.allocate<script::closure_object>(&kept.functions[0]);
        entry->owner = &kept;
        bool threw = false;
        value thrown = value::undefined();
        (void)cx.call_fenced(value::object(entry), {}, cx.global_this(), threw, thrown);
        // AN UNCAUGHT THROW IS REPORTED AND THE INSERTING SCRIPT CARRIES ON,
        // exactly as a listener's is - see fire_at for the two ways to fail.
        // Reported BEFORE currentScript is put back: "run a classic script"
        // reports the exception inside "execute the script element" step 6,
        // and step 7 is the restore - so a window.onerror reading
        // document.currentScript sees the script that threw
        // (Document.currentScript.html, "script-window-error").
        if (threw || cx.failed()) {
            const context::rooted keep_thrown{cx, thrown};
            const std::string fault =
                cx.failed()
                    ? cx.take_error()
                    : "uncaught " + (thrown.is_object()
                                         ? cx.to_string(cx.lookup_property(thrown, "message"))
                                         : cx.to_string(thrown));
            const bool handled = dispatch_error_value(fault, thrown);
            if (!handled && callback_error_.empty()) { callback_error_ = fault; }
        }
        if (auto * doc = document_object()) { doc->set("currentScript", outer); }
        frames_dirty_ = frames_dirty_ || frames_were_dirty;
    }
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
    auto * loc = cx.allocate<script::object_object>();
    set_method(cx, *loc, "reload", [this](context &, std::span<value>) {
        reload_requested_ = true;
        return value::undefined();
    });
    set_method(cx, *loc, "toString",
               [this](context & c, std::span<value>) { return c.string(location_href_); });
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
