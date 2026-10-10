//===- second_document.cpp - Second Documents -------------------*- C++ -*-===//
//
// dom_bindings for a second Document in the page's realm:
// `document.implementation.createHTMLDocument()` and `createDocument()`,
// `new Document()`, and DOMParser's `parseFromString`.
//
// A second Document is a second `dom_bindings` over its own tree, in the
// same realm. The per-document maps (`wrappers_`, `namespaces_`,
// `webgl_objects_`) are members, so each instance has its own and two nodes
// with the same slot never collide, and `doc_` stays a member because it is
// the document these bindings serve.
//
// What the documents share is what a realm shares:
//   * the atom table, so a tag name interned in one means the same in the other;
//   * the script context, because both documents are in one realm;
//   * the interface prototypes, so `made.createElement("div") instanceof
//     HTMLDivElement` is true against the one HTMLDivElement a page can name.
//     `install_dom_interfaces` defines those globals, so running it a second
//     time would leave two of each and break every cross-document
//     `instanceof`; `adopt_interfaces_of` takes the primary's instead.
//
// A node of one document handed to another: `handle_of` answers only for
// this instance's own wrappers, so one document's element is never mistaken
// for the other's. `node_from` adopts across the boundary by cloning into
// this slab and rebinding the page's wrapper to the copy, and every insertion
// method that resolves its argument through `node_from` (appendChild,
// insertBefore and replaceChild do, via `insertable` in
// element/node_methods.cpp) therefore adopts.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

/// Shares \p primary's interface prototypes with this document, so wrappers
/// made here pass `instanceof` against the realm's one set of interfaces.
void dom_bindings::adopt_interfaces_of(const dom_bindings & primary) {
    interface_prototypes_ = primary.interface_prototypes_;
    event_target_prototype_ = primary.event_target_prototype_;
    event_prototype_ = primary.event_prototype_;
    custom_event_prototype_ = primary.custom_event_prototype_;
    // The prototypes click() and dispatch_mouse create and recognise. Without
    // them a made document's click() would build a plain Event, which
    // activation behaviour (dispatch.cpp) does not treat as a click.
    mouse_event_prototype_ = primary.mouse_event_prototype_;
    pointer_event_prototype_ = primary.pointer_event_prototype_;
    message_event_prototype_ = primary.message_event_prototype_;
    dom_exception_prototype_ = primary.dom_exception_prototype_;
    // The flag `ensure_dom_interfaces` reads. Without it the first `wrap()` on
    // this document would rebuild the whole table and redefine the globals.
    interfaces_linked_ = true;
}

/// Registers \p fresh as a secondary document of the primary and returns its
/// new bindings, already sharing the primary's interface prototypes.
dom_bindings & dom_bindings::adopt_second_document(context & cx, document & fresh) {
    // Every made document is registered with the primary, whichever document
    // made it: `owner_of` and `is_a_document` search one flat list, and a
    // doctype that `made.implementation.createDocument` adopted has to be
    // findable by `document.implementation.createDocument` afterwards.
    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
    auto & made = *top.secondary_documents_.emplace_back(
        std::make_unique<dom_bindings>(fresh, *atoms_, *canvases_, *forms_, std::function<void()>{},
                                       std::function<void(node_id)>{}));
    made.secondary_ = true;
    made.primary_ = &top;
    made.cx_ = &cx;
    // One resource registry for the page: a frame's own subresources (its
    // <script src>, a frame inside it) are fetched the same way the page's are.
    // Left null, a frame's external scripts and nested frames would load nothing.
    made.assets_ = top.assets_;
    made.watch_shadow_disabled();
    // Build the primary's interface table before adopting it: the table is
    // built lazily on the first wrap(), and adopting an empty one would set
    // `interfaces_linked_` and leave the made document unable to build its own
    // (so `instanceof HTMLDivElement` would be false).
    ensure_dom_interfaces(cx);
    made.adopt_interfaces_of(*this);
    return made;
}

// `createHTMLDocument(title)`, HTML 8.6: a doctype, an `html`, a `head`, a
// `title` only if the argument was given, and a `body`. The skeleton is built
// by running the engine's own parser over that markup, so a made document has
// the same tree a parsed one has. The title goes in through a Text node
// afterwards, so an argument containing `<` stays text rather than markup.
value dom_bindings::make_html_document(context & cx, const std::string * title) {
    document & fresh = *(primary_ == nullptr ? *this : *primary_)
                            .owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    (void)parse_html(fresh, "<!DOCTYPE html><html><head></head><body></body></html>", false);
    dom_bindings & made = adopt_second_document(cx, fresh);
    if (title != nullptr) {
        const node_id head = made.first_html_element("head");
        const node_id element = fresh.create_element(atoms_->intern_lower("title"));
        if (head && element) {
            (void)fresh.append_child(head, element);
            // A Text node even when empty: `createHTMLDocument("")` has a
            // title with one child whose data is "" (HTML 8.6 step 5).
            (void)fresh.append_child(element, fresh.create_text(*title));
        }
    }
    made.install_document(cx);
    return made.document_;
}

// `createDocument(namespace, qualifiedName, doctype)`, DOM 4.5.1: an XML
// document, so no html/head/body and no quirks. The one element is the
// document element when a qualified name was given; otherwise the document is
// empty (`createDocument(null, "")` really has no children, which WPT's
// Document-contentType and append-on-Document.html tests rely on).
//
// The doctype argument is handled by the caller in install.cpp: the
// DocumentType is adopted into the new document and put ahead of the element.
//
// Also `new Document()`, DOM 4.5: a document with no browsing context, no
// children, content type application/xml and URL about:blank, which is
// `createDocument(null, "")` under the Document interface rather than
// XMLDocument (`as_xml_document` false). Document-constructor.html asserts
// the difference.
value dom_bindings::make_xml_document(context & cx, std::string_view ns,
                                      std::string_view qualified_name, bool as_xml_document) {
    document & fresh = *(primary_ == nullptr ? *this : *primary_)
                            .owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    // Mark it as an XML document: that is what makes `nodeName` keep its case
    // and `compatMode` answer CSS1Compat. `createDocument` never parses
    // anything, so nothing else would set the flag.
    fresh.set_xml(true);
    fresh.set_quirks(false);
    dom_bindings & made = adopt_second_document(cx, fresh);
    // DOM 4.5.1's content type table: the namespace alone decides, so
    // `createDocument(null, "x")` is application/xml whatever `x` is called.
    // (WPT Document-contentType/contentType/createDocument.html.)
    made.content_type_ = ns == "http://www.w3.org/1999/xhtml" ? "application/xhtml+xml"
                         : ns == "http://www.w3.org/2000/svg" ? "image/svg+xml"
                                                              : "application/xml";
    if (!qualified_name.empty()) {
        const node_ns kind = ns == "http://www.w3.org/1999/xhtml" ? node_ns::html
                             : ns == "http://www.w3.org/2000/svg" ? node_ns::svg
                                                                  : node_ns::other;
        // Interned as written: an XML document is case-sensitive, so the
        // qualified name is the tag and folding it would lose the case the
        // page asked for.
        const node_id root =
            fresh.create_element(atoms_->intern(qualified_name), kind,
                                 qualified_name.find(':') != std::string_view::npos);
        fresh.set_document_element(root);
        if (kind == node_ns::other || ns.empty()) {
            made.namespaces_.emplace(root.key(), std::string{ns});
        }
    }
    made.install_document(cx);
    // After install_document, which linked it to Document.prototype.
    if (as_xml_document) {
        if (const value proto = interface_prototype("XMLDocument"); proto.is_object()) {
            made.document_object()->prototype = proto;
        }
    }
    return made.document_;
}

/// Returns the bindings (this one, the primary, or a secondary) whose wrapper
/// \p v is, or nullptr if \p v wraps no node of the realm's documents.
dom_bindings * dom_bindings::owner_of(value v) {
    if (handle_of(v)) { return this; }
    dom_bindings * top = primary_ == nullptr ? this : primary_;
    if (top != this && top->handle_of(v)) { return top; }
    for (const auto & made : top->secondary_documents_) {
        if (made.get() != this && made->handle_of(v)) { return made.get(); }
    }
    return nullptr;
}

// `parseFromString`, HTML 8.6.2. text/html runs the HTML parser with
// scripting disabled (nothing here executes a <script>), and the four XML
// types run the XML parser. Per the specification an ill-formed XML input is
// a document whose root is a <parsererror>; here it is the tree the parser
// had when it stopped plus that element, which is what pages check for.
value dom_bindings::parse_from_string(context & cx, std::string_view markup,
                                      std::string_view type) {
    document & fresh = *(primary_ == nullptr ? *this : *primary_)
                            .owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    if (type == "text/html") {
        // Scripting off: nothing will ever run a script in this document, so
        // 13.2.6.4.4's scripting flag is false and `<noscript>` holds parsed
        // elements rather than raw text (the last assertion of WPT
        // DOMParser-parseFromString-html.html).
        (void)parse_html(fresh, markup, false);
        dom_bindings & made = adopt_second_document(cx, fresh);
        made.install_document(cx);
        made.take_url_of(cx, *this);
        return made.document_;
    }
    const xml_parse_result read = parse_xml(fresh, markup);
    dom_bindings & made = adopt_second_document(cx, fresh);
    made.content_type_ = std::string{type};
    if (!read.error.empty()) {
        const node_id error = fresh.create_element(atoms_->intern("parsererror"), node_ns::other);
        (void)fresh.append_child(error, fresh.create_text(read.error));
        if (fresh.read().kind(fresh.root()).value_or(node_kind::document) == node_kind::element) {
            (void)fresh.append_child(fresh.root(), error);
        } else {
            fresh.set_document_element(error);
        }
        made.namespaces_.emplace(
            error.key(), std::string{"http://www.mozilla.org/newlayout/xml/parsererror.xml"});
    }
    made.install_document(cx);
    // A `Document`, not an XMLDocument, whatever the type (HTML 8.6.2 step
    // 1: "a new Document, whose content type is type"), and its URL is the
    // parsing document's (DOMParser-parseFromString-xml.html, -url.html).
    made.take_url_of(cx, *this);
    return made.document_;
}

// HTML 8.6.2's "URL is this's relevant global object's associated Document's
// URL": copy URL, documentURI and baseURI from the document that made this one.
void dom_bindings::take_url_of(context & cx, const dom_bindings & maker) {
    if (auto * doc = document_object()) {
        for (const char * name : {"URL", "documentURI", "baseURI"}) {
            doc->set(name, cx.string(maker.location_href_));
        }
    }
    location_href_ = maker.location_href_;
}

/// `compareDocumentPosition` between nodes of different documents: 0 when
/// \p given belongs to this document (or none), otherwise DISCONNECTED |
/// IMPLEMENTATION_SPECIFIC plus PRECEDING or FOLLOWING, ordered consistently
/// by the address of each document's bindings as DOM 4.4 allows.
unsigned dom_bindings::foreign_document_position(value given) {
    dom_bindings * owner = owner_of(given);
    if (owner == nullptr) {
        dom_bindings * top = primary_ == nullptr ? this : primary_;
        if (top->is_the_document(given)) { owner = top; }
        for (const auto & made : top->secondary_documents_) {
            if (made->is_the_document(given)) { owner = made.get(); }
        }
    }
    if (owner == nullptr || owner == this) { return 0; }
    constexpr unsigned disconnected = 0x01, preceding = 0x02, following = 0x04,
                       implementation_specific = 0x20;
    return disconnected | implementation_specific |
           (std::less<const dom_bindings *>{}(owner, this) ? preceding : following);
}

/// True if \p v is the Document object of any of the realm's documents.
bool dom_bindings::is_a_document(value v) const {
    if (is_the_document(v)) { return true; }
    const dom_bindings * top = primary_ == nullptr ? this : primary_;
    if (top->is_the_document(v)) { return true; }
    for (const auto & made : top->secondary_documents_) {
        if (made->is_the_document(v)) { return true; }
    }
    return false;
}

} // namespace ctbrowser::shell
