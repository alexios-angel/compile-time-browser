// dom_bindings - `<iframe>`, which is to say a NESTED BROWSING CONTEXT.
//
// WHY THIS IS ONE FILE AND NOT A PROJECT. A second Document already exists here
// - `document.implementation.createHTMLDocument()` returns one, and the block
// above `adopt_interfaces_of` in bindings/document.cpp is the model: a second
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
//   * A FRAME LAYS OUT AS AN EMPTY BOX, as it always did. This is the DOM half
//     of an iframe; the painting half would need a second layout tree inside
//     the first, and nothing here pretends to have one.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <algorithm>
#include <memory>
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
    if (lowered == "xml") { return "text/xml"; }
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
void dom_bindings::reconcile_frames() {
    if (cx_ == nullptr || secondary_ || !frames_dirty_) { return; }
    frames_dirty_ = false;
    // A frame element with no `src` has an about:blank document in a browser.
    // It gets one here too - `load_frame` with an empty src builds an empty
    // HTML document - because `frame.contentDocument.body` is how a page
    // writes into one, and answering `undefined` is the failure that reads as
    // "iframes do not exist".
    const std::vector<node_id> elements = all_html_elements("iframe");
    std::vector<std::pair<std::uint64_t, std::string>> still;
    still.reserve(elements.size());
    for (const node_id id : elements) {
        std::string src;
        {
            const auto txn = doc_->read();
            src = std::string{txn.attribute_value(id, atoms_->intern("src"))};
        }
        const std::uint64_t key = pack(id);
        const auto seen =
            std::ranges::find_if(frames_, [&](const auto & entry) { return entry.first == key; });
        if (seen != frames_.end() && seen->second == src) {
            still.push_back(*seen);
            continue;
        }
        load_frame(*cx_, id, src);
        still.emplace_back(key, src);
    }
    // A FRAME THAT LEFT THE TREE IS FORGOTTEN, and its document is not: the
    // secondary bindings stay in `secondary_documents_` because a page may
    // still be holding the `contentDocument` it took before removing the
    // frame, and a removed frame put back gets a NEW document, which is what
    // the DOM says happens.
    frames_.swap(still);
}

// Build the frame's document. Synchronous, because the bytes are already on
// disk or in the registry; the EVENT it queues is what makes the load
// asynchronous in the way a page can observe.
void dom_bindings::load_frame(context & cx, node_id id, const std::string & src) {
    std::string bytes;
    bool ok = src.empty(); // an empty src is about:blank, and that always loads
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
            const std::vector<std::byte> loaded = assets_->load(src);
            ok = !loaded.empty();
            bytes.resize(loaded.size());
            for (std::size_t i = 0; i < loaded.size(); ++i) {
                bytes[i] = static_cast<char>(loaded[i]);
            }
        }
        if (type.empty()) { type = "application/octet-stream"; }
    } else {
        type = "text/html";
    }

    document & fresh = *owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    auto & made = *secondary_documents_.emplace_back(
        std::make_unique<dom_bindings>(fresh, *atoms_, *canvases_, *forms_, std::function<void()>{},
                                       std::function<void(node_id)>{}));
    made.secondary_ = true;
    made.cx_ = &cx;
    // BEFORE the adoption, for the reason `make_html_document` gives: the
    // primary builds its interface table on the first `wrap()`, and a page
    // whose first frame loads before anything has been wrapped would adopt an
    // empty one and never be able to build another.
    ensure_dom_interfaces(cx);
    made.adopt_interfaces_of(*this);
    made.content_type_ = type;

    const bool is_xml = type == "application/xhtml+xml" || type == "text/xml" ||
                        type == "application/xml" || type == "image/svg+xml";
    // about:blank IS A DOCUMENT WITH A BODY, and it has to be: `frame
    // .contentDocument.body` is how a page writes into a scratch frame, and
    // parsing the empty string leaves a tree with no body at all to write to.
    if (src.empty()) {
        (void)parse_html(fresh, "<html><head></head><body></body></html>");
    } else if (!ok) {
        // A `src` that resolved to nothing still leaves a Document behind - a
        // browser shows its error page in one - and `error` rather than `load`
        // is what the element hears about it.
        (void)parse_html(fresh, "<html><head></head><body></body></html>");
    } else if (is_xml) {
        // The XML front end, which is why `.xhtml` is worth having at all: the
        // HTML tree builder lowercases `viewBox` and hands `<![CDATA[` to the
        // JavaScript engine. A frame whose XML is not well-formed keeps the
        // tree the parser had, exactly as a top-level XML document does.
        (void)parse_xml(fresh, bytes);
    } else if (type == "text/html") {
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

    // Hung off the WRAPPER rather than kept in a table beside it, so the frame
    // document is reachable from the element that owns it and the collector
    // needs no new root: `wrappers_` is already marked.
    const value element = wrap(cx, id);
    if (!element.is_object()) { return; }
    auto * frame_object = static_cast<script::object_object *>(element.as_heap());
    frame_object->set("contentDocument", made.document_);

    // `contentWindow`, and it is a SMALL object on purpose. What a page does
    // with one is read `.document` off it and hand it to `instanceof`; the rest
    // of the Window interface belongs to a realm this frame does not have.
    auto * frame_window = static_cast<script::object_object *>(cx.make_object().as_heap());
    frame_window->set("document", made.document_);
    frame_window->set("frameElement", element);
    frame_window->set("self", value::object(frame_window));
    frame_window->set("window", value::object(frame_window));
    frame_window->set("length", value::number(0));
    // `parent` and `top` are the PAGE's window, which is true: this frame's
    // parent browsing context is the top-level one and there is no nesting
    // below it.
    //
    // THROUGH `cx.global`, NOT `window_`. The window a page can name is a PROXY
    // over `window_` - that is how the object and the globals stay one storage -
    // so handing back the raw object makes `frame.contentWindow.parent ===
    // window` false, which is exactly the identity a page tests.
    if (const value page_window = cx.global("window"); page_window.is_object()) {
        frame_window->set("parent", page_window);
        frame_window->set("top", page_window);
    }
    frame_object->set("contentWindow", value::object(frame_window));

    frame_loads_.push_back(pending_frame{id, ok});
}

// `load` at the frame, or `error` when the src resolved to no bytes. It does
// not bubble - an iframe's load event is fired at the element and stops there -
// which is why this is `fire_at` with `capturing` false and not `dispatch`.
void dom_bindings::settle_frame(context & cx, const pending_frame & waiting) {
    auto * event = static_cast<script::object_object *>(cx.make_object().as_heap());
    event->set("type", cx.string(waiting.ok ? "load" : "error"));
    event->set("target", wrap(cx, waiting.id));
    fire_at(path_step{waiting.id, listen_on::node}, waiting.ok ? "load" : "error",
            value::object(event), false);
}

} // namespace ctbrowser::shell
