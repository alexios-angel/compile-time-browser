#pragma once
// Private qualified-name helpers. Attribute-name validation is shared with
// native callers in dom/element.hpp; namespace URIs live in dom/xml.hpp.

#include <ctbrowser/dom/element.hpp>

#include <cstddef>
#include <string_view>

namespace ctbrowser::shell::detail {

// The prefix and the local part of a qualified name, split at the FIRST colon.
// `a:b:c` is prefix `a` and local `b:c`, which is what the DOM says and is not
// what the XML QName production says - the two disagree and the DOM is what a
// page is measured against.
struct qualified_name {
    std::string_view prefix; // empty when there is no colon
    std::string_view local;
    bool has_colon = false;
};

[[nodiscard]] inline qualified_name split_qualified(std::string_view name) {
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) { return qualified_name{{}, name, false}; }
    return qualified_name{name.substr(0, colon), name.substr(colon + 1), true};
}

// A "valid namespace prefix" (DOM 4.9, whatwg/dom#1079): not empty, no ASCII
// whitespace, U+0000, `/` or `>`. Looser than an attribute name - `=` is
// allowed, so `setAttributeNS(ns, "=:attr", v)` is legal
// (dom/nodes/name-validation.html) - and with no first-character rule.
[[nodiscard]] inline bool is_valid_namespace_prefix(std::string_view prefix) {
    return !prefix.empty() && prefix.find_first_of("\t\n\f\r />") == std::string_view::npos &&
           prefix.find('\0') == std::string_view::npos;
}

} // namespace ctbrowser::shell::detail
