// Admission/Functions.cpp - native lowering implementation.
#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

bool admission::function(ctjs::FuncOp fn) {
    mlir::Block & entry = fn.getBody().front();
    // THE RECEIVER IS A PARAMETER when the lift said so, and %arg0 is then
    // the one implicit argument that HAS a carrier: the generated class of
    // the shape every call site passes. The lift proved every use of it is
    // a constant-key access or another lifted method call; this asks the
    // same question of the IR that came out, because the lift's proof was
    // made before its own rewrite ran and a later refusal - the callee of
    // a `this.other()` that was not lifted - can have opened it since.
    const bool carriesReceiver = fn->hasAttr("ctnative.receiver");
    if (carriesReceiver && !isClosedObject(entry.getArgument(0))) {
        return refuse("its `this` is no longer a closed-shape receiver - " +
                      whyOpenReceiver(entry.getArgument(0)));
    }
    // THE THREE IMPLICIT ARGUMENTS - receiver, new.target, callee - have no
    // native carrier and must be unused.
    for (unsigned i = carriesReceiver ? 1 : 0; i < 3 && i < entry.getNumArguments(); ++i) {
        for (mlir::Operation * user : entry.getArgument(i).getUsers()) {
            // A closure that lowers to nothing does not READ these: a
            // declaration's pair is erased with its store, and a LIFTED
            // one's `$enclosing_closure` and `$enclosing_this` operands go
            // with the ctjs.call_direct that replaced its calls.
            // ResolveGlobals makes the same exemption on the same operand
            // (own_closure_escapes), for the same reason.
            if (closureLowersToNothing(user)) { continue; }
            if (llvm::isa<ctjs::CreateClosureOp>(user) && !environmentTarget(user).empty()) {
                continue; // environment construction drops the two enclosing-frame operands
            }
            // AND A CLOSURE THIS TIER CANNOT CARRY IS NOT THE SAME THING
            // as a function that reads its own closure. Both are uses of
            // %arg2, and this check runs before the body walk, so every
            // one of the 2,426 `uses its own closure` refusals measured
            // over the corpora was reported with the message for the wrong
            // one - on functions whose only crime is declaring a nested
            // function. The reason the lift wrote onto the closure is the
            // one a reader can act on. (A genuine reader of %arg2 - a
            // named function expression calling itself, a ctjs.load_upvalue
            // in a function nothing lifted - still gets the old sentence,
            // which is what refusal-corpus-shapes.mlir pins.)
            if (llvm::isa<ctjs::CreateClosureOp>(user)) { return refuse(closureRefusal(user)); }
            return refuse(i == 0   ? "uses `this`"
                          : i == 1 ? "uses new.target"
                                   : "uses its own closure");
        }
    }
    // PHASE 59 SLICE 1: THE LEADING PARAMETERS ARE CAPTURES, and the
    // diagnostic has to say so or it names a parameter the JavaScript does
    // not have. `ctnative.captures` is written by the lift; it is 0 on
    // every function that was not lifted, which is the shape below
    // unchanged.
    const auto capturesAttr = fn->getAttrOfType<mlir::IntegerAttr>("ctnative.captures");
    const unsigned captures = capturesAttr ? static_cast<unsigned>(capturesAttr.getInt()) : 0u;
    for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
        // THE OBJECT PARAMETERS, ASKED THE RECEIVER'S QUESTION. Their
        // carrier is the generated class one indirection away, so
        // `carrierOf` has no row for them and the loop below would refuse
        // every one; what has to hold is that the shape is still closed.
        if (isObjectArg(fn.getOperation(), i)) {
            if (!isClosedObject(entry.getArgument(i))) {
                return refuse("parameter " + std::to_string(i - 3 - captures) +
                              " is no longer a closed-shape object - " +
                              whyOpenReceiver(entry.getArgument(i)));
            }
            continue;
        }
        const mlir::Type t = typeOf(entry.getArgument(i));
        const bool isCapture = i - 3 < captures;
        // PHASE 59 SLICE 2 STEP 2: A SHARED CAPTURE SAYS SO. Its carrier
        // is the one the POINTER points at, and the refusal below is the
        // second condition of the carried-cell rule asked at the callee -
        // the same question `op` asks of the box in the owning frame, in
        // the function that reads it through the pointer.
        const std::string kind = !isCapture                        ? "parameter "
                                 : isCellArg(fn.getOperation(), i) ? "shared capture "
                                                                   : "capture ";
        const std::string which = kind + std::to_string(isCapture ? i - 3 : i - 3 - captures);
        if (carrierOf(t) == carrier::none) {
            // Distinguish an unknown parameter from a proved type this
            // tier cannot represent, such as an optional string. The
            // latter needs a carrier even when every caller is known.
            if (t == nullptr || llvm::isa<BoxedType>(t)) {
                return refuse(which + " is " + printed(t) +
                              " - no caller proves it (a closed-world call is Phase 62½-A)");
            }
            return refuse(which + " is " + printed(t) + ", which has no native carrier yet");
        }
    }
    bool ok = true;
    if (fn->hasAttr(kNativeStoredCallable)) {
        const auto supported = [&](mlir::Value value) {
            const auto c = carrierOf(typeOf(value));
            return isScalarCarrier(c) || c == carrier::string || c == carrier::map ||
                   isObjectCarrier(c);
        };
        for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
            if (!supported(entry.getArgument(i))) {
                return refuse("stored callable parameter has no supported concrete signature");
            }
        }
        fn.getBody().walk([&](ctjs::ReturnOp ret) { ok &= supported(ret.getValue()); });
        if (!ok) { return refuse("stored callable result has no supported concrete signature"); }
    }
    fn.getBody().walk([&](mlir::Operation * o) {
        if (!ok) { return; }
        if (o == fn.getOperation()) { return; }
        ok = op(o);
        if (!ok) { return; }
        // A declaration closure's result is dropped with its store, and
        // so is a load_global that only names a direct call's callee;
        // neither has a carrier and neither needs one.
        if (isDeclarationClosure(o) || isLiftedClosure(o) || isUnboxedCell(o)) { return; }
        if (isNativeMapBookkeeping(o)) { return; }
        if (o->getName().getStringRef() == "ub.poison") { return; }
        if (llvm::isa<ctjs::CreateObjectOp>(o) || isKeyOnlyString(o) || isVectorKeyString(o)) {
            return;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(o);
            load && feedsOnlyDirectCallees(load.getResult())) {
            return;
        }
        // EVERY JAVASCRIPT VALUE THIS OPERATION DEFINES OR CARRIES has a
        // carrier - including scf results and region arguments.
        for (mlir::Value r : o->getResults()) {
            if (llvm::isa<ctjs::ValueType>(r.getType()) && carrierOf(typeOf(r)) == carrier::none) {
                ok = refuse(("a value of type " + printed(typeOf(r)) + " from `" +
                             o->getName().getStringRef() + "`")
                                .str());
                return;
            }
        }
        for (mlir::Region & region : o->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument a : block.getArguments()) {
                    if (llvm::isa<ctjs::ValueType>(a.getType()) &&
                        carrierOf(typeOf(a)) == carrier::none) {
                        ok = refuse("a loop-carried value of type " + printed(typeOf(a)));
                        return;
                    }
                }
            }
        }
    });
    return ok;
}

} // namespace ctcompile::ctnative::lowering_detail
