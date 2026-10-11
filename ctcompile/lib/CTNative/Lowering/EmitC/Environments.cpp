//===- Environments.cpp - Closure environment lowering -------- -*- C++ -*-===//
//
// Chooses and emits the C++ representation of each admitted closure's
// captured environment, and rewrites the closure creations and capture reads
// that use it.
//
//===----------------------------------------------------------------------===//
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

// Picks a C++ representation for each admitted closure's captured
// environment: a direct call for session-owned Map members, the stored-callable
// ABI when the signature is concrete, otherwise a std::tuple alias emitted into
// `environments`. Marks the matching reads and direct calls so
// replaceEnvironment() can rewrite them.
void lowering::censusEnvironments(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    censusBorrowedCallbacks(accepted);
    llvm::StringMap<unsigned> callables;
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](ctjs::CreateClosureOp made) {
            const auto target = environmentTarget(made);
            if (target.empty()) { return; }
            if (sessionMapTargets.contains(target)) {
                // The member calls its exact owned Map directly. Its source
                // closure and field initialization disappear together.
                made->setAttr(kNativeStoredRead, mlir::UnitAttr::get(context));
                return;
            }
            const bool stored = made->hasAttr(kNativeStoredCallable);
            if (!sessionTargets.contains(target) &&
                (stored || hasConcreteCallableSignature(made))) {
                // Admission has already proved ownership and every invocation.
                // Choose the existing callable ABI only after its concrete
                // signature is known; other admitted signatures keep tuples.
                made->setAttr(kNativeStoredCallable, mlir::UnitAttr::get(context));
                callables.try_emplace(target, static_cast<unsigned>(made.getUpvalues().size()));
                censusStoredCallable(made, !stored);
                return;
            }
            const auto name = environmentType(target);
            std::string definition = "namespace ctnative {\nusing " + name + " = std::tuple<";
            for (auto [i, captured] : llvm::enumerate(made.getUpvalues())) {
                if (i != 0) { definition += ", "; }
                if (auto found = ownedObjectTypes.find(captured); found != ownedObjectTypes.end()) {
                    if (auto pointer = llvm::dyn_cast<ec::PointerType>(found->second)) {
                        definition += llvm::cast<ec::OpaqueType>(pointer.getPointee()).getValue();
                        definition += " *";
                        continue;
                    }
                }
                // A tuple element may use any stored-callable carrier except a
                // nullable String or an Element.
                const auto type = typeOf(captured);
                const auto c = carrierOf(type);
                if (!isCallableCarrier(c) || c == carrier::nullableString ||
                    c == carrier::domElement) {
                    llvm::report_fatal_error("native environment has an unowned capture");
                }
                definition += callableTypeSpelling(type);
            }
            environments.push_back(definition + ">;\n}\n");
        });
    }
    const auto storedCall = [&](unsigned captures) {
        return mlir::IntegerAttr::get(mlir::IntegerType::get(context, 32), captures);
    };
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](mlir::Operation * op) {
            if (op->hasAttr(kNativeEnvironmentRead) &&
                (callables.contains(environmentTarget(op)) ||
                 sessionTargets.contains(environmentTarget(op)))) {
                // Keep inference's capture operands until the invocation is
                // replaced. The existing stored-read sweep removes them then.
                op->setAttr(kNativeStoredRead, mlir::UnitAttr::get(context));
            }
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op);
            if (!call) { return; }
            if (sessionCalls.contains(op)) {
                call->setAttr(kNativeStoredCall,
                              storedCall(sessionTargets.lookup(call.getCallee())));
                return;
            }
            const auto found = callables.find(call.getCallee());
            if (found == callables.end()) { return; }
            const auto type = llvm::dyn_cast_or_null<ClosureType>(typeOf(call.getCalleeValue()));
            if (!type || type.getTarget() != call.getCallee()) { return; }
            call->setAttr(kNativeStoredCall, storedCall(found->second));
        });
    }
}

// Replaces a closure creation with its binder or std::make_tuple call, and a
// marked capture read with std::get<index>. Returns false when `op` is not an
// environment operation.
bool lowering::replaceEnvironment(mlir::Operation * op) {
    if (op->hasAttr(kNativeStoredRead)) { return true; }
    if (replaceClosureSetMember(op)) { return true; } // ClosureSets.cpp
    // A borrowed callback (ClosureLifting/BorrowedCallbacks.cpp): the closure
    // binds the lifted callback to its captures, and a call through the
    // parameter calls the ctnative::function_ref that borrows it.
    if (const auto target = borrowedTarget(op); !target.empty()) {
        mlir::OpBuilder at(op);
        const auto any = ec::OpaqueType::get(context, "auto");
        const auto named = names.find(target);
        llvm::SmallVector<mlir::Value> bound{ec::LiteralOp::create(
            at, op->getLoc(), any, named == names.end() ? cIdentifier(target) : named->second)};
        llvm::append_range(bound, llvm::cast<ctjs::CreateClosureOp>(op).getUpvalues());
        // The bound callable is a local of this frame, and the function_ref
        // the callee receives refers to that local.
        const mlir::Type lent = op->getResult(0).getType();
        op->getResult(0).replaceAllUsesWith(
            callValue(at, op->getLoc(), lent, opaqueSpelling(lent),
                      callValue(at, op->getLoc(), any, "std::bind_front", bound)));
        eraseIfUnused(op);
        return true;
    }
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(op); call && op->hasAttr(kNativeBorrowedCall)) {
        mlir::OpBuilder at(op);
        llvm::SmallVector<mlir::Value> operands{call.getCallee()};
        llvm::append_range(operands, call.getArgs());
        op->getResult(0).replaceAllUsesWith(callValue(at, op->getLoc(), op->getResult(0).getType(),
                                                      "ctnative::invoke_callable", operands));
        eraseIfUnused(op);
        return true;
    }
    const auto target = environmentTarget(op);
    if (target.empty()) { return false; }
    mlir::OpBuilder at(op);
    mlir::Value result;
    if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
        const std::string builder =
            made->hasAttr(kNativeStoredCallable) && !sessionTargets.contains(target)
                ? binderName(target)
                : "std::make_tuple";
        result =
            callValue(at, op->getLoc(), made.getResult().getType(), builder, made.getUpvalues());
        if (const auto name = names.lookup(target); callableBodies.contains(name)) {
            result.getDefiningOp()->setAttr("ctnative.callable_create",
                                            mlir::FlatSymbolRefAttr::get(context, name));
        }
    } else if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op);
               read && op->hasAttr(kNativeEnvironmentRead)) {
        const auto callee = "std::get<" + std::to_string(read.getIndex()) + ">";
        result = callValue(at, op->getLoc(), read.getResult().getType(), callee, read.getClosure());
    } else {
        return false;
    }
    op->getResult(0).replaceAllUsesWith(result);
    eraseIfUnused(op);
    return true;
}

// Spells the ctnative::function_ref of every borrowed parameter in the
// accepted functions, before any of them is lowered: the result and argument
// carriers of the calls through it, on which admission made every call and
// every callback agree. Each callback the parameter holds gets the same
// spelling, for the closure that binds it.
void lowering::censusBorrowedCallbacks(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](ctjs::CallOp call) {
            auto callbacks = call->getAttrOfType<mlir::ArrayAttr>(kNativeBorrowedCall);
            if (!callbacks || borrowedSignatures.contains(call.getCallee())) { return; }
            llvm::SmallVector<std::string> arguments;
            for (mlir::Value argument : call.getArgs()) {
                arguments.push_back(callableTypeSpelling(typeOf(argument)));
            }
            const std::string spelling = "ctnative::function_ref<" +
                                         callableTypeSpelling(typeOf(call.getResult())) + "(" +
                                         llvm::join(arguments, ", ") + ")>";
            borrowedSignatures[call.getCallee()] = spelling;
            for (auto symbol : callbacks.getAsRange<mlir::FlatSymbolRefAttr>()) {
                borrowedCallbackSignatures[symbol.getValue()] = spelling;
            }
        });
    }
}

bool lowering::retypeBorrowed(mlir::Value v) {
    std::string spelling;
    if (mlir::Operation * made = v.getDefiningOp(); made && !borrowedTarget(made).empty()) {
        spelling = borrowedCallbackSignatures.lookup(borrowedTarget(made));
    } else if (const auto found = borrowedSignatures.find(v); found != borrowedSignatures.end()) {
        spelling = found->second;
    } else {
        return false;
    }
    if (spelling.empty()) {
        llvm::report_fatal_error("ctnative lowering: a borrowed callback was admitted with no call "
                                 "through its parameter to spell its signature");
    }
    v.setType(ec::OpaqueType::get(context, spelling));
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
