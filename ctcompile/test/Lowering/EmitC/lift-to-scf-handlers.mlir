// WHY try/catch DOES NOT LIFT, WHICH OF THE TWO OPERATIONS IS ACTUALLY IN THE
// WAY, AND WHAT THE FIX HAS TO PRODUCE.
//
// Stage 45A of ctcompile-plan/22-lexical-cpp-backend.md. Measured on the
// corpora with --ctjs-lift-to-scf: 41 functions are refused - bootstrap 2,
// p5 27, phaser 12 - and every one of them reports
//
//     'ctjs.push_handler' op transformation does not support terminators with
//     side effects
//
// THAT NUMBER NAMES ONLY HALF THE PROBLEM, and the first function below is why.
// `checkTransformationPreconditions` walks the region and INTERRUPTS on the
// first operation that fails, so a function with a `try` reports whichever of
// its two offending terminators the walk reached first - and `ctjs.push_handler`
// always precedes the `ctjs.check`s inside its own body. `ctjs.check` is
// refused for exactly the same reason and never gets to say so.
//
// It is the harder half, too. There is ONE push_handler per protected region
// and one check per FALLIBLE OPERATION inside it, and a check carries the
// register snapshot that makes the handler see the frame as of the throw.
//
// THE PRECONDITION IS `isMemoryEffectFree`, and an operation that does not
// implement MemoryEffectOpInterface at all is not memory-effect-free - the
// interface's own contract, not a quirk. Neither of ours implements it. So
// "confirm rather than assume", which the plan asks for, comes out: BOTH are
// side-effecting as far as the utility is concerned, and neither can simply
// have the trait removed. `ctjs.check` really does read mutable state - the
// frame's caught status, which the fallible operation before it just set - so
// declaring it effect-free would licence CSE to merge two checks taken after
// two different calls.
//
// HOW "NO cf OPERATIONS LEFT" IS ASSERTED, because two obvious spellings of it
// are assertions that cannot fail, and both were tried here before this line
// was written.
//
//   1. A plain `CHECK-NOT: cf.` covers only the gap between its neighbouring
//      directives, not the function - and "cf." is a SUBSTRING of "scf.", so it
//      matches every structured operation the pass just produced. That is the
//      mistake that once reported this project's structuring rate as 48% when
//      it was 96.8%.
//   2. `--implicit-check-not=[^s]cf\.` covers the whole input and still cannot
//      fail: --implicit-check-not takes a FileCheck PATTERN, where a regular
//      expression has to be wrapped in `{{...}}`. Bare, it is the literal
//      nine-character string, which no output contains. Measured: against the
//      UNLIFTED module - `cf.br` on the second line of it - FileCheck exits 0.
//
// The wrapped form exits 1 on that same input and 0 on the lifted one, which is
// what makes it an assertion.
//
// RUN: ctjs-opt %s --ctjs-lift-to-scf | FileCheck %s --implicit-check-not='{{[^s]cf\.}}'

// --- THE HALF THE CORPUS NUMBER HIDES ---------------------------------------
//
// A `ctjs.check` with no `ctjs.push_handler` anywhere. Nothing in the dialect
// forbids it - CheckOp::verify asks only that both successors are in this
// region - and it is refused on its own, by name.
//
// CHECK-LABEL: ctjs.func @check_alone
// CHECK-SAME: ctjs.not_structured = "'ctjs.check' op{{.*}}side effects{{.*}}"
// CHECK: ctjs.check
ctjs.func @check_alone(%receiver: !ctjs.value, %new_target: !ctjs.value,
                       %callee: !ctjs.value, %f: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  ctjs.check ^cont(%f : !ctjs.value) caught ^pad(%f : !ctjs.value)
^cont(%b: !ctjs.value):
  ctjs.return %b
^pad(%p: !ctjs.value):
  %pad_id, %thrown = ctjs.catch_land
  ctjs.return %thrown
}

// --- AND WHAT THE SPLIT HAS TO PRODUCE --------------------------------------
//
// The plan's prescription is to split each offender into a side-effecting
// NON-terminator plus a pure terminator. This is that shape, written by hand so
// the target can be pinned before the dialect changes: `ctjs.pop_handler`
// stands in for the side-effecting non-terminator, and `ctjs.truthy` for the
// "was it caught" read that would replace `ctjs.check`'s status poll.
//
// THE PURE TERMINATOR MUST BE `cf.cond_br`, NOT A NEW PURE CTJS TERMINATOR.
// `mlir::ControlFlowToSCFTransformation` - the interface implementation this
// pass reuses - builds an `scf.if` from a `cf::CondBranchOp` and an
// `scf.index_switch` from a `cf::SwitchOp`, and knows no other multi-successor
// operation. On LLVM 23 that is an explicit precondition,
// `canConvertMultiSuccessorBranchOp`; on the pinned 22.1.8 the check does not
// exist yet, so a new pure terminator would pass the preconditions and fail
// later, inside createStructuredBranchRegionOp, with a worse message.
//
// CHECK-LABEL: ctjs.func @guarded_split
// CHECK-NOT: ctjs.not_structured
// CHECK: scf.if
// CHECK: ctjs.catch_land
ctjs.func @guarded_split(%receiver: !ctjs.value, %new_target: !ctjs.value,
                         %callee: !ctjs.value, %f: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  ctjs.pop_handler
  cf.br ^body(%f : !ctjs.value)
^body(%b: !ctjs.value):
  %caught = ctjs.truthy %b
  cf.cond_br %caught, ^pad(%b : !ctjs.value), ^cont(%b : !ctjs.value)
^cont(%c: !ctjs.value):
  ctjs.pop_handler
  ctjs.return %c
^pad(%p: !ctjs.value):
  %pad_id, %thrown = ctjs.catch_land
  ctjs.return %thrown
}

// AND WITH THE HANDLER INSIDE A LOOP, which is the case that says something.
// The pad is reached from inside a cycle, so the algorithm cannot simply make
// an `scf.if` of it: it hoists the pad OUT of the loop behind an
// `scf.index_switch` fed by an edge multiplexer.
//
// THAT MOVES `ctjs.catch_land` PAST THE LOOP EPILOGUE, and it is still correct
// for one specific reason worth writing down: catch_land READS state the
// unwinder already wrote into the frame - the ip and the catch register - and
// that state persists until something clears it. It is not a snapshot of a
// moment. A structuring that reordered a WRITE like that would not be safe, and
// `ctjs.pop_handler` is exactly such a write: its balance is a whole-function
// invariant no verifier can see, so a transformation that ever duplicated one
// onto two paths would silently take a CALLER's handler. Not observed here -
// the utility inserts multiplexers rather than duplicating blocks - and worth
// re-checking on the corpus when the split lands.
//
// CHECK-LABEL: ctjs.func @guarded_loop
// CHECK-NOT: ctjs.not_structured
// CHECK: scf.while
// CHECK: scf.index_switch
// CHECK: ctjs.catch_land
ctjs.func @guarded_loop(%receiver: !ctjs.value, %new_target: !ctjs.value,
                        %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  cf.br ^head(%n : !ctjs.value)
^head(%i: !ctjs.value):
  %more = ctjs.truthy %i
  cf.cond_br %more, ^body(%i : !ctjs.value), ^done(%i : !ctjs.value)
^body(%j: !ctjs.value):
  %caught = ctjs.truthy %j
  cf.cond_br %caught, ^pad(%j : !ctjs.value), ^next(%j : !ctjs.value)
^next(%k: !ctjs.value):
  %step = ctjs.unary neg %k
  cf.br ^head(%step : !ctjs.value)
^pad(%p: !ctjs.value):
  %pad_id, %thrown = ctjs.catch_land
  cf.br ^done(%thrown : !ctjs.value)
^done(%r: !ctjs.value):
  ctjs.return %r
}
