//===- Driver.h - Supercompilation driver -----------------------*- C++ -*-===//
//
// --ctnative-supercompile specialises a self-recursive function (a "kernel")
// on the constant arguments its calls pass. Driving a call with a set of
// constant bindings (a "configuration") clones the kernel with those constants
// bound, simplifies the clone symbolically, then drives its recursive calls
// in turn. A configuration equal to an earlier one folds back to that
// earlier residual function. The whistle, a homeomorphic-embedding test
// against the configurations on the current path, stops unbounded unfolding
// by generalizing (forgetting) the bindings that differ.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Symbolic/Facts.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative::supercompilation {

/// One constant per kernel entry argument; a null attribute marks a dynamic
/// argument.
using Bindings = llvm::SmallVector<mlir::Attribute>;

/// Budgets for one driver: configurations, work steps, residual operations,
/// and allowed code growth.
struct Limits {
    unsigned contexts, steps, residualOps, growth;
};
/// Counters reported in the pass summary.
struct Statistics {
    unsigned contexts = 0, folds = 0, whistles = 0, expressions = 0, branches = 0;
    unsigned generalizations = 0, residualOps = 0, steps = 0;
};

/// The termination whistle: whether `ancestor` homeomorphically embeds in
/// `next` under a finite abstraction of each binding (its kind, not its
/// value). Two numeric leaves can therefore embed without being equal numbers;
/// this relation is used only to stop unfolding, never to fold.
bool embeds(llvm::ArrayRef<mlir::Attribute> ancestor, llvm::ArrayRef<mlir::Attribute> next);
/// The bindings equal in `ancestor` and `next`; every position that differs
/// becomes dynamic (a runtime parameter of a freshly driven residual body).
Bindings commonBindings(llvm::ArrayRef<mlir::Attribute> ancestor,
                        llvm::ArrayRef<mlir::Attribute> next);
/// Why `function` cannot be supercompiled, or empty if it can. A kernel is a
/// private, indexed, capture-free, single-block function whose only calls are
/// ordinary self calls and whose body uses only primitive operations.
std::string refusal(ctjs::FuncOp function, mlir::ModuleOp module);

/// One transactional process graph per source kernel. Each residual function
/// is created before its recursive calls are driven, so a recursive call can
/// fold back to an ancestor's residual without a recursive C++ evaluator.
/// Residuals stay in a private draft module until commit().
class Driver {
public:
    Driver(mlir::ModuleOp module, ctjs::FuncOp source, Limits limits);
    /// Drives each root call. Returns false, with reason() set, if a budget
    /// ran out or nothing was simplified; the module is untouched either way.
    bool build(llvm::ArrayRef<ctjs::CallDirectOp> roots);
    /// Moves the residual functions into the module and redirects the roots.
    void commit();
    const Statistics & statistics() const { return stats; }
    const std::string & reason() const { return problem; }

private:
    struct Configuration {
        Bindings bindings;
        ctjs::FuncOp residual;
    };
    mlir::ModuleOp module;
    ctjs::FuncOp source;
    Limits limits;
    WorkBudget budget;
    mlir::OwningOpRef<mlir::ModuleOp> drafts;
    llvm::SmallVector<Configuration> configurations;
    llvm::SmallVector<std::pair<ctjs::CallDirectOp, ctjs::FuncOp>> redirects;
    Statistics stats;
    std::string problem;
    unsigned nextName = 0;
    bool spend();
    ctjs::FuncOp drive(Bindings bindings, llvm::SmallVector<unsigned> history);
};

} // namespace ctcompile::ctnative::supercompilation
