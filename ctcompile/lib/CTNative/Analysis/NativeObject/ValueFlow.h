#pragma once

#include "../ClosedValueFlow.h"

namespace ctcompile::ctnative::object_detail {

// Schema edges only: different allocations/calls still have distinct identities.
void connectValues(closedValueFlow & flow, mlir::ModuleOp module);
bool mapPayloadUse(mlir::OpOperand & use);
bool primitiveProducer(mlir::Value value);
bool valueObservation(mlir::Operation * op);
// A lifted register slot with no observable use, including loop exit arms.
bool inertPoison(mlir::Value value);

} // namespace ctcompile::ctnative::object_detail
