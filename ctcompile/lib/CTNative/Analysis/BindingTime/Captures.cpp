//===- Captures.cpp - Immutable closure-capture queries ---------*- C++ -*-===//
//
// Decides when a closure's target function and the cells it captures are
// fixed for the whole program, so the binding-time analysis may treat values
// read through them as known at compile time.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"

namespace ctcompile::ctnative {

/// Whether \p function's body contains a ctjs.create_closure.
bool constructsClosures(ctjs::FuncOp function) {
    return function &&
           function.getBody()
               .walk([](ctjs::CreateClosureOp) { return mlir::WalkResult::interrupt(); })
               .wasInterrupted();
}

/// Returns the function \p made creates when that closure's environment is
/// immutable, or null. Entry-block arguments 0-2 are the implicit receiver,
/// new.target and callee (see ctjs.call_direct); the callee (argument 2) is
/// what load_upvalue reads captures through. A target qualifies only if it is
/// created at exactly this one site, does not use its receiver or new.target,
/// reads captures only by in-range load_upvalue, and never writes a capture or
/// creates a closure itself.
ctjs::FuncOp immutableClosureTarget(ctjs::CreateClosureOp made, mlir::ModuleOp module) {
    if (made.getFunction() < 0) { return {}; }
    auto target = ctjs::uniqueFunction(module, static_cast<unsigned>(made.getFunction()));
    unsigned creations = 0;
    module.walk([&](ctjs::CreateClosureOp other) {
        creations += other.getFunction() == made.getFunction();
    });
    if (creations != 1 || !target || target.getBody().empty() ||
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

/// Returns a ctjs.cell_set that writes \p cell, or null if none does.
ctjs::CellSetOp captureCellWrite(ctjs::CreateCellOp cell) {
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
            write && use.getOperandNumber() == 0) {
            return write;
        }
    }
    return {};
}

/// Whether \p cell is written at most once and is otherwise only read or
/// captured by closures whose targets are immutable.
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
