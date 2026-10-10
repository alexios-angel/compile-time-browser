//===- Strategies.h - Fusion strategies -------------------------*- C++ -*-===//
//
// Decides whether a Map snapshot in lowered EmitC can be fused into its single
// scalar consumer (see Pass.cpp in this directory).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "mlir/Dialect/EmitC/IR/EmitC.h"

#include "llvm/ADT/SmallVector.h"

#include <optional>
#include <string>

namespace ctcompile::ctnative::deforestation_detail {

namespace ec = mlir::emitc;

/// How the single consumer observes the snapshot: `vec_length` or `vec_at`.
enum class consumption {
    length,
    index
};

/// A restricted instance of Lumberhack's fusion strategy (see
/// ctcompile/docs/native-deforestation.md): one known snapshot producer flows
/// through local slots and copies into one scalar destructor. Unsupported or
/// conflicting upper bounds select identity (no rewrite).
struct strategy {
    ec::CallOpaqueOp producer;
    ec::CallOpaqueOp consumer;
    consumption kind;
    /// True for a `map_keys` snapshot, false for `map_values`.
    bool keys;
    /// The slot, assignment and load operations the snapshot flows through;
    /// they are erased with the producer.
    llvm::SmallVector<mlir::Operation *> forwarding;
};

/// Whether `call` is a `ctnative::map_keys` or `ctnative::map_values` call on a
/// Map storage type whose snapshot the runtime can index directly.
bool isSnapshot(ec::CallOpaqueOp call);
/// The fusion strategy for `producer`, or nullopt with `reason` set when the
/// snapshot escapes, has other than one consumer, crosses a control or effect
/// barrier, or the scan exceeds `maxScan` steps.
std::optional<strategy> inferStrategy(ec::CallOpaqueOp producer, unsigned maxScan,

                                      std::string & reason);

} // namespace ctcompile::ctnative::deforestation_detail
