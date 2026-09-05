#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/SmallVector.h"

#include <string>

namespace ctcompile::ctnative::specialization {
std::string refusal(ctjs::FuncOp function, mlir::ModuleOp module);
llvm::SmallVector<mlir::Attribute> staticArguments(ctjs::CallDirectOp call, ctjs::FuncOp function);
unsigned bodySize(ctjs::FuncOp function);
} // namespace ctcompile::ctnative::specialization
