//===- json.hpp - JSON parser --------------------------------- -*- C++ -*-===//
//
// An owning JSON tree and the parser that builds it, used by the script
// engine's JSON.parse.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ctbrowser {

/// An owning JSON tree. A duplicated object key keeps its first position and
/// its last parsed value.
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

/// Parses `text` as one JSON value. On failure, the error is the byte offset
/// where parsing stopped, counted after any skipped whitespace. Raw non-control
/// bytes pass through, a lone escaped surrogate is kept as its WTF-8 (what a JS
/// string holds), and out-of-range numbers become positive zero. Nesting
/// recurses on the call stack with no depth limit of its own.
[[nodiscard]] std::expected<json_value, std::size_t> parse_json(std::string_view text);

/// QuoteJSONString (ECMA-262 25.5.2.3), appended to `out`. Uses the spec's short escapes
/// (\b, \f, \n, ...) so output matches other engines byte for byte; other C0
/// controls become \u00XX. Bytes at or above 0x80 pass through unescaped, as
/// other engines do (strings here are UTF-8, and so is JSON); lone surrogates
/// are escaped as \uXXXX.
void quote_json(std::string_view text, std::string & out);

} // namespace ctbrowser
