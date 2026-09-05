#pragma once

#include "mlir/Dialect/EmitC/IR/EmitC.h"

#include "llvm/ADT/SmallVector.h"

#include <optional>
#include <string>

namespace ctcompile::ctnative::deforestation_detail {

namespace ec = mlir::emitc;

enum class consumption {
    length,
    index
};

// A restricted instance of Lumberhack's fusion strategy: one known snapshot
// producer flows through local slots/copies into one scalar destructor.
// Unsupported or conflicting upper bounds select identity (no rewrite).
struct strategy {
    ec::CallOpaqueOp producer;
    ec::CallOpaqueOp consumer;
    consumption kind;
    bool keys;
    llvm::SmallVector<mlir::Operation *> forwarding;
};

bool isSnapshot(ec::CallOpaqueOp call);
std::optional<strategy> inferStrategy(ec::CallOpaqueOp producer, unsigned maxScan,
                                      std::string & reason);

} // namespace ctcompile::ctnative::deforestation_detail
