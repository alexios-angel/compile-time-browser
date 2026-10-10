//===- Runtime.h - Generated program prelude --------------------*- C++ -*-===//
//
// Spellings of the runtime include and defines placed at the top of every
// C++ program the native backend generates.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

/// The single header every generated program includes, and the two defines
/// that may precede it (include/ctcompile/CTNative/Runtime/ctnative.hpp
/// documents what each selects). The deforestation pass looks for
/// kOrderedMapsDefine in the module as proof that ordered Maps are enabled,
/// so the spelling is shared here.
inline constexpr llvm::StringLiteral kRuntimeHeader = "ctcompile/CTNative/Runtime/ctnative.hpp";
inline constexpr llvm::StringLiteral kOrderedMapsDefine = "#define CTNATIVE_ORDERED_MAPS 1";
inline constexpr llvm::StringLiteral kDOMDefine = "#define CTNATIVE_DOM 1";

} // namespace ctcompile::ctnative
