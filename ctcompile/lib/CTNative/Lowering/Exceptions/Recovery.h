#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/OwningOpRef.h"

#include <string>

namespace ctcompile::ctnative::lowering_detail {

struct ExceptionRecoveryResult {
    bool recovered = false;
    std::string refusal;
    mlir::OwningOpRef<ctjs::FuncOp> original = {};
};

enum class ExceptionRecoveryMode {
    ExplicitThrows,
    // Structural prerequisite only. Native admission/emission do not consume
    // invocation completions yet, so ordinary lowering must use ExplicitThrows.
    CheckedInvocations,
};

// Reconstruct one preserved importer handler. Failure leaves the function
// untouched. Success is structural only: native admission must separately prove
// the primitive payload, supported state/results and absence of implicit throws.
// The returned original retains the old body and attributes for that rollback.
// CheckedInvocations also needs the invocation effect/carrier and call-component
// consumers before adoption. Its snapshot retains all non-call checks too;
// structural success does not discharge any of those status edges.
ExceptionRecoveryResult recoverPrimitiveExceptionRegion(
    ctjs::FuncOp function, unsigned maxSteps = 100000,
    ExceptionRecoveryMode mode = ExceptionRecoveryMode::ExplicitThrows);

} // namespace ctcompile::ctnative::lowering_detail
