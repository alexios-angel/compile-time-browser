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

def EmitCEliminateBlockArguments
    : Pass<"emitc-eliminate-block-arguments", "mlir::ModuleOp"> {
  let summary = "Replace non-entry block arguments with EmitC variables";
  let description = [{
    BECAUSE THE C++ EMITTER LOSES A COPY on a block-argument edge. It
    serialises the parallel copy one assignment at a time with no temporaries,
    so an edge whose new value for one block argument is another block argument
    of the same block reads a destination already overwritten.
    test/Lowering/EmitC/block-argument-hazard.mlir compiles and RUNS that
    miscompile.

    Not a workaround - the lowering. Each block argument becomes an
    emitc.variable declared in the entry block, read at the top of its block and
    written on each incoming edge, so every read precedes every write.

    THE ENTRY BLOCK IS LEFT ALONE: its arguments are the function's C++
    parameters, no edge writes them, and the hazard cannot reach them.
  }];
  let dependentDialects = ["::mlir::emitc::EmitCDialect", "::mlir::cf::ControlFlowDialect"];
}

def CTJSLiftToSCF : Pass<"ctjs-lift-to-scf", "mlir::ModuleOp"> {
  let summary = "Recover structured control flow from the CFG the importer built";
  let description = [{
    Bytecode is a register machine with relative jumps, so the importer emits
    blocks and cf.br - and the C++ that falls out is a chain of `goto label9`.
    That is correct, unreadable, and opaque to the C++ optimiser, which cannot
    see a loop to unroll or a value to keep in a register.

    THE ALGORITHM IS UPSTREAM'S AND SO IS THE HARD PART. transformCFGToSCF
    implements Bahmann et al.'s "Perfect Reconstructability of Control Flow
    from Demand Dependence Graphs", lifting an ARBITRARY CFG - irreducible ones
    included - by inserting edge multiplexers. mlir::ControlFlowToSCFTransformation
    is a public implementation of the interface it needs.

    WHAT IS OURS IS THE WALK, AND ONLY BECAUSE UPSTREAM'S IS TOO NARROW:
    --lift-cf-to-scf does `op->walk([](func::FuncOp))`, matching by TYPE rather
    than by FunctionOpInterface. ctjs.func is a FunctionOpInterface and is not a
    func::FuncOp, so that pass matches nothing, reports success, and changes
    nothing - measured as twelve cf operations before and twelve after.

    A REGION THE UTILITY REFUSES IS LEFT IN CFG FORM rather than failing the
    module. The EmitC backend reads CFG perfectly well; lifting is an
    improvement, not a precondition.

    ONE THING RUNS BEFORE THE LIFT AND IS OURS: trivial-phi removal (Braun et
    al. 2013 §3.1) on the CFG. The importer gives every block the whole
    register file as arguments, so a loop header carries one for a variable
    assigned before the loop and never inside it - fed by the variable on the
    entry edge and by ITSELF on the back edge, which is the one shape
    simplifyRegions does not drop. Left in place it becomes a loop-carried
    value, and a closed-shape object literal threaded through a loop that way
    reads as open and is refused by the native tier. Part 24 Phase 56B's
    obligation O-3: one stack slot, however many SSA values reach it.
  }];
  let dependentDialects = ["::mlir::scf::SCFDialect", "::mlir::ub::UBDialect",
                           "::mlir::arith::ArithDialect",
                           "::mlir::cf::ControlFlowDialect"];
  let statistics = [
    Statistic<"droppedSelfCarriedArguments", "self-carried-args-dropped",
              "block arguments fed by themselves and one other value, replaced by that value (O-3)">
  ];
  let options = [
    Option<"report", "report", "bool", /*default=*/"false",
           "Emit the exact count of dropped self-carried arguments as a remark (the release LLVM package compiles pass statistics out)">
  ];
}

def CTJSDropUncompiled : Pass<"ctjs-drop-uncompiled", "mlir::ModuleOp"> {
  let summary = "Remove the functions the backend refused, so the rest can be translated";
  let description = [{
    A module still holding CTJS operations cannot be translated to C++ at all,
    so a file where anything is refused produces nothing rather than the parts
    that did compile. THAT IS WHAT A REAL COMPILER DOES WITH THIS: emit what it
    can and leave the rest to the interpreter, which is safe because the runtime
    only calls a compiled body when function_proto::aot_entry is set.

    SEPARATE FROM THE LOWERING, because erasing there would destroy the
    refusals before anybody could read them - and the refusals are the work
    list. Running this is the caller saying "I want a translation unit now".
  }];
}

def CTJSResolveGlobals : Pass<"ctjs-resolve-globals", "mlir::ModuleOp"> {
  let summary = "Name the callee of every call through a once-bound global - the closed world";
  let description = [{
    PHASE 62½-A. A whole-program compile knows every call site, and nothing in
    the importer's IR says so: `function add` is a ctjs.create_closure of
    function index 1 stored into the global "add", and every `add(x, x)` is a
    ctjs.load_global followed by a ctjs.call through the dispatcher. This pass
    proves, per global NAME across the whole module, that the name is bound
    exactly once - in the top level's hoisting prologue, to one closure of
    function F, before anything else runs - and that nothing in the program
    can write the globals table dynamically; then every
    `load_global "g"; call` becomes a ctjs.call_direct @F, and F is made
    private when every caller it has is one of those.

    A global that fails the proof stays a ctjs.call at every site - the
    diagnostic `callee unresolved` of part 24 §Phase 62½ - and the module
    attribute `ctjs.globals` records every name with its store count, what it
    resolved to and why or why not, so the verdict is in the IR.

    TWO OF ITS CLAUSES ARE PER NAME RATHER THAN PER MODULE, and neither was
    when they were written. A function the importer refused used to refuse
    every global in the program, because its `ctjs.store_global`s are not here
    to be counted; the importer now summarises each refused body from the
    BYTECODE it still has - the exact set of names its op::set_globals name -
    so only the names in that set are refused. And a `.constructor` read used
    to be assumed to be `Function`; it is now assumed so only when the RECEIVER
    can be a function, which `super(t, e)`, an object literal and a primitive
    cannot be, and a named read off such a value stops the taint unless the
    name is one of the few that hand `Function` back.

    C++ RATHER THAN DRR, and part 23 §1.2 is why: this is a module-wide
    census - a symbol table, a count of stores per name, an SSA reachability
    from the global object - and a Pat<> matches one operation's shape; it
    cannot count across a module.
  }];
  let statistics = [
    Statistic<"resolvedGlobals", "resolved-globals",
              "globals bound exactly once, in the prologue, to one closure">,
    Statistic<"rewrittenCalls", "rewritten-calls",
              "ctjs.call sites rewritten to ctjs.call_direct">,
    Statistic<"closedFunctions", "closed-functions",
              "functions made private because every caller is a ctjs.call_direct">
  ];
  let options = [
    Option<"report", "report", "bool", /*default=*/"false",
           "Emit the resolved-global, rewritten-call and closed-function counts as a remark (the release LLVM package compiles pass statistics out, so --mlir-pass-statistics measures nothing)">
  ];
}

#endif // CTCOMPILE_CTJS_PASSES
