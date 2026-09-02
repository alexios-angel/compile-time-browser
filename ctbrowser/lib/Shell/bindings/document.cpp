// dom_bindings - document, location, navigation and the tree-mutating entry points.
//
// One of six files carved out of a 3,926-line bindings.cpp on 2026-08-09.
// These are all member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp, so they split across translation
// units with nothing to declare and no linkage to arrange.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <algorithm>
#include <charconv>
#include <memory>
#include <numbers>
#include <optional>

// dom_bindings' method bodies - the API a page's script actually calls.
//
// The header lists what a page can reach; this is how each one works.

namespace ctbrowser::shell {

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
// url_parts and split_url USED TO BE HERE. They are gone, and the reason is
// worth keeping: this one reached for the last colon in the authority with no
// bracket guard, so `http://[::1]/` reported hostname `[:` and port `1]`. Its
// twin in net.cpp guarded exactly that case. Two parsers for one job, written
// apart, drifted apart - and nothing compared them because nothing could.
//
// shell/net/url.hpp parses once now, for both.

} // namespace

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
    // AFTER install_window: it defines the `window` proxy whose handler falls
    // back to the globals, which is what makes one bare global answer both
    // `getComputedStyle(el)` and `window.getComputedStyle(el)`.
    install_computed_style(cx);
    install_timers(cx);
    install_resources(cx);
    install_navigation(cx);
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
    // THE OTHER HALF, WHICH THE DOCUMENT DID NOT HAVE.
    //
    // The window had both and an element had both; the document could only ADD.
    // A page that tidied up after itself got
    // "`removeEventListener` is undefined, not a function" - and because the
    // fault happened inside an image-load callback, what it actually looked
    // like was Babylon's PBR material painting nothing, three layers away.
    //
    // The body is the window's, and identical on purpose: `listener::target`
    // empty means document-or-window by design, so the two share a bucket and
    // removing from one has always been able to remove the other's.
    method("removeEventListener", [this](context & c, std::span<value> args) {
        const std::string type = arg_string(c, args, 0);
        const value callback = arg(args, 1);
        std::erase_if(listeners_, [&](const listener & l) {
            return !l.target && l.type == type && l.callback.bits() == callback.bits();
        });
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
    // LIVE, unlike getElementsByTagName above, and the difference is not
    // decoration: five of the suite's own tests take the collection, mutate the
    // document and read the collection again. See make_live_collection.
    method("getElementsByClassName", [this](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = ordered_set(arg_string(c, args, 0));
        return make_live_collection(c, [this, tokens] { return all_by_class(node_id{}, tokens); });
    });
    // `document.getElementsByName`, which is keyed on the `name` ATTRIBUTE and
    // not on `id`. It is HTML's, not the DOM's - hence the document only, and
    // hence HTML elements only.
    method("getElementsByName", [this](context & c, std::span<value> args) {
        const std::string name = arg_string(c, args, 0);
        return make_live_collection(c, [this, name] { return all_by_name(name); });
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
    // NULL, NOT ABSENT. `document.fullscreenElement` is how a page asks whether
    // it is fullscreen - p5's own `fullscreen()` with no argument is exactly that
    // read - and undefined there is indistinguishable from "the property does not
    // exist", which is a different question. There is no window manager here, so
    // the answer is always null: nothing is fullscreen.
    for (const char * name :
         {"fullscreenElement", "webkitFullscreenElement", "mozFullScreenElement",
          "msFullscreenElement", "pointerLockElement"}) {
        doc->set(name, value::null());
    }
    // Asking to ENTER either is a no-op that succeeds quietly rather than a
    // missing method: a page calls these from a click handler and does not check.
    for (const char * name : {"exitFullscreen", "exitPointerLock"}) {
        doc->set(name, value::object(cx.allocate<script::native_object>(
                           name, [](context & c, std::span<value>) {
                               return c.make_promise(value::undefined(), false);
                           })));
    }
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

namespace {

// ASCII whitespace, as the DOM defines it: TAB, LF, FF, CR and SPACE. Not
// `isspace`, which is locale-dependent and includes vertical tab.
constexpr std::string_view dom_whitespace = "\t\n\f\r ";

// A property key that is a whole non-negative integer and nothing else. "1x" is
// not index 1, and neither is " 1", "+1" or "1.0" - a collection has to say no
// to those or `hasOwnProperty` starts agreeing to keys nobody indexed.
[[nodiscard]] std::optional<std::size_t> whole_index(std::string_view key) {
    if (key.empty() || (key.size() > 1 && key.front() == '0')) { return std::nullopt; }
    std::size_t at = 0;
    const char * first = key.data();
    const char * last = first + key.size();
    const auto [stopped, failed] = std::from_chars(first, last, at);
    if (failed != std::errc{} || stopped != last) { return std::nullopt; }
    return at;
}

} // namespace

std::vector<std::string> dom_bindings::ordered_set(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t start = text.find_first_not_of(dom_whitespace, at);
        if (start == std::string_view::npos) { break; }
        std::size_t end = text.find_first_of(dom_whitespace, start);
        if (end == std::string_view::npos) { end = text.size(); }
        std::string token{text.substr(start, end - start)};
        // AN ORDERED *SET*: "a a" asks for one class twice, and a duplicate in
        // the wanted list is a match requirement that is already satisfied.
        if (std::ranges::find(out, token) == out.end()) { out.push_back(std::move(token)); }
        at = end;
    }
    return out;
}

std::vector<node_id> dom_bindings::all_by_class(node_id root,
                                                const std::vector<std::string> & tokens) {
    if (tokens.empty()) { return {}; }
    const auto txn = doc_->read();
    const atom class_attribute = atoms_->intern("class");
    std::vector<node_id> found;
    const auto has_every = [&](node_id at) {
        // CASE-SENSITIVE, which is the standards-mode rule. A quirks-mode
        // document matches ASCII-case-insensitively; this engine does not carry
        // the document's mode past the tree builder yet, so the standards answer
        // is the one given - it is the right one for every document with a
        // doctype, which is every document a test suite writes on purpose.
        const std::vector<std::string> held = ordered_set(txn.attribute_value(at, class_attribute));
        return std::ranges::all_of(tokens, [&](const std::string & want) {
            return std::ranges::find(held, want) != held.end();
        });
    };
    const auto walk = [&](auto && self, node_id at, bool include) -> void {
        // ELEMENTS ONLY, and never the element the search started from: a search
        // rooted at an element looks at its DESCENDANTS.
        if (include && txn.tag(at).has_value() && has_every(at)) { found.push_back(at); }
        for (const node_id child : txn.children(at)) { self(self, child, true); }
    };
    // THE DOCUMENT'S ROOT IS THE <html> ELEMENT, not a Document node - this
    // tree builder makes `<html>` and calls set_root with it, and there is no
    // node above it. So a document-wide search must INCLUDE the root, or
    // `document.getElementsByClassName` silently cannot return the one element
    // that is most often given a class. An element-rooted search excludes it.
    walk(walk, root ? root : txn.root(), !root);
    return found;
}

std::vector<node_id> dom_bindings::all_by_name(std::string_view name) {
    const auto txn = doc_->read();
    const atom key = atoms_->intern("name");
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (txn.tag(at).has_value() && txn.element_ns(at) == node_ns::html &&
            txn.has_attribute(at, key) && txn.attribute_value(at, key) == name) {
            found.push_back(at);
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// THE LIVE COLLECTION. Three things have to be true at once and only a proxy
// gets all three: `length` is recomputed on every read, `collection[i]` is
// recomputed on every read, and `collection.hasOwnProperty(i)` agrees with both
// - which is exactly what `assert_array_equals` checks before it compares a
// single element.
//
// `item` and `namedItem` are ordinary properties of the TARGET rather than
// answers the trap fabricates, so they keep their identity across reads and so
// the trap's fallback - an ordinary lookup on the target - finds them along with
// everything Object.prototype provides.
value dom_bindings::make_live_collection(context & cx,
                                         std::function<std::vector<node_id>()> members) {
    auto * target = static_cast<script::object_object *>(cx.make_object().as_heap());
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    // Shared rather than copied into each trap: `members` walks the document, and
    // three copies of the same walk is three chances for them to disagree.
    const auto live = std::make_shared<std::function<std::vector<node_id>()>>(std::move(members));

    const auto native = [&](std::string name, script::native_fn fn) {
        return value::object(cx.allocate<script::native_object>(std::move(name), std::move(fn)));
    };
    target->set("item", native("item", [this, live](context & c, std::span<value> a) {
                    const std::vector<node_id> found = (*live)();
                    const double at = arg_number(a, 0);
                    if (at < 0 || at >= static_cast<double>(found.size())) { return value::null(); }
                    return wrap(c, found[static_cast<std::size_t>(at)]);
                }));
    target->set("namedItem", native("namedItem", [this, live](context & c, std::span<value> a) {
                    const std::string want = arg_string(c, a, 0);
                    const auto txn = doc_->read();
                    const atom id = atoms_->intern("id");
                    const atom name = atoms_->intern("name");
                    for (const node_id at : (*live)()) {
                        if (txn.attribute_value(at, id) == want ||
                            txn.attribute_value(at, name) == want) {
                            return wrap(c, at);
                        }
                    }
                    return value::null();
                }));

    handler->set("get", native("get", [this, live](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::undefined(); }
                     const std::string key = c.to_string(args[1]);
                     if (key == "length") {
                         return value::number(static_cast<double>((*live)().size()));
                     }
                     if (const std::optional<std::size_t> at = whole_index(key)) {
                         const std::vector<node_id> found = (*live)();
                         return *at < found.size() ? wrap(c, found[*at]) : value::undefined();
                     }
                     return c.lookup_property(args[0], key);
                 }));
    handler->set("has", native("has", [live](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::boolean(false); }
                     const std::string key = c.to_string(args[1]);
                     if (key == "length") { return value::boolean(true); }
                     if (const std::optional<std::size_t> at = whole_index(key)) {
                         return value::boolean(*at < (*live)().size());
                     }
                     return value::boolean(!c.lookup_property(args[0], key).is_undefined());
                 }));
    return value::object(
        cx.allocate<script::proxy_object>(value::object(target), value::object(handler)));
}

} // namespace ctbrowser::shell
