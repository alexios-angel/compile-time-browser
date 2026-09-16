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
// WHAT IS DELIBERATELY NOT HERE, named rather than left to be discovered:
//
//   * THE REALM IS SHARED, exactly as it is for `createHTMLDocument`. So
//     `frame.contentWindow.Element === window.Element` where a browser has two,
//     and `new frame.contentWindow.Comment()` makes a node in the PAGE's
//     document rather than the frame's. Every test that turns on cross-realm
//     identity still fails; what it no longer does is hang.
//   * NO NAVIGATION AND NO HISTORY. A frame is loaded once per `src`; there is
//     no `location` on `contentWindow` to assign to, no `about:blank` document
//     replaced by a real one, and no `srcdoc`.
//   * NO SCRIPTS RUN IN A FRAME. The frame's document is parsed, not executed:
//     one realm and one event loop mean a frame's script would be the page's
//     script wearing the frame's name, which is worse than nothing.
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
    const std::string_view ext = extension_of(path);
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

    // THE SAME WAY createHTMLDocument MAKES ONE - in the primary's flat list,
    // with `primary_` set. Built by hand here before, without `primary_`, so
    // `owner_of` run from inside a frame document could see nothing but the
    // frame's own wrappers: `frame.contentDocument.body.appendChild(pageNode)`
    // was "the argument is not a Node" (Node-isConnected.html's iframe case,
    // the node-realm-* files), and a frame's own `<iframe>` never loaded.
    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
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
        }
        (void)parse_html(fresh, bytes);
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

    // Hung off the WRAPPER rather than kept in a table beside it, so the frame
    // document is reachable from the element that owns it and the collector
    // needs no new root: `wrappers_` is already marked.
    const value element = wrap(cx, id);
    if (!element.is_object()) { return nullptr; }
    auto * frame_object = static_cast<script::object_object *>(element.as_heap());
    frame_object->set("contentDocument", made.document_);

    // `contentWindow`: a SMALL object with its own few properties, behind a
    // proxy whose miss reads the PAGE's globals. The realm is shared (see the
    // header), so `frame.contentWindow.DOMException`, `.TypeError` and
    // `.NodeList` ARE the page's, and answering them from the one table is
    // what makes `e instanceof frameWindow.DOMException` true for an exception
    // this frame's own DOM threw. Measured: every invalid-selector case in
    // `ParentNode-querySelector-All` is `assert_throws_dom("SyntaxError",
    // windowFor(root).DOMException, ...)`, and with that constructor undefined
    // testharness took it for the function to call - 272 subtests reporting
    // "`call` is undefined" about a method that had thrown correctly.
    auto * frame_window = cx.allocate<script::object_object>();
    auto * handler = cx.allocate<script::object_object>();
    // NOT THE PAGE'S BROWSING-CONTEXT STATE, though. `location`, `history`
    // and their kin are per-context, and this frame has none (see the header):
    // handing back the page's would let `frame.contentWindow.location.href =
    // x` rewrite the top document's address, which is worse than the
    // `undefined` a page can test for.
    const auto shared_global = [](context & c, std::string_view name) {
        for (const std::string_view own :
             {"location", "history", "frames", "name", "opener", "closed", "event"}) {
            if (own == name) { return false; }
        }
        return c.has_global(name);
    };
    set_method(cx, *handler, "get", [shared_global](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
        auto * target = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        if (target->find(name) == nullptr && target->find_accessor(name) == nullptr &&
            shared_global(c, name)) {
            return c.global(name);
        }
        return c.lookup_property(args[0], name);
    });
    set_method(cx, *handler, "has", [shared_global](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::boolean(false); }
        auto * target = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        return value::boolean(target->find(name) != nullptr ||
                              target->find_accessor(name) != nullptr || shared_global(c, name));
    });
    const value frame_view = value::object(
        cx.allocate<script::proxy_object>(value::object(frame_window), value::object(handler)));
    frame_window->set("document", made.document_);
    frame_window->set("frameElement", element);
    // THE NODE CONSTRUCTORS THAT NAME A DOCUMENT: `new frame.contentWindow
    // .Text()` is a node OF THE FRAME'S document (Text-constructor.html's
    // cross-global case), so those three are the frame's own natives over the
    // shared prototypes - `instanceof Text` still holds, `ownerDocument` is
    // the frame's.
    for (const char * name : {"Text", "Comment", "DocumentFragment"}) {
        auto * ctor = cx.allocate<script::native_object>(
            name, [&made, name](context & c, std::span<value> args) {
                return made.construct_node_interface(c, name, args);
            });
        ctor->set("prototype", cx.lookup_property(cx.global(name), "prototype"));
        frame_window->set(name, value::object(ctor));
    }
    frame_window->set("self", frame_view);
    frame_window->set("window", frame_view);
    frame_window->set("length", value::number(0));
    // `parent` and `top` are the PAGE's window, which is true: this frame's
    // parent browsing context is the top-level one and there is no nesting
    // below it.
    //
    // THROUGH `cx.global`, NOT `window_`. The window a page can name is a PROXY
    // over `window_` - that is how the object and the globals stay one storage -
    // so handing back the raw object makes `frame.contentWindow.parent ===
    // window` false, which is exactly the identity a page tests. And NOT
    // behind `is_object()`, which is kind-exact and false for a proxy: that
    // guard was never taken, and `parent` was undefined.
    const value page_window = cx.global("window");
    frame_window->set("parent", page_window);
    frame_window->set("top", page_window);
    frame_object->set("contentWindow", frame_view);
    // AND THE DOCUMENT KNOWS ITS WINDOW. install_document set `defaultView`
    // null for a secondary, which is right for createHTMLDocument and wrong
    // for a frame: `root.ownerDocument.defaultView` is how a test reaches the
    // global an exception must have come from.
    if (auto * doc = made.document_object()) { doc->set("defaultView", frame_view); }

    // ON THE PRIMARY'S QUEUE, which is the one the tick drains, naming the
    // owner whose element it is; settle_frame hands it back.
    top.frame_loads_.push_back(pending_frame{id, ok, false, &top == this ? nullptr : this});
    return &made;
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
    const std::string_view type = waiting.ok ? "load" : "error";
    const value event = make_event_object(cx, type, false, false);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("target", wrap(cx, waiting.id));
    // The engine dispatched it, so `isTrusted` is true - as make_event says.
    object->set(std::string{detail::trusted_property}, value::boolean(true));
    object->set(std::string{detail::initialised_property}, value::boolean(true));
    fire_at(path_step{waiting.id, listen_on::node}, type, event, false);
}

} // namespace ctbrowser::shell
