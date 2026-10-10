//===- entry.cpp - Document bindings entry points ---------------*- C++ -*-===//
//
// dom_bindings' entry points: install(), the GC roots, location and
// navigation, and the small mutators the rest of the bindings share.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include "../element/internal.hpp" // document_base_url

#include <optional>

namespace ctbrowser::shell {

using namespace detail;

/// Records the page's new address and updates what depends on it: the
/// document's URL properties and the `:target` element.
void dom_bindings::observe_location(std::string href, std::string hash) {
    location_href_ = std::move(href);
    location_hash_ = std::move(hash);
    // Keep document.URL, documentURI and baseURI in step with the address.
    // The Location getters read the same two strings.
    if (cx_ != nullptr) {
        if (auto * doc = document_object()) {
            for (const char * name : {"URL", "documentURI", "baseURI"}) {
                doc->set(name, cx_->string(location_href_));
            }
        }
    }
    // The indicated element, HTML 7.4.6.3: the fragment names an id (as
    // written, then percent-decoded), and that element is what `:target`
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

/// Registers this document's GC roots and its weak live-range list with the
/// realm. Only the primary calls this.
///
/// The realm has one external-roots callback: `set_external_roots` replaces
/// rather than appends, so a second dom_bindings registering its own would
/// silently unhook the primary's and the page's own document would be swept
/// on the next collection. The primary therefore walks itself and then every
/// document it has made; a secondary never registers.
void dom_bindings::register_roots(context & cx) {
    cx.set_external_roots([this](const context::root_visitor & mark) { mark_roots(mark); });
    // Live ranges are weak (DOM 5.5): nothing keeps a Range or NodeIterator
    // alive. Dead ones are pruned after each collection rather than marked.
    // Marking them leaked every range a page made and made every mutation walk
    // them all.
    cx.set_weak_prune([this](const context::survives_fn & survives) {
        std::erase_if(live_ranges_, [&](const value & range) { return !survives(range); });
    });
}

dom_bindings::~dom_bindings() = default;

/// Marks every value only these bindings keep alive, then recurses into each
/// document this one made.
void dom_bindings::mark_roots(const context::root_visitor & mark) const {
    for (const listener & l : listeners_.entries()) {
        mark(l.payload.callback);
        mark(l.payload.abort_signal);
        // A standalone EventTarget can be reachable from nowhere else: a
        // page may `new EventTarget()`, register on it and drop the
        // variable, and the listener is then the only reference there is.
        mark(l.payload.host);
    }
    for (const timer & t : timers_) {
        mark(t.callback);
        mark(t.window);
        for (const value held : t.arguments) { mark(held); }
    }
    // A queued fetch holds the only reference to the promise a page is
    // waiting on, and to the signal that may cancel it. Neither is reachable
    // from anywhere else between the call and the turn that settles it.
    for (const pending_fetch & waiting : fetches_) {
        mark(waiting.promise);
        mark(waiting.signal);
    }
    // A queued image load holds the only reference to the wrapper whose
    // onload will run and to decode()'s promise.
    for (const pending_image & waiting : image_loads_) {
        mark(waiting.target);
        mark(waiting.promise);
    }
    // A queued read holds the only reference to the reader whose onload will
    // run and to the blob it is reading.
    for (const pending_read & waiting : reads_) {
        mark(waiting.reader);
        mark(waiting.blob);
    }
    for (const animation_frame_callback & frame : animation_callbacks_) { mark(frame.callback); }
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
    // A WebGL context object is reachable only from here once the page has
    // dropped its variable, and getContext must still return the same one.
    for (const auto & [packed, obj] : webgl_objects_) {
        if (obj != nullptr) { mark(value::object(obj)); }
    }
    // Blob.prototype is held here as well as on the global, which normally
    // keeps it alive; but a page can delete a global, and a Blob whose
    // prototype was collected stops being `instanceof Blob`.
    mark(blob_prototype_);
    // The rendering contexts' prototypes: their natives capture the
    // CanvasGradient, CanvasPattern and TextMetrics prototypes by pointer and
    // reach them through private keys here, and a page may delete the globals.
    mark(canvas2d_prototype_);
    mark(webgl_prototype_);
    mark(webgl2_prototype_);
    // Event.prototype and CustomEvent.prototype, for the same reason
    // Blob.prototype is here: they are held on a global a page can delete,
    // and an event whose prototype was collected stops being an Event.
    mark(event_prototype_);
    mark(custom_event_prototype_);
    mark(message_event_prototype_);
    mark(event_target_prototype_);
    mark(abort_signal_prototype_);
    mark(media_query_list_prototype_);
    // Mark every animation's objects here, not only those in an `Animation`
    // interface's `retained` list (sync_animation_roots). A style flush creates
    // a frame document's CSS animations even when its bindings lack that
    // interface. Without this, their `finished` promises were collected and
    // then read by cancel_record (found by AddressSanitizer).
    for (const animation_record & a : animations_) {
        mark(a.self);
        mark(a.finished);
    }
    for (const keyframe_effect_record & e : effects_) { mark(e.self); }
    for (const value & list : pending_media_changes_) { mark(list); }
    for (const value & promise : rejections_to_notify_) { mark(promise); }
    for (const value & promise : outstanding_rejections_) { mark(promise); }
    // DOMException.prototype, for the same reason: `assert_throws_dom`
    // requires `e.constructor === DOMException`, and a prototype the
    // collector could not see would break that on the first sweep.
    mark(dom_exception_prototype_);
    mark(css_interface_);
    mark(location_);
    mark(document_);
    // The proxy traces its own target, so this is belt and braces, but the
    // two are set in two statements and a collection between them would
    // otherwise sweep the object the proxy is about to point at.
    mark(document_target_);
    mark(window_);
    mark(window_view_);
    // And every document this one made, recursively: a document made by a
    // document made by the page is still reachable only from here.
    for (const auto & made : secondary_documents_) { made->mark_roots(mark); }
}

/// Installs the document's answer to the parser's question for DOM 4.8 step 3
/// (dom/document.hpp): does a custom element definition (the page's registry
/// holds them all) disable a shadow root on `host`?
void dom_bindings::watch_shadow_disabled() {
    if (doc_ == nullptr) { return; }
    // The hook may outlive these bindings (a reload makes new ones over the
    // same document, and a navigation destroys the document first), so it
    // holds a weak token rather than trusting `this`, and nothing here
    // touches the document on the way out.
    doc_->set_shadow_disabled_hook([this, alive = std::weak_ptr<int>{lifetime_}](node_id host) {
        if (alive.expired()) { return false; }
        const auto txn = doc_->read();
        const std::size_t def = custom_definition_for(txn, host);
        const dom_bindings & registry = primary_ == nullptr ? *this : *primary_;
        return def < registry.custom_definitions_.size() &&
               registry.custom_definitions_[def].disable_shadow;
    });
}

/// Installs the whole page-facing API into `cx`'s global object. The order of
/// the calls below matters; each constraint is noted where it applies.
void dom_bindings::install(context & cx) {
    cx_ = &cx;
    watch_shadow_disabled();
    register_roots(cx);
    install_console(cx);
    location_ = make_location(cx);
    install_document(cx);
    install_window(cx);
    // After install_window, which is where `window_` is assigned: this puts
    // `dispatchEvent` on the window object.
    install_event_interfaces(cx);
    // After install_window: it defines the `window` proxy whose handler falls
    // back to the globals, which is what makes one bare global answer both
    // `getComputedStyle(el)` and `window.getComputedStyle(el)`.
    install_computed_style(cx);
    // Before anything that may throw a DOMException.
    install_dom_exception(cx);
    // After both: a signal is an EventTarget and aborts with a DOMException.
    install_abort(cx);
    install_media_queries(cx);
    install_promise_rejections(cx);
    install_xhr(cx);
    install_css_interface(cx);
    install_mutation_observer(cx);
    install_range(cx);
    // After install_css_interface, because the sheet objects throw through
    // `dom_exception_prototype_` and hang their state off `document_`.
    install_style_sheets(cx);
    install_timers(cx);
    install_image_bitmap(cx);
    install_resources(cx);
    install_navigation(cx);
    // The interface objects, last and eagerly. ensure_dom_interfaces does
    // nothing until event_target_prototype_ exists, so it cannot run earlier.
    // It must run eagerly: the interface objects (`Text`, `HTMLDivElement`,
    // `NodeList`, ...) are globals. If they were built lazily on the first
    // wrap(), a page whose first statement is `new Text("x")` or
    // `x instanceof HTMLDivElement` would get a TypeError or `false`.
    ensure_dom_interfaces(cx);
    // `Image` is a legacy factory function for HTMLImageElement (HTML
    // 4.8.4.1): its `prototype` is HTMLImageElement.prototype, so
    // `new Image() instanceof Image` answers true, as does every <img>. Set
    // here, where the interface prototypes exist.
    if (const value image = cx.global("Image"); image.is_kind(script::heap_kind::native)) {
        if (const value proto = interface_prototype("HTMLImageElement"); proto.is_object()) {
            static_cast<script::native_object *>(image.as_heap())
                ->define("prototype", proto, script::attr_none);
        }
    }
    // After the interface table: `document.getSelection` goes on
    // Document.prototype, which does not exist until ensure_dom_interfaces
    // has built it.
    install_selection(cx);
    // After the interface table for the same reason: HTMLSlotElement.prototype,
    // Element.prototype and ShadowRoot.prototype are where slots and
    // setHTMLUnsafe go.
    install_shadow_dom(cx);
    install_autocomplete(cx);
    install_xml_serializer(cx);
}

/// Returns the node a wrapper stands for, or an empty id when `v` is not one
/// of this document's wrappers. node_id keys are slot/generation pairs in one
/// document's slab, so a wrapper from another document (createHTMLDocument,
/// a frame) would otherwise name an unrelated node here. A wrapper counts as
/// ours only if wrappers_ maps its key to that exact object, so an object a
/// page builds by hand, such as `{__node: 5}`, names nothing. Entries are
/// never erased, so a detached element still resolves.
node_id dom_bindings::handle_of(value v) {
    v = unwrap_legacy_proxy(v); // a form's or a select's wrapper is a proxy over this one
    if (!v.is_object()) { return node_id{}; }
    auto * obj = static_cast<script::object_object *>(v.as_heap());
    const value * slot = obj->find(std::string{handle_property});
    if (slot == nullptr) { return node_id{}; }
    const auto packed = static_cast<std::uint64_t>(context::to_number(*slot));
    const auto held = wrappers_.find(packed);
    if (held == wrappers_.end() || held->second != obj) { return node_id{}; }
    return unpack(packed);
}

/// The concatenated text of `id` and all its descendants, in tree order.
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
    // sets its own data ("replace data"), and a DocumentType ignores it. Only an
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
    // "String replace all", DOM 4.4: the Text node is made only if the string
    // is not empty. `el.textContent = ""` leaves no child, and WPT
    // Node-textContent.html asserts `firstChild` is null afterwards.
    if (!text.empty()) {
        if (const node_id created = doc_->create_text(text)) {
            (void)doc_->append_child(id, created);
        }
    }
    mutated();
}

/// The funnel every native that changes the document calls afterwards: it
/// drains the document's write log and runs everything that reacts to a
/// mutation, script-running steps last.
void dom_bindings::mutated() {
    // Mutation observers first. This runs here, not in each native that
    // changes the document, because every such native already calls
    // mutated(). It costs one branch on a page with no observer.
    // The document's write log, drained once and read twice: the observers'
    // records, then the attribute change steps this engine models.
    const std::vector<document::write_note> writes =
        doc_ == nullptr ? std::vector<document::write_note>{} : doc_->take_writes();
    record_mutations(writes);
    settle_attribute_writes(writes);
    settle_form_writes(writes);
    settle_live_ranges(writes);
    signal_slot_changes(writes);
    // A "replace all" note is for the mutation it preceded and no other.
    replace_all_.reset();
    // An <input> whose type changed runs HTML 4.10.5's type-change steps now,
    // and a `value` attribute those steps carry over is written here, because
    // the store reads under a read transaction and cannot write. Guarded, because that
    // write comes back through this funnel.
    if (forms_ != nullptr && !settling_types_) {
        settling_types_ = true;
        std::vector<std::pair<node_id, std::string>> writes;
        {
            const auto txn = doc_->read();
            writes = forms_->settle_types(txn, *atoms_);
        }
        for (const auto & [id, text] : writes) {
            (void)doc_->set_attribute(id, atoms_->intern("value"), text);
        }
        settling_types_ = false;
    }
    // An `<iframe>` can only appear, change its `src` or leave through a
    // mutation, so this is where the reconcile is told there is something to
    // look at. The walk itself is not done here: it needs the script context
    // and it must not run inside a native that is halfway through a tree edit.
    frames_dirty_ = true;
    if (on_mutation_) { on_mutation_(); }
    moved_by_mutation_.clear();
    // The steps that run script go last: an inserted <script>'s
    // post-connection steps, then the custom element reactions (a
    // connectedCallback may mutate again and arrive back here). Everything
    // above is bookkeeping.
    run_inserted_scripts();
    // Then the insertion steps of the blank <iframe>s it connected, which a
    // script of the same insertion that preceded them did not see
    // (frames.cpp, connect_inserted_frames).
    connect_inserted_frames(writes);
    react_custom_elements();
}

/// HTML 4.12.1 "prepare the script element" for scripts a page inserted: each
/// one connected, with a src or non-empty text, and not "already started"
/// (leaving unstarted_scripts_ is what starting means).
///
/// The batch is taken first: every script the mutation made ready leaves the
/// list before any of them runs. So a script that gives a later one its text
/// arrives back here from that insertion and finds only the later one, which
/// runs nested, ahead of the rest of the batch; that is the order the
/// post-connection steps have.
///
/// ponytail: scripts run in creation order, not tree order. The test pages
/// insert scripts in the order they create them. Sort by tree position if a
/// page ever depends on the difference.
void dom_bindings::run_inserted_scripts() {
    // A frame's document runs what its scripts insert, in the frame's realm; a
    // document with no browsing context (createHTMLDocument's) runs nothing.
    if (unstarted_scripts_.empty() || cx_ == nullptr || (secondary_ && realm_ == nullptr) ||
        moving_) {
        return;
    }
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
        // An earlier script of the batch may have removed this one. A script
        // that is not connected when its turn comes does not run; it was never
        // started, so it goes back on the list for a later insertion (WPT
        // later-script-removed-by-earlier-script.html).
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
            // An external script runs in a later task (HTML 4.12.1.1 "prepare
            // the script element" step 33: a script-inserted classic script
            // with a src is fetched and executed "as soon as possible", after
            // the script that inserted it has finished, whether or not the
            // element is still connected by then). WPT
            // Document-prototype-currentScript.html depends on it. Its `load`
            // fires after it ran.
            (void)add_timer(
                native(cx, "external script",
                       [this, id, src](context & c, std::span<value>) {
                           const std::vector<std::byte> bytes =
                               assets_ == nullptr ? std::vector<std::byte>{}
                                                  : assets_->load(subresource_name(src));
                           if (!bytes.empty()) {
                               std::string text(reinterpret_cast<const char *>(bytes.data()),
                                                bytes.size());
                               execute_script_element(c, id, text);
                           }
                           // In this task, not the next tick's: "execute the
                           // script element" fires `load` (or, for a failed
                           // fetch, `error`) as part of the task that ran it,
                           // so a task queued meanwhile (a rejection
                           // notification) comes after (WPT
                           // promise-resolution-order.html).
                           settle_frame(c, pending_frame{id, !bytes.empty(), true});
                           return value::undefined();
                       }),
                0, false);
            continue;
        }
        execute_script_element(cx, id, source);
    }
}

/// "Execute the script element" (HTML 4.12.1.2) for a classic script's
/// source: `document.currentScript` is the element while it runs (null for one
/// in a shadow tree, which is not in the document tree) and what it was before
/// afterwards; an uncaught throw is reported, and the caller carries on.
void dom_bindings::execute_script_element(context & cx, node_id id, const std::string & source) {
    if (doc_ == nullptr) { return; }
    // In this document's realm: a frame's script declares into the frame's
    // globals and reads the frame's `document` (frames.cpp).
    const auto in_realm = enter_realm(cx);
    {
        // `document.currentScript` is this element while it runs (and while
        // its parse error is reported), and afterwards what it was before: the
        // outer script when there is one.
        value outer = value::null();
        if (auto * doc = document_object()) {
            if (const value * had = doc->find("currentScript"); had != nullptr) { outer = *had; }
        }
        // Post-connection order, DOM "insert" step 7.7: the post-connection
        // steps run per inserted node in tree order, and an <iframe>'s are
        // what give it a document. So a frame that precedes this script has
        // one by the time the script runs, and a frame that follows it does
        // not yet: `iframe.contentWindow` is null from inside the script (WPT
        // Node-appendChild-script-and-iframe.html). The frames before are
        // loaded here and the lazy reconcile is held off while the script
        // runs; a mutation the script makes re-arms it.
        // ponytail: tree order stands in for insertion order, so a frame an
        // earlier mutation inserted later in the tree is held off too; keep
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
                        if (!loaded) { preceding.emplace_back(at, frame_source(txn, at)); }
                    }
                    for (const node_id child : txn.children(at)) { self(self, child); }
                };
                walk(walk, txn.root());
            }
            for (const auto & [frame, src] : preceding) {
                // A blank one has its `load` at once (frames.cpp).
                if (!connect_blank_frame(frame, src)) {
                    frames_.push_back(frame_entry{frame, src, start_frame(frame, src)});
                }
            }
        }
        const bool frames_were_dirty = frames_dirty_;
        frames_dirty_ = false;
        // Null for a script whose root is a shadow root (HTML 4.12.1.2 step
        // 5.2: only a node in the document tree is exposed).
        {
            const auto txn = doc_->read();
            set_current_script(root_of_tree(txn, id, false) == txn.root() ? id : node_id{});
        }
        script::program compiled = script::compiler::compile(source);
        if (!compiled.ok) {
            (void)dispatch_error(compiled.error);
            if (auto * doc = document_object()) { doc->set("currentScript", outer); }
            frames_dirty_ = frames_dirty_ || frames_were_dirty;
            return;
        }
        const script::program & kept = cx.own_program(std::move(compiled));
        // Its `var`s exist before it starts, as a parser-run script's do
        // (browser/scripts.cpp; GlobalDeclarationInstantiation).
        for (const std::string & name : kept.hoisted_vars) { cx.declare_global_var(name, false); }
        auto * entry = cx.allocate<script::closure_object>(&kept.functions[0]);
        entry->owner = &kept;
        entry->realm = &cx.current_realm();
        bool threw = false;
        value thrown = value::undefined();
        (void)cx.call_fenced(value::object(entry), {}, cx.global_this(), threw, thrown);
        // An uncaught throw is reported and the inserting script carries on,
        // exactly as for a listener; see fire_at for the two ways to fail.
        // It is reported before currentScript is put back: "run a classic
        // script" reports the exception inside "execute the script element"
        // step 6, and step 7 is the restore, so a window.onerror reading
        // document.currentScript sees the script that threw (WPT
        // Document.currentScript.html, "script-window-error").
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

/// Installs `alert`, the global `location`, and the document's URL,
/// `location` and `defaultView` properties.
void dom_bindings::install_navigation(context & cx) {
    cx.define_native("alert", [this](context & c, std::span<value> args) {
        const std::string message = arg_string(c, args, 0);
        if (on_alert_) { on_alert_(message); }
        return value::undefined();
    });
    cx.define_global("location", location_);
    // `document.location` and `window.location` are the same object as the
    // global one, not three copies: a page reads whichever it learned, and
    // they have to agree.
    if (auto * doc = document_object()) {
        define_location_property(cx, *doc);
        // `document.URL`, `documentURI` and `baseURI` are the same string as
        // location.href and are set beside it so they cannot drift.
        // ponytail: baseURI should be the document base URL, which a <base>
        // element moves; it ignores <base> here although link resolution
        // honours it (document_base_url). Use that if a page needs it.
        for (const char * name : {"URL", "documentURI", "baseURI"}) {
            doc->set(name, cx.string(location_href_));
        }
        // document.defaultView must be the window proxy, the same object
        // `window` and `self` name, because pages compare them by identity.
        // testharness's assert_throws_dom reads `doc.defaultView.DOMException`,
        // so a missing defaultView fails every DOMException assertion with a
        // misleading message. Set here because the proxy exists only after
        // install_window.
        doc->set("defaultView", cx.global("window"));
    }
    if (auto * window = window_object()) { define_location_property(cx, *window); }
}

/// Builds this document's Location object (HTML 7.2.4) over its URL. Every
/// member is [LegacyUnforgeable], so an own, non-configurable property of the
/// object rather than a prototype's: the getters read the document's address
/// as it is now, so a navigation is seen without the object being rewritten.
///
/// The setters navigate ("Location-object navigate"). `href`, assign() and
/// replace() parse the new URL against the entry document's base URL (the
/// realm the calling script runs in), and a failure is a SyntaxError; the
/// part setters edit a copy of this URL. A URL that differs from the current
/// one only in its fragment is a fragment navigation, handled here: the
/// address moves, `:target` follows, and `hashchange` fires on a later task
/// when the fragment changed. Anything else navigates this document's
/// navigable, a frame's <iframe> (bindings/frames.cpp, navigate_frame).
/// ponytail: a script cannot navigate the top-level page, which belongs to
/// the embedder, so such a navigation is dropped. Route it to the embedder if
/// a page needs it.
value dom_bindings::make_location(context & cx) {
    auto * loc = cx.allocate<script::object_object>();
    constexpr std::uint8_t unforgeable = script::attr_enumerable;
    // Location-object navigate to `url`, absolute.
    const auto navigate = [this](context & c, const std::string & url) {
        const std::size_t hash = url.find('#');
        const std::string_view current =
            std::string_view{location_href_}.substr(0, location_href_.find('#'));
        if (hash != std::string::npos && !location_href_.empty() &&
            std::string_view{url}.substr(0, hash) == current) {
            // Navigate to a fragment (HTML 7.4.2.3.3). This is synchronous,
            // and `hashchange` fires a task later if the fragment changed.
            const std::string old_url = location_href_;
            const std::size_t old_mark = old_url.find('#');
            const std::optional<std::string> old_fragment =
                old_mark == std::string::npos ? std::nullopt
                                              : std::optional{old_url.substr(old_mark + 1)};
            const std::string fragment = url.substr(hash + 1);
            observe_location(url, fragment.empty() ? std::string{} : "#" + fragment);
            if (old_fragment != fragment) {
                const value fire =
                    native(c, "hashchange", [this, old_url, url](context &, std::span<value>) {
                        (void)dispatch_hash_change(old_url, url);
                        return value::undefined();
                    });
                dom_bindings & top = primary_ == nullptr ? *this : *primary_;
                (void)top.add_timer(fire, 0, false);
            }
            return;
        }
        const auto [navigable, element] = choose_navigable("_self");
        if (navigable == nullptr) { return; }
        form_navigation navigation;
        navigation.src = url;
        navigation.request.url = url;
        navigate_frame(*navigable, element, std::move(navigation));
    };
    // The given URL parsed against the entry document, or the SyntaxError.
    const auto parse_given = [this](context & c, value given,
                                    std::string_view what) -> std::optional<std::string> {
        const std::string text = to_usv_string(c.to_string(given));
        if (c.throw_pending()) { return std::nullopt; }
        dom_bindings & entry = bindings_of(c.global("window"));
        const std::string base =
            document_base_url(entry.doc_->read(), *entry.atoms_, entry.fallback_base_url());
        const std::optional<url_record> parsed =
            base.empty() ? parse_url(text) : parse_url(text, base);
        if (!parsed) {
            throw_dom_exception(c, "SyntaxError",
                                "Failed to " + std::string{what} + " on 'Location': '" + text +
                                    "' is not a valid URL.");
            return std::nullopt;
        }
        return parsed->serialize();
    };
    const auto accessor = [&](const char * name, script::native_fn get, script::native_fn set) {
        const value setter = set ? value::object(cx.allocate<script::native_object>(
                                       std::string{"set "} + name, std::move(set)))
                                 : value::undefined();
        loc->define_accessor(name,
                             value::object(cx.allocate<script::native_object>(
                                 std::string{"get "} + name, std::move(get))),
                             setter, unforgeable);
    };
    accessor(
        "href", [this](context & c, std::span<value>) { return c.string(location_href_); },
        [navigate, parse_given](context & c, std::span<value> a) {
            if (const auto url = parse_given(c, arg(a, 0), "set the 'href' property")) {
                navigate(c, *url);
            }
            return value::undefined();
        });
    accessor(
        "origin",
        [this](context & c, std::span<value>) {
            const std::string origin = location_parts(location_href_).origin;
            return c.string(origin.empty() ? "null" : origin);
        },
        nullptr);
    // The URL parts: each getter is a piece of the address, each setter the
    // URL Standard's setter (URL 6.1, set_url_part) over a copy of it,
    // followed by the navigation. `hash` is the one a page writes most, and the fragment
    // navigation above is what it does.
    struct part {
        const char * name;
        std::string location_url::* field;
        url_part which;
    };
    for (const part & one : {part{"protocol", &location_url::protocol, url_part::protocol},
                             part{"host", &location_url::host, url_part::host},
                             part{"hostname", &location_url::hostname, url_part::hostname},
                             part{"port", &location_url::port, url_part::port},
                             part{"pathname", &location_url::pathname, url_part::pathname},
                             part{"search", &location_url::search, url_part::search},
                             part{"hash", &location_url::hash, url_part::hash}}) {
        accessor(
            one.name,
            [this, field = one.field, hash = one.which == url_part::hash](context & c,
                                                                          std::span<value>) {
                return c.string(hash ? location_hash_ : location_parts(location_href_).*field);
            },
            [this, navigate, which = one.which](context & c, std::span<value> a) {
                const std::string given = to_usv_string(c.to_string(arg(a, 0)));
                if (c.throw_pending()) { return value::undefined(); }
                std::optional<url_record> copy = parse_url(location_href_);
                if (!copy) { return value::undefined(); }
                if (which == url_part::hash) {
                    // Location's own steps: the fragment is set, never nulled
                    // (`location.hash = ""` goes to `#`), and a fragment that
                    // did not change navigates nowhere.
                    const std::optional<std::string> before = copy->fragment;
                    (void)set_url_part(*copy, url_part::hash, given.empty() ? "#" : given);
                    if (copy->fragment == before) { return value::undefined(); }
                } else {
                    const bool needs_hierarchy =
                        which == url_part::host || which == url_part::hostname ||
                        which == url_part::port || which == url_part::pathname;
                    if (needs_hierarchy && copy->opaque_path) { return value::undefined(); }
                    (void)set_url_part(*copy, which, given);
                    // A protocol setter that left http(s) terminates.
                    if (which == url_part::protocol && copy->scheme != "http" &&
                        copy->scheme != "https") {
                        return value::undefined();
                    }
                }
                navigate(c, copy->serialize());
                return value::undefined();
            });
    }
    const auto method = [&](const char * name, unsigned length, script::native_fn fn) {
        auto * made = cx.allocate<script::native_object>(name, std::move(fn));
        made->define("length", value::number(length), script::attr_configurable);
        loc->define(name, value::object(made), unforgeable);
    };
    method("assign", 1, [navigate, parse_given](context & c, std::span<value> a) {
        if (const auto url = parse_given(c, arg(a, 0), "execute 'assign'")) { navigate(c, *url); }
        return value::undefined();
    });
    method("replace", 1, [navigate, parse_given](context & c, std::span<value> a) {
        if (const auto url = parse_given(c, arg(a, 0), "execute 'replace'")) { navigate(c, *url); }
        return value::undefined();
    });
    // reload(): the page's is the browser's, between callbacks; a frame's
    // navigates the frame to where it is.
    method("reload", 0, [this](context &, std::span<value>) {
        const auto [navigable, element] = choose_navigable("_self");
        if (navigable == nullptr) {
            reload_requested_ = true;
        } else {
            form_navigation navigation;
            navigation.src = location_href_;
            navigation.request.url = location_href_;
            navigate_frame(*navigable, element, std::move(navigation));
        }
        return value::undefined();
    });
    method("toString", 0,
           [this](context & c, std::span<value>) { return c.string(location_href_); });
    return value::object(loc);
}

/// Defines `location` on `holder` (the window or the document) as
/// [PutForwards=href]: an accessor whose setter is `location.href = v`. So
/// `frame.contentWindow.location = url` navigates the frame (and throws a
/// SyntaxError for a URL that does not parse) rather than replacing the
/// property with a string.
void dom_bindings::define_location_property(context & cx, script::object_object & holder) {
    holder.define_accessor(
        "location",
        value::object(cx.allocate<script::native_object>(
            "get location", [this](context &, std::span<value>) { return location_; })),
        value::object(cx.allocate<script::native_object>("set location",
                                                         [this](context & c, std::span<value> a) {
                                                             c.store_property(location_, "href",
                                                                              arg(a, 0));
                                                             return value::undefined();
                                                         })),
        script::attr_enumerable);
}

/// The document object behind the named-access proxy, not the proxy.
/// Everything in these bindings that writes a property on the document goes
/// through here; the proxy has no `set` trap to take the write. See the
/// document_ and document_target_ members.
script::object_object * dom_bindings::document_object() {
    return document_target_.is_object()
               ? static_cast<script::object_object *>(document_target_.as_heap())
               : nullptr;
}

/// The window object, or null before install_window.
script::object_object * dom_bindings::window_object() {
    return window_.is_object() ? static_cast<script::object_object *>(window_.as_heap()) : nullptr;
}

} // namespace ctbrowser::shell
