//===- Passes.td - ctnative passes ---------------------------*- tablegen -*-===//
//
// THE NATIVE TIER'S PASSES. Part 24 §1.2: the output links neither the
// interpreter nor its collector, and anything unproved is a diagnostic.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_TRANSFORMS_PASSES_TD
#define CTCOMPILE_CTNATIVE_TRANSFORMS_PASSES_TD

include "mlir/Pass/PassBase.td"

def CTNativeLowerToEmitC : Pass<"ctnative-lower-to-emitc", "mlir::ModuleOp"> {
  let summary = "Lower every PROVED function to EmitC over plain C++ types; diagnose the rest";
  let description = [{
    Part 24 Phase 62½-C, the first native artefact. Runs the Phase 54A type
    inference (with the closed-world global rule) over the module, and for
    every `ctjs.func` decides ONE of two things:

      * every `!ctjs.value` in it has a proved type the native tier can
        represent - `bool` as `bool`, `num<...>` as `double`, and
        `opt<num<...>>`/`opt<bottom>` as a `double` whose undefined is NaN,
        admitted only where that representation is exact (arithmetic,
        relational comparison, truthiness) - and every operation is one this
        pass lowers: the function becomes an `emitc.func` over those types,
        its `scf` left for `--convert-scf-to-emitc`, its `ctjs.return` an
        `emitc.return`;

      * or it is refused, and the refusal is a DIAGNOSTIC on the function -
        `ctnative.not_native = "<the first value or operation that failed>"`
        - never a boxed value under a collector.

    The importer's top-level function becomes `main`, and at its end every
    numeric global is printed as `name=%.17g` - the convention the
    compilation-unit gate compares against the interpreter.

    NOT a dialect conversion. The function is already structured and typed;
    the lowering retypes every value in place from the lattice and replaces
    each operation with its EmitC form, which keeps the whole of `scf`
    untouched for the upstream conversion that already handles it.

    Runs AFTER --ctjs-resolve-globals and --ctjs-lift-to-scf; a function that
    still carries `cf` branches is refused as unstructured.
  }];
  let dependentDialects = [
    "::ctcompile::ctnative::CTNativeDialect",
    "::mlir::emitc::EmitCDialect",
    "::mlir::scf::SCFDialect"
  ];
}

#endif // CTCOMPILE_CTNATIVE_TRANSFORMS_PASSES_TD
