//===- Json.hpp - Native JSON.stringify ------------------------*- C++ -*-===//
//
// ctnative::JSON, whose stringify serializes the values native code builds as
// ctbrowser::json_value trees. Only programs that call it include
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
struct stringify_method {
    js_string operator()(const ctbrowser::json_value & value) const {
        std::string out;
        detail::json_write(value, out);
        return js_string{std::move(out)};
    }
};

/// The JSON object's members native code calls: generated code reads
/// `ctnative::JSON.stringify(tree)`.
struct json_object {
    stringify_method stringify;
};
inline constexpr json_object JSON{};

} // namespace ctnative
