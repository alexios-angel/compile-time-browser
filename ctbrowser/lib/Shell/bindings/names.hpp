//===- names.hpp - Node constants and qualified names -----------*- C++ -*-===//
//
// Private qualified-name helpers for the DOM bindings, and the Node
// interface's constants. Attribute-name validation is shared with native
// callers in dom/element.hpp; namespace URIs live in dom/xml.hpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/dom/element.hpp>

#include <cstddef>
#include <string_view>
#include <utility>

namespace ctbrowser::shell::detail {

/// The Node interface's constants (DOM 4.4) in IDL order: the node types, then
/// the DOCUMENT_POSITION_ bits. Node.prototype, Node and the document all
/// carry them.
inline constexpr std::pair<const char *, double> node_constants[] = {
    {"ELEMENT_NODE", 1},
    {"ATTRIBUTE_NODE", 2},
    {"TEXT_NODE", 3},
    {"CDATA_SECTION_NODE", 4},
    {"ENTITY_REFERENCE_NODE", 5},
    {"ENTITY_NODE", 6},
    {"PROCESSING_INSTRUCTION_NODE", 7},
    {"COMMENT_NODE", 8},
    {"DOCUMENT_NODE", 9},
    {"DOCUMENT_TYPE_NODE", 10},
    {"DOCUMENT_FRAGMENT_NODE", 11},
    {"NOTATION_NODE", 12},
    {"DOCUMENT_POSITION_DISCONNECTED", 0x01},
    {"DOCUMENT_POSITION_PRECEDING", 0x02},
    {"DOCUMENT_POSITION_FOLLOWING", 0x04},
    {"DOCUMENT_POSITION_CONTAINS", 0x08},
    {"DOCUMENT_POSITION_CONTAINED_BY", 0x10},
    {"DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC", 0x20},
};

/// The prefix and the local part of a qualified name, split at the first colon.
/// `a:b:c` is prefix `a` and local `b:c`. That is the DOM's rule, not the XML
/// QName production's; the two disagree, and a page is measured against the
/// DOM.
struct qualified_name {
    std::string_view prefix; ///< Empty when there is no colon.
    std::string_view local;
    bool has_colon = false;
};

/// Splits `name` at its first colon (see qualified_name).
[[nodiscard]] inline qualified_name split_qualified(std::string_view name) {
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) { return qualified_name{{}, name, false}; }
    return qualified_name{name.substr(0, colon), name.substr(colon + 1), true};
}

/// Whether `prefix` is a "valid namespace prefix" (DOM 4.9, whatwg/dom#1079):
/// not empty, with no ASCII whitespace, U+0000, `/` or `>`. It is looser than
/// an attribute name: `=` is allowed, so `setAttributeNS(ns, "=:attr", v)` is
/// legal (dom/nodes/name-validation.html), and there is no first-character
/// rule.
[[nodiscard]] inline bool is_valid_namespace_prefix(std::string_view prefix) {
    return !prefix.empty() && prefix.find_first_of("\t\n\f\r />") == std::string_view::npos &&
           prefix.find('\0') == std::string_view::npos;
}

} // namespace ctbrowser::shell::detail
