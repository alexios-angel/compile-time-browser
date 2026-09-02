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

def CTNativePrintDeduced : Pass<"ctnative-print-deduced", "mlir::ModuleOp"> {
  let summary = "Mark the declarations the emitter may print as `auto`, each pinned by a static_assert";
  let description = [{
    Part 24 Stage 53E, the printing policy, and Stage 53F, the pins; turned
    on at Phase 62½-E, against a module that already compiles and runs with
    every type printed. `auto` is an ATTRIBUTE, not a type: this pass sets
    `ctnative.deduced` on each single-result operation that is printed as a
    declaration with an initialiser and whose initialiser fixes the type -
    arithmetic, comparison, cast, a call of a generated function, the logical,
    bitwise and conditional ops. The IR keeps the proved type on every value;
    the forked TranslateToCpp is the one consumer, printing `auto` for the
    declaration and `CTCOMPILE_PIN(name, "site", type);` after it.

    The never-deduce list (53E) is what this pass does NOT mark: a constant
    (`auto x = 0;` is int, a JavaScript number is double), a load (`auto x =
    *p;` copies, `auto & x = *p;` does not, and which one a line means is
    Phase 55's question), a variable declaration (no initialiser), an opaque
    call (the standard-library boundary prints its result type), and anything
    inside an emitc.expression (printed inline, never declared).

    The pin macro is a static_assert on decltype naming the JavaScript site,
    defined at the top of the module after the includes and empty under
    -DCTCOMPILE_NO_TYPE_PINS. `mutate=N` pins the Nth deduced double as
    int32_t instead; the emitted file must then fail to build naming the
    site, which is the gate that proves the pins bite. A mutation that lands
    on nothing is a pass failure, not a silent pass.
  }];
  let options = [
    Option<"mutate", "mutate", "unsigned", /*default=*/"0",
           "Stage 53F's mutation gate: pin the Nth deduced double as int32_t so the file fails to build naming its site">
  ];
  let dependentDialects = ["::mlir::emitc::EmitCDialect"];
}

def CTNativePruneDeadStores : Pass<"ctnative-prune-dead-stores"> {
  let summary = "Erase emitc.variables that are only ever assigned, with their assigns, and what then dies";
  let description = [{
    Part 24 Phase 63 Step 7: the generated file compiles clean under
    -Wall -Wextra -Werror, and any diagnostic the C++ compiler emits on
    generated code is a ctcompile bug. `--convert-scf-to-emitc` gives every
    loop result an `emitc.variable`, assigns it on every path and loads it
    after the loop; a result nothing reads is then a load nothing uses (which
    a canonicalize removes) and a variable that is set but never read (which
    it does not: an assign has a write effect). This pass erases every
    variable whose uses are all `emitc.assign` targets - or that has no use
    at all - together with those assigns, then erases whatever became
    trivially dead, to a fixpoint. A variable read through `emitc.load`,
    `emitc.member` or anything else is untouched.
  }];
  let statistics = [
    Statistic<"prunedVariables", "pruned-variables", "emitc.variables erased because nothing ever read them">,
    Statistic<"prunedOps", "pruned-ops", "operations erased as trivially dead once their store was gone">,
    Statistic<"markedStatements", "marked-statements", "calls whose result nothing reads, marked to print as a statement rather than an unused variable">
  ];
  let options = [
    Option<"report", "report", "bool", /*default=*/"false",
           "Emit the exact prune counts as a remark (the release LLVM package compiles pass statistics out)">
  ];
  // THE LAST TWO ARE THE PRICE OF THE PDLL PATTERN. PruneDeadStores.pdll is
  // compiled to a C++ file that embeds the pattern as PDL *text* and parses it
  // at run time, and the pattern set then lowers PDL to pdl_interp bytecode -
  // so both dialects must be loaded in the context before the pass runs, or
  // the parse fails on an unregistered dialect inside a pass body.
  let dependentDialects = [
    "::mlir::emitc::EmitCDialect",
    "::mlir::pdl::PDLDialect",
    "::mlir::pdl_interp::PDLInterpDialect"
  ];
}

#endif // CTCOMPILE_CTNATIVE_TRANSFORMS_PASSES_TD
