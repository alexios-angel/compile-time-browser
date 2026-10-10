//===- ctcompile/lib/CTJS/Lowering/CTJSToEmitC.cpp - ctjs to EmitC pass ---===//
//
// The --ctjs-lower-to-emitc pass: turns each ctjs.func into an emitc.func
// shaped exactly like ct_aot_entry_fn, so that mlir-translate --mlir-to-cpp
// produces a translation unit that compiles against the real aot.hpp
// (test/Lowering/EmitC/entry-shape.mlir compiles what it describes). The work
// is done by emitc_detail::lowering in EmitC/.
//
// A function the pass does not fully understand is refused, not half-lowered,
// and the reason is stored as a ctjs.not_lowered attribute so ctjs-opt shows
// it. The pass accepts only the operations EmitC/Admission.cpp admits (see
// test/Lowering/EmitC/refusals.mlir for what is refused).
//
// Three constraints from the runtime shape this backend:
//
//   argv dies at ct_aot_enter. It is an interior pointer into
//   context::registers_, and enter resizes that vector, so the parameters are
//   copied into locals before the call and never read after it. It cannot be
//   re-derived: ct_aot_slots hands back the compiled frame's own span, not the
//   caller's argument window.
//
//   new.target and the callee are not entry parameters. The importer gives
//   every function three implicit arguments before its declared ones
//   (receiver, new.target, callee); only the receiver arrives in the entry
//   signature. The body fetches the other two after ct_aot_enter with
//   ct_aot_new_target and ct_aot_callee (see EmitC/Functions.cpp).
//
//   Only the entry block's arguments may reach the C++ emitter: it drops a
//   copy on other block-argument edges
//   (test/Lowering/EmitC/block-argument-hazard.mlir). Multi-block functions
//   are lowered, and --emitc-eliminate-block-arguments
//   (EmitCBlockArguments.cpp) must run afterwards to turn the rest into
//   variables.
//
//===----------------------------------------------------------------------===//
#include "EmitC/Lowering.h"
#include "ctcompile/CTJS/Transforms/Passes.h"

namespace ctcompile::ctjs {
#define GEN_PASS_DEF_CTJSLOWERTOEMITC
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {
struct CTJSLowerToEmitCPass : impl::CTJSLowerToEmitCBase<CTJSLowerToEmitCPass> {
    using CTJSLowerToEmitCBase::CTJSLowerToEmitCBase;
    emitc_detail::lowering state;
    void runOnOperation() override { state.run(getOperation(), &getContext()); }
};
} // namespace
} // namespace ctcompile::ctjs
