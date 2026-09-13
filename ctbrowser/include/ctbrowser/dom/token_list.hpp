#pragma once

#include <ctbrowser/dom/document.hpp>

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser {

enum class token_error {
    empty,
    whitespace
};

// HTML's ordered set: split on its five ASCII whitespace characters and keep
// each token's first occurrence. The returned strings own their bytes.
[[nodiscard]] std::vector<std::string> parse_ordered_tokens(std::string_view text);
[[nodiscard]] std::optional<token_error> validate_token(std::string_view token);

// Write an already normalized token set to its associated attribute. False
// means the empty set and absent attribute skipped the update steps. True
// includes writes of identical bytes; a failed attempted write returns its
// document error. The document and node retain their usual borrowed lifetime.
[[nodiscard]] std::expected<bool, dom_error> update_tokens(document & doc, node_id element,
                                                           atom attribute,
                                                           std::span<const std::string> tokens);

struct token_toggle_result {
    bool present;
    // Independent from the computed membership: a failed write retains that
    // result so adapters can preserve their existing error handling.
    std::expected<bool, dom_error> update;
};

// Validation errors precede all DOM reads/writes. DOM-write errors are in
// result.update; native callers must check both. A forced no-op preserves the
// attribute's original bytes, including duplicate tokens and whitespace.
[[nodiscard]] std::expected<token_toggle_result, token_error> toggle_token(
    document & doc, node_id element, atom attribute, std::string_view token,
    std::optional<bool> force = std::nullopt);

} // namespace ctbrowser
