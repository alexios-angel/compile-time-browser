// THE C++ EMITTER LOSES A COPY ON A BLOCK-ARGUMENT EDGE, AND THIS IS THAT BUG.
//
// It is not a style note. `mlir-translate --mlir-to-cpp` in the pinned LLVM
// (22.1.8) serialises the parallel copy on a branch edge naively - it assigns
// the block arguments one at a time, in order, with no temporaries - so an edge
// where one block argument's new value is ANOTHER block argument of the same
// block reads a destination that has already been overwritten.
//
// MEASURED, NOT INFERRED. The emitted function below was compiled and run:
//
//     n=0 -> 10   n=1 -> 20   n=2 -> 20   n=3 -> 20   n=4 -> 20   n=5 -> 20
//
// Each iteration swaps (a, b) from (10, 20), so MLIR's answer is 10 for even n
// and 20 for odd. n=2 and n=4 are wrong. No diagnostic, exit status 0.
//
// WHY THIS PROJECT IS EXPOSED TO IT SPECIFICALLY. The bytecode importer models
// the register file as BLOCK ARGUMENTS - that is its design, chosen over SSA
// construction - so every register live across a branch is a block argument,
// and any loop that permutes two registers across its back edge is exactly the
// shape below. A `[a, b] = [b, a]` in a loop is not an exotic program.
//
// THE HAZARD IS CONDITIONAL, WHICH IS WHY IT NEEDS A TEST. Route the same swap
// through an intermediate block and the emitter is correct, because the extra
// edge introduces the temporaries by accident; the first attempt at this test
// did that and passed. It only loses when a block argument's new value is a
// block argument of the SAME block and the destination is written before the
// source is read.
//
// SO THE BACKEND MUST NOT EMIT BLOCK ARGUMENTS ON NON-ENTRY BLOCKS. It lowers
// them to an emitc.variable per argument, with every read issued before any
// write. This file exists so that if anybody ever removes that step, or decides
// block arguments look fine because a simpler case worked, it fails here with
// the reason attached rather than in a rendered page.
//
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
  // A TRUE SWAP ACROSS THE BACK EDGE, with no intermediate block.
  cf.cond_br %more, ^loop(%b, %a, %inext : i32, i32, i32), ^exit

^exit:
  emitc.return %a : i32
}

// THE LOST COPY, ASSERTED. The two assignments name the same variable as
// destination and source in sequence: the second reads what the first just
// wrote, so `b` ends up holding the new `a` rather than the old one.
//
// Anchored inside the taken branch, because the same two-assignment shape
// appears harmlessly where the loop is entered.
// CHECK:      if ({{v[0-9]+}}) {
// CHECK-NEXT: [[A:v[0-9]+]] = [[B:v[0-9]+]];
// CHECK-NEXT: [[B]] = [[A]];
