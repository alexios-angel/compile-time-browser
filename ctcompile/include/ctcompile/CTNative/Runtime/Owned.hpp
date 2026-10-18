//===- Owned.hpp - A class instance one field owns --------------*- C++ -*-===//
//
// ctnative::owned<T>, the member that holds a class instance which one field
// of another instance owns by value (ClosureLifting's ownsByValue).
// JavaScript can read that field before the constructor stores the instance,
// and after the program stores null into it, so the member also records which
// of the three the field holds. A read or write through the field in either
// other state throws the TypeError the interpreter throws. The instance itself
// lives as long as its owner: a method still running on it after the field is
// set to null keeps a live object, as it keeps a reachable one in JavaScript.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Error.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ctnative {

/// What a field that owns an instance holds, as JavaScript sees it.
enum class owned_state : unsigned char {
    undefined,
    instance,
    null
};

/// A field that owns a class instance by value.
template <class T> struct owned {
    T value{};
    owned_state state = owned_state::undefined;
};

/// `owner.field = instance`.
template <class T> void own(owned<T> * field, T instance) {
    field->value = std::move(instance);
    field->state = owned_state::instance;
}

/// `owner.field = null`. The instance stays alive until its owner is gone.
template <class T> void disown(owned<T> * field) {
    field->state = owned_state::null;
}

namespace detail {
/// The TypeError for using `key` on a field that holds undefined or null
/// (ECMA-262 13.3.2.1 and 7.3.4: a property reference whose base is undefined
/// or null), worded as the interpreter words it.
template <class T>
[[noreturn]] void throw_unowned(const owned<T> & field, std::string_view verb,
                                std::string_view key) {
    const std::string held = field.state == owned_state::null ? "null" : "undefined";
    throw type_error("Cannot " + std::string(verb) + " properties of " + held + " (" +
                     (verb == "read" ? "reading '" : "setting '") + std::string(key) + "')");
}
} // namespace detail

/// The instance `owner.field` holds, about to have property `key` read
/// through it (or a method `key` called on it).
template <class T> T * reading(owned<T> * field, std::string_view key) {
    if (field->state != owned_state::instance) { detail::throw_unowned(*field, "read", key); }
    return &field->value;
}

/// The instance `owner.field` holds, about to have property `key` written.
template <class T> T * setting(owned<T> * field, std::string_view key) {
    if (field->state != owned_state::instance) { detail::throw_unowned(*field, "set", key); }
    return &field->value;
}

} // namespace ctnative
