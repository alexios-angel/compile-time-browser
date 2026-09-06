#include "SourceNames.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::cpp {
namespace {

std::string sourceHint(mlir::Value value) {
    std::string hint;
    bool conflicting = false;
    value.getLoc()->walk([&](mlir::Location loc) -> mlir::WalkResult {
        auto fused = llvm::dyn_cast<mlir::FusedLoc>(loc);
        if (!fused) { return mlir::WalkResult::advance(); }
        auto metadata = llvm::dyn_cast_or_null<mlir::DictionaryAttr>(fused.getMetadata());
        if (!metadata) { return mlir::WalkResult::advance(); }
        auto name = metadata.getAs<mlir::StringAttr>("ctnative.source_name");
        if (auto result = llvm::dyn_cast<mlir::OpResult>(value)) {
            if (auto array = metadata.getAs<mlir::ArrayAttr>("ctnative.source_names")) {
                if (result.getResultNumber() < array.size()) {
                    name = llvm::dyn_cast<mlir::StringAttr>(array[result.getResultNumber()]);
                }
            }
        }
        if (!name || name.empty()) { return mlir::WalkResult::advance(); }
        if (!hint.empty() && hint != name.getValue()) { conflicting = true; }
        hint = name.str();
        return mlir::WalkResult::advance();
    });
    return conflicting ? std::string{} : hint;
}

} // namespace

llvm::DenseMap<mlir::Value, std::string> inferSourceNames(mlir::Operation * function) {
    llvm::DenseMap<mlir::Value, std::string> hints;
    llvm::DenseSet<mlir::Value> roots;
    llvm::SmallVector<mlir::Value> work;
    const auto seed = [&](mlir::Value value) {
        std::string hint = sourceHint(value);
        if (hint.empty()) { return; }
        hints[value] = std::move(hint);
        roots.insert(value);
        work.push_back(value);
    };
    function->walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * op) {
        for (mlir::Region & region : op->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) { seed(argument); }
            }
        }
        for (mlir::Value result : op->getResults()) { seed(result); }
    });

    // Copies, loads and join storage carry the binding in both directions.
    // Computations carry a destination's name backwards to their intermediate
    // producers. They never rename a different source binding or a parameter.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> edges;
    const auto connect = [&](mlir::Value a, mlir::Value b) {
        edges[a].push_back(b);
        edges[b].push_back(a);
    };
    function->walk([&](mlir::Operation * op) {
        if (llvm::isa<mlir::emitc::AssignOp>(op)) {
            connect(op->getOperand(0), op->getOperand(1));
        } else if (llvm::isa<mlir::emitc::LoadOp>(op)) {
            connect(op->getResult(0), op->getOperand(0));
        } else if (llvm::isa<mlir::emitc::ConditionalOp>(op)) {
            connect(op->getResult(0), op->getOperand(1));
            connect(op->getResult(0), op->getOperand(2));
        } else if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(op)) {
            for (auto [argument, incoming] :
                 llvm::zip(branch.getDest()->getArguments(), branch.getDestOperands())) {
                connect(argument, incoming);
            }
        }
    });
    const auto propagate = [&] {
        for (std::size_t cursor = 0; cursor < work.size(); ++cursor) {
            const mlir::Value from = work[cursor];
            const std::string hint = hints.lookup(from);
            for (mlir::Value to : edges.lookup(from)) {
                if (roots.contains(to)) { continue; }
                auto [position, inserted] = hints.try_emplace(to, hint);
                if (inserted) {
                    work.push_back(to);
                } else if (!position->second.empty() && position->second != hint) {
                    // Ambiguous merged names stay anonymous; the conflict also
                    // reaches any names previously inferred from that merge.
                    position->second.clear();
                    work.push_back(to);
                }
            }
        }
    };
    propagate();
    function->walk([&](mlir::Operation * op) {
        if (op->getNumResults() != 1 || op->getNumRegions() != 0 ||
            llvm::isa<mlir::emitc::LoadOp, mlir::emitc::ConditionalOp>(op)) {
            return;
        }
        for (mlir::Value operand : op->getOperands()) {
            // Shared constants describe many expressions; retain their own
            // explicit binding, if any, rather than invent a destination name.
            if (llvm::isa<mlir::BlockArgument>(operand) ||
                llvm::isa_and_nonnull<mlir::emitc::ConstantOp, mlir::emitc::LiteralOp,
                                      mlir::emitc::GetGlobalOp>(operand.getDefiningOp())) {
                continue;
            }
            edges[op->getResult(0)].push_back(operand);
        }
    });
    // Revisit existing facts after adding computation edges. The lattice has
    // only unknown -> one name -> conflicting, so loops terminate naturally.
    propagate();
    return hints;
}

} // namespace ctcompile::cpp
