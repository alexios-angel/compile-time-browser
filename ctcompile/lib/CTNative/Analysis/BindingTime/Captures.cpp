#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"

namespace ctcompile::ctnative {

bool constructsClosures(ctjs::FuncOp function) {
    bool found = false;
    if (function) {
        function.getBody().walk([&](ctjs::CreateClosureOp) { found = true; });
    }
    return found;
}

ctjs::FuncOp immutableClosureTarget(ctjs::CreateClosureOp made, mlir::ModuleOp module) {
    if (made.getFunction() < 0) { return {}; }
    ctjs::FuncOp target;
    unsigned matches = 0;
    module.walk([&](ctjs::FuncOp function) {
        if (functionIndex(function) == static_cast<unsigned>(made.getFunction())) {
            target = function;
            ++matches;
        }
    });
    unsigned creations = 0;
    module.walk([&](ctjs::CreateClosureOp other) {
        creations += other.getFunction() == made.getFunction();
    });
    if (creations != 1 || matches != 1 || !target || target.getBody().empty() ||
        target.getBody().front().getNumArguments() < 3 ||
        static_cast<size_t>(target.getUpvalueCount()) != made.getUpvalues().size()) {
        return {};
    }
    auto parent = made->getParentOfType<ctjs::FuncOp>();
    if (!parent || parent.getBody().empty() || parent.getBody().front().getNumArguments() < 3 ||
        made.getEnclosingClosure() != parent.getBody().front().getArgument(2)) {
        return {};
    }
    if (auto indices = made.getEnclosingIndicesAttr();
        indices && llvm::any_of(indices.asArrayRef(), [](int32_t index) { return index >= 0; })) {
        return {};
    }
    auto & entry = target.getBody().front();
    for (unsigned i = 0; i < 2; ++i) {
        for (mlir::Operation * use : entry.getArgument(i).getUsers()) {
            if (!llvm::isa<ctjs::RootOp>(use)) { return {}; }
        }
    }
    for (mlir::OpOperand & use : entry.getArgument(2).getUses()) {
        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
        auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
        if (!read || use.getOperandNumber() != 0 || read.getIndex() < 0 ||
            static_cast<size_t>(read.getIndex()) >= made.getUpvalues().size()) {
            return {};
        }
    }
    bool changesBinding = false;
    target.getBody().walk([&](mlir::Operation * op) {
        changesBinding |= llvm::isa<ctjs::StoreUpvalueOp, ctjs::CreateClosureOp>(op);
    });
    return changesBinding ? ctjs::FuncOp{} : target;
}

ctjs::CellSetOp captureCellWrite(ctjs::CreateCellOp cell) {
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
            write && use.getOperandNumber() == 0) {
            return write;
        }
    }
    return {};
}

bool immutableCaptureCell(ctjs::CreateCellOp cell, mlir::ModuleOp module) {
    unsigned writes = 0;
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
        if (llvm::isa<ctjs::CellGetOp>(use.getOwner()) && use.getOperandNumber() == 0) { continue; }
        if (llvm::isa<ctjs::CellSetOp>(use.getOwner()) && use.getOperandNumber() == 0) {
            if (++writes > 1) { return false; }
            continue;
        }
        auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
        if (!closure || use.getOperandNumber() < 2 || !immutableClosureTarget(closure, module)) {
            return false;
        }
    }
    return true;
}

} // namespace ctcompile::ctnative
