#pragma once
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ctbrowser {

// Owning JSON tree. Object keys keep their first position and last parsed value.
struct json_value {
    struct member;
    using array = std::vector<json_value>;
    using object = std::vector<member>;
    std::variant<std::nullptr_t, bool, double, std::string, array, object> data;
};

struct json_value::member {
    std::string key;
    json_value value;
};

// The error is the byte position after the existing parser's trailing whitespace skip.
// Preserves existing behavior: raw non-control bytes pass through, lone escaped
// surrogates become U+FFFD, and out-of-range numbers become positive zero.
// Nesting uses the call stack with no added depth limit.
[[nodiscard]] std::expected<json_value, std::size_t> parse_json(std::string_view text);

} // namespace ctbrowser
