//===- element.hpp - Element attribute helpers ---------------- -*- C++ -*-===//
//
// Element references and the qualified-name attribute operations behind the DOM
// bindings' getAttribute, setAttribute and toggleAttribute.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser/dom/document.hpp>

#include <optional>
#include <string>

namespace ctbrowser {

/// A borrowed element: a document and a node in it. Equal node IDs in
/// different documents are different elements. Owns neither; the document must
/// outlive every copy (detaching a node does not destroy it).
struct element_ref {
    document * owner = nullptr;
    node_id id{};

    [[nodiscard]] friend bool operator==(element_ref, element_ref) noexcept = default;
};

/// Checks an element reference from a native caller before use: a non-null
/// owner must name a live document, and the generation-tagged ID rejects a
/// stale node within it.
[[nodiscard]] std::expected<void, dom_error> validate_element(element_ref element);

/// The attribute-name rule the DOM bindings use: false means they throw
/// InvalidCharacterError before converting the value or mutating anything.
[[nodiscard]] bool is_valid_attribute_name(std::string_view name);

/// The atom for qualified attribute name `qualified` on element `id`:
/// ASCII-lowercased only on HTML elements in HTML documents. Namespaced
/// attribute APIs deliberately do not use this.
[[nodiscard]] atom attribute_key(document & doc, node_id id, std::string_view qualified);

/// getAttribute: a copy of the first attribute with qualified name `name`, ""
/// for an empty value, or nullopt when absent. Does not validate the name;
/// native element parameters must first pass validate_element.
[[nodiscard]] std::optional<std::string> get_element_attribute(document & doc, node_id id,
                                                               std::string_view name);

/// setAttribute: validates the name, then sets the first attribute with that
/// qualified name through document::set_attribute (so a same-value write is
/// still logged). Native element parameters must first pass validate_element.
[[nodiscard]] std::expected<void, dom_error> set_element_attribute(document & doc, node_id id,
                                                                   std::string_view name,
                                                                   std::string_view text);

/// toggleAttribute's result.
struct attribute_toggle_result {
    /// The requested state, even when another namespaced match remains.
    bool present;
    /// False for a forced no-op, true for a successful change. A failed write
    /// still reports `present`, so the bindings can report the error.
    std::expected<bool, dom_error> update;
};

/// toggleAttribute: toggles the first attribute with qualified name `name`,
/// leaving its bytes unchanged on a forced no-op. Name validation happens
/// before any DOM read or write; native callers validate their element and
/// check both the result and its `update`.
[[nodiscard]] std::expected<attribute_toggle_result, dom_error> toggle_element_attribute(
    document & doc, node_id id, std::string_view name, std::optional<bool> force = std::nullopt);

} // namespace ctbrowser
