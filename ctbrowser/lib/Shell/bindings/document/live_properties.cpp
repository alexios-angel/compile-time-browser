//===- live_properties.cpp - createElementNS name checks --------*- C++ -*-===//
//
// dom_bindings::validate_and_extract_element, the name and namespace checks
// behind createElementNS.
//
//===----------------------------------------------------------------------===//
#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

/// DOM "validate and extract" for an element, in the order the DOM puts the
/// two halves: the shape of the name is decided before the namespace is looked
/// at, so `createElementNS(XMLNS_NS, "1foo")` is an InvalidCharacterError and
/// not the NamespaceError its namespace would otherwise earn. Both orderings
/// throw; only one throws what WPT asserts.
///
/// A prefix is checked for being writable and non-empty and nothing else:
/// `createElementNS(ns, "0:a")` is legal and `"a:0"` is not, because it is the
/// local name that has to be a name and the prefix is only a label in front of
/// it. Returns false after throwing the DOMException into `cx`.
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

} // namespace ctbrowser::shell
