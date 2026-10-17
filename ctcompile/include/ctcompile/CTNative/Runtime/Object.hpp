//===- Object.hpp - The native JavaScript Object ----------------*- C++ -*-===//
//
// The Object constructor's static methods that generated native code calls,
// spelled as JavaScript spells them: `ctnative::Object.keys(o)`. Each method
// is an overload set over the native forms an object can take; today that is
// a dictionary (Dictionary.hpp, an object used as a string-keyed table).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "String.hpp"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace ctnative {

template <class V> class dictionary;

};

/// Object.keys, Object.values and Object.entries (ECMA-262 20.1.2, through
/// EnumerableOwnProperties, 7.3.23). On a dictionary they enumerate its keys in
/// dictionary::order(), the OrdinaryOwnPropertyKeys order.
struct object_constructor {
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
