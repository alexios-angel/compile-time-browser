// dom_bindings - `<iframe>`, which is to say a NESTED BROWSING CONTEXT.
//
// WHY THIS IS ONE FILE AND NOT A PROJECT. A second Document already exists here
// - `document.implementation.createHTMLDocument()` returns one, and the block
// above `adopt_interfaces_of` in bindings/document/second_document.cpp is the
// model: a second
// `dom_bindings` over its own tree, sharing the atom table, the script context
// and the interface objects with the page's own. A frame is that same second
// document with two things added, and both of them are small:
//
//   * THE BYTES. `src` is resolved through the asset registry, which is where
//     every other subresource on the page comes from - so a frame loads from
//     the document root a server would have been serving and opens no socket
//     of its own. `tools/wpt/run-wpt.py` sets that root; nothing else does.
//   * THE EVENT. `load` fires at the `<iframe>` on a LATER turn, drained by the
//     event loop beside the image loads, because
//     `document.body.appendChild(frame)` is followed by `frame.onload = f`
//     often enough that firing from the insertion would fire at nothing.
//
// WHAT IT IS MEASURED AGAINST. Thirty-two `dom/nodes` files timed out at 10 or
// 60 seconds each waiting for an iframe's `load` that could never come, and
// thirteen of them are `Document-contentType/contentType/*`, each one async_test
// asserting `iframe.contentDocument.contentType`. `Document-createElement.html`
// is 98 subtests that read `xmlIframe.contentDocument` inside a `window` `load`
// handler, which is why the reconcile below runs BEFORE that event rather than
// with the timers after it.
//
// AND A THIRD, SINCE 2026-09-25: THE FRAME RUNS ITS OWN SCRIPTS, in a realm
// of its own (install_frame_realm) - its `var`s are its own, its functions
// read its `document` whoever calls them, and its window hears its `load`
// before the <iframe>'s own. dom/ranges drives its Range-*Contents cases
// through a frame's `run()`, and until the frame could define one every case
// was "`run` is undefined".
//
// WHAT IS DELIBERATELY NOT HERE, named rather than left to be discovered:
//
//   * THE INTRINSICS ARE SHARED. A frame's realm has its own bindings but the
//     page's constructors, so `frame.contentWindow.Element === window.Element`
//     where a browser has two. Every test that turns on cross-realm identity
//     of a prototype still fails.
//   * NO NAVIGATION AND NO HISTORY. A frame is loaded once per `src`; there is
//     no `location` on `contentWindow` to assign to, no `about:blank` document
//     replaced by a real one, and no `srcdoc`.
//   * NO PARSER-TIME SCRIPTS. The frame's document is parsed whole and its
//     classic scripts run afterwards in tree order, as an XHTML page's do; a
//     `document.write` from one of them writes into a finished document.
//   * A FRAME PAINTS AS AN EMPTY BOX. This is the DOM half of an iframe; the
//     frame's document IS laid out, at the size its box got, by the browser
//     (browser/nested.cpp) so that what a script measures inside it is about
//     the frame - but nothing draws that layout into the page's.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include "events/internal.hpp"

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell {

namespace {

// The markup a document gets when the bytes are not markup. HTML 7.5 gives a
// plain-text resource a document whose body is one `<pre>`; anything else this
// engine cannot display gets an empty one rather than a lie about its content.
[[nodiscard]] std::string escaped(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

[[nodiscard]] std::string_view extension_of(std::string_view path) {
    // The query and the fragment are not part of the name on disk, and the
    // registry has already been asked with them on: this is only about which
    // parser to run.
    const std::size_t cut = path.find_first_of("?#");
    if (cut != std::string_view::npos) { path = path.substr(0, cut); }
    const std::size_t dot = path.rfind('.');
    const std::size_t slash = path.find_last_of("/\\");
    if (dot == std::string_view::npos) { return {}; }
    if (slash != std::string_view::npos && dot < slash) { return {}; }
    return path.substr(dot + 1);
}

} // namespace

// THE CONTENT TYPE COMES FROM THE NAME, because there is no server to send one.
//
// That is a real difference from a browser and it is visible in the corpus:
// `Document-contentType/contentType/contenttype_mimeheader_01.html` asks for a
// file whose type is set by a `.headers` sidecar, and this cannot answer it.
// The ELEVEN files beside it name the type in the extension, which this can.
//
// The table is the one `mime.types` and WPT's own server agree on for these
// extensions, and it is small on purpose: a type this engine has never heard of
// is better reported as `application/octet-stream` - which is what a browser
// does with it - than guessed at.
std::string_view dom_bindings::mime_for_path(std::string_view path) {
    // The extension of the PATH: `blank.html?n=v` is an HTML file with a
    // query, which a form submission's URL carries.
    const std::string_view ext =
        extension_of(path.substr(0, std::min(path.find('?'), path.find('#'))));
    // ASCII-folded through the shared helper: `FOO.HTML` is an HTML file, and
    // core/algorithms.hpp is where this engine folds case so a render never
    // depends on LC_ALL.
    const std::string lowered = ascii_lower_copy(ext);
    if (lowered == "html" || lowered == "htm") { return "text/html"; }
    if (lowered == "xhtml" || lowered == "xht") { return "application/xhtml+xml"; }
    // application/xml, not text/xml: it is what a server sends for `.xml`
    // and what Document-createElement-namespace.html reads back.
    if (lowered == "xml") { return "application/xml"; }
    if (lowered == "svg") { return "image/svg+xml"; }
    if (lowered == "txt") { return "text/plain"; }
    if (lowered == "css") { return "text/css"; }
    if (lowered == "js" || lowered == "mjs") { return "text/javascript"; }
    if (lowered == "json") { return "application/json"; }
    if (lowered == "png") { return "image/png"; }
    if (lowered == "gif") { return "image/gif"; }
    if (lowered == "jpg" || lowered == "jpeg") { return "image/jpeg"; }
    if (lowered == "bmp") { return "image/bmp"; }
    if (lowered == "webp") { return "image/webp"; }
    if (lowered == "ico") { return "image/x-icon"; }
    if (lowered == "pdf") { return "application/pdf"; }
    return {};
}

// ONE PASS OVER THE FRAMES THIS DOCUMENT HAS, and the three things that can
// have happened to one since the last pass: it appeared, its `src` changed, or
// it went away. Guarded by `frames_dirty_`, which `mutated()` sets - without
// that this walks the whole tree on every frame of an idle page, which is
// exactly what "a frame runs only what changed" forbids.
//
// A FRAME DOCUMENT RECONCILES ITS OWN FRAMES TOO: a nested `<iframe>` gets its
// document the same way, on the accessor - `install_frame_accessors` asks the
// element's owner - since only the primary is asked on the tick. A document
// WITHOUT a browsing context - createHTMLDocument's, DOMParser's - has no
// nested navigables and its `<iframe>`s stay empty (`contentDocument` null),
// and "has a window" is exactly that fact: install_document leaves
// `defaultView` null for one and load_frame sets it for a frame's.
void dom_bindings::reconcile_frames() {
    if (cx_ == nullptr || !frames_dirty_) { return; }
    if (secondary_) {
        script::object_object * doc = document_object();
        const value * window = doc == nullptr ? nullptr : doc->find("defaultView");
        if (window == nullptr || window->is_nullish()) { return; }
    }
    frames_dirty_ = false;
    // A frame element with no `src` has an about:blank document in a browser.
    // It gets one here too - `load_frame` with an empty src builds an empty
    // HTML document - because `frame.contentDocument.body` is how a page
    // writes into one, and answering `undefined` is the failure that reads as
    // "iframes do not exist".
    const std::vector<node_id> elements = all_html_elements("iframe");
    std::vector<frame_entry> still;
    still.reserve(elements.size());
    for (const node_id id : elements) {
        std::string src;
        {
            const auto txn = doc_->read();
            src = std::string{txn.attribute_value(id, atoms_->intern("src"))};
        }
        const auto seen =
            std::ranges::find_if(frames_, [&](const auto & entry) { return entry.element == id; });
        if (seen != frames_.end() && seen->src == src) {
            still.push_back(*seen);
            continue;
        }
        still.push_back(frame_entry{id, src, load_frame(*cx_, id, src)});
    }
    // A FRAME THAT LEFT THE TREE IS FORGOTTEN, and its document is not: the
    // secondary bindings stay in `secondary_documents_` because a page may
    // still be holding the `contentDocument` it took before removing the
    // frame, and a removed frame put back gets a NEW document, which is what
    // the DOM says happens.
    frames_.swap(still);
}

// A FRAME HAS ITS DOCUMENT THE MOMENT IT IS INSERTED, not at the next tick.
// `appendChild(iframe).contentWindow` is how event-global-extra.window.js and
// the cross-realm listener files reach another global, and a browser answers
// it synchronously. The accessors live on the prototype and a reconciled
// frame carries the two as OWN properties, so they run only while a frame is
// still unbuilt - and building it is one pass over the frames that changed.
void dom_bindings::install_frame_accessors(context & cx) {
    const value iface = interface_prototype("HTMLIFrameElement");
    if (!iface.is_object()) { return; }
    auto * proto = static_cast<script::object_object *>(iface.as_heap());
    for (const char * name : {"contentWindow", "contentDocument"}) {
        proto->define_accessor(
            name,
            value::object(cx.allocate<script::native_object>(
                name,
                [this, name](context & c, std::span<value>) {
                    const value self = c.current_this();
                    if (!self.is_object()) { return value::null(); }
                    dom_bindings & owner = target_owner(self);
                    if (owner.handle_of(self)) { owner.reconcile_frames(); }
                    const value * held =
                        static_cast<script::object_object *>(self.as_heap())->find(name);
                    return held == nullptr ? value::null() : *held;
                })),
            value::undefined());
    }
}

// Build the frame's document. Synchronous, because the bytes are already on
// disk or in the registry; the EVENT it queues is what makes the load
// asynchronous in the way a page can observe.
dom_bindings * dom_bindings::load_frame(context & cx, node_id id, const std::string & src) {
    std::string bytes;
    // A FRAME ALWAYS LOADS. A navigation that fetched nothing - a 404, and
    // here a name the registry cannot find - still ends in a document (the
    // error page), and `load` fires at the element for it; `error` is not an
    // iframe's event. An EMPTY file is the same as a missing one to the
    // registry, and Document-createElement-namespace.html's empty.html waits
    // on exactly that load.
    bool ok = true;
    std::string type{mime_for_path(src)};
    // THE SAME WAY createHTMLDocument MAKES ONE - in the primary's flat list,
    // with `primary_` set (below); the primary is also where the object URL
    // table lives.
    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
    if (src.starts_with("blob:")) {
        // An object URL's type is the Blob's (File API §10.3), not an
        // extension's: `iframe.src = URL.createObjectURL(new Blob([html],
        // {type: "text/html"}))` is how every html5lib_url.html variant loads
        // its fixture, and with no type it was an octet-stream shown as an
        // empty body - 60 files of the html/syntax corpus.
        for (const auto & [name, stated] : top.object_url_types_) {
            if (name == src) { type = stated; }
        }
    }
    if (!src.empty()) {
        // A data: URL CARRIES ITS OWN TYPE, and it is the only source here that
        // states one - RFC 2397 puts the media type in the URL, so this is the
        // one case that does not have to guess from an extension. The bytes
        // still come through the registry, which decodes data: URLs itself.
        if (is_data_url(src)) {
            data_url stated;
            type = parse_data_url(src, stated) ? stated.mime : "text/plain";
        }
        if (assets_ != nullptr) {
            // WITHOUT THE FRAGMENT: `page.html#target` names page.html to a
            // server and to the registry alike - a fragment never leaves the
            // client - and the registry matches names literally. A data: URL
            // goes over whole, as it always did; parse_data_url owns its shape.
            const std::size_t hash = is_data_url(src) ? std::string::npos : src.find('#');
            const std::vector<std::byte> loaded = assets_->load(src.substr(0, hash));
            bytes.resize(loaded.size());
            for (std::size_t i = 0; i < loaded.size(); ++i) {
                bytes[i] = static_cast<char>(loaded[i]);
            }
        }
        if (type.empty()) { type = "application/octet-stream"; }
    } else {
        type = "text/html";
    }

    // Built by hand here before, without `primary_`, so
    // `owner_of` run from inside a frame document could see nothing but the
    // frame's own wrappers: `frame.contentDocument.body.appendChild(pageNode)`
    // was "the argument is not a Node" (Node-isConnected.html's iframe case,
    // the node-realm-* files), and a frame's own `<iframe>` never loaded.
    document & fresh = *top.owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    dom_bindings & made = adopt_second_document(cx, fresh);
    made.content_type_ = type;

    const bool is_xml = type == "application/xhtml+xml" || type == "text/xml" ||
                        type == "application/xml" || type == "image/svg+xml";
    // about:blank IS A DOCUMENT WITH A BODY, and it has to be: `frame
    // .contentDocument.body` is how a page writes into a scratch frame, and
    // parsing the empty string leaves a tree with no body at all to write to.
    if (src.empty() || (bytes.empty() && !is_xml)) {
        (void)parse_html(fresh, "<html><head></head><body></body></html>");
    } else if (is_xml) {
        // An EMPTY .xml is still an XML document - createElement in it makes
        // a null-namespace element, which Document-createElement-namespace
        // .html's empty.xml row reads.
        // The XML front end, which is why `.xhtml` is worth having at all: the
        // HTML tree builder lowercases `viewBox` and hands `<![CDATA[` to the
        // JavaScript engine. A frame whose XML is not well-formed keeps the
        // tree the parser had, exactly as a top-level XML document does.
        (void)parse_xml(fresh, bytes);
    } else if (type == "text/html") {
        if (const std::string declared = prescan_encoding(bytes); !declared.empty()) {
            fresh.set_encoding(declared);
        } else {
            fresh.set_encoding(doc_->encoding()); // 13.2.3.2 step 8: the parent's, same-origin
        }
        (void)parse_html(fresh, decode_document_bytes(bytes, fresh.encoding()));
    } else if (type.starts_with("text/") || type == "application/json" ||
               type == "text/javascript") {
        (void)parse_html(fresh, "<html><head></head><body><pre>" + escaped(bytes) +
                                    "</pre></body></html>");
    } else {
        // An image, or anything else this engine will not display: HTML gives
        // it a document with the resource as the body's only element, and the
        // TYPE is the part a page can read back.
        (void)parse_html(fresh, "<html><head></head><body></body></html>");
    }
    made.install_document(cx);
    // THE FRAME DOCUMENT'S ADDRESS: the src resolved against this document's,
    // with its fragment - which is what makes `:target` match inside a frame
    // loaded as `page.html#target` (ParentNode-querySelector-All.html's
    // in-document cases run in one).
    if (!src.empty()) {
        const std::string href = location_href_.empty() ? src : resolve(location_href_, src);
        const std::size_t hash = href.find('#');
        made.observe_location(href, hash == std::string::npos ? std::string{} : href.substr(hash));
    }

    // THE FRAME'S OWN `location`: its document's address, which a page reads
    // back after a form submission navigated the frame (`iframe.contentWindow
    // .location.search` is how every form-submission test reads the entry
    // list). Made after observe_location, which writes its parts.
    made.location_ = made.make_location(cx);

    // Hung off the WRAPPER rather than kept in a table beside it, so the frame
    // document is reachable from the element that owns it and the collector
    // needs no new root: `wrappers_` is already marked.
    const value element = wrap(cx, id);
    if (!element.is_object()) { return nullptr; }
    auto * frame_object = static_cast<script::object_object *>(element.as_heap());
    frame_object->set("contentDocument", made.document_);

    // `contentWindow`: THE FRAME'S OWN WINDOW, over a realm of its own (see
    // install_frame_realm) - and then the scripts its parse left, run in it.
    made.install_frame_realm(cx, *this, id);
    frame_object->set("contentWindow", made.window_view_);
    made.run_frame_scripts(cx);

    // ON THE PRIMARY'S QUEUE, which is the one the tick drains, naming the
    // owner whose element it is; settle_frame hands it back.
    top.frame_loads_.push_back(pending_frame{id, ok, false, &top == this ? nullptr : this});
    return &made;
}

// HTML 4.10.21.3 "submit", steps 17-22, for the one case this engine can
// take without a server: the GET method aimed at a nested navigable this
// document names - `<form target="if1">` and `<iframe name="if1">`. The
// action URL's query becomes the entry list, urlencoded, and the frame is
// navigated to it: a new document under the same element, whose
// `location.search` is the query and whose `load` fires at the element.
// `_self`/`_blank`/`_parent`/`_top` and the POST method are the browser's
// (a navigation of the page itself, or one that needs a server) and stay
// where they were: recorded as `last_submission`. Answers whether it
// navigated. ponytail: no `formtarget`/`formaction` on the submitter, which
// browser::submit does not carry.
bool dom_bindings::navigate_form_target(node_id form, const form_pairs & entries) {
    if (cx_ == nullptr || !form) { return false; }
    std::string action;
    std::string method;
    std::string target;
    {
        const auto txn = doc_->read();
        action = std::string{txn.attribute_value(form, atoms_->intern("action"))};
        method = ascii_lower_copy(txn.attribute_value(form, atoms_->intern("method")));
        target = std::string{txn.attribute_value(form, atoms_->intern("target"))};
    }
    if (method == "post" || method == "dialog") { return false; }
    if (target.empty() || target.front() == '_') { return false; }
    node_id frame;
    {
        const auto txn = doc_->read();
        for (const node_id id : all_html_elements("iframe")) {
            if (txn.attribute_value(id, atoms_->intern("name")) == target) {
                frame = id;
                break;
            }
        }
    }
    if (!frame) { return false; }
    // "Mutate action URL": the action - this document's address when there is
    // none - with its query replaced by the entries. Kept as the page wrote it
    // (a path the registry resolves), not resolved into an absolute URL, so
    // the load goes where the page's other subresources go.
    std::string url = action.empty() ? location_href_ : action;
    url = url.substr(0, std::min(url.find('?'), url.find('#')));
    url += "?" + serialize_form_urlencoded(entries);
    reconcile_frames();
    dom_bindings * made = load_frame(*cx_, frame, url);
    const auto entry =
        std::ranges::find_if(frames_, [&](const frame_entry & e) { return e.element == frame; });
    if (entry != frames_.end()) {
        entry->bindings = made;
    } else {
        frames_.push_back(frame_entry{frame, std::string{}, made});
    }
    return true;
}

// `load` at the frame, or `error` when the src resolved to no bytes. It does
// not bubble - an iframe's load event is fired at the element and stops there -
// which is why this is `fire_at` with `capturing` false and not `dispatch`.
// A REAL Event rather than a bare object, since a `<link>`, `<style>` and
// `<script>` announce through here too (announce_load): `timeStamp` is what a
// page compares a paint entry against.
void dom_bindings::settle_frame(context & cx, const pending_frame & waiting) {
    if (waiting.owner != nullptr && waiting.owner != this) {
        waiting.owner->settle_frame(cx, pending_frame{waiting.id, waiting.ok, waiting.resource});
        return;
    }
    // THE FRAME'S OWN LOAD COMES FIRST: its document's DOMContentLoaded and
    // its window's `load` run before the element's `load` in this document -
    // a page's `iframe.onload` finds the frame's `onload` work already done.
    for (const frame_entry & entry : frames_) {
        if (entry.element == waiting.id && entry.bindings != nullptr) {
            entry.bindings->finish_frame_load(cx);
            break;
        }
    }
    const std::string_view type = waiting.ok ? "load" : "error";
    const value event = make_event_object(cx, type, false, false);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("target", wrap(cx, waiting.id));
    // The engine dispatched it, so `isTrusted` is true - as make_event says.
    object->set(std::string{detail::trusted_property}, value::boolean(true));
    object->set(std::string{detail::initialised_property}, value::boolean(true));
    fire_at(path_step{waiting.id, listen_on::node}, type, event, false);
}

// A FRAME'S BROWSING CONTEXT: its window, and the realm its scripts run in.
// `this` is the frame document's bindings, `owner` the document whose
// <iframe> - `element` - it is.
//
// ITS OWN REALM, which is what lets a frame run its scripts at all. They were
// not run before because the realm was shared: a frame's `var run` would have
// been the page's `run`. Now the frame's declarations land in the frame's
// binding table, a function the frame declared resolves its free names there
// whoever calls it (script::realm_record), and `frame.contentWindow.run` read
// by the page is the frame's. The table starts as the page's template - the
// builtins and the interface objects, not the page's own vars - so
// `frame.contentWindow.DOMException` is still the constructor an exception
// the frame's DOM threw is an instance of. ponytail: ONE set of intrinsics, so
// `contentWindow.Array === Array` where a browser has two; distinct prototypes
// are the upgrade when a cross-realm identity test is the next clearest one.
//
// NOT THE PAGE'S BROWSING-CONTEXT STATE: `location`, `history` and their kin
// are per-context. Handing back the page's would let `frame.contentWindow
// .location.href = x` rewrite the top document's address, so the frame's own
// replace them and the rest are removed from its table.
void dom_bindings::install_frame_realm(context & cx, dom_bindings & owner, node_id element) {
    script::realm_record & realm = cx.create_realm();
    realm_ = &realm;
    for (const std::string_view foreign : {"history", "name", "opener", "closed", "event"}) {
        (void)realm.globals.erase(std::string{foreign});
    }
    auto * frame_window = cx.allocate<script::object_object>();
    window_ = value::object(frame_window);
    const value frame_view = make_window_view(cx, window_, realm);
    window_view_ = frame_view;
    // `parent` is the window of the document the <iframe> is in - the page's,
    // or a frame's for a frame inside one - and `top` is that window's top.
    // The PROXY, never the object behind it: scripts compare `w != w.parent`
    // by identity (see install_window).
    const value parent_view =
        owner.window_view_.is_heap() ? owner.window_view_ : cx.global("window");
    value top_view = parent_view;
    if (script::object_object * above = owner.window_object(); above != nullptr) {
        if (const value * held = above->find("top"); held != nullptr && held->is_heap()) {
            top_view = *held;
        }
    }
    const value element_view = owner.wrap(cx, element);

    frame_window->set("document", document_);
    frame_window->set("location", location_);
    frame_window->set("customElements", custom_elements_registry(cx)); // custom_elements.cpp
    install_window_scrolling(cx, *frame_window); // scrollX/scrollY, scrollTo on a frame's window
    // The frame's own `matchMedia`: a list that reads the frame's viewport.
    const value match = native(cx, "matchMedia", [this](context & c, std::span<value> a) {
        return match_media(c, a.empty() ? std::string{} : c.to_string(a[0]));
    });
    frame_window->set("matchMedia", match);
    frame_window->set("frameElement", element_view);
    frame_window->set("self", frame_view);
    frame_window->set("window", frame_view);
    frame_window->set("frames", frame_view);
    frame_window->set("parent", parent_view);
    frame_window->set("top", top_view);
    frame_window->set("opener", value::null());
    frame_window->set("event", value::undefined());
    // `self.origin`: this document's, serialized - "null" for a file: one,
    // as its location.origin says.
    frame_window->define_accessor("origin",
                                  value::object(cx.allocate<script::native_object>(
                                      "origin",
                                      [this](context & c, std::span<value>) {
                                          const std::string origin =
                                              location_parts(location_href_).origin;
                                          return c.string(origin.empty() ? "null" : origin);
                                      })),
                                  value::undefined());
    // `length` counts THIS document's child frames, as the page's does.
    frame_window->define_accessor(
        "length",
        value::object(cx.allocate<script::native_object>(
            "length",
            [this](context &, std::span<value>) {
                return value::number(static_cast<double>(all_html_elements("iframe").size()));
            })),
        value::undefined());
    if (auto * doc = document_object()) {
        doc->set("location", location_);
        // AND THE DOCUMENT KNOWS ITS WINDOW. install_document set
        // `defaultView` null for a secondary, which is right for
        // createHTMLDocument and wrong for a frame: `root.ownerDocument
        // .defaultView` is how a test reaches the global an exception must
        // have come from.
        doc->set("defaultView", frame_view);
    }

    // THE REALM'S OWN BINDINGS, defined with it current.
    const context::realm_scope in_frame{cx, realm};
    cx.set_global_this(frame_view);
    cx.define_global("window", frame_view);
    cx.define_global("self", frame_view);
    cx.define_global("document", document_);
    cx.define_global("location", location_);
    cx.define_global("matchMedia", match);
    // THE NODE CONSTRUCTORS THAT NAME A DOCUMENT: `new frame.contentWindow
    // .Text()` - and a bare `new Text()` in the frame's own script - is a node
    // OF THE FRAME'S document (Text-constructor.html's cross-global case), so
    // those three are the frame's own natives over the shared prototypes:
    // `instanceof Text` still holds, `ownerDocument` is the frame's.
    for (const char * name : {"Text", "Comment", "DocumentFragment"}) {
        auto * ctor = cx.allocate<script::native_object>(
            name, [this, name](context & c, std::span<value> args) {
                return construct_node_interface(c, name, args);
            });
        ctor->set("prototype", cx.lookup_property(cx.global(name), "prototype"));
        frame_window->set(name, value::object(ctor));
        cx.define_global(name, value::object(ctor));
    }
    // Listening at THIS window: a frame script's `addEventListener("load", f)`
    // hears the frame's load, not the page's.
    install_window_listeners(cx, *frame_window);
    install_post_message(cx, *frame_window);
    install_window_handlers(cx, *frame_window);
    // scrollX/scrollY and scroll()/scrollTo()/scrollBy() over THIS document's
    // viewport: a frame script reading `scrollY` threw a ReferenceError, and a
    // test waiting on the frame's reply to a message timed out on it
    // (cssom-view/scrollIntoView-iframes.html).
    install_window_scrolling(cx, *frame_window);
    const value frame_dispatch =
        native(cx, "dispatchEvent", [this](context & c, std::span<value> a) {
            const value event = arg(a, 0);
            // OrdinaryHasInstance against Event: one set of interfaces for every
            // realm, so the frame's `Event` is the page's.
            if (!event.is_object() || !c.instance_of(event, c.global("Event"))) {
                c.throw_error("TypeError", "Failed to execute 'dispatchEvent' on 'Window': "
                                           "parameter 1 is not of type 'Event'.");
                return value::boolean(false);
            }
            return value::boolean(!dispatch_to(event, path_step{node_id{}, listen_on::window}));
        });
    frame_window->set("dispatchEvent", frame_dispatch);
    cx.define_global("dispatchEvent", frame_dispatch);
    // `Function` AND `eval` OF THIS REALM. CreateDynamicFunction and
    // PerformEval take the CALLEE's realm (20.2.1.1.1, 19.2.1.1), so
    // `new frames[0].Function(src)` is a function of frame 0 - its free names
    // are the frame's globals and its throws are reported at the frame's
    // window - and `contentWindow.eval(src)` runs in the frame. Each wraps the
    // one intrinsic inside a realm_scope; `prototype` stays %Function.prototype%
    // (the shared-intrinsics ceiling noted above).
    for (const char * name : {"Function", "eval"}) {
        const value intrinsic = cx.global(name);
        if (!intrinsic.is_callable()) { continue; }
        script::realm_record * const frame_realm = &realm;
        auto * wrapper = cx.allocate<script::native_object>(
            name, [intrinsic, frame_realm](context & c, std::span<value> args) {
                const context::realm_scope in_frame{c, *frame_realm};
                return c.call(intrinsic, args);
            });
        wrapper->retained.push_back(intrinsic);
        if (std::string_view{name} == "Function") {
            wrapper->define("prototype", cx.lookup_property(intrinsic, "prototype"),
                            script::attr_none);
        } else {
            wrapper->is_constructor = false;
        }
        wrapper->define("length", value::number(1), script::attr_configurable);
        wrapper->define("name", cx.string(name), script::attr_configurable);
        cx.define_global(name, value::object(wrapper));
    }
}

dom_bindings & dom_bindings::bindings_of(value window) {
    dom_bindings * found = find_bindings_of(window);
    return found != nullptr ? *found : primary();
}

dom_bindings * dom_bindings::find_bindings_of(value window) {
    const auto find = [window](const auto & self, dom_bindings & at) -> dom_bindings * {
        if (at.window_view_.is_heap() && at.window_view_.as_heap() == window.as_heap()) {
            return &at;
        }
        for (const frame_entry & frame : at.frames_) {
            if (frame.bindings == nullptr) { continue; }
            if (dom_bindings * found = self(self, *frame.bindings)) { return found; }
        }
        return nullptr;
    };
    return window.is_heap() ? find(find, primary()) : nullptr;
}

dom_bindings * dom_bindings::bindings_owning(value wrapper) {
    const auto find = [wrapper](const auto & self, dom_bindings & at) -> dom_bindings * {
        if (at.handle_of(wrapper)) { return &at; }
        for (const frame_entry & frame : at.frames_) {
            if (frame.bindings == nullptr) { continue; }
            if (dom_bindings * found = self(self, *frame.bindings)) { return found; }
        }
        return nullptr;
    };
    return wrapper.is_object() ? find(find, primary()) : nullptr;
}

std::optional<context::realm_scope> dom_bindings::enter_realm(context & cx) {
    if (realm_ == nullptr) { return std::nullopt; }
    return std::optional<context::realm_scope>{std::in_place, cx, *realm_};
}

// A SUBRESOURCE OF THIS DOCUMENT, as the asset registry names it. The page's
// relative names already mean what the registry resolves them against - the
// page's own directory - so they pass through; a frame's are relative to the
// FRAME's address, which is often another directory (`resources/frame.html`
// loading `helper.js`), so they are resolved against it and a file: URL is
// handed over as its path. A data: or blob: URL, and a document with no
// address, keep what the page wrote.
std::string dom_bindings::subresource_name(std::string_view src) const {
    if (realm_ == nullptr || location_href_.empty() || is_data_url(src) ||
        src.starts_with("blob:")) {
        return std::string{src};
    }
    const std::string url = resolve(location_href_, src);
    if (url.starts_with("file://")) { return percent_decode(url.substr(7)); }
    return url;
}

// THE SCRIPTS A FRAME'S PARSE LEFT, in tree order, in the frame's realm.
// HTML's parser runs each as it reaches it; a frame's document is parsed
// whole first (load_frame), so its classic scripts run afterwards in document
// order - which is also how an XHTML page's run (browser/scripts.cpp). A data
// block (`type="text/plain"`) and a module are not classic scripts; the
// module loader is the browser's.
void dom_bindings::run_frame_scripts(context & cx) {
    if (realm_ == nullptr || doc_ == nullptr) { return; }
    struct found {
        node_id id;
        std::string src;
        std::string text;
    };
    std::vector<found> scripts;
    {
        const auto txn = doc_->read();
        const atom script_tag = atoms_->intern_lower("script");
        const atom src_name = atoms_->intern("src");
        const atom type_name = atoms_->intern("type");
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.tag(at).value_or(atom{}) == script_tag && txn.element_ns(at) == node_ns::html) {
                const std::string type =
                    ascii_lower_copy(trim(txn.attribute_value(at, type_name), html_whitespace));
                if (type.empty() || type == "text/javascript" || type == "application/javascript") {
                    found script{at, std::string{txn.attribute_value(at, src_name)}, {}};
                    for (const node_id child : txn.children(at)) {
                        if (is_text_kind(txn.kind(child).value_or(node_kind::comment))) {
                            script.text += txn.text(child);
                        }
                    }
                    scripts.push_back(std::move(script));
                }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
    }
    for (found & script : scripts) {
        if (!script.src.empty()) {
            const std::vector<std::byte> bytes = assets_ == nullptr
                                                     ? std::vector<std::byte>{}
                                                     : assets_->load(subresource_name(script.src));
            if (bytes.empty()) { continue; }
            script.text.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        } else if (script.text.empty()) {
            continue;
        }
        execute_script_element(cx, script.id, script.text);
    }
}

// The end of a frame document's load: DOMContentLoaded and then the window's
// `load`, in its realm, ONCE - settle_frame asks, before the <iframe>'s own
// `load` in the document around it.
void dom_bindings::finish_frame_load(context & cx) {
    if (realm_ == nullptr || frame_load_finished_) { return; }
    frame_load_finished_ = true;
    const auto in_realm = enter_realm(cx);
    reconcile_frames();
    set_ready_state("interactive");
    (void)dispatch("DOMContentLoaded", node_id{});
    set_ready_state("complete");
    (void)dispatch("load", node_id{});
}

} // namespace ctbrowser::shell
