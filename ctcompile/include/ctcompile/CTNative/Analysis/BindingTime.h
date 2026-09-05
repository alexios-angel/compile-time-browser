#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include <memory>

namespace ctcompile::ctnative {
enum class BindingTime {
    Unknown,
    Static,
    Dynamic
};
llvm::StringRef bindingTimeName(BindingTime time);

// Computed from original IR, never from input annotations. Static operation
// eligibility includes control and heap effects at that operation's position;
// a later dynamic write does not retroactively invalidate a static prefix.
// These staging facts do not replace the partial evaluator's independent
// closed-environment, callable, ownership and exact-execution checks.
class BindingTimeAnalysis {
public:
    explicit BindingTimeAnalysis(mlir::ModuleOp module);
    ~BindingTimeAnalysis();
    BindingTime get(mlir::Value value) const;
    bool isStatic(mlir::Value value) const;
    bool isStatic(mlir::Operation * operation) const;
    mlir::Attribute knownArgument(ctjs::FuncOp function, unsigned index) const;
    llvm::StringRef reason(mlir::Operation * operation) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ctcompile::ctnative
