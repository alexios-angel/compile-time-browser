// dom_bindings - a second Document: createHTMLDocument and createDocument.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ============================================================================
// A SECOND DOCUMENT
// ============================================================================
//
// `document.implementation.createHTMLDocument()` and `createDocument()`, and
// the design decision behind them. The note that used to stand where they are
// installed said what a second Document would cost - a document handle beside
// the node handle in every key, `doc_` becoming an argument rather than a
// member, across ~90 uses in six files - and refused on that basis. It was
// right about the cost of THAT design.
//
// THIS IS A DIFFERENT ONE: a second Document is a second `dom_bindings` over
// its own tree, in the same realm. Every key the note listed - `wrappers_`,
// `namespaces_`, `webgl_objects_` - is already a member, so a
// second instance simply has a second set of them and two nodes with the same
// slot cannot collide. `doc_` stays a member because it is now true: it IS the
// document these bindings are about.
//
// WHAT IS SHARED is what a second document genuinely shares with the first:
//   * the ATOM TABLE, so a tag name interned in one means the same in the other;
//   * the script CONTEXT, because both documents are in one realm;
//   * the INTERFACE PROTOTYPES, so `made.createElement("div") instanceof
//     HTMLDivElement` is true against the ONE HTMLDivElement a page can name.
//     `install_dom_interfaces` DEFINES those ninety globals, so running it a
//     second time would leave two of each and break every cross-document
//     `instanceof` - `adopt_interfaces_of` takes the primary's instead.
//
// A NODE OF ONE DOCUMENT HANDED TO THE OTHER: `handle_of` answers only for this
// instance's own wrappers, so nothing can mistake one document's element for
// the other's - and `node_from` ADOPTS across the boundary, by cloning into
// this slab and rebinding the page's wrapper to the copy. Every insertion
// that resolves its argument through `node_from` therefore adopts; the ones
// that still go through `handle_of` alone (element/node_methods.cpp's
// appendChild, insertBefore and replaceChild) refuse with a TypeError.
void dom_bindings::adopt_interfaces_of(const dom_bindings & primary) {
    interface_prototypes_ = primary.interface_prototypes_;
    event_target_prototype_ = primary.event_target_prototype_;
    event_prototype_ = primary.event_prototype_;
    custom_event_prototype_ = primary.custom_event_prototype_;
    dom_exception_prototype_ = primary.dom_exception_prototype_;
    // The flag `ensure_dom_interfaces` reads. Without it the first `wrap()` on
    // this document would rebuild the whole table and redefine the globals.
    interfaces_linked_ = true;
}

// `createHTMLDocument(title)`, HTML 8.6: a doctype, an `html`, a `head`, a
// `title` ONLY IF the argument was given, and a `body`. Built by running the
// engine's own parser over that markup rather than by six `create_element`
// calls, so the tree a made document has is the tree a parsed one has - and
// the title goes in through `set_text` afterwards, which is how an argument
// containing `<` stays a text node rather than becoming markup.
dom_bindings & dom_bindings::adopt_second_document(context & cx, document & fresh) {
    auto & made = *secondary_documents_.emplace_back(
        std::make_unique<dom_bindings>(fresh, *atoms_, *canvases_, *forms_, std::function<void()>{},
                                       std::function<void(node_id)>{}));
    made.secondary_ = true;
    made.primary_ = this;
    made.cx_ = &cx;
    // BEFORE the adoption, and this is not belt and braces. The primary builds
    // its interface table lazily, on the first `wrap()` - so a page whose very
    // first statement is `createHTMLDocument(...).createElement("div")` adopted
    // an EMPTY table and set `interfaces_linked_`, and the made document could
    // then never build one. `instanceof HTMLDivElement` was false for exactly
    // that page and true for one that had touched an element first.
    ensure_dom_interfaces(cx);
    made.adopt_interfaces_of(*this);
    return made;
}

value dom_bindings::make_html_document(context & cx, const std::string * title) {
    document & fresh = *owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    (void)parse_html(fresh, "<!DOCTYPE html><html><head></head><body></body></html>");
    dom_bindings & made = adopt_second_document(cx, fresh);
    if (title != nullptr) {
        const node_id head = made.first_html_element("head");
        const node_id element = fresh.create_element(atoms_->intern_lower("title"));
        if (head && element) {
            (void)fresh.append_child(head, element);
            made.set_text(element, *title);
        }
    }
    made.install_document(cx);
    return made.document_;
}

// `createDocument(namespace, qualifiedName, doctype)`, DOM 4.5.1 - an XML
// document, so NO html/head/body and no quirks. The one element is the document
// element when a qualified name was given, and an empty document otherwise;
// `createDocument(null, "")` really does produce a Document with no children,
// which `Document-contentType` and `append-on-Document.html` both use.
//
// THE DOCTYPE ARGUMENT is handled by the caller in install.cpp: the
// DocumentType is adopted into the new document and put ahead of the element.
//
// ALSO `new Document()`, DOM 4.5: a document with no browsing context, no
// children, content type application/xml and URL about:blank - which is
// `createDocument(null, "")` under the Document interface rather than
// XMLDocument. `Document-constructor.html` asserts the difference, and
// dom/common.js opens with one.
value dom_bindings::make_xml_document(context & cx, std::string_view ns,
                                      std::string_view qualified_name, bool as_xml_document) {
    document & fresh = *owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    // IT IS AN XML DOCUMENT, and saying so is what makes `nodeName` keep its
    // case and `compatMode` answer CSS1Compat. `createDocument` never parses
    // anything, so nothing else would have set the flag.
    fresh.set_xml(true);
    fresh.set_quirks(false);
    dom_bindings & made = adopt_second_document(cx, fresh);
    // DOM 4.5.1's own table, and it is the NAMESPACE that decides rather than
    // anything about the tree: `createDocument(null, "x")` is application/xml
    // whatever `x` is called. `Document-contentType/contentType/
    // createDocument.html` walks all three rows.
    made.content_type_ = ns == "http://www.w3.org/1999/xhtml" ? "application/xhtml+xml"
                         : ns == "http://www.w3.org/2000/svg" ? "image/svg+xml"
                                                              : "application/xml";
    if (!qualified_name.empty()) {
        const node_ns kind = ns == "http://www.w3.org/1999/xhtml" ? node_ns::html
                             : ns == "http://www.w3.org/2000/svg" ? node_ns::svg
                                                                  : node_ns::other;
        // INTERNED AS WRITTEN: an XML document is case-sensitive, so the
        // qualified name is the tag and folding it would lose the case the
        // page asked for.
        const node_id root =
            fresh.create_element(atoms_->intern(qualified_name), kind,
                                 qualified_name.find(':') != std::string_view::npos);
        auto builder = fresh.build();
        builder.set_root(root);
        if (kind == node_ns::other || ns.empty()) {
            made.namespaces_.emplace(made.pack(root), std::string{ns});
        }
    }
    made.install_document(cx);
    // AFTER install_document, which linked it to Document.prototype.
    if (as_xml_document) {
        if (const value proto = interface_prototype("XMLDocument"); proto.is_object()) {
            made.document_object()->prototype = proto;
        }
    }
    return made.document_;
}

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
// scripting disabled - nothing here executes a <script> - and the four XML
// types run the XML parser; an ill-formed one is, per the specification, a
// document whose root is a <parsererror>, and here it is the tree the parser
// had when it stopped plus that element, which is what a page checks for.
value dom_bindings::parse_from_string(context & cx, std::string_view markup,
                                      std::string_view type) {
    document & fresh = *owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    if (type == "text/html") {
        (void)parse_html(fresh, markup);
        dom_bindings & made = adopt_second_document(cx, fresh);
        made.install_document(cx);
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
            fresh.build().set_root(error);
        }
        made.namespaces_.emplace(
            made.pack(error), std::string{"http://www.mozilla.org/newlayout/xml/parsererror.xml"});
    }
    made.install_document(cx);
    if (const value proto = interface_prototype("XMLDocument"); proto.is_object()) {
        made.document_object()->prototype = proto;
    }
    return made.document_;
}

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
