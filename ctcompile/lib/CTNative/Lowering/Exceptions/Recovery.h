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

// Reconstruct one preserved importer handler. Failure leaves the function
// untouched. Success is structural only: native admission must separately prove
// the primitive payload, supported state/results and absence of implicit throws.
// The returned original retains the old body and attributes for that rollback.
ExceptionRecoveryResult recoverPrimitiveExceptionRegion(ctjs::FuncOp function,
                                                        unsigned maxSteps = 100000);

} // namespace ctcompile::ctnative::lowering_detail
