#pragma once

#include <ctbrowser/dom/document.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser {

// Convert a null-namespace attribute name to its supported dataset key.
// False excludes non-data names and ASCII uppercase letters. The output is
// unspecified on failure; reads must use supported keys, not reverse mapping.
[[nodiscard]] bool dataset_name_of(std::string_view attribute_name, std::string & out);

enum class dataset_fault : std::uint8_t {
    none,
    syntax,
    character
};

// Validate a dataset write's key before converting its value. On success,
// pass out to document::set_attribute_ns with the empty namespace. Syntax
// faults precede attribute-name faults; on a character fault out retains the
// invalid attribute name for diagnostics. Removal uses remove_attribute_ns.
[[nodiscard]] dataset_fault dataset_attribute_of(std::string_view key, std::string & out);

// Owning copies of the document's current null-namespace data-* values.
// Entries preserve attribute order. Missing keys differ from present empty
// strings; invalid IDs read as empty, like read_txn::attributes.
// Native element inputs must first pass validate_element from element.hpp.
[[nodiscard]] std::optional<std::string> dataset_value(document & doc, node_id element,
                                                       std::string_view key);
[[nodiscard]] std::vector<std::pair<std::string, std::string>> dataset_entries(document & doc,
                                                                               node_id element);

} // namespace ctbrowser
