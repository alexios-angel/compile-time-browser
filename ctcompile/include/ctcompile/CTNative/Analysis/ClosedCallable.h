//===- ClosedCallable.h - Closed callable proofs ----------------*- C++ -*-===//
//
// Proofs that a callable (a function, or an object holding closures) is
// closed: every use of it is known, so the native backend may call it
// directly instead of through the dynamic dispatcher.
//
//===----------------------------------------------------------------------===//

#pragma once
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/Error.h"
#include <optional>
#include <string>
namespace ctcompile::ctnative {
/// The operations that touch one object holding callables: its property
/// stores, the closure each property read yields, its calls, and the global
/// loads that reach it.
struct CallableObject {
    llvm::SmallVector<ctjs::SetPropertyOp> stores;
    llvm::SmallVector<std::pair<ctjs::GetPropertyOp, ctjs::CreateClosureOp>> reads;
    llvm::SmallVector<mlir::Operation *> calls;
    llvm::SmallVector<ctjs::LoadGlobalOp> loads;
};
/// Proves local slot identity and order without changing IR. Callers must
/// separately prove the initial prototype, every callable use, and the
/// complete source effects. A projected use census (`uses`) must prove all
/// omitted transport and initialization before every alias read or capture.
/// Stores still require the original object.
llvm::Expected<CallableObject> analyzeLocalCallableObject(
    ctjs::CreateObjectOp object, llvm::function_ref<bool()> spend,
    llvm::function_ref<llvm::SmallVector<mlir::OpOperand *>(mlir::Value)> uses = {});
/// The same proof for a fixed holder that the entry publishes to a global
/// before any operation can call source code. The same prototype,
/// callable-use and complete-effect obligations apply.
llvm::Expected<CallableObject> analyzeGlobalCallableObject(ctjs::StoreGlobalOp publication,
                                                           llvm::function_ref<bool()> spend);
/// Whether `store` is a unique hoisting declaration of `target` whose loads
/// are used only as closed direct callees.
bool closedDeclaration(ctjs::StoreGlobalOp store, ctjs::FuncOp target, mlir::ModuleOp module);
/// Whether every use of `load` is a ctjs.call_direct callee slot (or, with
/// `rooted`, also a ctjs.root), so the loaded value reaches no source
/// operation.
bool calleeSlotOnly(ctjs::LoadGlobalOp load, bool rooted = false);
/// Whether `entry`'s receiver, new.target and callee reach only ctjs.root.
bool implicitArgumentsRooted(mlir::Block & entry);
/// Whether `function` creates, reads or writes a closure or cell environment.
bool usesEnvironment(ctjs::FuncOp function);
/// Whether a closure of `function`, directly or through a global it is stored
/// to, is the callee value of a call_direct of another symbol: a specialized
/// body's boxed dispatch, whose actuals belong to `function` too.
bool hasAlternateCalleeCalls(ctjs::FuncOp function, mlir::ModuleOp module);
/// Every symbol use of `function`, when each is a call_direct of it passing
/// exactly its entry arguments; none otherwise.
std::optional<llvm::SmallVector<ctjs::CallDirectOp>> directCallers(ctjs::FuncOp function,
                                                                   mlir::ModuleOp module);
/// Why `function` is not a closed callable (how its closure escapes), or
/// empty when it is.
std::string closedCallableProblem(ctjs::FuncOp function, mlir::ModuleOp module);
} // namespace ctcompile::ctnative
