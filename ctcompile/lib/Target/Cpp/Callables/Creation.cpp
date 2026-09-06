#include "Body.h"

#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"

namespace ctcompile::cpp {

mlir::FailureOr<mlir::emitc::FuncOp> callableCreationTarget(mlir::emitc::CallOpaqueOp call) {
    if (!call->hasAttr("ctnative.callable_create")) { return mlir::emitc::FuncOp{}; }
    const auto invalid = [&]() -> mlir::FailureOr<mlir::emitc::FuncOp> {
        call.emitOpError("invalid native callable creation");
        return mlir::failure();
    };
    auto symbol = call->getAttrOfType<mlir::FlatSymbolRefAttr>("ctnative.callable_create");
    auto function =
        symbol ? mlir::SymbolTable::lookupNearestSymbolFrom<mlir::emitc::FuncOp>(call, symbol)
               : mlir::emitc::FuncOp{};
    if (!function || function.isExternal() || call.getArgs() || call.getTemplateArgs() ||
        call->getNumResults() != 1) {
        return invalid();
    }
    auto body = function->getAttrOfType<mlir::DictionaryAttr>("ctnative.callable_body");
    auto binder = body ? body.getAs<mlir::StringAttr>("binder") : mlir::StringAttr{};
    auto captures = body ? body.getAs<mlir::ArrayAttr>("captures") : mlir::ArrayAttr{};
    auto type = body ? body.getAs<mlir::StringAttr>("type") : mlir::StringAttr{};
    auto result = llvm::dyn_cast<mlir::emitc::OpaqueType>(call->getResult(0).getType());
    if (!binder || !captures || !type || !result || binder.getValue() != call.getCallee() ||
        result.getValue() != type.getValue() || captures.size() != call->getNumOperands() ||
        function.getNumArguments() < captures.size()) {
        return invalid();
    }
    for (auto [capture, argument] :
         llvm::zip(call->getOperands(), function.getArguments().take_front(captures.size()))) {
        if (capture.getType() != argument.getType()) { return invalid(); }
    }
    return function;
}

namespace {

bool bounded(mlir::emitc::FuncOp function, llvm::DenseSet<mlir::Operation *> & active,
             unsigned & remaining) {
    if (active.size() >= 8 || !active.insert(function).second) { return false; }
    const llvm::scope_exit leave([&] { active.erase(function); });
    const auto result = function.walk([&](mlir::Operation * op) {
        if (remaining == 0) { return mlir::WalkResult::interrupt(); }
        --remaining;
        auto call = llvm::dyn_cast<mlir::emitc::CallOpaqueOp>(op);
        if (!call || !call->hasAttr("ctnative.callable_create")) {
            return mlir::WalkResult::advance();
        }
        auto target = callableCreationTarget(call);
        if (mlir::failed(target) || !bounded(*target, active, remaining)) {
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    return !result.wasInterrupted();
}

} // namespace

bool boundedCallableExpansion(mlir::emitc::FuncOp function) {
    // Count repeated child bodies each time: sharing in the IR can duplicate
    // code when printed. Recursive or large trees retain the existing binder.
    llvm::DenseSet<mlir::Operation *> active;
    unsigned remaining = 4096;
    return bounded(function, active, remaining);
}

} // namespace ctcompile::cpp
