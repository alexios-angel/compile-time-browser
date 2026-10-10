// Why try/catch does not lift to structured control flow, which of the two
// operations is actually in the way, and what a fix has to produce.
//
// When this test was written, every function --ctjs-lift-to-scf refused in the
// benchmark corpora (41 of them: Bootstrap 2, p5.js 27, Phaser 12) reported
// this, and only this, diagnostic:
//
//     'ctjs.push_handler' op transformation does not support terminators with
//     side effects
//
// That message names only half the problem, and the first function below shows
// why. `checkTransformationPreconditions` walks the region and stops at the
// first operation that fails, so a function with a `try` reports whichever of
// its two offending terminators the walk reached first, and `ctjs.push_handler`
// always precedes the `ctjs.check`s inside its own body. `ctjs.check` is
// refused for exactly the same reason and never gets to say so.
//
// It is also the harder half. There is one push_handler per protected region
// and one check per fallible operation inside it, and a check carries the
// register snapshot that makes the handler see the frame as of the throw.
//
// The precondition is `isMemoryEffectFree`, and an operation that does not
// implement MemoryEffectOpInterface at all is not memory-effect-free; that is
// the interface's own contract, not a quirk. Neither of ours implements it, so
// both count as side-effecting to the utility, and neither can simply have the
// trait removed. `ctjs.check` really does read mutable state: the frame's
// caught status, which the fallible operation before it just set. Declaring it
// effect-free would license CSE to merge two checks taken after two different
// calls.
//
// How "no cf operations left" is asserted matters, because the two obvious
// spellings of it are assertions that cannot fail, and the lift's output is
// full of `scf.` operations that a careless pattern also matches. Both were
// tried before the form in the RUN line below:
//
//   1. A plain `CHECK-NOT: cf.` covers only the gap between its neighbouring
//      directives, not the function. And "cf." is a substring of "scf.", so
//      it matches every structured operation the pass just produced: the
//      assertion is both too narrow and wrong.
//
//   2. `--implicit-check-not=[^s]cf\.` covers the whole input and still cannot
//      fail: --implicit-check-not takes a FileCheck pattern, where a regular
//      expression has to be wrapped in `{{...}}`. Bare, it is the literal
//      nine-character string, which no output contains. Measured: against the
//      unlifted module (`cf.br` on its second line), FileCheck exits 0.
//
// The wrapped form exits 1 on that same input and 0 on the lifted one, which is
// what makes it an assertion.
//
// RUN: ctjs-opt %s --ctjs-lift-to-scf | FileCheck %s --implicit-check-not='{{[^s]cf\.}}'

// --- The half the corpus number hides ----------------------------------------
//
// A `ctjs.check` with no `ctjs.push_handler` anywhere. Nothing in the dialect
// forbids it (CheckOp::verify asks only that both successors are in this
// region), and it is refused on its own, by name.
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

// --- What a split has to produce ---------------------------------------------
//
// The proposed fix is to split each offender into a side-effecting
// non-terminator plus a pure terminator. This is that shape, written by hand so
// the target can be pinned before the dialect changes: `ctjs.pop_handler`
// stands in for the side-effecting non-terminator, and `ctjs.truthy` for the
// "was it caught" read that would replace `ctjs.check`'s status poll.
//
// The pure terminator must be `cf.cond_br`, not a new pure CTJS terminator.
// `mlir::ControlFlowToSCFTransformation`, the interface implementation this
// pass reuses, builds an `scf.if` from a `cf::CondBranchOp` and an
// `scf.index_switch` from a `cf::SwitchOp`, and knows no other multi-successor
// operation; its `canConvertMultiSuccessorBranchOp` precondition rejects any
// other multi-successor terminator up front. A new pure CTJS terminator would
// therefore be refused by the lift's preconditions rather than structured, and
// the pass would gain nothing from it.
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

// The same with the handler inside a loop, which is the informative case.
// The pad is reached from inside a cycle, so the algorithm cannot simply make
// an `scf.if` of it: it hoists the pad out of the loop behind an
// `scf.index_switch` fed by an edge multiplexer.
//
// That moves `ctjs.catch_land` past the loop epilogue, and it is still correct
// for one specific reason: catch_land reads state the unwinder already wrote
// into the frame (the ip and the catch register), and that state persists until
// something clears it; it is not a snapshot of a moment. A structuring that
// reordered a write like that would not be safe, and `ctjs.pop_handler` is
// exactly such a write: its balance is a whole-function invariant no verifier
// can see, so a transformation that ever duplicated one onto two paths would
// silently take a caller's handler. The utility inserts multiplexers rather
// than duplicating blocks, so this does not happen here; re-check it if
// ctjs.check/push_handler are ever split as described above.
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
