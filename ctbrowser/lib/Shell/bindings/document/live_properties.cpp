// Split from install.cpp: live properties.
#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// The document's own live properties. `body` and `documentElement` can be set
// once because the node never changes; these cannot - a title is rewritten by
// script and the focused element changes on every click - so they are pushed
// again whenever the wrappers are, exactly as location.href is.
// VALIDATE AND EXTRACT, in the order the DOM puts the two halves: the shape
// of the name is decided BEFORE the namespace is looked at, so
// `createElementNS(XMLNS_NS, "1foo")` is an InvalidCharacterError and not the
// NamespaceError its namespace would otherwise earn. Both orderings throw;
// only one of them throws what the suite asserts.
//
// A prefix is checked for being writable and non-empty and NOTHING ELSE -
// `createElementNS(ns, "0:a")` is legal and `"a:0"` is not, because it is the
// LOCAL name that has to be a name and the prefix is only ever a label in
// front of it.
bool dom_bindings::validate_and_extract_element(context & cx, std::string_view where,
                                                const std::string & ns,
                                                const std::string & qualified) {
    const qualified_name split = split_qualified(qualified);
    const bool prefixed = split.has_colon;
    if ((prefixed && !is_valid_namespace_prefix(split.prefix)) ||
        !is_valid_element_local_name(split.local)) {
        throw_dom_exception(cx, "InvalidCharacterError",
                            std::string{where} + ": '" + qualified + "' is not a qualified name");
        return false;
    }
    const auto fail = [&](const std::string & why) {
        throw_dom_exception(cx, "NamespaceError", std::string{where} + ": " + why);
        return false;
    };
    if (prefixed && ns.empty()) { return fail("a prefix needs a namespace"); }
    if (split.prefix == "xml" && ns != xml_namespace) {
        return fail("the xml prefix belongs to the XML namespace");
    }
    if ((qualified == "xmlns" || split.prefix == "xmlns") && ns != xmlns_namespace) {
        return fail("xmlns belongs to the XMLNS namespace");
    }
    if (ns == xmlns_namespace && qualified != "xmlns" && split.prefix != "xmlns") {
        return fail("the XMLNS namespace is only for xmlns");
    }
    return true;
}

void dom_bindings::refresh_document() {
    auto * doc = document_object();
    if (doc == nullptr || cx_ == nullptr) { return; }
    // `title` is NOT here any more - it is an accessor, installed once. A data
    // property refreshed on the tick answered a read taken in the same
    // statement as the write with the value from before it, which is the shape
    // of nearly every test in html/dom's title group: set it, read it back.
    // HTML 6.6.2: when nothing in the document is focused the answer is the
    // body element, and null only while there is no body (a page reads
    // `document.activeElement === document.body` after blur()).
    doc->set("activeElement", wrap(*cx_, focused_ ? focused_ : body_element()));
}

} // namespace ctbrowser::shell
