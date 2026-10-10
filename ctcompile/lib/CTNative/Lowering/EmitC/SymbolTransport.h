//===- SymbolTransport.h - Symbols across SCF edges -------------*- C++ -*-===//
//
// Declares the post-pass that lets Symbol values flow through scf.if and
// scf.while results in EmitC output.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "mlir/Dialect/EmitC/IR/EmitC.h"

namespace ctcompile::ctnative::lowering_detail {

/// Wraps every Symbol carried across an scf.if or scf.while edge in
/// std::optional storage. SCF-to-EmitC declares such state before assigning
/// it; the optional stays disengaged until an incoming Symbol initializes it,
/// so no placeholder JavaScript Symbol identity is ever created.
void transportSymbols(mlir::emitc::FuncOp function);

} // namespace ctcompile::ctnative::lowering_detail
