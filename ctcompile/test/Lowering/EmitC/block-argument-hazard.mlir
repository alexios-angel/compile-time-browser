// The C++ emitter loses a copy on a block-argument edge; this test pins that bug.
//
// `mlir-translate --mlir-to-cpp` (seen in LLVM 22.1.8; the assertion at the end
// shows whether the pinned version still does it) serialises the parallel copy
// on a branch edge naively: it assigns the block arguments one at a time, in
// order, with no temporaries. So an edge where one block argument's new value
// is another block argument of the same block reads an overwritten variable.
//
// This was measured, not inferred. The emitted function below, compiled and run:
//
//     n=0 -> 10   n=1 -> 20   n=2 -> 20   n=3 -> 20   n=4 -> 20   n=5 -> 20
//
// Each iteration swaps (a, b) from (10, 20), so MLIR's answer is 10 for even n
// and 20 for odd. n=2 and n=4 are wrong, with no diagnostic and exit status 0.
//
// This project is exposed to it because the bytecode importer models the
// register file as block arguments (by design, instead of SSA construction),
// so every register live across a branch is a block argument, and any loop
// that permutes two registers across its back edge has exactly the shape
// below. A `[a, b] = [b, a]` in a loop is not an exotic program.
//
// The hazard is conditional, which is why it needs a test. Route the same swap
// through an intermediate block and the emitter is correct, because the extra
// edge introduces the temporaries by accident; a first version of this test
// did that and passed. It only loses when a block argument's new value is a
// block argument of the same block and the destination is written before the
// source is read.
//
// So the backend must not emit block arguments on non-entry blocks.
// --emitc-eliminate-block-arguments lowers them to an emitc.variable per
// argument, with every read issued before any write (see
// block-arguments-eliminated.mlir). If that step is ever removed, or block
// arguments are judged safe because a simpler case worked, this file explains
// why the output is wrong.
// If a later LLVM fixes this, the CHECK below stops matching and the reason to
// keep the elimination step disappears with it. That is the intended way for
// this file to die.

// RUN: mlir-translate %s --mlir-to-cpp --declare-variables-at-top | FileCheck %s

emitc.func @swaploop(%n: i32) -> i32 {
  %a0 = "emitc.constant"() <{value = 10 : i32}> : () -> i32
  %b0 = "emitc.constant"() <{value = 20 : i32}> : () -> i32
  %i0 = "emitc.constant"() <{value = 0 : i32}> : () -> i32
  cf.br ^loop(%a0, %b0, %i0 : i32, i32, i32)

^loop(%a: i32, %b: i32, %i: i32):
  %one = "emitc.constant"() <{value = 1 : i32}> : () -> i32
  %inext = emitc.add %i, %one : (i32, i32) -> i32
  %more = emitc.cmp lt, %i, %n : (i32, i32) -> i1
  // A true swap across the back edge, with no intermediate block.
  cf.cond_br %more, ^loop(%b, %a, %inext : i32, i32, i32), ^exit

^exit:
  emitc.return %a : i32
}

// The lost copy. The two assignments name the same variable as destination
// and source in sequence: the second reads what the first just wrote, so `b`
// ends up holding the new `a` rather than the old one.
//
// Anchored inside the taken branch, because the same two-assignment shape
// appears harmlessly where the loop is entered.
// CHECK:      if ({{v[0-9]+}}) {
// CHECK-NEXT: [[A:v[0-9]+]] = [[B:v[0-9]+]];
// CHECK-NEXT: [[B]] = [[A]];
