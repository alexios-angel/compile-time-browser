#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"

#include <string>
#include <utility>

namespace ctcompile::ctnative {
struct HostContract;
}

namespace ctcompile::ctnative::lowering_detail {

// Normalize lifted switch dispatch and unused/all-poison if results on a
// disposable function clone. Failure can leave that clone partly rewritten;
// the caller must withhold it. This grants no exception or effect authority.
llvm::Error normalizeStructuredExits(ctjs::FuncOp function, unsigned & remaining);

// Only on the fingerprinted private DOM candidate. Preserve the original
// function on refusal; success still requires complete fresh DOM identity,
// String ownership and invocation proof before publication or native lowering.
// `function` defaults to the contract entry; a handler-owning local helper is
// normalized under the same checks before expandDOMHelpers inlines it.
llvm::Error normalizeDOMURI(mlir::ModuleOp candidate, const HostContract & contract,
                            unsigned maxSteps = 100000, llvm::StringRef function = {});

struct ExceptionRecoveryResult {
    bool recovered = false;
    std::string refusal;
    mlir::OwningOpRef<ctjs::FuncOp> original = {};
    unsigned steps = 0;
};

// Original source handles, published together only after the existing handler,
// acyclic prefix/tail and exact call/check register proofs succeed. This is
// structural evidence only: every discarded non-call status edge still needs
// its own effect proof, as do callee identity and the catch payload's uses.
// Any source mutation invalidates this result. Inspection never changes IR.
struct ExceptionInvocationSource {
    ctjs::PushHandlerOp push;
    ctjs::CatchLandOp landing;
    ctjs::FrameEnterOp frame;
    // The first checked call. `chain` lists every checked call in source
    // order along one normal continuation, starting with this one.
    mlir::Operation * call = nullptr;
    ctjs::CheckOp check;
    llvm::SmallVector<std::pair<mlir::Operation *, ctjs::CheckOp>> chain;
    // Reachable source block censuses, not execution order. Normal and caught
    // may share a continuation. Unreachable source still needs separate proof.
    llvm::SmallVector<mlir::Block *> prefix, normal, caught;
    unsigned steps = 0;
    std::string refusal;
    [[nodiscard]] bool proved() const { return call != nullptr; }
};

// `maxCalls` above one accepts a chain of checked calls on one normal
// continuation, each reached only through the previous call's success edge.
// Zero admits no invocation source.
ExceptionInvocationSource inspectSingleInvocationRegion(ctjs::FuncOp function,
                                                        unsigned maxSteps = 100000,
                                                        unsigned maxCalls = 1);

enum class ExceptionRecoveryMode {
    ExplicitThrows,
    // Structural prerequisite only. Native admission/emission do not consume
    // invocation completions yet, so ordinary lowering must use ExplicitThrows.
    // Both original CallOp and resolved CallDirectOp retain their exact operands;
    // recording an ordinary call supplies no host identity or effect authority.
    CheckedInvocations,
    // Also prove every operation outside the represented call unable to throw
    // or reenter before adopting the clone. This still does not admit the
    // throwing call component or prove its payload/result/state carriers.
    // Source callee loads require a fresh bounded declaration/identity/use and
    // whole-module effect census; host reads and unknown alternatives refuse.
    // Primitive completion operands may cross an acyclic closed call family,
    // with exact actual/formal contexts and a maximum call depth of 32. This
    // neither proves a single payload carrier nor changes ordinary admission.
    EffectCheckedInvocations,
};

// Reconstruct one preserved importer handler. Failure leaves the function
// untouched. Success is structural only: native admission must separately prove
// the primitive payload, supported state/results and absence of implicit throws.
// The returned original retains the old body and attributes for that rollback.
// CheckedInvocations also needs the invocation effect/carrier and call-component
// consumers before adoption. Its snapshot retains all non-call checks too;
// structural success does not discharge any of those status edges.
// EffectCheckedInvocations discharges recovered tail checks from the source graph, not
// inferred result types or persisted attributes. Unsupported effects and work
// exhaustion leave the original function untouched, including all checks.
// An acyclic prefix stays outside the recovered try at its original installation
// site, including its checks and early exits. A remaining multi-block prefix keeps
// ctjs.not_structured until a separate proof can structure/admit the whole function.
ExceptionRecoveryResult recoverPrimitiveExceptionRegion(
    ctjs::FuncOp function, unsigned maxSteps = 100000,
    ExceptionRecoveryMode mode = ExceptionRecoveryMode::ExplicitThrows);

} // namespace ctcompile::ctnative::lowering_detail
