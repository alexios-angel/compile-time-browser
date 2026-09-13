#pragma once

#include <ctbrowser/dom/document.hpp>

namespace ctbrowser {

// A borrowed element: equal node IDs in different documents are different
// elements. The document must outlive every copy; detaching a node does not
// destroy it. This pair owns neither the document nor the node.
struct element_ref {
    document * owner = nullptr;
    node_id id{};

    [[nodiscard]] friend bool operator==(element_ref, element_ref) noexcept = default;
};

// Check incoming native handles before use. A non-null owner must still name
// a live document; generation-tagged IDs reject stale nodes within it.
[[nodiscard]] std::expected<void, dom_error> validate_element(element_ref element);

// The same attribute-name rule used by the DOM bindings, including their
// InvalidCharacterError validation before value conversion or mutation.
[[nodiscard]] bool is_valid_attribute_name(std::string_view name);

// Qualified names fold only on HTML elements in HTML documents. Namespaced
// attribute APIs deliberately do not use this helper.
[[nodiscard]] atom attribute_key(document & doc, node_id id, std::string_view qualified);

// Validate the name, then update the first attribute with that qualified name
// through document::set_attribute. DOM errors and same-value write records
// are preserved. Native element parameters must first pass validate_element.
[[nodiscard]] std::expected<void, dom_error> set_element_attribute(document & doc, node_id id,
                                                                   std::string_view name,
                                                                   std::string_view text);

} // namespace ctbrowser
