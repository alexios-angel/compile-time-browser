//===- internal.hpp - Object and Reflect shared operations ------*- C++ -*-===//
//
// Private to lib/Script/builtins/objects/ (not installed): the abstract
// operations that Object and Reflect both use, shared among this directory's
// files and defined in operations.cpp. ../internal.hpp is the builtins' wider
// private header.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../internal.hpp"

namespace ctbrowser::script::detail {

/// CreateListFromArrayLike (7.3.19) into a fresh array: a TypeError for a
/// non-object, a RangeError when the length exceeds `limit`. The returned array
/// keeps earlier arguments alive across later getters.
[[nodiscard]] value create_argument_list(context & cx, value list, double limit);

// key_filter and own_property_names are declared in ../internal.hpp: the JSON
// reviver walk needs OwnPropertyKeys too.

/// A property key as the value it names: a string, or the symbol rebuilt from
/// it.
[[nodiscard]] value key_value(context & cx, const std::string & key);

/// [[GetPrototypeOf]] for every kind of value.
[[nodiscard]] value prototype_of(context & cx, value of);

/// [[SetPrototypeOf]] for every kind of value; false is the refusal in
/// ECMA-262 10.1.2.1 (OrdinarySetPrototypeOf).
[[nodiscard]] bool set_prototype_of(context & cx, value of, value proto);

/// The three TypeErrors of ECMA-262 ToPropertyDescriptor (6.2.6.5) itself: a
/// non-callable getter, a non-callable setter, and a descriptor that mixes
/// accessor and data fields. False means the TypeError is in flight.
[[nodiscard]] bool valid_descriptor(context & cx, const context::property_descriptor & d);

} // namespace ctbrowser::script::detail
