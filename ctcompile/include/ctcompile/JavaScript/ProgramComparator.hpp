#pragma once

#include <optional>
#include <string>

#include <ctbrowser/script/bytecode.hpp>

// DOES THIS PROGRAM MATCH THAT ONE, FIELD BY FIELD.
//
// The acceptance test for the program image: a program written to bytes and read
// back must be indistinguishable from the one the compiler produced, and the
// only honest way to know is to compare every field the VM can read.
//
// CONSTANTS ARE COMPARED BY BITS, not by value, and that is the one decision in
// here worth stating. `value` is NaN-boxed, so comparing as doubles would make
// NaN differ from ITSELF - failing a correct round trip - while +0 and -0 would
// compare EQUAL, passing an image that changed `1/x` from Infinity to
// -Infinity. Bits get both right, and they are what the image writes.
namespace ctcompile::js {

struct difference {
    std::string where; // "function 12, instruction 40", "constant 3"
    std::string what;
};

[[nodiscard]] std::optional<difference> compare(const ctbrowser::script::program & expected,
                                                const ctbrowser::script::program & actual);

} // namespace ctcompile::js
