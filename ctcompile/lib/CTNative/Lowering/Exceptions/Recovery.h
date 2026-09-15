#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/OwningOpRef.h"
#include "llvm/ADT/SmallVector.h"

#include <string>

namespace ctcompile::ctnative::lowering_detail {

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
    mlir::Operation * call = nullptr;
    ctjs::CheckOp check;
    // Reachable source block censuses, not execution order. Normal and caught
    // may share a continuation. Unreachable source still needs separate proof.
    llvm::SmallVector<mlir::Block *> prefix, normal, caught;
    unsigned steps = 0;
    std::string refusal;
    [[nodiscard]] bool proved() const { return call != nullptr; }
};

ExceptionInvocationSource inspectSingleInvocationRegion(ctjs::FuncOp function,
                                                        unsigned maxSteps = 100000);

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
