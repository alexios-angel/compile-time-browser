//===- case_mapping.cpp - Unicode case mapping for the VM -------*- C++ -*-===//
//
// String.prototype.toLowerCase and toUpperCase's default case conversion
// (ECMA-262 22.1.3.28 / 22.1.3.30). The tables and Final_Sigma live in
// ctbrowser core (lib/Core/case_mapping.cpp), which the native runtime calls
// too, so the VM and native code cannot map a code point differently.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/algorithms.hpp>

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

std::string unicode_case(std::string_view s, bool upper) {
    return ctbrowser::unicode_case(s, upper);
}

} // namespace ctbrowser::script::builtins_detail
