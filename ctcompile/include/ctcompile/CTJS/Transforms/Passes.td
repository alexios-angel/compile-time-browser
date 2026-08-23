//===- Passes.td - CTJS passes --------------------------------------------===//
//
// EMPTY AND REAL, like the enums and attributes. -gen-pass-decls -name CTJS
// over a pass-less file still emits a valid registerCTJSPasses(), which is what
// ctjs-opt calls - so the driver the policy asks for in Phase 7 can be built
// before any pass exists.
//
// "Never hand-write registerCTJSPasses(), an options struct, or a
// PassRegistration<> global. -gen-pass-decls -name CTJS produces all of it."
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_PASSES
#define CTCOMPILE_CTJS_PASSES

include "mlir/Pass/PassBase.td"

def CTJSLowerToRuntime : Pass<"ctjs-lower-to-runtime", "mlir::ModuleOp"> {
  let summary = "Lower CTJS operations to calls into the shared runtime helpers";
  let description = [{
    ONE PATTERN FOR FIFTY OPERATIONS, which is what CTJS_RuntimeCallOpInterface
    exists for. Every operation implementing it lowers identically - a call to
    the helper its `getHelperID` names, with the frame handle first and the
    operation's operands after - so there is one conversion pattern rather than
    fifty near-identical files and fifty chances to get the operand order wrong.

    THE PATTERN MATCHES ON THE INTERFACE, NOT ON NAMES. "A pass that switches on
    operation names must be revisited every time an operation is added. A pass
    that queries a trait never is." Adding an operation to the dialect adds
    nothing here.
  }];
  // func ONLY. The LLVM dialect belongs to the SECOND backend (Phases 11-12A)
  // and naming it here would make this pass depend on LLVM's dialect library
  // for nothing - the helpers are called as func.call, which EmitC lowers to
  // ordinary C++ calls and the LLVM path lowers to llvm.call when it arrives.
  let dependentDialects = ["::mlir::func::FuncDialect"];
}

def CTJSLowerToEmitC : Pass<"ctjs-lower-to-emitc", "mlir::ModuleOp"> {
  let summary = "Emit each CTJS function as a compiled C++ entry";
  let description = [{
    THE NARROW END OF THE BACKEND: a ctjs.func becomes an emitc.func shaped
    exactly like ct_aot_entry_fn, so mlir-translate --mlir-to-cpp produces a
    translation unit that compiles against the real aot.hpp.

    IT REFUSES MOST FUNCTIONS AND RECORDS WHY on each one it leaves, as a
    `ctjs.not_lowered` attribute. The importer set the precedent - the count of
    what a stage leaves behind is the work list - and a backend that
    half-lowered a function it did not understand would emit C++ that compiles
    and computes the wrong thing.
  }];
  let dependentDialects = ["::mlir::emitc::EmitCDialect", "::mlir::cf::ControlFlowDialect"];
}

#endif // CTCOMPILE_CTJS_PASSES
