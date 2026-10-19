//===- Owned.hpp - A class instance one field owns --------------*- C++ -*-===//
//
// The members that hold a class instance which one field of another instance
// owns (ClosureLifting's ownsByValue). JavaScript can read that field before
// the instance is stored and after the program stores null into it, so each
// member also records which of the three the field holds, and a read or write
// through the field in either other state throws the TypeError the
// interpreter throws.
//
// ctnative::owned<T> holds the instance by value, for a field only the class's
// constructor stores an instance into: the constructor runs once per owner,
// so nothing replaces an instance while one of its methods runs.
// ctnative::replaceable<T> is for a field a method stores into: the instance
// lives on the heap, and one the field held before stays alive with the
// owner, so a method still running on it keeps a live object, as JavaScript
// keeps it reachable through `this`. A store of null keeps the instance alive
// in both.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Error.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

/// A field that owns a class instance a method of its class can replace.
template <class T> struct replaceable {
    std::unique_ptr<T> value;
    /// The instances the field held before, alive until the owner is gone.
    std::vector<std::unique_ptr<T>> replaced;
    owned_state state = owned_state::undefined;

    replaceable() = default;
    /// Copying an owner copies the instance its field holds, as copying an
    /// owner with an owned<T> field does.
    replaceable(const replaceable & other)
        : value(other.value ? std::make_unique<T>(*other.value) : nullptr), state(other.state) {}
    replaceable & operator=(const replaceable & other) {
        if (this != &other) {
            if (value) { replaced.push_back(std::move(value)); }
            value = other.value ? std::make_unique<T>(*other.value) : nullptr;
            state = other.state;
        }
        return *this;
    }
    replaceable(replaceable &&) noexcept = default;
    replaceable & operator=(replaceable &&) noexcept = default;
    ~replaceable() = default;
};

/// `owner.field = instance`.
template <class T> void own(owned<T> * field, T instance) {
    field->value = std::move(instance);
    field->state = owned_state::instance;
}
template <class T> void own(replaceable<T> * field, T instance) {
    if (field->value) { field->replaced.push_back(std::move(field->value)); }
    field->value = std::make_unique<T>(std::move(instance));
    field->state = owned_state::instance;
}

/// `owner.field = null`. The instance stays alive until its owner is gone.
template <class Field> void disown(Field * field) {
    field->state = owned_state::null;
}

/// `owner.field` as a condition (ECMA-262 7.1.2 ToBoolean): true exactly when
/// the field holds the instance, since undefined and null are false.
template <class Field> bool held(const Field * field) {
    return field->state == owned_state::instance;
}

namespace detail {
/// The TypeError for using `key` on a field that holds undefined or null
/// (ECMA-262 13.3.2.1 and 7.3.4: a property reference whose base is undefined
/// or null), worded as the interpreter words it.
[[noreturn]] inline void throw_unowned(owned_state state, std::string_view verb,
                                       std::string_view key) {
    const std::string held = state == owned_state::null ? "null" : "undefined";
    throw type_error("Cannot " + std::string(verb) + " properties of " + held + " (" +
                     (verb == "read" ? "reading '" : "setting '") + std::string(key) + "')");
}
/// The instance a field holds.
template <class T> T * instance(owned<T> * field) {
    return &field->value;
}
template <class T> T * instance(replaceable<T> * field) {
    return field->value.get();
}
} // namespace detail

/// The instance `owner.field` holds, about to have property `key` read
/// through it (or a method `key` called on it).
template <class Field> auto reading(Field * field, std::string_view key) {
    if (field->state != owned_state::instance) { detail::throw_unowned(field->state, "read", key); }
    return detail::instance(field);
}

/// The instance `owner.field` holds, about to have property `key` written.
template <class Field> auto setting(Field * field, std::string_view key) {
    if (field->state != owned_state::instance) { detail::throw_unowned(field->state, "set", key); }
    return detail::instance(field);
}

} // namespace ctnative
