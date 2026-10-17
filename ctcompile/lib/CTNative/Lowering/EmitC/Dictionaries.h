//===- Dictionaries.h - Emission of objects used as tables ------*- C++ -*-===//
//
// An object literal prepareNativeDictionaries proved is used only as a table
// lowers to a std::shared_ptr<ctnative::dictionary<V>> and the table helpers
// of Runtime/Dictionary.hpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

/// Replaces one operation of a proved table with its runtime call; false for
/// any other operation.
bool replaceDictionary(lowering & lower, mlir::Operation * o);

} // namespace ctcompile::ctnative::lowering_detail
