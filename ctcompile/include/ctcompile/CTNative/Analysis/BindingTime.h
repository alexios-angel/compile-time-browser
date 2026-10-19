//===- BindingTime.h - Binding-time analysis --------------------*- C++ -*-===//
//
// Binding-time analysis: which values and operations are known at compile
// time (static) and which only at run time (dynamic). This is the partial
// evaluator's staging input.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include <memory>

namespace ctcompile::ctnative {
/// Whether a value is known at compile time (Static) or only at run time
/// (Dynamic). Unknown means the analysis recorded no fact for it.
enum class BindingTime {
    Unknown,
    Static,
    Dynamic
};
/// The lowercase spelling of `time`, for diagnostics.
llvm::StringRef bindingTimeName(BindingTime time);

/// Computes binding times for a whole module.
///
/// Facts are computed from the original IR, never from input annotations. An
/// operation's static eligibility includes control and heap effects at that
/// operation's position; a later dynamic write does not retroactively
/// invalidate a static prefix. These staging facts do not replace the partial
/// evaluator's own checks of the closed environment, callables, ownership and
/// exact execution.
class BindingTimeAnalysis {
public:
    /// Prepares heap facts with the caller's callback, then computes binding
    /// times. The callback must rederive its facts from the IR, never trust
    /// input annotations.
    BindingTimeAnalysis(mlir::ModuleOp module,
                        llvm::function_ref<void(mlir::ModuleOp)> prepareHeapFacts);
    ~BindingTimeAnalysis();
    /// The binding time of `value`, or Unknown when no fact was recorded.
    BindingTime get(mlir::Value value) const;
    bool isStatic(mlir::Value value) const;
    /// Whether `operation` is eligible to run at compile time.
    bool isStatic(mlir::Operation * operation) const;
    /// The statically known value of argument `index` of `function`, or null.
    mlir::Attribute knownArgument(ctjs::FuncOp function, unsigned index) const;
    /// Why `operation` is or is not static, for diagnostics.
    llvm::StringRef reason(mlir::Operation * operation) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ctcompile::ctnative
