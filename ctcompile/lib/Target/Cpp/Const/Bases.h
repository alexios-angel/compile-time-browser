#pragma once

//===- Bases.h - emitc.class base classes -----------------------*- C++ -*-===//
//
// Prints a class's `ctnative.bases` as public base classes. A registry class
// whose callbacks hold an instance by its owner derives
// std::enable_shared_from_this, set by EmitC/Module.cpp.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/Operation.h"
#include "llvm/Support/raw_ostream.h"

namespace ctcompile::cpp {

/// Prints the class's `ctnative.bases` attribute as ` : public B1, public B2`
/// after the class name and any `final` specifier. Refuses an empty name,
/// which would print `public` with nothing after it. No-op when the attribute
/// is absent.
mlir::LogicalResult printClassBases(mlir::emitc::ClassOp classOp, llvm::raw_ostream & os);

} // namespace ctcompile::cpp
