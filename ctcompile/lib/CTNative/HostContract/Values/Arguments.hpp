#pragma once

#include "../Analysis.h"

namespace ctcompile::ctnative::host_detail {

inline mlir::Value explicitArgument(mlir::Operation * operation, unsigned index) {
    if (index < 3) { return {}; }
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
    auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
    if (!direct && !call) { return {}; }
    auto args = direct ? direct.getArgs() : call.getArgs();
    return index - 3 < args.size() ? args[index - 3] : mlir::Value{};
}

} // namespace ctcompile::ctnative::host_detail
