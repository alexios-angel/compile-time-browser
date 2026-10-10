//===- Json.hpp - Native JSON.stringify ------------------------*- C++ -*-===//
//
// json_quote and json_stringify: JSON.stringify for the values native code
// builds as ctbrowser::json_value trees. Only programs that call them include
// this header; the quoting is ctbrowser core's, which the VM's JSON.stringify
// also calls, so the two cannot escape a string differently.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "String.hpp"

#include <ctbrowser/core/json.hpp>
#include <ctbrowser/core/number_format.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace ctnative {

/// QuoteJSONString (ECMA-262 25.5.2.3) of `text`, the quotes included.
inline js_string json_quote(const js_string & text) {
    std::string out;
    ctbrowser::quote_json(text.value(), out);
    return js_string{std::move(out)};
}

namespace detail {
/// SerializeJSONProperty (ECMA-262 25.5.2.2) for one node, with no replacer
/// and no gap, appended to `out`.
inline void json_write(const ctbrowser::json_value & value, std::string & out) {
    std::visit(
        [&]<class T>(const T & node) {
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                out += "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                out += node ? "true" : "false";
            } else if constexpr (std::is_same_v<T, double>) {
                // Step 9: a finite Number is ToString(value); NaN and the
                // infinities are "null".
                out += std::isfinite(node) ? ctbrowser::number_to_string(node) : "null";
            } else if constexpr (std::is_same_v<T, std::string>) {
                ctbrowser::quote_json(node, out);
            } else if constexpr (std::is_same_v<T, ctbrowser::json_value::array>) {
                // SerializeJSONArray (25.5.2.6) with an empty gap.
                out += '[';
                for (std::size_t i = 0; i < node.size(); ++i) {
                    if (i != 0) { out += ','; }
                    json_write(node[i], out);
                }
                out += ']';
            } else {
                // SerializeJSONObject (25.5.2.5) with an empty gap, in the
                // tree's own member order.
                out += '{';
                for (std::size_t i = 0; i < node.size(); ++i) {
                    if (i != 0) { out += ','; }
                    ctbrowser::quote_json(node[i].key, out);
                    out += ':';
                    json_write(node[i].value, out);
                }
                out += '}';
            }
        },
        value.data);
}
} // namespace detail

/// JSON.stringify(value) (ECMA-262 25.5.2.1) with no replacer and no space
/// argument. The tree holds no undefined, function or symbol, so every value
/// serializes and the result is always a String.
inline js_string json_stringify(const ctbrowser::json_value & value) {
    std::string out;
    detail::json_write(value, out);
    return js_string{std::move(out)};
}

} // namespace ctnative
