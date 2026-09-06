#pragma once

#include "../Symbolic/Facts.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative::supercompilation {

using Bindings = llvm::SmallVector<mlir::Attribute>;

struct Limits {
    unsigned contexts, steps, residualOps, growth;
};
struct Statistics {
    unsigned contexts = 0, folds = 0, whistles = 0, expressions = 0, branches = 0;
    unsigned generalizations = 0, residualOps = 0, steps = 0;
};

// This finite abstraction is used only by the termination whistle. In
// particular, two numeric leaves can embed without denoting equal numbers.
bool embeds(llvm::ArrayRef<mlir::Attribute> ancestor, llvm::ArrayRef<mlir::Attribute> next);
// The common exact bindings describe both entries; every differing position
// remains an ordered runtime parameter in a freshly driven residual body.
Bindings commonBindings(llvm::ArrayRef<mlir::Attribute> ancestor,
                        llvm::ArrayRef<mlir::Attribute> next);
std::string refusal(ctjs::FuncOp function, mlir::ModuleOp module);

// One transactional process graph per source kernel. Promises exist before
// driving, allowing genuine recursive folding without a recursive C++ evaluator.
class Driver {
public:
    Driver(mlir::ModuleOp module, ctjs::FuncOp source, Limits limits);
    bool build(llvm::ArrayRef<ctjs::CallDirectOp> roots);
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
    symbolic::Budget budget;
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
