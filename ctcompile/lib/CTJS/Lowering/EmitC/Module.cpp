// Boxed EmitC module lowering.
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

void lowering::run(mlir::ModuleOp module, mlir::MLIRContext * context) {
    mlir::OpBuilder build(context);

    // COLLECTED FIRST, then rewritten. Mutating the module during its own
    // walk invalidates the iterator that is doing the walking.
    llvm::SmallVector<FuncOp> functions;
    module.walk([&](FuncOp function) { functions.push_back(function); });

    // THE HEADER THE EMITTED UNIT NEEDS, and the only declaration it
    // carries. emitc.call_opaque resolves nothing and emits no prototype,
    // so the helpers come from aot.hpp itself - which is what keeps the
    // generated code agreeing with the runtime instead of with a signature
    // this pass invented.
    bool lowered_anything = false;
    for (FuncOp function : functions) {
        if (lower(function, build, context)) { lowered_anything = true; }
    }
    if (lowered_anything) {
        build.setInsertionPointToStart(module.getBody());
        // aot.hpp for the helpers, value.hpp for the one thing a constant
        // needs: value::bits(), so no NaN-boxing pattern is written here.
        // <bit> for bit_cast and <cstdint> for UINT64_C, both of which a
        // number constant spells; value.hpp for value::bits().
        ec::IncludeOp::create(build, module.getLoc(), build.getStringAttr("bit"),
                              /*is_standard_include=*/build.getUnitAttr());
        ec::IncludeOp::create(build, module.getLoc(), build.getStringAttr("cstdint"),
                              /*is_standard_include=*/build.getUnitAttr());
        ec::IncludeOp::create(build, module.getLoc(),
                              build.getStringAttr("ctbrowser/script/value.hpp"),
                              /*is_standard_include=*/build.getUnitAttr());
        // AND bytecode.hpp, because an opcode is spelled as an enumerator
        // of ctbrowser::script::op rather than as the number the ABI
        // actually passes. That is the whole point of spelling it: a
        // renumbering becomes a build error here instead of a call to a
        // different operator.
        ec::IncludeOp::create(build, module.getLoc(),
                              build.getStringAttr("ctbrowser/script/bytecode.hpp"),
                              /*is_standard_include=*/build.getUnitAttr());
        ec::IncludeOp::create(build, module.getLoc(), build.getStringAttr("ctbrowser/aot/aot.hpp"),
                              /*is_standard_include=*/build.getUnitAttr());

        // A SMALL PRELUDE OF SHIMS, AND WHY THE BACKEND EMITS ITS OWN.
        //
        // Several helpers answer with a MACHINE quantity rather than a
        // JavaScript value: ct_aot_strict_equals returns a uint32_t 0 or 1,
        // ct_aot_compare an int32_t ordering, ct_aot_to_number a double.
        // The result of the CTJS operation is a !ctjs.value, so each has to
        // be boxed - and THE ABI HAS NO ROW THAT BOXES ONE. There is no
        // ct_aot_from_bool and no ct_aot_from_double; every row that
        // returns a value builds it from something else.
        //
        // THERE ARE ONLY TWO, because a shim exists only where an SSA
        // value has to be boxed. `undefined` has no operand, so it is
        // spelled inline as a literal - member call and all - and a third
        // shim for it would be an unused function in every translation
        // unit this backend emits.
        //
        // In C++ that is `value::boolean(b).bits()`, which is a member call
        // on a temporary - and emitc.call_opaque emits `callee(args)` and
        // nothing else, so it cannot spell one. Rather than add rows to a
        // runtime ABI for a backend's convenience, the backend emits three
        // static inline functions into its own translation unit. They are
        // generated code, not engine code: nothing links against them and
        // no other backend has to agree about them.
        std::string prelude;
        if (boxes_bool) {
            prelude += "\ninline uint64_t ctc_box_bool(bool b) { return "
                       "::ctbrowser::script::value::boolean(b).bits(); }";
        }
        if (boxes_number) {
            prelude += "\ninline uint64_t ctc_box_number(double d) { return "
                       "::ctbrowser::script::value::number(d).bits(); }";
        }
        if (!prelude.empty()) {
            ec::VerbatimOp::create(build, module.getLoc(),
                                   build.getStringAttr("namespace {" + prelude + "\n}"));
        }
    }
}

} // namespace ctcompile::ctjs::emitc_detail
