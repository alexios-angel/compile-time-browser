#pragma once

#include "mlir/IR/Value.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringSet.h"

#include <string>

namespace mlir {
class Operation;
} // namespace mlir

namespace ctcompile::cpp {

// Presentation only. Keep this separate from the emitter's valueMapper, which
// also records declarations already emitted and caches deferred expressions.
class SourceNames {
public:
    void prepare(mlir::Operation * function, llvm::function_ref<bool(mlir::Value)> materialized,
                 llvm::ArrayRef<std::string> parameters = {});
    bool enabled() const { return active; }
    void finish() { active = false; }
    llvm::StringRef get(mlir::Value value, llvm::StringRef fallback = "v");

private:
    bool active = false;
    unsigned nextTemporary = 0;
    llvm::DenseMap<mlir::Value, std::string> names;
    llvm::StringSet<> unavailable;
    mlir::Operation * reservedRoot = nullptr;
    llvm::StringSet<> moduleIdentifiers;
};

llvm::DenseMap<mlir::Value, std::string> inferSourceNames(mlir::Operation * function);
std::string localIdentifier(llvm::StringRef source);
void reserveCppIdentifiers(mlir::Operation * function, llvm::StringSet<> & names);

} // namespace ctcompile::cpp
