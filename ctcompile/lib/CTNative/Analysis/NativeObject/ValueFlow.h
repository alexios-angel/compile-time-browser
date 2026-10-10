//===- ValueFlow.h - Object schema value flow -------------------*- C++ -*-===//
//
// Value-flow helpers shared by the native object analyses.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../ClosedValueFlow.h"

namespace ctcompile::ctnative::object_detail {

/// Joins values that must share one object schema (field layout): native Map
/// values with their `get` results, and scf.if / scf.while / scf.for results,
/// arguments and yields. Values from different allocations or calls keep
/// distinct runtime identities; only their schema is shared.
void connectValues(closedValueFlow & flow, mlir::ModuleOp module);
/// True when `use` is the value operand of a native Map `set` call.
bool mapPayloadUse(mlir::OpOperand & use);
/// True when `value` is always a primitive if its producer returns: a
/// non-String primitive constant, an operator result, or a Map `has`,
/// `delete` or `size` result.
bool primitiveProducer(mlir::Value value);
/// True when `op` only reads a value (comparison, truthiness, arithmetic,
/// loop plumbing) and cannot store or publish it.
bool valueObservation(mlir::Operation * op);
/// Whether the ub.poison `value` can only reach roots, loop-carried slots and
/// loop exits that are never read. Loop lifting creates such placeholders for
/// register slots, including in loop exit arms. Gives up with false after
/// 4096 visited values or uses.
bool inertPoison(mlir::Value value);

} // namespace ctcompile::ctnative::object_detail
