//===- Candidates.h - Specialization candidates -----------------*- C++ -*-===//
//
// Helpers for --ctnative-specialize: which functions may be specialised, which
// call arguments are constant, and how to clone a function with constants
// bound to its parameters.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/SmallVector.h"

#include <string>

namespace ctcompile::ctnative::specialization {
/// Why `function` cannot be specialised, or empty if it can.
std::string refusal(ctjs::FuncOp function, mlir::ModuleOp module);
/// The constant arguments of `call`, one per entry argument of `function`. A
/// null attribute marks an argument that is not a primitive constant, an
/// implicit argument, or a parameter the body never uses.
llvm::SmallVector<mlir::Attribute> staticArguments(ctjs::CallDirectOp call, ctjs::FuncOp function);
/// Operation count of `function`, used to cap the size of clones.
unsigned bodySize(ctjs::FuncOp function);
/// A detached clone of `source` whose entry argument i is replaced by the
/// constant `bindings[i]` where that is set. ctnative.* annotations are
/// dropped, because facts about the original are not evidence for the clone.
ctjs::FuncOp cloneWithBindings(ctjs::FuncOp source, llvm::ArrayRef<mlir::Attribute> bindings);
} // namespace ctcompile::ctnative::specialization
