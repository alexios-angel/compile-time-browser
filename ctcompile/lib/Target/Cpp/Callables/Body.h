#pragma once

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/SmallVector.h"

#include <string>

namespace ctcompile::cpp {

struct CallableBody {
    std::string type;
    std::string binder;
    std::string name;
    llvm::SmallVector<std::string> parameters;
    unsigned captures = 0;
    bool inlineBody = false;
    bool retainFunction = true;
    bool inlineCreation = false;
    bool retainBinder = true;
};

// Presentation of an already admitted callable. Retain a lifted function if
// another emitted use names it, or if a const capture cannot host its body.
mlir::FailureOr<CallableBody> callableBodyPlan(mlir::emitc::FuncOp function);

// Only a marked, signature-checked creation can become a lambda initializer.
mlir::FailureOr<mlir::emitc::FuncOp> callableCreationTarget(mlir::emitc::CallOpaqueOp call);

bool callableFunctionReferenced(mlir::emitc::FuncOp function);
bool callableBinderNeeded(mlir::emitc::FuncOp function, llvm::StringRef binder);
bool boundedCallableExpansion(mlir::emitc::FuncOp function);

} // namespace ctcompile::cpp
