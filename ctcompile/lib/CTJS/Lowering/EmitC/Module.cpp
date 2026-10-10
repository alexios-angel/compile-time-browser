//===- ctcompile/lib/CTJS/Lowering/EmitC/Module.cpp - Lower a ctjs module -===//
//
// lowering::run: lowers every ctjs.func in a module and, when anything was
// lowered, adds the includes and the shim prelude the emitted C++ needs.
//
//===----------------------------------------------------------------------===//
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

void lowering::run(mlir::ModuleOp module, mlir::MLIRContext * context) {
    mlir::OpBuilder build(context);

    // Collected first, then rewritten. Mutating the module during its own
    // walk invalidates the iterator that is doing the walking.
    llvm::SmallVector<FuncOp> functions;
    module.walk([&](FuncOp function) { functions.push_back(function); });

    // The emitted unit declares nothing itself: emitc.call_opaque emits no
    // prototype, so the helpers come from aot.hpp, which keeps the generated
    // code agreeing with the runtime instead of with a signature this pass
    // invented.
    bool lowered_anything = false;
    for (FuncOp function : functions) {
        if (lower(function, build, context)) { lowered_anything = true; }
    }
    if (lowered_anything) {
        build.setInsertionPointToStart(module.getBody());
        // aot.hpp for the helpers; value.hpp for value::bits(), so no
        // NaN-boxing pattern is written here; <bit> and <cstdint> for the
        // bit_cast and UINT64_C a number constant spells; bytecode.hpp
        // because an opcode is spelled as a ctbrowser::script::op enumerator
        // rather than a number, so a renumbering becomes a build error here
        // instead of a call to a different operator.
        for (const llvm::StringRef header :
             {"bit", "cstdint", "ctbrowser/script/value.hpp", "ctbrowser/script/bytecode.hpp",
              "ctbrowser/aot/aot.hpp"}) {
            ec::IncludeOp::create(build, module.getLoc(), build.getStringAttr(header),
                                  /*is_standard_include=*/build.getUnitAttr());
        }

        // A small prelude of shims, emitted by the backend itself. Several
        // helpers answer with a machine quantity rather than a JavaScript
        // value (ct_aot_strict_equals a uint32_t 0 or 1, ct_aot_compare an
        // int32_t ordering, ct_aot_to_number a double), and the ctjs result
        // is a !ctjs.value, so each must be boxed. No runtime helper boxes
        // one, and the C++ spelling `value::boolean(b).bits()` is a member
        // call on a temporary, which emitc.call_opaque cannot emit.
        //
        // Rather than add helpers to the runtime ABI for one backend's
        // convenience, the backend emits the shims it used (ctc_box_bool,
        // ctc_box_number; see shim_rows) as inline functions in an anonymous
        // namespace of its own translation unit. They are generated code, not
        // engine code: nothing links against them. `undefined` needs no shim
        // because it has no operand and is spelled inline as a literal.
        std::string prelude;
        for (const auto [index, row] : llvm::enumerate(shim_rows)) {
            if (used_shims.test(index)) { prelude += "\n" + row.body.str(); }
        }
        if (!prelude.empty()) {
            ec::VerbatimOp::create(build, module.getLoc(),
                                   build.getStringAttr("namespace {" + prelude + "\n}"));
        }
    }
}

} // namespace ctcompile::ctjs::emitc_detail
