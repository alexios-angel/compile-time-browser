//===- Operations.cpp - Per-operation dispatch ----------------------------===//
//
// lowering::convert: dispatches one ctjs operation to the value or runtime
// converter, and aborts loudly if neither handles it.
//
//===----------------------------------------------------------------------===//
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

void lowering::convert(mlir::Operation & op, mlir::OpBuilder & build, mlir::IRMapping & mapping,
                       compiled_entry & scope) {
    if (convertValues(op, build, mapping, scope) || convertRuntime(op, build, mapping, scope)) {
        return;
    }
    // Not llvm_unreachable: under NDEBUG that is __builtin_unreachable(), and
    // an operation body_is_supported admits but convert() does not handle
    // would pass through silently and fail verification somewhere else.
    llvm::report_fatal_error(llvm::Twine("ctcompile: body_is_supported admits ") +
                             op.getName().getStringRef() + " and convert() does not emit it");
}

} // namespace ctcompile::ctjs::emitc_detail
