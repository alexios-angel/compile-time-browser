//===- DropUncompiled.cpp - Drop refused functions ------------------------===//
//
// The --ctjs-drop-uncompiled pass: erases every ctjs.func the EmitC lowering
// refused (marked ctjs.not_lowered), so the rest of the module can be
// translated to C++.
//
// A module still holding ctjs operations cannot be translated at all, since
// mlir-translate does not know the dialect, so one refusal would otherwise
// produce no output. Dropping a refused function loses nothing: the runtime
// calls a compiled body only when function_proto::aot_entry is set, and a
// function that was never emitted never sets one, so it runs in the
// interpreter.
//
// It is a separate pass so the lowering does not destroy the refusals before
// they can be read: refusals.mlir checks them one by one. Running this pass
// means "I want a translation unit now", a different question from "what can
// you compile".
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Transforms/Passes.h"

#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSDROPUNCOMPILED
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

struct CTJSDropUncompiledPass : impl::CTJSDropUncompiledBase<CTJSDropUncompiledPass> {
    using CTJSDropUncompiledBase::CTJSDropUncompiledBase;

    void runOnOperation() override {
        llvm::SmallVector<mlir::Operation *> refused;
        getOperation().walk([&](mlir::Operation * op) {
            if (op->getName().getStringRef() == "ctjs.func" && op->hasAttr(notLoweredMarker)) {
                refused.push_back(op);
            }
        });
        // Collected first, because erasing during a walk invalidates the
        // iterator doing the walking.
        for (mlir::Operation * op : refused) { op->erase(); }

        // A remaining ctjs.func without a reason means the lowering never ran,
        // the same pass-order mistake --emitc-eliminate-block-arguments
        // refuses. Continuing would quietly delete every function in the
        // module.
        if (mlir::WalkResult left = getOperation().walk([](mlir::Operation * op) {
                return op->getName().getStringRef() == "ctjs.func" ? mlir::WalkResult::interrupt()
                                                                   : mlir::WalkResult::advance();
            });
            left.wasInterrupted()) {
            getOperation().emitError()
                << "--ctjs-drop-uncompiled found a CTJS function with no ctjs.not_lowered reason "
                   "on it, which is what an un-run lowering looks like. It must run AFTER "
                   "--ctjs-lower-to-emitc, or it would delete functions nobody refused";
            signalPassFailure();
        }
    }
};

} // namespace

} // namespace ctcompile::ctjs
