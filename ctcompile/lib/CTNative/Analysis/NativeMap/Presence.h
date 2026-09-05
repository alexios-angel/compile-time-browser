//===- Presence.h - path-sensitive membership of proved native Maps -------===//
#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLFunctionalExtras.h"

#include <string>

namespace ctcompile::ctnative::map_detail {

// Schema families are consulted only to invalidate possible aliases. A fact
// is established solely for one runtime instance and one key.
std::string provePresence(mlir::ModuleOp module, llvm::ArrayRef<ctjs::CallOp> calls,
                          llvm::ArrayRef<ctjs::CallOp> reads,
                          llvm::function_ref<mlir::Value(mlir::Value)> familyOf);

} // namespace ctcompile::ctnative::map_detail
