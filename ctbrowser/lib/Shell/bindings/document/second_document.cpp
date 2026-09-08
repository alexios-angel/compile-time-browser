// dom_bindings - a second Document: createHTMLDocument and createDocument.
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
// `namespaces_`, `mirrors_`, `webgl_objects_` - is already a member, so a
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
// WHAT IS NOT DONE, said plainly rather than left to be discovered. `importNode`
// and `adoptNode` still do not cross between two documents. A node of one
// handed to the other is REFUSED - `handle_of` checks that the wrapper it was
// given is in THIS instance's table - so the failure is a method that does
// nothing rather than `getElementById` on one document silently returning the
// other's element, which is the outcome the old note was avoiding.
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
value dom_bindings::make_html_document(context & cx, const std::string * title) {
    document & fresh = *owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    (void)parse_html(fresh, "<!DOCTYPE html><html><head></head><body></body></html>");
    auto & made = *secondary_documents_.emplace_back(
        std::make_unique<dom_bindings>(fresh, *atoms_, *canvases_, *forms_, std::function<void()>{},
                                       std::function<void(node_id)>{}));
    made.secondary_ = true;
    made.cx_ = &cx;
    // BEFORE the adoption, and this is not belt and braces. The primary builds
    // its interface table lazily, on the first `wrap()` - so a page whose very
    // first statement is `createHTMLDocument(...).createElement("div")` adopted
    // an EMPTY table and set `interfaces_linked_`, and the made document could
    // then never build one. `instanceof HTMLDivElement` was false for exactly
    // that page and true for one that had touched an element first.
    ensure_dom_interfaces(cx);
    made.adopt_interfaces_of(*this);
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
// THE DOCTYPE ARGUMENT IS ACCEPTED AND DROPPED. This tree has no DocumentType
// node - `document.doctype` is null and `compatMode` is read off a flag - so
// storing one would mean inventing a node kind for it. What that costs is one
// subtest per file rather than the file.
value dom_bindings::make_xml_document(context & cx, std::string_view ns,
                                      std::string_view qualified_name) {
    document & fresh = *owned_documents_.emplace_back(std::make_unique<document>(*atoms_));
    // IT IS AN XML DOCUMENT, and saying so is what makes `nodeName` keep its
    // case and `compatMode` answer CSS1Compat. `createDocument` never parses
    // anything, so nothing else would have set the flag.
    fresh.set_xml(true);
    fresh.set_quirks(false);
    auto & made = *secondary_documents_.emplace_back(
        std::make_unique<dom_bindings>(fresh, *atoms_, *canvases_, *forms_, std::function<void()>{},
                                       std::function<void(node_id)>{}));
    made.secondary_ = true;
    made.cx_ = &cx;
    ensure_dom_interfaces(cx); // see make_html_document
    made.adopt_interfaces_of(*this);
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
        const node_id root = fresh.create_element(atoms_->intern(qualified_name), kind);
        auto builder = fresh.build();
        builder.set_root(root);
        if (kind == node_ns::other || ns.empty()) {
            made.namespaces_.emplace(made.pack(root), std::string{ns});
        }
    }
    made.install_document(cx);
    return made.document_;
}

} // namespace ctbrowser::shell
