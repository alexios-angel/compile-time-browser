//===- Passes.td - ctnative passes ---------------------------*- tablegen -*-===//
//
// THE NATIVE TIER'S PASSES. Part 24 §1.2: the output links neither the
// interpreter nor its collector, and anything unproved is a diagnostic.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_TRANSFORMS_PASSES_TD
#define CTCOMPILE_CTNATIVE_TRANSFORMS_PASSES_TD

include "mlir/Pass/PassBase.td"

def CTNativePruneUnreachable : Pass<"ctnative-prune-unreachable", "mlir::ModuleOp"> {
  let summary = "Remove private functions unreachable from checked symbolic and closure roots";
  let description = [{
    Optional bounded reachability analysis after partial evaluation. Public and
    external functions, the script entry and executable module operations are
    roots. Symbol references and numeric closure identities retain their targets,
    including the original callee values used by boxed dispatch. Ambiguous closure
    identities retain every target. Unknown references, scopes and exhausted work
    retain the entire module. No operation in a live function is removed.
  }];
  let options = [
    Option<"maxSteps", "max-steps", "unsigned", "100000", "Maximum graph discovery work">,
    Option<"report", "report", "bool", "false", "Report removed private functions">
  ];
  let dependentDialects = ["::ctcompile::ctjs::CTJSDialect"];
}

def CTNativePrecompute : Pass<"ctnative-precompute", "mlir::ModuleOp"> {
  let summary = "Precompute primitive expressions and simplify proved symbolic results";
  let description = [{
    Reuses the compiler's ctbrowser primitive adapter for literal operations.
    Symbolic result domains permit only identities valid for their proved
    JavaScript values. Known structured branches retain the selected effects
    exactly once. Calls and unknown coercions remain runtime operations;
    input annotations never authorize folding. Work is bounded.
  }];
  let options = [
    Option<"maxSteps", "max-steps", "unsigned", "100000", "Maximum symbolic analysis and rewrite work">,
    Option<"report", "report", "bool", "false", "Report precomputed expressions and control">
  ];
  let dependentDialects = ["::ctcompile::ctjs::CTJSDialect", "::mlir::scf::SCFDialect",
                          "::mlir::arith::ArithDialect", "::mlir::pdl::PDLDialect",
                          "::mlir::pdl_interp::PDLInterpDialect"];
}

def CTNativeSpecialize : Pass<"ctnative-specialize", "mlir::ModuleOp"> {
  let summary = "Create bounded direct-call variants for exact primitive argument tuples";
  let description = [{
    Specializes private, capture-free direct callees while keeping runtime
    parameters and effects. Exact literal argument tuples share one variant;
    original argument evaluation and the generic function remain. Captures,
    observed implicit state, nested closure construction and recursive call
    graphs are excluded. A generic call is retained for original native type
    inference. Limits bound variant count and cloned operations; annotations
    record outcomes but confer no authority on a repeated pass.
  }];
  let options = [
    Option<"maxVariants", "max-variants", "unsigned", "32", "Maximum new function variants">,
    Option<"maxClonedOps", "max-cloned-ops", "unsigned", "4096", "Maximum cloned body operations">,
    Option<"report", "report", "bool", "false", "Report function variants and redirected calls">
  ];
  let dependentDialects = ["::ctcompile::ctjs::CTJSDialect", "::mlir::scf::SCFDialect", "::mlir::arith::ArithDialect"];
}

def CTNativeSupercompile : Pass<"ctnative-supercompile", "mlir::ModuleOp"> {
  let summary = "Drive bounded recursive configurations and fold residual backedges";
  let description = [{
    Opt-in first supercompilation slice for private capture-free self-recursive
    kernels. Literal argument configurations simplify through shared primitive
    semantics; unknown branches remain residual. Pending exact configuration
    promises allow recursive folding. A finite-shape homeomorphic embedding
    whistle retains generic calls on growth, without assuming state equivalence.
    Heap, host, environment and non-self calls are outside this slice. The
    original generic definition and one external call remain for native proofs.
    Transactional identity/driven alternatives respect module work and code-size
    bounds. Generalization and full multi-result search remain future work.
  }];
  let options = [
    Option<"maxContexts", "max-contexts", "unsigned", "32", "Maximum retained configurations per module">,
    Option<"maxSteps", "max-steps", "unsigned", "20000", "Maximum local driving work including refused alternatives">,
    Option<"maxResidualOps", "max-residual-ops", "unsigned", "4096", "Maximum added residual operations per module">,
    Option<"maxGrowth", "max-growth", "unsigned", "512", "Maximum added operations per source kernel; generic code remains">,
    Option<"report", "report", "bool", "false", "Report kernels, configurations, folds and whistles">
  ];
  let dependentDialects = ["::ctcompile::ctjs::CTJSDialect", "::mlir::scf::SCFDialect",
                          "::mlir::arith::ArithDialect", "::mlir::pdl::PDLDialect",
                          "::mlir::pdl_interp::PDLInterpDialect"];
}

def CTNativeDeforest : Pass<"ctnative-deforest", "mlir::ModuleOp"> {
  let summary = "Infer snapshot producer/consumer strategies and eliminate proved intermediates";
  let description = [{
    A bounded Lumberhack-style strategy inference and elaboration slice for
    concrete native Map snapshots. Producer/consumer flow constraints select
    one scalar projection strategy or retain identity on unsupported, escaping
    or conflicting uses. Effect barriers preserve call-by-value observation
    order. Runs after native lowering and before expression canonicalization.
    This slice does not implement general recursive functional fusion.
  }];
  let options = [
    Option<"maxSnapshots", "max-snapshots", "unsigned", "256", "Maximum snapshot producers considered">,
    Option<"maxScan", "max-scan", "unsigned", "10000", "Maximum producer/consumer flow work">,
    Option<"report", "report", "bool", "false", "Report eliminated snapshot allocations">
  ];
  let dependentDialects = ["::mlir::emitc::EmitCDialect"];
}

def CTNativeBindingTimeAnalysis : Pass<"ctnative-binding-time-analysis", "mlir::ModuleOp"> {
  let summary = "Derive static and dynamic bindings, control and heap effects";
  let description = [{
    Offline staging facts for native partial evaluation. Rechecks constant
    caller arguments and tracks local heap state at each operation. Unknown
    writes, calls and control make affected reads dynamic; constants may have
    static values while runtime control still decides their execution.
    Function summaries propagate static closed calls conservatively. Loops
    remain dynamic until an invariant is proved. Fresh annotations include
    binding-time reasons, result/argument bindings and per-function counts.
    Input annotations are ignored. Staging facts do not replace independent
    environment, callable, ownership and exact-evaluation checks.
  }];
  let options = [Option<"report", "report", "bool", "false", "Report static/dynamic binding counts">];
  let dependentDialects = ["::ctcompile::ctjs::CTJSDialect", "::mlir::scf::SCFDialect", "::mlir::arith::ArithDialect"];
}

def CTNativePartialEvaluate : Pass<"ctnative-partial-evaluate", "mlir::ModuleOp"> {
  let summary = "Evaluate closed factories or static prefixes and residualize their reachable heap";
  let description = [{
    Opt-in native initialization specialization. Evaluates a private function
    when inputs are known and every executed effect stays within its fresh
    heap. Binding-Time Analysis identifies straight-line static initialization
    prefixes when explicit arguments or later operations remain dynamic. The
    residual graph and live constants precede the original runtime suffix.
    Primitive operations, known branches, bounded loops, nested direct calls,
    plain objects and proved standard Maps are supported. Residual CTJS allocates
    each live node once per runtime invocation, then initializes its final
    edges, preserving identity, sharing and Map order. The ordinary native
    ownership/type checks still decide which graphs have a C++ representation.

    Unsupported effects, unknown inputs, cyclic returned graphs and exhausted
    budgets leave the original body intact with ctnative.partial_eval_reason.
    Result and proof annotations are recomputed on every invocation. Run after
    ctjs-resolve-globals and before ctnative-lower-to-emitc; structured and basic
    CFG branches are accepted. No runtime VM heap is embedded in the output.
  }];
  let options = [
    Option<"maxSteps", "max-steps", "unsigned", "10000", "Maximum evaluated operations per factory">,
    Option<"maxNodes", "max-nodes", "unsigned", "256", "Maximum fresh heap nodes per factory">,
    Option<"maxDepth", "max-depth", "unsigned", "32", "Maximum nested evaluation depth">,
    Option<"report", "report", "bool", "false", "Report evaluated functions, residual nodes and declines">
  ];
  let dependentDialects = ["::ctcompile::ctjs::CTJSDialect", "::mlir::scf::SCFDialect", "::mlir::arith::ArithDialect"];
}

def CTNativeLowerToEmitC : Pass<"ctnative-lower-to-emitc", "mlir::ModuleOp"> {
  let summary = "Lower every PROVED function to EmitC over plain C++ types; diagnose the rest";
  let description = [{
    Part 24 Phase 62½-C, the first native artefact. Runs the Phase 54A type
    inference (with the closed-world global rule) over the module, and for
    every `ctjs.func` decides ONE of two things:

      * every `!ctjs.value` in it has a proved type the native tier can
        represent - `bool` as `bool`, `num<...>` as `double`, and optional
        scalar values as `ctnative::nullable_scalar`, whose tag preserves
        null, undefined, booleans and present numbers including NaN - and
        every operation is one this
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

    Known capture-free callback arguments are specialized first. When every
    caller supplies the same target, calls through the parameter become
    direct calls retaining the original closure value. A call-only parameter
    whose callback identity cannot escape is removed from all signatures and
    call sites; its closure is recreated locally for the capture lift. Raw
    argument windows, unknown callers and lexical receivers remain refused.
    Escaping callbacks retain their value and their native diagnostic.

    PHASE 59 SLICE 1 THEN RUNS INSIDE THIS PASS: closure conversion by
    LAMBDA LIFTING. A `ctjs.create_closure` whose every use is a call, whose
    captures are all immutable cells, and whose target is not already reached
    another way, is rewritten so the captured VALUES become extra LEADING
    parameters of the target and each call becomes a `ctjs.call_direct`
    passing them. The closure then allocates nothing, owns nothing and needs
    no functor - which is why it is the right first carrier for a tier with no
    collector. Anything else is refused by name and left for slice 2.
  }];
  let options = [
    Option<"report", "report", "bool", /*default=*/"false",
           "Emit the Phase 59 lift counts as a remark (the release LLVM package compiles pass statistics out)">,
    Option<"census", "census", "bool", /*default=*/"false",
           "Emit, as a remark, why each method-bearing object literal is NOT a closed shape - one bucket per blocking use, and the objects a single bucket alone blocks">
  ];
  // pdl AND pdl_interp ARE NOT AN EMITTED DIALECT, THEY ARE THE PATTERN'S
  // RUNTIME. `+x` on a proved number is erased by UnaryPlusIsIdentity.pdll, and
  // mlir-pdll does not compile a matcher: it emits PDL TEXT that MLIRParser
  // parses when the pattern set is built, PDLToPDLInterp lowers, and a
  // bytecode interpreter runs. Parsing that text needs both dialects loaded
  // before the pass runs, which is what this list is for.
  let dependentDialects = [
    "::ctcompile::ctnative::CTNativeDialect",
    "::mlir::emitc::EmitCDialect",
    "::mlir::pdl::PDLDialect",
    "::mlir::pdl_interp::PDLInterpDialect",
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
