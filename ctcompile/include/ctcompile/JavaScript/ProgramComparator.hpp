//===- ProgramComparator.hpp - Compare two bytecode programs ----*- C++ -*-===//
//
// Does this program match that one, field by field?
//
// The acceptance test for the program image: a program written to bytes and
// read back must be indistinguishable from the one the compiler produced, so
// every field the VM can read is compared.
//
// Constants are compared by bits, not by value. `value` is NaN-boxed, so
// comparing as doubles would make NaN differ from itself (failing a correct
// round trip) while +0 and -0 would compare equal (passing an image that
// changed `1/x` from Infinity to -Infinity). Bits get both right, and they are
// what the image writes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include "ctcompile/Support/Difference.hpp"

#include <ctbrowser/script/bytecode.hpp>

namespace ctcompile::js {

/// Where two programs first disagree; `where` reads like
/// "function 12, instruction 40" or "constant 3".
using difference = ctcompile::difference;

/// Nothing when the two programs agree on every field; otherwise the first
/// disagreement.
[[nodiscard]] std::optional<difference> compare(const ctbrowser::script::program & expected,
                                                const ctbrowser::script::program & actual);

} // namespace ctcompile::js
