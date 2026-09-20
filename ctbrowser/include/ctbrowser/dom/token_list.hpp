#pragma once

#include <ctbrowser/dom/document.hpp>

#include <cstddef>
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

// Membership does not validate the argument: an empty or whitespace-containing
// token is simply absent. Both strings are borrowed for the duration of the call.
[[nodiscard]] bool contains_token(std::string_view attribute_text, std::string_view token);

struct token_argument_error {
    std::size_t index;
    token_error error;
};

// All arguments are validated in order before any DOM access. The outer error
// identifies the first invalid token; the inner result has update_tokens' write
// semantics, including normalization with no arguments and same-value writes.
using token_update_result = std::expected<std::expected<bool, dom_error>, token_argument_error>;
[[nodiscard]] token_update_result add_tokens(document & doc, node_id element, atom attribute,
                                             std::span<const std::string> tokens);
[[nodiscard]] token_update_result remove_tokens(document & doc, node_id element, atom attribute,
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
