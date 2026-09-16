#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

// THE GENERATED PROGRAM'S ONE INCLUDE, and the two defines that may precede
// it. The header (include/ctcompile/CTNative/Runtime/ctnative.hpp) documents
// what each switch selects; the deforestation pass reads the ordered define
// back out of the module as the proof that the ordered Map contract is
// present, so its spelling is shared here rather than typed twice.
inline constexpr llvm::StringLiteral kRuntimeHeader = "ctcompile/CTNative/Runtime/ctnative.hpp";
inline constexpr llvm::StringLiteral kOrderedMapsDefine = "#define CTNATIVE_ORDERED_MAPS 1";
inline constexpr llvm::StringLiteral kDOMDefine = "#define CTNATIVE_DOM 1";

} // namespace ctcompile::ctnative
