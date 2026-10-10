//===- dataset.hpp - HTMLElement.dataset helpers -------------- -*- C++ -*-===//
//
// Conversions between data-* attribute names and `element.dataset` keys, and
// dataset reads.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser/dom/document.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser {

/// Converts a null-namespace attribute name such as `data-foo-bar` to its
/// dataset key (`fooBar`). Returns false when the name lacks the `data-`
/// prefix or contains an ASCII uppercase letter; `out` is then unspecified.
/// Lookups must compare these keys, not map a key back to an attribute,
/// because the two directions are not inverses.
[[nodiscard]] bool dataset_name_of(std::string_view attribute_name, std::string & out);

/// Why dataset_attribute_of rejected a key: `syntax` when a `-` is followed by
/// an ASCII lowercase letter (SyntaxError), `character` when the resulting
/// attribute name is not a valid name (InvalidCharacterError).
enum class dataset_fault : std::uint8_t {
    none,
    syntax,
    character
};

/// Converts dataset key `key` (`fooBar`) to its attribute name
/// (`data-foo-bar`) in `out`. Call it before converting the written value. On
/// success pass `out` to document::set_attribute_ns with the empty namespace
/// (removal uses remove_attribute_ns). A syntax fault is reported before a
/// character fault; on a character fault `out` holds the invalid name for the
/// error message.
[[nodiscard]] dataset_fault dataset_attribute_of(std::string_view key, std::string & out);

/// Reads `element`'s null-namespace data-* attributes as dataset entries.
/// dataset_value returns the value for `key`, or nullopt when no attribute maps
/// to it (a present empty value is "", not nullopt). dataset_entries returns
/// (key, value) pairs in attribute order. An invalid node ID behaves like an
/// element with no attributes. Element inputs from native callers must first
/// pass validate_element (element.hpp).
[[nodiscard]] std::optional<std::string> dataset_value(document & doc, node_id element,
                                                       std::string_view key);
[[nodiscard]] std::vector<std::pair<std::string, std::string>> dataset_entries(document & doc,
                                                                               node_id element);

} // namespace ctbrowser
