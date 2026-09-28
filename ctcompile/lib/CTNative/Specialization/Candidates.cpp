#include "Candidates.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace ctcompile::ctnative::specialization {
namespace {
bool recursive(ctjs::FuncOp function) {
    llvm::SmallVector<ctjs::FuncOp> pending{function};
    llvm::SmallPtrSet<mlir::Operation *, 32> visited;
    bool found = false;
    while (!pending.empty() && !found) {
        auto next = pending.pop_back_val();
        if (!visited.insert(next).second) { continue; }
        next.getBody().walk([&](ctjs::CallDirectOp call) {
            auto target = call.getTarget();
            found |= target == function;
            if (target && !visited.contains(target)) { pending.push_back(target); }
        });
    }
    return found;
}

bool primitive(mlir::Attribute attr) {
    auto text = llvm::dyn_cast<ctjs::StringAttr>(attr);
    return text ? text.getValue().size() <= 65536 : ctjs::isPrimitiveAttr(attr);
}
} // namespace

std::string refusal(ctjs::FuncOp function, mlir::ModuleOp module) {
    if (function.getBody().empty() || function->getParentOp() != module || !function.isPrivate()) {
        return "requires a private module function with a visible body";
    }
    auto & entry = function.getBody().front();
    if (entry.getNumArguments() < 3 || !entry.hasNoPredecessors() ||
        function.getUpvalueCount() != 0) {
        return "requires a capture-free function with a single entry";
    }
    if (!implicitArgumentsRooted(entry)) { return "function observes implicit call state"; }
    if (usesEnvironment(function)) {
        return "closure or cell environments need separate specialization";
    }
    if (recursive(function)) { return "recursive call graph remains generic"; }
    return closedCallableProblem(function, module);
}

llvm::SmallVector<mlir::Attribute> staticArguments(ctjs::CallDirectOp call, ctjs::FuncOp function) {
    auto & entry = function.getBody().front();
    llvm::SmallVector<mlir::Attribute> known(entry.getNumArguments());
    if (call->getNumOperands() != known.size()) { return known; }
    // An immediate closure participates in the closure lifter's target/signature
    // proof. Do not redirect it to a different code identity in this slice.
    auto callee = call.getCalleeValue().getDefiningOp();
    if (!llvm::isa_and_nonnull<ctjs::LoadGlobalOp, ctjs::ConstantOp>(callee)) { return known; }
    for (unsigned i = 3; i < known.size(); ++i) {
        const bool used = llvm::any_of(entry.getArgument(i).getUsers(), [](mlir::Operation * use) {
            return !llvm::isa<ctjs::RootOp>(use);
        });
        auto constant = call->getOperand(i).getDefiningOp<ctjs::ConstantOp>();
        if (used && constant && primitive(constant.getValue())) { known[i] = constant.getValue(); }
    }
    return known;
}

unsigned bodySize(ctjs::FuncOp function) {
    unsigned count = 0;
    function.getBody().walk([&](mlir::Operation *) { ++count; });
    return count;
}
} // namespace ctcompile::ctnative::specialization
