//===- Json.hpp - Native JSON.stringify ------------------------*- C++ -*-===//
//
// ctnative::JSON, whose stringify serializes the values native code builds as
// ctbrowser::json_value trees, and json_record, which builds the tree of an
// object literal's own properties. Only programs that call it include
// this header; the quoting is ctbrowser core's, which the VM's JSON.stringify
// also calls, so the two cannot escape a string differently.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Number.hpp"
#include "String.hpp"
#include "ctnative.hpp"

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

/// The JSON node of one property value. Only these carriers are admitted
/// into a record JSON.stringify serializes; each always serializes.
inline ctbrowser::json_value to_json(js_num value) {
    return ctbrowser::json_value{value.value()};
}
inline ctbrowser::json_value to_json(double value) {
    return ctbrowser::json_value{value};
}
inline ctbrowser::json_value to_json(js_boolean_t value) {
    return ctbrowser::json_value{static_cast<bool>(value)};
}
inline ctbrowser::json_value to_json(const js_string & value) {
    return ctbrowser::json_value{value.value()};
}

inline void define_json_properties(ctbrowser::json_value::object &) {}
template <class Value, class... Rest>
void define_json_properties(ctbrowser::json_value::object & out, const char * key,
                            const Value & value, const Rest &... rest) {
    out.push_back({key, to_json(value)});
    define_json_properties(out, rest...);
}
} // namespace detail

/// The object an object literal is, for JSON.stringify: `fields` alternate a
/// key and its value, in the order the literal defined them. The compiler
/// passes distinct keys none of which is an array index, so that order is
/// OrdinaryOwnPropertyKeys order (ECMA-262 10.1.11.1), the order
/// SerializeJSONObject (25.5.2.5) enumerates.
template <class... Fields> ctbrowser::json_value json_record(const Fields &... fields) {
    ctbrowser::json_value::object members;
    detail::define_json_properties(members, fields...);
    return ctbrowser::json_value{std::move(members)};
}

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
