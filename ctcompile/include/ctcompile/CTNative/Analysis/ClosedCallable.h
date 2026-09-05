#pragma once
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include <optional>
#include <string>
namespace ctcompile::ctnative {
std::optional<unsigned> functionIndex(ctjs::FuncOp function);
std::string closedCallableProblem(ctjs::FuncOp function, mlir::ModuleOp module);
} // namespace ctcompile::ctnative
