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

// WHICH HALF OF OwnPropertyKeys A CALLER WANTS. 20.1.2.10
// (getOwnPropertyNames) and 20.1.2.11 (getOwnPropertySymbols) are the same walk
// filtered two different ways, and 7.3.7/7.3.24 want it unfiltered - a symbol
// key is copied by Object.assign and read by Object.defineProperties, which is
// the one place OwnPropertyKeys and EnumerableOwnProperties differ.
enum class key_filter : std::uint8_t {
    strings,
    symbols,
    all
};

// A context::property_descriptor AS JAVASCRIPT SEES IT (6.2.6.4,
// FromPropertyDescriptor).
[[nodiscard]] object_object * descriptor_object(context & cx,
                                                const context::property_descriptor & from);

// EVERY OWN KEY OF ANY VALUE, including the synthesised ones.
[[nodiscard]] std::vector<std::string> own_property_names(context & cx, value of,
                                                          key_filter which = key_filter::strings);

// [[GetPrototypeOf]], FOR EVERY KIND OF VALUE.
[[nodiscard]] value prototype_of(context & cx, value of);

// 6.2.6.6 ToPropertyDescriptor's OWN three refusals.
[[nodiscard]] bool valid_descriptor(context & cx, const context::property_descriptor & d);

} // namespace ctbrowser::script::detail
