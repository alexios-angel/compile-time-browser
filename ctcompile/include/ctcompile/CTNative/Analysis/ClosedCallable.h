#pragma once
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include <optional>
#include <string>
namespace ctcompile::ctnative {
std::optional<unsigned> functionIndex(ctjs::FuncOp function);
// A unique hoisting declaration whose loads stay in closed direct-callee uses.
bool closedDeclaration(ctjs::StoreGlobalOp store, ctjs::FuncOp target, mlir::ModuleOp module);
std::string closedCallableProblem(ctjs::FuncOp function, mlir::ModuleOp module);
} // namespace ctcompile::ctnative
