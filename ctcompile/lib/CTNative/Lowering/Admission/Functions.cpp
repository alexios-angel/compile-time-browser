//===- Admission/Functions.cpp - Whole-function native admission ----------===//
//
// admission::function, the entry point of native admission: it checks the
// function's implicit and declared parameters, then admits every operation
// in the body and requires every JavaScript value it defines to have a
// native carrier.
//
//===----------------------------------------------------------------------===//

#include "../../Analysis/OwnedGlobalRoots.h"
#include "Admission.h"
#include "ctcompile/CTJS/Import/EngineGlobals.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Analysis/Instances.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {
/// The reason closure lifting recorded on a closure that names `fn`, worded as
/// closureRefusal words it, or empty when no closure naming `fn` carries one
/// (a declaration, for example, is never judged).
std::string receiverLiftRefusal(ctjs::FuncOp fn) {
    const std::optional<unsigned> index = ctjs::functionIndex(fn);
    auto module = fn->getParentOfType<mlir::ModuleOp>();
    if (!index || !module) { return {}; }
    std::string why;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (ctjs::closureFunction(made) != index || !made->hasAttr(kNativeClosureReason) ||
            made->hasAttr(kNativeLifted)) {
            return mlir::WalkResult::advance();
        }
        why = admission::closureRefusal(made);
        return mlir::WalkResult::interrupt();
    });
    return why;
}
/// The engine helper's IR name `op` uses as the script's own global or
/// constant property key, or empty. The importer gives the VM's class helpers
/// those names (EngineGlobals.h), so a script's own binding of one would read
/// as the helper; it is refused rather than renamed.
llvm::StringRef engineReservedName(mlir::Operation * op) {
    if (op->hasAttr(ctjs::kEngineReservedUse)) {
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) { return load.getName(); }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) { return store.getName(); }
    }
    mlir::Value key;
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) { key = get.getKey(); }
    if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) { key = set.getKey(); }
    const llvm::StringRef name = key ? ctjs::constantKey(key) : llvm::StringRef{};
    return ctjs::engineHelperVMName(name).empty() ? llvm::StringRef{} : name;
}
} // namespace

bool admission::function(ctjs::FuncOp fn) {
    if (auto why = fn->getAttrOfType<mlir::StringAttr>("ctnative.structure_refusal")) {
        return refuse(why.getValue().str());
    }
    mlir::Block & entry = fn.getBody().front();
    const auto unsupported = [&](mlir::Value value) {
        if (nativeMapRecordPayload(value) ||
            (nativeMapRecordOrigin(value) && isClosedObject(value))) {
            return false;
        }
        const auto storage = carrierOf(typeOf(value));
        // JsonType also describes widened lattice facts. Only the complete DOM
        // proof establishes a parsed tree with owning, identity-free contents.
        return storage == carrier::none ||
               (storage == carrier::json && (!domEntry || domEntry->entry() != fn));
    };
    // The receiver is a parameter when the lift said so, and %arg0 is then
    // the one implicit argument that has a carrier: the generated class of
    // the shape every call site passes. The lift proved every use of it is
    // a constant-key access or another lifted method call. This asks the
    // same question of the IR that came out, because the lift's proof was
    // made before its own rewrite ran, and a later refusal (the callee of
    // a `this.other()` that was not lifted) can have opened it since.
    const bool carriesReceiver = fn->hasAttr(kNativeReceiver);
    // An identity class's constructor hands `this` to its registry Map, which
    // the registry proof checked (Analysis/Instances.h).
    if (carriesReceiver && !isClosedObject(entry.getArgument(0)) && !isIdentityClass(fn)) {
        return refuse("its `this` is no longer a closed-shape receiver - " +
                      whyOpenReceiver(entry.getArgument(0)));
    }
    // The three implicit arguments (receiver, new.target, callee) have no
    // native carrier and must be unused.
    for (unsigned i = carriesReceiver ? 1 : 0; i < 3 && i < entry.getNumArguments(); ++i) {
        for (mlir::Operation * user : entry.getArgument(i).getUsers()) {
            if (domEntry &&
                (domEntry->entry() == fn || llvm::is_contained(domEntry->callbacks(), fn))) {
                if (llvm::isa<ctjs::RootOp>(user)) { continue; }
                if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
                    closure && domEntry->callback(closure)) {
                    continue;
                }
            }
            // A closure that lowers to nothing does not read these: a
            // declaration's pair is erased with its store, and a lifted
            // one's `$enclosing_closure` and `$enclosing_this` operands go
            // with the ctjs.call_direct that replaced its calls.
            // ResolveGlobals makes the same exemption on the same operand
            // (own_closure_escapes), for the same reason.
            if (closureLowersToNothing(user)) { continue; }
            if (llvm::isa<ctjs::CreateClosureOp>(user) && !environmentTarget(user).empty()) {
                continue; // environment construction drops the two enclosing-frame operands
            }
            // A nested closure the lift refused also uses %arg2, but that
            // does not mean this function reads its own closure. Report the
            // reason the lift recorded on the closure instead. A real reader
            // of %arg2 (a named function expression calling itself, or a
            // ctjs.load_upvalue in an unlifted function) still gets "uses
            // its own closure"; Admission/refusal/corpus-shapes.mlir tests
            // that.
            if (llvm::isa<ctjs::CreateClosureOp>(user)) { return refuse(closureRefusal(user)); }
            // A method or callback whose `this` the receiver lift would have
            // made a parameter: name why the lift refused it, which is the
            // actionable reason, instead of the bare use.
            if (i == 0) {
                if (const std::string why = receiverLiftRefusal(fn); !why.empty()) {
                    return refuse("uses `this` - " + why);
                }
            }
            return refuse(i == 0   ? "uses `this`"
                          : i == 1 ? "uses new.target"
                                   : "uses its own closure");
        }
    }
    // In a lifted closure, the parameters after the three implicit ones start
    // with its captured values. `ctnative.captures` counts them (0 on any
    // function that was not lifted), and diagnostics subtract them so that a
    // parameter number matches the JavaScript source.
    const auto capturesAttr = fn->getAttrOfType<mlir::IntegerAttr>(kNativeCaptures);
    const unsigned captures = capturesAttr ? static_cast<unsigned>(capturesAttr.getInt()) : 0u;
    for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
        // A borrowed callback parameter is a ctnative::function_ref; the calls
        // through it are admitted one by one (BorrowedCallbacks.cpp).
        if (isBorrowedParameter(entry.getArgument(i))) { continue; }
        // Object parameters get the receiver's check. Their carrier is the
        // generated class one indirection away, so `carrierOf` has no row for
        // them and the check below would refuse every one; what has to hold is
        // that the shape is still closed.
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
        // A shared capture is a cell passed by pointer, so the refusal names
        // it as one. Its carrier is that of the value it points at: the same
        // check `op` makes on the cell in the frame that owns it.
        const std::string kind = !isCapture                        ? "parameter "
                                 : isCellArg(fn.getOperation(), i) ? "shared capture "
                                                                   : "capture ";
        const std::string which = kind + std::to_string(isCapture ? i - 3 : i - 3 - captures);
        if (unsupported(entry.getArgument(i))) {
            // Distinguish an unknown parameter from a proved type native
            // lowering cannot represent, such as an optional string. The
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
            return isCallableCarrier(c) &&
                   (c != carrier::domElement ||
                    (ownedGlobals && ownedGlobals->proved() && !ownedGlobals->domInputs().empty()));
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
        if (const llvm::StringRef reserved = engineReservedName(o); !reserved.empty()) {
            ok = refuse(("uses the engine-reserved name `" + reserved + "`").str());
            return;
        }
        ok = op(o);
        if (!ok) { return; }
        // A result that lowers to nothing needs no carrier: lowersToNothing's
        // list, plus the bookkeeping, literal and owned-global rows here.
        // Map bookkeeping is asked of the operation, which may have no result.
        if (isNativeMapBookkeeping(o)) { return; }
        if (domEntry && domBookkeeping(*domEntry, o)) { return; }
        if (o->getNumResults() == 1 && lowersToNothing(o->getResult(0))) { return; }
        // An object literal, or a record its callee returned, is its class.
        if (llvm::isa<ctjs::CreateObjectOp>(o) ||
            (o->getNumResults() == 1 && TypeInference::isRecordCall(o->getResult(0)))) {
            return;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(o);
            load && ownedGlobalValue(load.getResult())) {
            return;
        }
        // Every JavaScript value this operation defines or carries needs a
        // carrier, including scf results and region arguments.
        for (mlir::Value r : o->getResults()) {
            if (llvm::isa<ctjs::ValueType>(r.getType()) && unsupported(r)) {
                ok = refuse(("a value of type " + printed(typeOf(r)) + " from `" +
                             o->getName().getStringRef() + "`")
                                .str());
                return;
            }
        }
        for (mlir::Region & region : o->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument a : block.getArguments()) {
                    if (auto invoke = llvm::dyn_cast<ctjs::InvokeOp>(o);
                        invoke && domEntry && domEntry->invocation(invoke) &&
                        (&region == &invoke.getUnwindBody() ||
                         (invoke.getNumResults() == 0 && &region == &invoke.getNormalBody())) &&
                        a.getArgNumber() == 0 && a.use_empty()) {
                        continue; // No native value represents the unobserved semantic payload.
                    }
                    if (llvm::isa<ctjs::ValueType>(a.getType()) && unsupported(a)) {
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
