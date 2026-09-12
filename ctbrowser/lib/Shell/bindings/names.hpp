#pragma once
// Private to lib/Shell/bindings/ - not installed. The name rules the document/
// and element/ files BOTH apply: a qualified name's two halves, a valid
// attribute name and a valid namespace prefix. One copy, because createAttribute
// and setAttribute answering the same question differently was a bug the
// corpus found (18a38dd7 changed the rule in two places). The namespace URIs
// they compare against are dom/xml.hpp's.

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

// WHAT AN ATTRIBUTE MAY BE CALLED - and it is NOT the XML `Name` production.
//
// `dom/nodes/productions.js` is the whole of the evidence and it is blunt:
//
//     var invalid_names = [""]
//     var valid_names = ["x", "X", ":", "a:0", "invalid^Name", "\\", "'",
//                        '"', "0", "0:a", ":a", "x:y:x", "~"]
//
// Thirteen names, every one of which the XML `Name` production refuses, and
// every one of which `Document-createAttribute.html` and `attributes.html`
// require to SUCCEED. Only the empty string throws. That is not an oversight
// in the corpus: an attribute name is measured by whether it survives being
// written into a start tag and read back, and the HTML tokenizer's attribute
// name state ends the name on whitespace, `/`, `>` and `=` and on nothing
// else. `"` and `'` inside one are a parse error the tokenizer explicitly
// recovers from BY INCLUDING THE CHARACTER, so they round-trip; `~` and `^`
// are not special at all. There is NO first-character rule: `"0"` and `":a"`
// are legal attribute names and illegal element names (document/internal.hpp
// has the element rule), which is exactly the pair productions.js draws.
//
// BYTE-WISE ON PURPOSE, and exact rather than approximate: every character the
// rule names is ASCII, and no byte of a multi-byte UTF-8 sequence is. So no
// decoder, and no dependence on how the VM happens to store a string.
inline constexpr std::string_view attribute_name_breaks = "\t\n\f\r /=>";

[[nodiscard]] inline bool is_valid_attribute_name(std::string_view name) {
    // U+0000 is the one character the tokenizer cannot carry: it becomes
    // U+FFFD, so a name containing one does not read back as itself.
    return !name.empty() && name.find_first_of(attribute_name_breaks) == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
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
