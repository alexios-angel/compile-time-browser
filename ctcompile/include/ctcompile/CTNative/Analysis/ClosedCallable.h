#pragma once
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/Error.h"
#include <optional>
#include <string>
namespace ctcompile::ctnative {
struct LocalCallableObject {
    llvm::SmallVector<ctjs::SetPropertyOp> stores;
    llvm::SmallVector<std::pair<ctjs::GetPropertyOp, ctjs::CreateClosureOp>> reads;
    llvm::SmallVector<mlir::Operation *> calls;
};
// Prove local slot identity/order without changing IR. Callers must separately
// prove the initial prototype, every callable use, and the complete source effects.
llvm::Expected<LocalCallableObject> analyzeLocalCallableObject(ctjs::CreateObjectOp object,
                                                               llvm::function_ref<bool()> spend);
// A unique hoisting declaration whose loads stay in closed direct-callee uses.
bool closedDeclaration(ctjs::StoreGlobalOp store, ctjs::FuncOp target, mlir::ModuleOp module);
std::string closedCallableProblem(ctjs::FuncOp function, mlir::ModuleOp module);
} // namespace ctcompile::ctnative
