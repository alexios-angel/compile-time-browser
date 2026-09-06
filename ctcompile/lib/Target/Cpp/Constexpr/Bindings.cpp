#include "Bindings.h"
#include "../Const/Bindings.h"

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::cpp {

void ConstexprBindings::prepare(mlir::Operation * function, const ConstBindings & immutable) {
    values.clear();
    const auto module = function->getParentOfType<mlir::ModuleOp>();
    if (!module || !module->hasAttrOfType<mlir::UnitAttr>("ctnative.constexpr_bindings")) {
        return;
    }
    llvm::SmallVector<mlir::Operation *> work;
    llvm::DenseSet<mlir::Operation *> queued;
    const auto enqueue = [&](mlir::Operation * op) {
        if (op->getNumResults() == 1 && immutable.qualifies(op->getResult(0)) &&
            constexpr_detail::scalarType(op->getResult(0).getType()) && queued.insert(op).second) {
            work.push_back(op);
        }
    };
    function->walk(enqueue);
    // Monotone forward DFA: unknown/dynamic inputs never manufacture a static
    // value. Each proved result becomes static once and wakes dependent users.
    // Exhaustion keeps the remaining declarations at their ordinary const tier.
    constexpr unsigned maxSteps = 100000;
    for (std::size_t cursor = 0; cursor < work.size() && cursor < maxSteps; ++cursor) {
        auto * op = work[cursor];
        queued.erase(op);
        if (values.contains(op->getResult(0))) { continue; }
        if (auto known = constexpr_detail::evaluate(op, values)) {
            values.try_emplace(op->getResult(0), known);
            for (mlir::Operation * user : op->getResult(0).getUsers()) { enqueue(user); }
        }
    }
}

} // namespace ctcompile::cpp
