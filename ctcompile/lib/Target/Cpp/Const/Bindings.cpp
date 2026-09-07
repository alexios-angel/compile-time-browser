#include "Bindings.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::cpp {
namespace ec = mlir::emitc;

void ConstBindings::prepare(mlir::Operation * function) {
    mutableBindings.clear();
    const auto module = function->getParentOfType<mlir::ModuleOp>();
    active = module && module->hasAttrOfType<mlir::UnitAttr>("ctnative.const_bindings");
    if (!active) { return; }

    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> owners;
    llvm::SmallVector<mlir::Value> work;
    const auto mark = [&](mlir::Value value) {
        if (mutableBindings.insert(value).second) { work.push_back(value); }
    };
    function->walk([&](mlir::Operation * op) {
        for (mlir::OpOperand & operand : op->getOpOperands()) {
            if (!readsBinding(operand)) { mark(operand.get()); }
        }
        if (auto expression = llvm::dyn_cast<ec::ExpressionOp>(op)) {
            // Expression arguments print as their captured operand, without
            // a C++ copy. Mutating a capture therefore mutates that binding.
            for (auto [argument, captured] : llvm::zip(
                     expression->getRegion(0).front().getArguments(), expression->getOperands())) {
                owners[argument].push_back(captured);
                // An inlined C++ expression may itself be a glvalue (a
                // conditional selecting a captured binding, for example).
                owners[expression->getResult(0)].push_back(captured);
            }
        }
        if (llvm::isa<ec::MemberOp, ec::SubscriptOp>(op)) {
            mlir::Value base = op->getOperand(0);
            mlir::Type type = base.getType();
            if (auto place = llvm::dyn_cast<ec::LValueType>(type)) { type = place.getValueType(); }
            // Indexing a pointer changes its pointee, not the pointer binding.
            if (!llvm::isa<ec::PointerType>(type)) { owners[op->getResult(0)].push_back(base); }
        }
    });
    // Finite lattice: a binding moves from potentially const to mutable once.
    // Each alias edge is processed once, including shared and cyclic views.
    for (std::size_t cursor = 0; cursor < work.size(); ++cursor) {
        for (mlir::Value owner : owners.lookup(work[cursor])) { mark(owner); }
    }
}

bool ConstBindings::qualifies(mlir::Value value) const {
    if (!active || !supportsConstBinding(value.getType()) || mutableBindings.contains(value)) {
        return false;
    }
    auto result = llvm::dyn_cast<mlir::OpResult>(value);
    if (!result) { return true; } // Function parameters and copied catch payloads.
    mlir::Operation * op = result.getOwner();
    if (op->getNumResults() != 1 || llvm::isa<ec::VariableOp>(op)) { return false; }
    if (auto constant = llvm::dyn_cast<ec::ConstantOp>(op)) {
        if (auto opaque = llvm::dyn_cast<ec::OpaqueAttr>(constant.getValue())) {
            if (opaque.getValue().empty()) { return false; }
        }
    }
    return true;
}

} // namespace ctcompile::cpp
