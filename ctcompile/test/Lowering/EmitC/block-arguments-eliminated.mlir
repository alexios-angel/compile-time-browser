// THE FIX FOR THE MISCOMPILE, AND THE TEST RUNS IT.
//
// block-argument-hazard.mlir in this directory pins the defect: the C++ emitter
// serialises a branch edge's parallel copy one assignment at a time, so an edge
// that swaps two of a block's own arguments reads a destination it has already
// written. That test asserts the BAD output, because the bug is in the pinned
// LLVM and the backend has to route around it.
//
// This is the routing. --emitc-eliminate-block-arguments gives every non-entry
// block argument an emitc.variable, reads it at the top of its block, and writes
// it on each incoming edge - so every read is issued before any write, which is
// what a parallel copy means.
//
// THE SECOND RUN LINE IS THE WHOLE TEST. Reading the emitted C++ and agreeing
// that it looks right is what would have accepted the original bug: that code
// compiles, and `v8 = v9; v9 = v8;` looks like a swap. So this compiles the
// output, RUNS it, and lets the program's own exit status be the assertion.
// Before the pass it prints 20 where 10 is correct and exits 1; after it, 0.
//
// The loop swaps (10, 20) once per iteration, so the answer is 10 for even n
// and 20 for odd - a fact about the IR, not about the emitter.

// RUN: ctjs-opt %s --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top \
// RUN:   | FileCheck %s

// RUN: ctjs-opt %s --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp \
// RUN:   && %cxx_exe %t.cpp -o %t.exe && %t.exe

// THE INCLUDES GO FIRST, and emitc.include is how - a trailing emitc.verbatim
// lands after the function that needs the declarations.
emitc.include <"cstdint">
emitc.include <"cstdio">

emitc.func @swaploop(%n: i32) -> i32 {
  %a0 = "emitc.constant"() <{value = 10 : i32}> : () -> i32
  %b0 = "emitc.constant"() <{value = 20 : i32}> : () -> i32
  %i0 = "emitc.constant"() <{value = 0 : i32}> : () -> i32
  cf.br ^loop(%a0, %b0, %i0 : i32, i32, i32)

^loop(%a: i32, %b: i32, %i: i32):
  %one = "emitc.constant"() <{value = 1 : i32}> : () -> i32
  %inext = emitc.add %i, %one : (i32, i32) -> i32
  %more = emitc.cmp lt, %i, %n : (i32, i32) -> i1
  cf.cond_br %more, ^loop(%b, %a, %inext : i32, i32, i32), ^exit

^exit:
  emitc.return %a : i32
}

// THE READS, AT THE TOP OF THE BLOCK THAT READS THEM. Both loads are issued
// before control reaches any edge, which is what makes the writes below safe in
// either order.
// CHECK:      [[LOOP:label[0-9]+]]:
// CHECK-NEXT: [[A:v[0-9]+]] = [[VA:v[0-9]+]];
// CHECK-NEXT: [[B:v[0-9]+]] = [[VB:v[0-9]+]];

// AND THE WRITES, ON AN EDGE OF THEIR OWN. Each names a variable as its
// destination and a value LOADED EARLIER as its source - never a variable this
// same edge has just assigned, which is exactly the difference from the
// hazard test.
// CHECK:      [[VA]] = [[B]];
// CHECK-NEXT: [[VB]] = [[A]];

// --- THE SAME SUCCESSOR TWICE, WHICH IS WHY EDGES ARE SPLIT ----------------
//
// `cf.cond_br %c, ^join(%x), ^join(%y)` is legal, and the two edges carry
// DIFFERENT values into the same block. Assigning both sets before the branch -
// the obvious placement, and correct for every single-successor terminator -
// performs both assignments on whichever path is taken, so the second wins
// unconditionally and `pick` returns %y for every input.
//
// Nothing about the swap above catches this: it has one successor carrying
// operands, so in-place assignment works there. The case needs its own
// function, and the program below calls it with both values of the condition.
// CHECK-LABEL: int32_t pick(
emitc.func @pick(%c: i1, %x: i32, %y: i32) -> i32 {
  cf.cond_br %c, ^join(%x : i32), ^join(%y : i32)
^join(%v: i32):
  emitc.return %v : i32
}

// The program that settles it. An answer of 20 for an even n is the bug.
emitc.verbatim "int main() {"
emitc.verbatim "  int bad = 0;"
emitc.verbatim "  for (int32_t n = 0; n <= 6; ++n) {"
emitc.verbatim "    int32_t got = swaploop(n);"
emitc.verbatim "    int32_t want = (n % 2 == 0) ? 10 : 20;"
emitc.verbatim "    if (got != want) { ++bad; std::printf(\22n=%d gave %d, want %d\5Cn\22, n, got, want); }"
emitc.verbatim "  }"
emitc.verbatim "  if (pick(true, 7, 9) != 7) { ++bad; std::printf(\22pick(true) lost its edge\5Cn\22); }"
emitc.verbatim "  if (pick(false, 7, 9) != 9) { ++bad; std::printf(\22pick(false) lost its edge\5Cn\22); }"
emitc.verbatim "  return bad;"
emitc.verbatim "}"
