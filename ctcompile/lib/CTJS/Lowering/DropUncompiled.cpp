// WHAT THE BACKEND COULD NOT COMPILE IS LEFT BEHIND, AND THEN REMOVED.
//
// The lowering refuses a function it does not understand and records why on it,
// which is the work list. But a module still holding CTJS operations cannot be
// translated to C++ at all - mlir-translate has never heard of the dialect -
// so a file where anything is refused produces nothing rather than the parts
// that did compile.
//
// THAT IS WHAT A REAL COMPILER DOES WITH THIS: emit the functions it can and
// leave the rest to the interpreter. Nothing is lost by dropping them, because
// the runtime only calls a compiled body when function_proto::aot_entry is set,
// and a function that was never emitted never sets one.
//
// IT IS A SEPARATE PASS FOR ONE REASON. Erasing inside the lowering would
// destroy the refusals before anybody could read them, and the refusals are the
// point - refusals.mlir asserts them one by one and the work list is how the
// next operation gets chosen. Running this pass is the caller saying "I want a
// translation unit now", which is a different question from "what can you
// compile".
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
            if (op->getName().getStringRef() == "ctjs.func" && op->hasAttr("ctjs.not_lowered")) {
                refused.push_back(op);
            }
        });
        // COLLECTED FIRST, because erasing during a walk invalidates the
        // iterator doing the walking.
        for (mlir::Operation * op : refused) { op->erase(); }

        // AND ANY CTJS FUNCTION WITHOUT A REASON IS NOT THIS PASS'S BUSINESS.
        // It means the lowering never ran, which is the pass-order mistake
        // --emitc-eliminate-block-arguments refuses for the same reason: this
        // pass would quietly delete every function in the module.
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
