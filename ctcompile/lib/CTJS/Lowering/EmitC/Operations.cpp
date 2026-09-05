// Boxed EmitC operations lowering.
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

void lowering::convert(mlir::Operation & op, mlir::OpBuilder & build, mlir::IRMapping & mapping,
                       compiled_entry & scope) {
    if (convertValues(op, build, mapping, scope) || convertRuntime(op, build, mapping, scope)) {
        return;
    }
    // NOT llvm_unreachable, WHICH IS SILENT IN RELEASE. Under -DNDEBUG it
    // is __builtin_unreachable(), so an operation reaching here does not
    // abort - it produces whatever the compiler decided the impossible
    // branch should do. That happened: ctjs.create_closure was added to
    // body_is_supported and not to this switch, and instead of the loud
    // failure this line was written to give, the operation survived into
    // the output with its operands rewritten, and the module failed to
    // verify somewhere else entirely.
    llvm::report_fatal_error(llvm::Twine("ctcompile: body_is_supported admits ") +
                             op.getName().getStringRef() + " and convert() does not emit it");
}

} // namespace ctcompile::ctjs::emitc_detail
