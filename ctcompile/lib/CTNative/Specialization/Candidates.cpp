//===- Candidates.cpp - Specialization candidates -------------------------===//
//
// Implements the helpers declared in Candidates.h.
//
//===----------------------------------------------------------------------===//

#include "Candidates.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace ctcompile::ctnative::specialization {
namespace {
/// Whether `function` can reach itself through direct calls.
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

/// Whether `attr` may be bound as a constant. Strings longer than 64 KiB are
/// not, to keep cloned variants small.
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
    if (entry.getNumArguments() < ctjs::implicit_arguments || !entry.hasNoPredecessors() ||
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
    // A callee produced by create_closure is handled by the closure lifter,
    // which proves its target and signature. Only specialize calls through a
    // global or constant callee, so that proof is not invalidated.
    auto callee = call.getCalleeValue().getDefiningOp();
    if (!llvm::isa_and_nonnull<ctjs::LoadGlobalOp, ctjs::ConstantOp>(callee)) { return known; }
    for (unsigned i = ctjs::implicit_arguments; i < known.size(); ++i) {
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
ctjs::FuncOp cloneWithBindings(ctjs::FuncOp source, llvm::ArrayRef<mlir::Attribute> bindings) {
    auto clone = llvm::cast<ctjs::FuncOp>(source->clone());
    clone.walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
    auto & entry = clone.getBody().front();
    mlir::OpBuilder at(&entry, entry.begin());
    for (auto [i, literal] : llvm::enumerate(bindings)) {
        if (!literal) { continue; }
        auto constant = ctjs::ConstantOp::create(at, clone.getLoc(), literal);
        entry.getArgument(static_cast<unsigned>(i)).replaceAllUsesWith(constant.getResult());
    }
    return clone;
}

} // namespace ctcompile::ctnative::specialization
