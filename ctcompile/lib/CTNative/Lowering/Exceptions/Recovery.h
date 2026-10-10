//===- Recovery.h - Exception recovery API ----------------------*- C++ -*-===//
//
// Interface of exception recovery: rebuilding the importer's handler-based
// control flow (push_handler / check / catch_land) into a structured ctjs.try,
// plus the DOM-entry normalizations and the proof-budget helpers they share.
// The implementation is in Recovery/ and DOM.cpp.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <string>
#include <utility>

namespace ctcompile::ctnative {
struct HostContract;
}

namespace ctcompile::ctnative::lowering_detail {

/// Charges `amount` against a proof's budget; returns false, charging nothing,
/// when the budget is short.
inline bool charge(unsigned & remaining, uint64_t amount = 1) {
    if (amount > remaining) { return false; }
    remaining -= static_cast<unsigned>(amount);
    return true;
}

/// Charges `factor` times cost(operation) for each operation `root` walks, in
/// walk order; returns false at the first one the remaining budget cannot cover.
template <class Cost>
bool chargeWalk(mlir::Operation * root, unsigned & remaining, unsigned factor, Cost && cost) {
    return !root->walk([&](mlir::Operation * operation) {
                    const uint64_t amount = cost(operation);
                    if (amount > remaining / factor) { return mlir::WalkResult::interrupt(); }
                    remaining -= static_cast<unsigned>(amount * factor);
                    return mlir::WalkResult::advance();
                })
                .wasInterrupted();
}

/// A proof's remaining work and its first refusal; `exhausted` is the refusal
/// reported when the budget runs out.
struct workBudget {
    unsigned remaining;
    llvm::StringRef exhausted;
    std::string refusal = {};

    bool reject(llvm::StringRef why) {
        if (refusal.empty()) { refusal = why.str(); }
        return false;
    }
    bool spend(uint64_t amount = 1) { return charge(remaining, amount) || reject(exhausted); }
    /// Returns the unspent budget to `caller`, and the refusal as an error
    /// unless `complete`.
    llvm::Error finish(bool complete, unsigned & caller) const {
        caller = remaining;
        return complete ? llvm::Error::success() : llvm::createStringError(refusal);
    }
};

/// Sets `name` on `operation` to `value`, or removes it when `value` is null.
inline void setOrRemoveAttr(mlir::Operation * operation, llvm::StringRef name,
                            mlir::Attribute value) {
    if (value) {
        operation->setAttr(name, value);
    } else {
        operation->removeAttr(name);
    }
}

/// The scf.yield ending `region`'s single nonempty block with exactly `count`
/// operands; null otherwise.
inline mlir::scf::YieldOp exactYield(mlir::Region & region, unsigned count) {
    if (!region.hasOneBlock() || region.front().empty()) { return {}; }
    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().back());
    return yield && yield.getNumOperands() == count ? yield : mlir::scf::YieldOp{};
}

/// Normalizes lifted switch dispatch and unused or all-poison scf.if results on
/// a disposable function clone. Failure can leave the clone partly rewritten,
/// so the caller must discard it. This proves nothing about exceptions or
/// effects.
llvm::Error normalizeStructuredExits(ctjs::FuncOp function, unsigned & remaining);

/// Structures the remaining CFG of a disposable DOM candidate after recovery
/// proved its acyclic prefix. This proves nothing about effects or handlers;
/// generic exception recovery still keeps the original prefix verbatim.
llvm::Error structureRecoveredPrefix(ctjs::FuncOp function, unsigned & remaining);

/// Rewrites a decodeURIComponent/JSON.parse try/catch into structured code.
/// Runs only on the private copy of the DOM entry module whose host contract
/// matched. On refusal the original function is left unchanged. Success is not
/// enough to publish: the DOM proof must still establish fresh element
/// identity, String ownership and the invocations before native lowering.
/// `function` defaults to the contract entry; a local helper that owns a
/// handler is normalized under the same checks before expandDOMHelpers inlines
/// it.
llvm::Error normalizeDOMURI(mlir::ModuleOp candidate, const HostContract & contract,
                            unsigned maxSteps = 100000, llvm::StringRef function = {});

/// On the private DOM candidate only: removes a try whose explicit throws are
/// consumed by its own catch, or selects the normal completion of a call proved
/// not to throw. `elementParameters` (entry parameters known to be Elements)
/// comes from the matched host contract; helpers have none. The DOM proof
/// still validates all values and catch effects afterwards; no C++ exception
/// is introduced.
llvm::Expected<bool> normalizeDOMCaughtThrow(ctjs::FuncOp function, unsigned maxSteps,
                                             llvm::ArrayRef<unsigned> elementParameters = {});

/// Removes a source catch kept until after helper expansion. The protected
/// body's effects must be proved independently before any completion is
/// selected.
llvm::Error normalizeDOMRecoveredCatch(ctjs::FuncOp function, unsigned maxSteps,
                                       llvm::ArrayRef<unsigned> elementParameters);

/// After helper binding, selects the normal completion only for Element
/// attribute calls and initial Number calls proved not to throw. Inert
/// completion values are preserved; the DOM proof still runs afterwards. On
/// failure the caller discards the candidate.
llvm::Error normalizeDOMAttributeInvocations(ctjs::FuncOp function, unsigned maxSteps,
                                             llvm::ArrayRef<unsigned> elementParameters);

/// Result of recoverPrimitiveExceptionRegion(). `original` holds the function's
/// old body and attributes so a later refusal can roll back.
struct ExceptionRecoveryResult {
    bool recovered = false;
    std::string refusal;
    mlir::OwningOpRef<ctjs::FuncOp> original = {};
    unsigned steps = 0;
};

/// Handles into the source function, filled in together only after the
/// handler, the acyclic prefix and tails, and the exact call/check registers
/// are proved. This is structural evidence only: every dropped non-call
/// ctjs.check edge still needs its own effect proof, as do the callee's
/// identity and the catch payload's uses. Any change to the source IR
/// invalidates this result. Inspection never changes IR.
struct ExceptionInvocationSource {
    ctjs::PushHandlerOp push;
    ctjs::CatchLandOp landing;
    ctjs::FrameEnterOp frame;
    /// The first checked call. `chain` lists every checked call in source
    /// order along one normal continuation, starting with this one.
    mlir::Operation * call = nullptr;
    ctjs::CheckOp check;
    llvm::SmallVector<std::pair<mlir::Operation *, ctjs::CheckOp>> chain;
    /// The reachable source blocks of each part, in no particular order.
    /// `normal` and `caught` may share a continuation. Unreachable blocks
    /// still need a separate proof.
    llvm::SmallVector<mlir::Block *> prefix, normal, caught;
    unsigned steps = 0;
    std::string refusal;
    [[nodiscard]] bool proved() const { return call != nullptr; }
};

/// Finds the single handler region of `function` and the checked call(s) it
/// protects, without changing IR. `maxCalls` above one accepts a chain of
/// checked calls on one normal continuation, each reached only through the
/// previous call's success edge. Zero admits no invocation source.
ExceptionInvocationSource inspectSingleInvocationRegion(ctjs::FuncOp function,
                                                        unsigned maxSteps = 100000,
                                                        unsigned maxCalls = 1);

/// What recoverPrimitiveExceptionRegion() may recover.
enum class ExceptionRecoveryMode {
    /// Only explicit `throw`s inside the protected region.
    ExplicitThrows,
    /// Also a checked call, structurally only. Native admission and emission do
    /// not consume invocation completions yet, so ordinary lowering must use
    /// ExplicitThrows. Both an original CallOp and a resolved CallDirectOp keep
    /// their exact operands; recording a call proves nothing about the host
    /// object or its effects.
    CheckedInvocations,
    /// Like CheckedInvocations, but also proves every operation outside the
    /// represented call unable to throw or reenter before adopting the clone.
    /// This still does not admit the throwing call itself or prove its
    /// payload, result or state carriers. A callee loaded from a global needs
    /// a fresh bounded proof of its declaration, identity and uses plus a
    /// whole-module effect scan; host reads and unknown alternatives refuse.
    /// Primitive completion operands may cross an acyclic closed call family,
    /// with exact actual/formal arguments and a maximum call depth of 32.
    EffectCheckedInvocations,
};

/// Rebuilds one handler the importer kept as push_handler/check/catch_land into
/// a structured ctjs.try. Failure leaves the function untouched. Success is
/// structural only: native admission must separately prove the primitive
/// payload, the supported state and results, and that nothing throws
/// implicitly; the returned `original` keeps the old body and attributes for
/// that rollback.
///
/// With CheckedInvocations the snapshot also keeps every non-call check, and
/// structural success removes none of those exception edges.
/// EffectCheckedInvocations removes the recovered tail checks using the source
/// graph, never inferred result types or stored attributes; unsupported
/// effects and an exhausted budget leave the function untouched.
///
/// An acyclic prefix stays outside the recovered try where the handler was
/// installed, with its checks and early exits. A remaining multi-block prefix
/// keeps ctjs.not_structured until a separate proof can structure the whole
/// function.
ExceptionRecoveryResult recoverPrimitiveExceptionRegion(
    ctjs::FuncOp function, unsigned maxSteps = 100000,
    ExceptionRecoveryMode mode = ExceptionRecoveryMode::ExplicitThrows);

} // namespace ctcompile::ctnative::lowering_detail
