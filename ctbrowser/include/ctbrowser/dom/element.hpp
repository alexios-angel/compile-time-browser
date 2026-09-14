#pragma once

#include <ctbrowser/dom/document.hpp>

#include <optional>
#include <string>

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

// Copy the first qualified-name match, preserving empty values and returning
// nullopt when absent. Reads do not validate attribute names; native element
// parameters must first pass validate_element.
[[nodiscard]] std::optional<std::string> get_element_attribute(document & doc, node_id id,
                                                               std::string_view name);

// Validate the name, then update the first attribute with that qualified name
// through document::set_attribute. DOM errors and same-value write records
// are preserved. Native element parameters must first pass validate_element.
[[nodiscard]] std::expected<void, dom_error> set_element_attribute(document & doc, node_id id,
                                                                   std::string_view name,
                                                                   std::string_view text);

struct attribute_toggle_result {
    // The requested state, even when another namespaced match remains.
    bool present;
    // False for a forced no-op, true for a successful attempted change.
    // A failed write retains the requested presence for VM adapters.
    std::expected<bool, dom_error> update;
};

// Toggle the first attribute with this qualified name, preserving its bytes
// on a forced no-op. Name validation precedes all DOM reads and writes;
// native callers validate their element and check both result and update.
[[nodiscard]] std::expected<attribute_toggle_result, dom_error> toggle_element_attribute(
    document & doc, node_id id, std::string_view name, std::optional<bool> force = std::nullopt);

} // namespace ctbrowser
