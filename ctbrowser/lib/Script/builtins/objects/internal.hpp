#pragma once
// Private to lib/Script/builtins/objects/. NOT installed and in no file set:
// ../internal.hpp is the builtins' shared private header and this only adds
// what the five files carved out of objects.cpp on 2026-09-08 share with each
// other and with nothing else - the abstract operations Object and Reflect
// both answer through. They were in objects.cpp's anonymous namespace; they
// gained external linkage in ctbrowser::script::detail and nothing else, and
// their bodies are in operations.cpp.

#include "../internal.hpp"

namespace ctbrowser::script::detail {

// CreateListFromArrayLike, with the caller's existing limit for a generic walk.
// The returned array keeps earlier arguments alive across later getters.
[[nodiscard]] value create_argument_list(context & cx, value list, double limit);

// key_filter and own_property_names are declared in ../internal.hpp: the JSON
// reviver walk needs OwnPropertyKeys too.

// A property key as the value it names: a string, or the symbol rebuilt from it.
[[nodiscard]] value key_value(context & cx, const std::string & key);

// [[GetPrototypeOf]], FOR EVERY KIND OF VALUE.
[[nodiscard]] value prototype_of(context & cx, value of);

// [[SetPrototypeOf]], FOR EVERY KIND OF VALUE; false is 10.1.2.1's refusal.
[[nodiscard]] bool set_prototype_of(context & cx, value of, value proto);

// 6.2.6.6 ToPropertyDescriptor's OWN three refusals.
[[nodiscard]] bool valid_descriptor(context & cx, const context::property_descriptor & d);

} // namespace ctbrowser::script::detail
