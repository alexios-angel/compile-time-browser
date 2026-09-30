#pragma once

#include "../Analysis.h"

namespace ctcompile::ctnative::host_detail {

inline mlir::Value explicitArgument(mlir::Operation * operation, unsigned index) {
    auto js = jsCallOf(operation);
    if (index < ctjs::implicit_arguments || !js) { return {}; }
    index -= ctjs::implicit_arguments;
    return index < js->args.size() ? js->args[index] : mlir::Value{};
}

} // namespace ctcompile::ctnative::host_detail
