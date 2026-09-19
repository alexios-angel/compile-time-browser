#pragma once
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/Error.h"
#include <optional>
#include <string>
namespace ctcompile::ctnative {
struct CallableObject {
    llvm::SmallVector<ctjs::SetPropertyOp> stores;
    llvm::SmallVector<std::pair<ctjs::GetPropertyOp, ctjs::CreateClosureOp>> reads;
    llvm::SmallVector<mlir::Operation *> calls;
    llvm::SmallVector<ctjs::LoadGlobalOp> loads;
};
// Prove local slot identity/order without changing IR. Callers must separately
// prove the initial prototype, every callable use, and the complete source effects.
// A projected use census must prove all omitted transport and initialization
// before every alias read/capture. Stores still require the original object.
llvm::Expected<CallableObject> analyzeLocalCallableObject(
    ctjs::CreateObjectOp object, llvm::function_ref<bool()> spend,
    llvm::function_ref<llvm::SmallVector<mlir::OpOperand *>(mlir::Value)> uses = {});
// A fixed holder published by the entry before any operation can call source code.
// The same prototype, callable-use and complete-effect obligations apply.
llvm::Expected<CallableObject> analyzeGlobalCallableObject(ctjs::StoreGlobalOp publication,
                                                           llvm::function_ref<bool()> spend);
// A unique hoisting declaration whose loads stay in closed direct-callee uses.
bool closedDeclaration(ctjs::StoreGlobalOp store, ctjs::FuncOp target, mlir::ModuleOp module);
std::string closedCallableProblem(ctjs::FuncOp function, mlir::ModuleOp module);
} // namespace ctcompile::ctnative
