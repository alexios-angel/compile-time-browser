//===- Object.hpp - The native JavaScript Object ----------------*- C++ -*-===//
//
// The Object constructor's static methods that generated native code calls,
// spelled as JavaScript spells them: `ctnative::Object.keys(o)`. Each method
// is an overload set over the native forms an object can take; today that is
// a dictionary (Dictionary.hpp, an object used as a string-keyed table). And
// Object.prototype.toString through `call`, of a primitive, which Bootstrap's
// toType reads a value's type name from.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTNative/Runtime/ctnative.hpp"

#include <cstddef>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ctnative {

template <class V> class dictionary;

/// Object.prototype.toString (ECMA-262 20.1.3.6) called on a primitive:
/// "[object " + builtinTag + "]", where ToObject's wrapper gives the tag
/// (Number, String or Boolean) and undefined and null have their own. No
/// primitive's wrapper prototype defines @@toStringTag, so the tag is final.
struct object_to_string_method {
    js_string call(undefined_t) const { return js_string{"[object Undefined]"}; }
    js_string call(js_null_t) const { return js_string{"[object Null]"}; }
    js_string call(const js_string &) const { return js_string{"[object String]"}; }
    js_string call(js_num) const { return js_string{"[object Number]"}; }
    template <class T>
    requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
    js_string call(T) const {
        return js_string{"[object Number]"};
    }
    js_string call(js_boolean_t) const { return js_string{"[object Boolean]"}; }
    js_string call(const nullable_scalar & value) const {
        switch (value.tag) {
        case nullable_scalar::kind::undefined: return call(undefined_t{});
        case nullable_scalar::kind::null: return call(js_null_t{});
        case nullable_scalar::kind::number: return call(value.value);
        case nullable_scalar::kind::boolean: return call(js_boolean_t{});
        }
        std::terminate();
    }
    js_string call(const nullable_string & value) const {
        switch (value.tag) {
        case nullable_string::kind::undefined: return call(undefined_t{});
        case nullable_string::kind::null_value: return call(js_null_t{});
        case nullable_string::kind::string: return call(js_string{});
        }
        std::terminate();
    }
};

/// The Object prototype's methods native code calls.
struct object_prototype {
    object_to_string_method toString;
};

/// Object.keys, Object.values and Object.entries (ECMA-262 20.1.2, through
/// EnumerableOwnProperties, 7.3.23). On a dictionary they enumerate its keys in
/// dictionary::order(), the OrdinaryOwnPropertyKeys order.
struct object_constructor {
    object_prototype prototype;
    template <class V> std::vector<js_string> keys(const std::shared_ptr<dictionary<V>> & o) const {
        std::vector<js_string> out;
        for (const std::size_t i : o->order()) { out.emplace_back(o->at(i).first); }
        return out;
    }
    template <class V> std::vector<V> values(const std::shared_ptr<dictionary<V>> & o) const {
        std::vector<V> out;
        for (const std::size_t i : o->order()) { out.push_back(o->at(i).second); }
        return out;
    }
    template <class V>
    std::vector<std::pair<js_string, V>> entries(const std::shared_ptr<dictionary<V>> & o) const {
        std::vector<std::pair<js_string, V>> out;
        for (const std::size_t i : o->order()) {
            out.emplace_back(js_string{o->at(i).first}, o->at(i).second);
        }
        return out;
    }
};
inline constexpr object_constructor Object{};

} // namespace ctnative
