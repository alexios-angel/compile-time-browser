//===- token_list.hpp - DOMTokenList operations --------------- -*- C++ -*-===//
//
// The DOMTokenList operations (classList, relList and the like) on an element
// attribute: parse, validate, add, remove, toggle and write back.
//
//===----------------------------------------------------------------------===//

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

/// Why a token was rejected: empty (SyntaxError) or containing ASCII
/// whitespace (InvalidCharacterError).
enum class token_error {
    empty,
    whitespace
};

/// HTML's ordered set parser: splits on the five ASCII whitespace characters
/// and keeps each token's first occurrence.
[[nodiscard]] std::vector<std::string> parse_ordered_tokens(std::string_view text);
/// nullopt when `token` is valid, otherwise why it is not.
[[nodiscard]] std::optional<token_error> validate_token(std::string_view token);

/// Writes an already normalized token set to `attribute`. Returns false when
/// nothing was written (the set is empty and the attribute is absent), true
/// when the attribute was written even with identical bytes, and the
/// dom_error when the write failed.
[[nodiscard]] std::expected<bool, dom_error> update_tokens(document & doc, node_id element,
                                                           atom attribute,
                                                           std::span<const std::string> tokens);

/// Whether `token` is in `attribute_text`'s token set. Does not validate:
/// an empty or whitespace-containing token is simply absent.
[[nodiscard]] bool contains_token(std::string_view attribute_text, std::string_view token);

/// The first invalid argument (by index) and why it is invalid.
struct token_argument_error {
    std::size_t index;
    token_error error;
};

/// DOMTokenList add() and remove(). All arguments are validated in order before
/// any DOM access; the outer error identifies the first invalid token. The
/// inner result has update_tokens' semantics, including normalizing the
/// attribute when called with no arguments and writing identical values.
using token_update_result = std::expected<std::expected<bool, dom_error>, token_argument_error>;
[[nodiscard]] token_update_result add_tokens(document & doc, node_id element, atom attribute,
                                             std::span<const std::string> tokens);
[[nodiscard]] token_update_result remove_tokens(document & doc, node_id element, atom attribute,
                                                std::span<const std::string> tokens);

/// toggle()'s result: the token's resulting membership and the write outcome.
struct token_toggle_result {
    bool present;
    /// Independent of `present`: a failed write keeps the computed membership
    /// so callers can report the error separately.
    std::expected<bool, dom_error> update;
};

/// DOMTokenList toggle(). Validation errors are returned before any DOM read
/// or write; DOM write errors are in result.update, so callers must check
/// both. A forced no-op preserves the attribute's original bytes, including
/// duplicate tokens and whitespace.
[[nodiscard]] std::expected<token_toggle_result, token_error> toggle_token(
    document & doc, node_id element, atom attribute, std::string_view token,
    std::optional<bool> force = std::nullopt);

} // namespace ctbrowser
