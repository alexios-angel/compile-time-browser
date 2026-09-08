#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/OwningOpRef.h"

#include <string>

namespace ctcompile::ctnative::lowering_detail {

struct ExceptionRecoveryResult {
    bool recovered = false;
    std::string refusal;
    mlir::OwningOpRef<ctjs::FuncOp> original = {};
    unsigned steps = 0;
};

enum class ExceptionRecoveryMode {
    ExplicitThrows,
    // Structural prerequisite only. Native admission/emission do not consume
    // invocation completions yet, so ordinary lowering must use ExplicitThrows.
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
// EffectCheckedInvocations discharges them from the current source graph, not
// inferred result types or persisted attributes. Unsupported effects and work
// exhaustion leave the original function untouched, including all checks.
ExceptionRecoveryResult recoverPrimitiveExceptionRegion(
    ctjs::FuncOp function, unsigned maxSteps = 100000,
    ExceptionRecoveryMode mode = ExceptionRecoveryMode::ExplicitThrows);

} // namespace ctcompile::ctnative::lowering_detail
