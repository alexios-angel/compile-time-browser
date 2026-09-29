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
//     often enough that firing from the insertion would fire at nothing -
//     EXCEPT for a frame inserted with no `src`, whose about:blank fires its
//     `load` during the insertion, as HTML's insertion steps say, and so at
//     nothing listening yet (connect_blank_frame).
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
//   * NO HISTORY. A frame is loaded once per source - its `srcdoc`, else its
//     `src` - and NAVIGATED by a form submission, its Location's setters or
//     window.open() (the planned navigations below); there is no session
//     history to go back through, and a navigation makes a new window where a
//     browser keeps one WindowProxy per navigable.
//   * NO PARSER-TIME SCRIPTS. The frame's document is parsed whole and its
//     classic scripts run afterwards in tree order, as an XHTML page's do; a
//     `document.write` from one of them writes into a finished document.
//   * A FRAME PAINTS AS AN EMPTY BOX. This is the DOM half of an iframe; the
//     frame's document IS laid out, at the size its box got, by the browser
//     (browser/nested.cpp) so that what a script measures inside it is about
//     the frame - but nothing draws that layout into the page's.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include "element/internal.hpp"
#include "events/internal.hpp"
#include "window/internal.hpp"

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

// An <iframe srcdoc>'s source, as the frame tables key it: the markup behind a
// prefix no URL can spell, so a changed srcdoc re-navigates as a changed src
// does (frame_source).
constexpr std::string_view srcdoc_key{"\0srcdoc:", 8};
// ...and the markup a `javascript:` URL's string result is (run_planned_navigations).
constexpr std::string_view markup_key{"\0markup:", 8};

// A frame source that is a `javascript:` URL: the scheme, ASCII
// case-insensitively, after the leading C0 controls and spaces the URL parser
// strips.
[[nodiscard]] bool is_javascript_url(std::string_view source) {
    while (!source.empty() && static_cast<unsigned char>(source.front()) <= 0x20) {
        source.remove_prefix(1);
    }
    return ascii_istarts_with(source, "javascript:");
}

// A source that is about:blank (HTML's "matches about:blank": the about
// scheme and the path `blank`, a query or fragment allowed) - or none at all.
[[nodiscard]] bool is_blank_source(std::string_view source) {
    source = trim(source, " \t\n\f\r");
    if (source.empty()) { return true; }
    if (!ascii_istarts_with(source, "about:blank")) { return false; }
    return source.size() == 11 || source[11] == '?' || source[11] == '#';
}

// `text` as a JavaScript string literal.
[[nodiscard]] std::string js_string_literal(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (byte < 0x20) {
            constexpr std::string_view hex = "0123456789abcdef";
            out += "\\x";
            out.push_back(hex[byte >> 4u]);
            out.push_back(hex[byte & 0x0Fu]);
        } else {
            out.push_back(c);
        }
    }
    // U+2028 and U+2029 end a line inside a string literal before ES2019;
    // escaped, whichever the compiler implements.
    for (const auto & [raw, escaped] : {std::pair{std::string_view{"\xE2\x80\xA8"}, "\\u2028"},
                                        std::pair{std::string_view{"\xE2\x80\xA9"}, "\\u2029"}}) {
        for (std::size_t at = out.find(raw); at != std::string::npos; at = out.find(raw, at)) {
            out.replace(at, raw.size(), escaped);
        }
    }
    return out + "\"";
}

} // namespace

// WHAT A FRAME NAVIGATES TO - HTML 4.8.5, "process the iframe attributes":
// its `srcdoc` when it has one (the attribute wins over `src`), else its
// `src`; empty is about:blank.
std::string dom_bindings::frame_source(const read_txn & txn, node_id id) const {
    if (const atom srcdoc = atoms_->intern("srcdoc"); txn.has_attribute(id, srcdoc)) {
        return std::string{srcdoc_key} + std::string{txn.attribute_value(id, srcdoc)};
    }
    return std::string{txn.attribute_value(id, atoms_->intern("src"))};
}

// HTML 4.2.3's FALLBACK BASE URL: an iframe srcdoc document's is the document
// base URL of the document its <iframe> is in; anything else's is its own
// address. What a relative reference in a srcdoc resolves against.
std::string dom_bindings::fallback_base_url() {
    if (frame_owner_ != nullptr && location_href_ == "about:srcdoc") {
        const std::string fallback = frame_owner_->fallback_base_url();
        return detail::document_base_url(frame_owner_->doc_->read(), *atoms_, fallback);
    }
    return location_href_;
}

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
// document the same way - the primary's pass, which the tick asks for, goes
// on into every frame's document, and the accessor asks the element's owner
// (`install_frame_accessors`). A document
// WITHOUT a browsing context - createHTMLDocument's, DOMParser's - has no
// nested navigables and its `<iframe>`s stay empty (`contentDocument` null),
// and "has a window" is exactly that fact: install_document leaves
// `defaultView` null for one and load_frame sets it for a frame's.
void dom_bindings::reconcile_frames() {
    // Not re-entered: a frame's script that dirties this document and reads
    // `window.length` while it loads would otherwise start the frames this
    // pass has not filed yet over again, and its script again.
    if (cx_ == nullptr || reconciling_) { return; }
    reconciling_ = true;
    [this] {
        if (!frames_dirty_) { return; }
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
                src = frame_source(txn, id);
            }
            const auto seen = std::ranges::find_if(
                frames_, [&](const auto & entry) { return entry.element == id; });
            if (seen != frames_.end() && seen->src == src) {
                still.push_back(*seen);
                continue;
            }
            still.push_back(frame_entry{id, src, may_nest(src) ? start_frame(id, src) : nullptr});
        }
        // A FRAME THAT LEFT THE TREE IS FORGOTTEN, and its document is not: the
        // secondary bindings stay in `secondary_documents_` because a page may
        // still be holding the `contentDocument` it took before removing the
        // frame, and a removed frame put back gets a NEW document, which is what
        // the DOM says happens.
        frames_.swap(still);
    }();
    // AND EACH FRAME'S DOCUMENT ITS OWN, since the tick asks only the
    // primary: an <iframe src> a script put into a frame's document loads
    // and fires its `load` without anyone reading its `contentWindow` first
    // (shadow-dom/.../html-forms/test-003.html submits a frame document's
    // form at one). Over a copy - a load runs script, which can reconcile.
    reconciling_ = false;
    const std::vector<frame_entry> children = frames_;
    for (const frame_entry & entry : children) {
        if (entry.bindings != nullptr) { entry.bindings->reconcile_frames(); }
    }
}

// WHETHER AN <iframe> HERE GETS A NAVIGABLE AT ALL. HTML 4.8.5's shared
// attribute processing steps return null - no navigation, no `load` - when an
// inclusive ancestor navigable's document has the frame's URL, fragments
// excluded: a page that embeds itself stops at one copy. about:blank, srcdoc
// and javascript: sources are exempt from that; nothing is from the depth
// cap, which bounds what script and srcdoc can chain.
// ponytail: Gecko's cap of 10 nested content frames.
bool dom_bindings::may_nest(const std::string & source) const {
    constexpr std::size_t max_depth = 10;
    std::size_t depth = 0;
    for (const dom_bindings * up = frame_owner_; up != nullptr; up = up->frame_owner_) {
        if (++depth >= max_depth) { return false; }
    }
    if (is_blank_source(source) || source.starts_with(srcdoc_key) ||
        source.starts_with(markup_key) || is_javascript_url(source)) {
        return true;
    }
    const auto unfragmented = [](std::string url) { return url.substr(0, url.find('#')); };
    const std::string url =
        unfragmented(location_href_.empty() ? source : resolve(location_href_, source));
    for (const dom_bindings * doc = this; doc != nullptr; doc = doc->frame_owner_) {
        if (unfragmented(doc->location_href_) == url) { return false; }
    }
    return true;
}

// AN <iframe> CONNECTED WITH NO `src`, OR ONE THAT IS about:blank - HTML
// 4.8.5's insertion steps ("create a new child navigable", then "process the
// iframe attributes" with initialInsertion; false for any other source): its
// navigable exists at once, and the about:blank it starts on fires `load`
// SYNCHRONOUSLY, during the insertion - so a listener
// added after the insertion never hears it. The Range tests append two blank
// frames, then set `onload` and a `src`, and waited for the navigation's
// `load`; queued for the next tick, the about:blank one arrived instead and
// the test ran against an empty frame (dom/ranges/Range-insertNode.html).
//
// A frame WITH a src keeps loading lazily (reconcile_frames): what it shows is
// fetched, and its `load` is a later task however it arrives.
//
// The lazy reconcile is held off while the `load` runs, as it is while an
// inserted script runs (execute_script_element): a later frame of the same
// insertion has not had its insertion steps yet, and reading its
// `contentWindow` from this one's `load` must answer null.
// The queued `load` load_frame left for this document's <iframe> `id`, which
// its caller announces itself - or, for a javascript: source, not at all.
void dom_bindings::forget_frame_load(node_id id) {
    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
    dom_bindings * const owner = &top == this ? nullptr : this;
    std::erase_if(top.frame_loads_, [&](const pending_frame & waiting) {
        return !waiting.resource && waiting.id == id && waiting.owner == owner;
    });
}

// A frame's FIRST document, for the source it has when it is reconciled. A
// `javascript:` URL is a navigation of the about:blank the frame starts on
// (HTML 7.4.2.3.2): that document, announcing nothing, and the URL's script
// evaluated a task later in its realm (run_planned_navigations) - a string
// result becomes the frame's document, anything else leaves it as it is. A
// form a page submits at the frame meanwhile navigates it after the script
// ran (jsurl-navigation-then-form-submit.html).
dom_bindings * dom_bindings::start_frame(node_id id, const std::string & source) {
    if (cx_ == nullptr) { return nullptr; }
    if (!is_javascript_url(source)) { return load_frame(*cx_, id, source); }
    dom_bindings * made = load_frame(*cx_, id, std::string{});
    forget_frame_load(id);
    form_navigation navigation;
    navigation.src = source;
    navigation.request.url = source;
    navigate_frame(*this, id, std::move(navigation));
    return made;
}

bool dom_bindings::connect_blank_frame(node_id id, const std::string & source) {
    if (cx_ == nullptr || !is_blank_source(source) || !may_nest(source)) { return false; }
    if (std::ranges::any_of(frames_, [id](const frame_entry & e) { return e.element == id; })) {
        return true;
    }
    dom_bindings * made = load_frame(*cx_, id, source);
    frames_.push_back(frame_entry{id, source, made});
    forget_frame_load(id);
    const bool were_dirty = frames_dirty_;
    frames_dirty_ = false;
    settle_frame(*cx_, pending_frame{id, true, false, nullptr});
    frames_dirty_ = frames_dirty_ || were_dirty;
    return true;
}

// The insertion steps of the <iframe>s a mutation connected, in tree order:
// each blank one gets its navigable and its synchronous `load`
// (connect_blank_frame). Only a document with a browsing context has nested
// navigables - createHTMLDocument's does not.
void dom_bindings::connect_inserted_frames(const std::vector<document::write_note> & writes) {
    if (cx_ == nullptr || doc_ == nullptr || (secondary_ && realm_ == nullptr)) { return; }
    std::vector<std::pair<node_id, std::string>> blank;
    {
        const auto txn = doc_->read();
        const atom iframe_tag = atoms_->intern_lower("iframe");
        const auto walk = [&](const auto & self, node_id at) -> void {
            if (txn.tag(at).value_or(atom{}) == iframe_tag && txn.element_ns(at) == node_ns::html &&
                std::ranges::none_of(blank, [at](const auto & one) { return one.first == at; })) {
                if (std::string source = frame_source(txn, at); is_blank_source(source)) {
                    blank.emplace_back(at, std::move(source));
                }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        for (const document::write_note & note : writes) {
            if (note.kind == document::write_note::edit::inserted && note.child &&
                txn.contains(note.child)) {
                walk(walk, note.child);
            }
        }
    }
    for (const auto & [id, source] : blank) {
        // Connected - and still, after a `load` an earlier frame fired, which
        // can have moved or removed this one.
        if (is_connected(id)) { (void)connect_blank_frame(id, source); }
    }
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

// THE EMBEDDER'S ANSWER to a request (browser_options::on_request), asked
// before the asset registry by a frame's load and by load_resource - nullopt
// when there is no handler or it left the request alone. The handler is the
// primary's: a frame document asks the one its page was given.
void dom_bindings::observe_requests(const request_handler * handler) {
    request_handler_ = handler;
}

std::optional<resource_reply> dom_bindings::ask_request_handler(resource_request request) {
    const dom_bindings & top = primary_ == nullptr ? *this : *primary_;
    if (top.request_handler_ == nullptr || !*top.request_handler_) { return std::nullopt; }
    // A fragment never leaves the client.
    request.url = request.url.substr(0, request.url.find('#'));
    return (*top.request_handler_)(request);
}

// Build the frame's document. Synchronous, because the bytes are already on
// disk or in the registry; the EVENT it queues is what makes the load
// asynchronous in the way a page can observe.
//
// `request` is a NAVIGATION's (run_planned_navigations): a form submission's
// method, absolute URL, Content-Type and body. Null is an ordinary load of
// `src` - a GET of it, resolved against this document's address. Either is
// offered to the embedder's request handler first, whose Content-Type is then
// the document's type and whose charset, when it names an encoding, the
// document's encoding: "the transport layer" of HTML 13.2.3.1, which only a
// BOM overrules.
dom_bindings * dom_bindings::load_frame(context & cx, node_id id, const std::string & src,
                                        const resource_request * request) {
    std::string bytes;
    // A FRAME ALWAYS LOADS. A navigation that fetched nothing - a 404, and
    // here a name the registry cannot find - still ends in a document (the
    // error page), and `load` fires at the element for it; `error` is not an
    // iframe's event. An EMPTY file is the same as a missing one to the
    // registry, and Document-createElement-namespace.html's empty.html waits
    // on exactly that load.
    bool ok = true;
    std::string type{mime_for_path(src)};
    std::string charset; // the transport layer's, when a request handler named one
    // THE SAME WAY createHTMLDocument MAKES ONE - in the primary's flat list,
    // with `primary_` set (below); the primary is also where the object URL
    // table lives.
    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
    // THE DOCUMENT'S ADDRESS, fragment and all: the navigation's, else the
    // src resolved against this document's.
    std::string href;
    // AN iframe srcdoc DOCUMENT (4.8.5): the attribute's markup is the
    // resource - text/html, UTF-8 - and about:srcdoc its address.
    // A `javascript:` URL's string result is the same kind of resource, at
    // the address the frame already had - about:blank, which a frame here
    // spells as no address at all.
    const bool markup = src.starts_with(markup_key);
    const bool srcdoc = markup || src.starts_with(srcdoc_key);
    if (srcdoc) {
        href = markup ? std::string{} : std::string{"about:srcdoc"};
        bytes = src.substr(srcdoc_key.size());
        type = "text/html";
        charset = "UTF-8";
    } else if (request != nullptr && !request->url.empty()) {
        href = request->url;
    } else if (!src.empty()) {
        href = location_href_.empty() ? src : resolve(location_href_, src);
    }
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
    // THE EMBEDDER FIRST, for anything asked for by URL - a data: or a blob:
    // URL's bytes are already here.
    bool answered = srcdoc;
    if (!answered && !src.empty() && !src.starts_with("blob:") && !is_data_url(src)) {
        resource_request asked = request != nullptr ? *request : resource_request{};
        asked.url = href;
        if (std::optional<resource_reply> reply = ask_request_handler(std::move(asked))) {
            answered = true;
            bytes = std::move(reply->body);
            // The essence - `text/plain; charset=UTF-8` is text/plain - and the
            // charset parameter, by the Encoding Standard's label table.
            const std::string_view stated = reply->content_type;
            const std::size_t semicolon = stated.find(';');
            type = ascii_lower_copy(trim(stated.substr(0, semicolon), " \t"));
            if (semicolon != std::string_view::npos) {
                charset = std::string{
                    encoding_from_label(encoding_from_meta_content(stated.substr(semicolon + 1)))};
            }
        }
    }
    if (!answered && !src.empty()) {
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
            bytes.assign(reinterpret_cast<const char *>(loaded.data()), loaded.size());
            // A `file:` URL IS A PATH: what a submission's action is once it is
            // resolved against a page opened from disk. Read as that path,
            // percent-decoded, without its query - as load_resource reads one.
            if (bytes.empty() && href.starts_with("file://")) {
                std::string_view path = std::string_view{href}.substr(7);
                path = path.substr(std::min(path.find('/'), path.size()));
                path = path.substr(0, std::min(path.find('?'), path.find('#')));
                const std::vector<std::byte> read = assets_->load(percent_decode(path));
                bytes.assign(reinterpret_cast<const char *>(read.data()), read.size());
            }
        }
    }
    if (type.empty()) { type = src.empty() ? "text/html" : "application/octet-stream"; }

    // Built by hand here before, without `primary_`, so
    // `owner_of` run from inside a frame document could see nothing but the
    // frame's own wrappers: `frame.contentDocument.body.appendChild(pageNode)`
    // was "the argument is not a Node" (Node-isConnected.html's iframe case,
    // the node-realm-* files), and a frame's own `<iframe>` never loaded.
    document & fresh = *top.owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    dom_bindings & made = adopt_second_document(cx, fresh);
    made.content_type_ = type;
    // A FRAME'S CLICKS ARE MOUSE EVENTS: dispatch picks a click's activation
    // target only for an event on the MouseEvent prototype, and a frame
    // document's `element.click()` builds its event from these two - left
    // unset, no click in a frame ever activated anything.
    made.mouse_event_prototype_ = top.mouse_event_prototype_;
    made.pointer_event_prototype_ = top.pointer_event_prototype_;
    // ITS NAVIGABLE, known from the start: a form its own scripts submit to
    // `_self` while it is still loading - before the reconcile has filed it
    // under this document's frames - navigates the frame it is in
    // (getactionurl.html).
    made.frame_owner_ = this;
    made.frame_element_ = id;

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
        const bool has_bom = bytes.starts_with("\xEF\xBB\xBF") || bytes.starts_with("\xFE\xFF") ||
                             bytes.starts_with("\xFF\xFE");
        if (!charset.empty() && !has_bom) {
            fresh.set_encoding(charset); // the transport layer's (13.2.3.1): certain
        } else if (const std::string declared = prescan_encoding(bytes); !declared.empty()) {
            fresh.set_encoding(declared);
        } else {
            fresh.set_encoding(doc_->encoding()); // 13.2.3.2 step 8: the parent's, same-origin
        }
        (void)parse_html(fresh, decode_document_bytes(bytes, fresh.encoding()));
    } else if (type.starts_with("text/") || type == "application/json" ||
               type == "text/javascript") {
        // HTML 7.5's plain-text document: the parser as if it had seen a `pre`
        // start tag and ONE newline - which the in-body rules drop - so a text
        // that itself starts with a newline keeps it. Decoded by the transport
        // layer's charset when there is one.
        if (!charset.empty()) { fresh.set_encoding(charset); }
        (void)parse_html(
            fresh, "<html><head></head><body><pre>\n" +
                       escaped(charset.empty() ? bytes : decode_document_bytes(bytes, charset)) +
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
    if (!href.empty()) {
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
    // A FRAME DOCUMENT'S DEFAULT ACTIONS. The page's are the browser's -
    // browser::activate, through set_activate_hook - and a frame's document has
    // no browser of its own, so a submit or reset button clicked in one did
    // nothing at all; submit-entity-body.html clicks the button inside the
    // frame it loaded. The same decision the browser makes (form_store::
    // form_action_of: the button, its form owner, "actually disabled"), run
    // through the frame's own bindings. ponytail: a <label> in a frame clicks
    // nothing and a <select> opens no popup - browser::activate's other
    // branches are the page's alone until a frame is rendered.
    made.set_activate_hook([&made](node_id target) {
        form_store::form_action action;
        {
            const auto txn = made.doc_->read();
            action = form_store::form_action_of(txn, *made.atoms_, target);
        }
        if (!action.form) { return; }
        // The submission and reset algorithms are installed WITH the frame
        // document's operations, which a frame builds on its first operation
        // call - and a dispatched click calls none.
        if (made.operations_.empty() && made.cx_ != nullptr) { made.install_operations(*made.cx_); }
        if (action.submits && made.submit_from_) { (void)made.submit_from_(action.form, target); }
        if (action.resets && made.reset_form_) { made.reset_form_(action.form); }
    });
    frame_object->set("contentWindow", made.window_view_);
    made.run_frame_scripts(cx);

    // ON THE PRIMARY'S QUEUE, which is the one the tick drains, naming the
    // owner whose element it is; settle_frame hands it back.
    top.frame_loads_.push_back(pending_frame{id, ok, false, &top == this ? nullptr : this});
    return &made;
}

// THE RULES FOR CHOOSING A NAVIGABLE (HTML 7.3.1.7), for the navigables this
// engine has - a frame document's own and each <iframe>'s - from this
// document's: "" and `_self` its own; `_parent` the one its frame is in, or
// its own when it has none; `_top` the top-level one; any other name but
// `_blank` the navigable of that name - this document's own frame, then an
// <iframe> in this document, then anywhere in the page. Answered as the
// <iframe> - its document's bindings and the element - or nothing for the
// TOP-LEVEL navigable, which is the embedder's page, and for a name nothing
// has (a new top-level traversable, which this user agent never opens: its
// popup blocker is always on).
std::pair<dom_bindings *, node_id> dom_bindings::choose_navigable(std::string_view target) {
    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
    // The <iframe> whose content document `doc` is - its document's bindings
    // and the element - or nothing for the page itself.
    const auto holding = [](const dom_bindings * doc) {
        return std::pair<dom_bindings *, node_id>{doc->frame_owner_, doc->frame_element_};
    };
    const std::string keyword = ascii_lower_copy(target);
    const std::pair<dom_bindings *, node_id> own = holding(this);
    if (keyword.empty() || keyword == "_self") { return own; }
    if (keyword == "_parent") { return own.first == nullptr ? own : holding(own.first); }
    if (keyword == "_top" || keyword == "_blank") {
        return {nullptr, node_id{}};
    }
    const atom name = atoms_->intern("name");
    if (own.first != nullptr) {
        const auto txn = own.first->doc_->read();
        if (txn.attribute_value(own.second, name) == target) { return own; }
    }
    // The first <iframe> of `doc` whose name is the target. RECONCILED first,
    // which can build a frame and run its scripts - so the search walks copies
    // of each frame list, never the live one: a script there that reads an
    // ancestor's `contentWindow` reconciles that ancestor's list, and swapping
    // a vector under a range-for is a use-after-free. A dom_bindings is never
    // freed while the page lives (secondary_documents_), so a copied pointer
    // stays good.
    const auto named = [&](dom_bindings & doc) -> node_id {
        doc.reconcile_frames();
        const std::vector<node_id> frames = doc.all_html_elements("iframe");
        const auto txn = doc.doc_->read();
        for (const node_id id : frames) {
            if (txn.attribute_value(id, name) == target) { return id; }
        }
        return node_id{};
    };
    if (const node_id found = named(*this)) { return {this, found}; }
    std::pair<dom_bindings *, node_id> chosen{nullptr, node_id{}};
    const auto search = [&](const auto & self, dom_bindings & doc) -> void {
        if (chosen.first != nullptr) { return; }
        if (const node_id found = named(doc)) {
            chosen = {&doc, found};
            return;
        }
        const std::vector<frame_entry> children = doc.frames_;
        for (const frame_entry & entry : children) {
            if (entry.bindings != nullptr) { self(self, *entry.bindings); }
        }
    };
    search(search, top);
    return chosen;
}

// HTML 4.10.21.3 "submit", steps 20-24, and "plan to navigate": the navigable
// a submission goes to, and the navigation PLANNED there rather than run.
// build_submission made `navigation` - the URL, a GET's query already the
// entry list, and the request that loads it; the rules for choosing a
// navigable decide where it goes. A TOP-LEVEL navigable is the embedder's
// page, which no submission replaces here: `_top`, `_blank`, `_self` in the
// page itself and `_parent` out of a frame the page holds all answer false,
// and the browser records the entry list instead (browser::submit's
// last_submission).
//
// PLANNED, one per form: "if the form has a non-null planned navigation,
// remove it from its task queue" - a later submission of the same form, from
// whatever and at whatever target, replaces the earlier one before it ran
// (form-double-submit*.html). The plan runs at the start of the next tick
// (run_planned_navigations), a task after the one that submitted.
bool dom_bindings::navigate_form_target(node_id form, form_navigation navigation) {
    if (cx_ == nullptr || !form) { return false; }
    const auto [navigable, element] = choose_navigable(navigation.target);
    if (navigable == nullptr) { return false; }
    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
    std::erase_if(top.planned_navigations_, [&](const planned_navigation & plan) {
        return plan.form_document == this && plan.form == form;
    });
    top.planned_navigations_.push_back(
        planned_navigation{this, form, navigable, element, std::move(navigation)});
    return true;
}

// "Navigate" an <iframe> from script - a Location's setters and window.open():
// the navigation joins the planned ones and runs at the start of the next tick,
// and it CANCELS whatever was still waiting there for the same navigable - the
// navigate algorithm's "cancel any existing but not yet mature navigation", a
// planned form submission included (form-submit-iframe-then-location-navigate
// .html, whose behaviour browsers agree on and HTML leaves implicit).
void dom_bindings::navigate_frame(dom_bindings & navigable, node_id element,
                                  form_navigation navigation) {
    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
    std::erase_if(top.planned_navigations_, [&](const planned_navigation & plan) {
        return plan.navigable == &navigable && plan.element == element;
    });
    top.planned_navigations_.push_back(
        planned_navigation{nullptr, node_id{}, &navigable, element, std::move(navigation)});
}

// The planned navigations, run: each <iframe> loads what its plan asked for -
// a new document under the same element - unless the plan's form's document
// is no longer active in the page, or the navigable's: a planned navigation
// is an element task, and a navigated frame's document runs no more tasks.
// A script's navigation (navigate_frame) has no form document to ask about.
void dom_bindings::run_planned_navigations() {
    if (cx_ == nullptr || planned_navigations_.empty()) { return; }
    std::vector<planned_navigation> due;
    due.swap(planned_navigations_);
    const auto active = [this](const dom_bindings * doc) {
        if (doc == this) { return true; }
        const auto walk = [doc](const auto & self, const dom_bindings & at) -> bool {
            const std::vector<frame_entry> children = at.frames_;
            for (const frame_entry & entry : children) {
                if (entry.bindings == doc ||
                    (entry.bindings != nullptr && self(self, *entry.bindings))) {
                    return true;
                }
            }
            return false;
        };
        return walk(walk, *this);
    };
    for (planned_navigation & plan : due) {
        if ((plan.form_document != nullptr && !active(plan.form_document)) ||
            !active(plan.navigable)) {
            continue;
        }
        dom_bindings & owner = *plan.navigable;
        if (!owner.is_connected(plan.element)) { continue; }
        owner.reconcile_frames();
        std::string source = plan.navigation.src;
        if (is_javascript_url(source)) {
            // "Evaluate a javascript: URL" (7.4.2.3.2) in the frame's current
            // document: the percent-decoded body as a script, and its
            // completion value - a string is the new document's markup, and
            // anything else navigates nowhere.
            const auto current = std::ranges::find_if(
                owner.frames_, [&](const frame_entry & e) { return e.element == plan.element; });
            if (current == owner.frames_.end() || current->bindings == nullptr) { continue; }
            const std::size_t colon = source.find(':');
            script::program compiled = script::compiler::compile(
                "return eval(" + js_string_literal(percent_decode(source.substr(colon + 1))) +
                ");");
            if (!compiled.ok) { continue; }
            value result = value::undefined();
            {
                const auto in_frame = current->bindings->enter_realm(*cx_);
                result = cx_->run_nested(cx_->own_program(std::move(compiled)));
            }
            if (cx_->failed()) { (void)cx_->take_error(); }
            if (cx_->throw_pending()) { (void)cx_->take_pending_throw(); }
            if (!result.is_string()) { continue; }
            source = std::string{markup_key} + cx_->to_string(result);
        }
        dom_bindings * made =
            owner.load_frame(*cx_, plan.element, source, &plan.navigation.request);
        // THE ELEMENT'S `src` IS UNCHANGED, so the entry keeps what it read -
        // the next reconcile must not load it over the navigated document.
        const auto entry = std::ranges::find_if(
            owner.frames_, [&](const frame_entry & e) { return e.element == plan.element; });
        if (entry != owner.frames_.end()) {
            entry->bindings = made;
        } else {
            std::string src;
            {
                const auto txn = owner.doc_->read();
                src = owner.frame_source(txn, plan.element);
            }
            owner.frames_.push_back(frame_entry{plan.element, std::move(src), made});
        }
    }
}

namespace {

// "Tokenize the features argument" (HTML 7.2.2.1): name/value pairs split on
// the feature separators - ASCII whitespace, `=` and `,` - both lowercased.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> tokenize_features(
    std::string_view features) {
    const auto separator = [](char c) {
        return c == '=' || c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
    };
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t at = 0;
    const auto collect = [&](bool separators) {
        const std::size_t from = at;
        while (at < features.size() && separator(features[at]) == separators) { ++at; }
        return ascii_lower_copy(features.substr(from, at - from));
    };
    while (at < features.size()) {
        (void)collect(true);
        std::string name = collect(false);
        std::string value;
        while (at < features.size() && features[at] != '=') {
            if (features[at] == ',' || !separator(features[at])) { break; }
            ++at;
        }
        if (at < features.size() && separator(features[at])) {
            while (at < features.size() && separator(features[at])) {
                if (features[at] == ',') { break; }
                ++at;
            }
            value = collect(false);
        }
        if (!name.empty()) { out.emplace_back(std::move(name), std::move(value)); }
    }
    return out;
}

// "Check if a window feature is set" as a boolean: "" `yes` and `true` are
// true, and otherwise the value's integer is - 0 when it does not parse.
[[nodiscard]] bool boolean_feature(const std::vector<std::pair<std::string, std::string>> & list,
                                   std::string_view name) {
    for (const auto & [key, value] : list) {
        if (key != name) { continue; }
        if (value.empty() || value == "yes" || value == "true") { return true; }
        long long parsed = 0;
        return detail::parse_html_integer(value, parsed) && parsed != 0;
    }
    return false;
}

} // namespace

// `window.open()`, HTML 7.2.2.1 "the window open steps", on the window of
// THIS document: the URL encoding-parsed against the entry document's base
// URL, a failure a SyntaxError; the target chosen by the rules for choosing a
// navigable; an existing <iframe> navigated (navigate_frame) and its
// WindowProxy returned unless `noopener`. A NEW top-level traversable - the
// `_blank` default, or a name no navigable has - is a popup, and this user
// agent's popup blocker is always on: null, as a blocked popup is.
// ponytail: `_self`, `_parent` or `_top` reaching the embedder's page answers
// its WindowProxy without navigating it - top-level navigation is the
// embedder's (browser::set_navigate_hook) and a script cannot ask for it yet.
void dom_bindings::install_window_open(context & cx, script::object_object & window) {
    const value open = native(cx, "open", [this](context & c, std::span<value> a) -> value {
        const std::string url =
            a.empty() || a[0].is_undefined() ? std::string{} : to_usv_string(c.to_string(a[0]));
        std::string target =
            a.size() < 2 || a[1].is_undefined() ? std::string{"_blank"} : c.to_string(a[1]);
        const std::string features =
            a.size() < 3 || a[2].is_undefined() ? std::string{} : c.to_string(a[2]);
        if (c.throw_pending()) { return value::undefined(); }
        // The ENTRY document: the realm the calling script runs in.
        dom_bindings & entry = bindings_of(c.global("window"));
        form_navigation navigation;
        if (!url.empty()) {
            const std::string fallback = entry.fallback_base_url();
            const std::string base =
                detail::document_base_url(entry.doc_->read(), *entry.atoms_, fallback);
            const std::optional<url_record> parsed =
                base.empty() ? parse_url(url) : parse_url(url, base);
            if (!parsed) {
                throw_dom_exception(c, "SyntaxError",
                                    "Failed to execute 'open' on 'Window': Unable to open a "
                                    "window with invalid URL '" +
                                        url + "'.");
                return value::undefined();
            }
            navigation.request.url = parsed->serialize();
            navigation.src = navigation.request.url;
        }
        if (target.empty()) { target = "_blank"; }
        const auto tokens = tokenize_features(features);
        const bool noopener =
            boolean_feature(tokens, "noopener") || boolean_feature(tokens, "noreferrer");
        const auto [navigable, element] = choose_navigable(target);
        if (navigable == nullptr) {
            const std::string keyword = ascii_lower_copy(target);
            if (noopener || (!keyword.empty() && keyword != "_self" && keyword != "_parent" &&
                             keyword != "_top")) {
                return value::null();
            }
            const dom_bindings & top = primary_ == nullptr ? *this : *primary_;
            return top.window_view_.is_heap() ? top.window_view_ : c.global("window");
        }
        if (!navigation.request.url.empty()) {
            navigation.target = target;
            navigate_frame(*navigable, element, std::move(navigation));
        }
        if (noopener) { return value::null(); }
        return c.lookup_property(navigable->wrap(c, element), "contentWindow");
    });
    auto * made = static_cast<script::native_object *>(open.as_heap());
    made->define("length", value::number(0), script::attr_configurable);
    window.set("open", open);
    // The window is the global object, so the bare name is the same method.
    cx.define_global("open", open);
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
    // AN INSTANCE OF Window, as the page's window is (element/interfaces.cpp),
    // and of the shared intrinsics' interfaces: its location a Location, its
    // six BarProps its own.
    frame_window->prototype = cx.lookup_property(cx.global("Window"), "prototype");
    if (location_.is_object()) {
        static_cast<script::object_object *>(location_.as_heap())->prototype =
            cx.lookup_property(cx.global("Location"), "prototype");
    }
    install_bar_props(cx, *frame_window, cx.lookup_property(cx.global("BarProp"), "prototype"));
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
    define_location_property(cx, *frame_window); // [PutForwards=href], entry.cpp
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
    frame_window->define_accessor("length",
                                  value::object(cx.allocate<script::native_object>(
                                      "length",
                                      [this](context &, std::span<value>) {
                                          return value::number(
                                              static_cast<double>(child_navigables().size()));
                                      })),
                                  value::undefined());
    if (auto * doc = document_object()) {
        define_location_property(cx, *doc);
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
    install_history(cx, *frame_window); // the frame's own session history
    install_dom_parser(cx);             // a DOMParser whose documents are the frame's
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
        cx.define_builtin_global(name, value::object(ctor));
    }
    // Listening at THIS window: a frame script's `addEventListener("load", f)`
    // hears the frame's load, not the page's.
    install_window_listeners(cx, *frame_window);
    install_post_message(cx, *frame_window);
    install_window_open(cx, *frame_window);
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
        cx.define_builtin_global(name, value::object(wrapper));
    }
}

dom_bindings & dom_bindings::bindings_of(value window) {
    dom_bindings * found = find_bindings_of(window);
    return found != nullptr ? *found : primary();
}

// THE DOCUMENT-TREE CHILD NAVIGABLES in tree order (HTML 7.3.1.1) - what
// `window.length` counts and `window[i]` indexes: each connected <iframe>
// whose navigable exists. Reconciled first, so an <iframe src> inserted this
// turn has one; a later <iframe> of the same insertion, whose insertion steps
// have not run while an earlier one's `load` does, has none yet
// (insertion-removing-steps-iframe.window.js).
std::vector<node_id> dom_bindings::child_navigables() {
    reconcile_frames();
    std::vector<node_id> out;
    for (const node_id id : all_html_elements("iframe")) {
        if (std::ranges::any_of(frames_, [id](const frame_entry & entry) {
                return entry.element == id && entry.bindings != nullptr;
            })) {
            out.push_back(id);
        }
    }
    return out;
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
    if (frame_owner_ != nullptr && location_href_ == "about:srcdoc") {
        return frame_owner_->subresource_name(src);
    }
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
