// A COMPILED FUNCTION, AS C++ THE HOST COMPILER WILL ACCEPT.
//
// This is the narrow end of the backend: it turns a ctjs.func into an
// emitc.func shaped exactly like ct_aot_entry_fn, so that mlir-translate
// --mlir-to-cpp produces a translation unit that compiles against the real
// aot.hpp. test/Lowering/EmitC/entry-shape.mlir is the target it aims at, and
// that file compiles what it describes rather than only matching text.
//
// IT REFUSES MOST FUNCTIONS ON PURPOSE, and says so on each one it leaves. The
// importer set the precedent - "the count of what it leaves behind is the work
// list" - and a backend that half-lowered a function it did not understand
// would produce a translation unit that compiles and computes the wrong thing,
// which is this project's recurring failure mode. What it refuses and why is
// recorded as an attribute on the operation, so the work list is readable with
// ctjs-opt rather than only from a log.
//
// WHAT IT ACCEPTS TODAY: a body of frame_enter, frame_exit, return and
// constants - which is `function f(a) { return a; }` and not much more. That is
// the whole point of doing it now: the pipeline from JavaScript to a compiled
// .cpp is either connected or it is not, and every operation added afterwards
// is an increment on something that demonstrably works end to end.
//
// THREE CONSTRAINTS FROM THE RUNTIME SHAPE THIS FILE, none of them obvious:
//
//   argv DIES AT ct_aot_enter. It is an interior pointer into
//   context::registers_, and enter resizes that vector - so the parameters are
//   copied into locals BEFORE the call, never read after it. There is no way
//   to re-derive it: ct_aot_slots hands back the compiled frame's own span,
//   not the caller's argument window.
//
//   new.target AND callee CANNOT BE DELIVERED AT ALL. The importer gives every
//   function three implicit arguments before its declared ones - receiver,
//   new.target, callee - because the bytecode reads them with their own
//   opcodes. `receiver` arrives in the entry signature. The other two come only
//   from ct_aot_new_target and ct_aot_callee, and NEITHER HAS A BODY: they are
//   declared in aot.hpp and defined nowhere, so emitting a call to either is a
//   link error. A function that uses them is refused rather than given
//   undefined, because undefined is an answer and a wrong one.
//
//   THE ENTRY BLOCK'S ARGUMENTS ARE SAFE; NO OTHER BLOCK'S ARE. The C++ emitter
//   loses a copy on a block-argument edge - see
//   test/Lowering/EmitC/block-argument-hazard.mlir, which compiles and runs the
//   miscompile. The entry block's arguments become real C++ parameters and are
//   unaffected. Every other block's would have to be lowered to variables
//   first, and until that exists this pass refuses any function with more than
//   one block.
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
