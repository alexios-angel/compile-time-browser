// Checks the workaround for the C++ emitter's block-argument miscompile by
// compiling and running the output.
//
// block-argument-hazard.mlir pins the defect: the emitter serialises a branch
// edge's parallel copy one assignment at a time, so an edge that swaps two of a
// block's own arguments reads a destination it has already written. That test
// asserts the bad output, because the bug is in the pinned LLVM.
//
// --emitc-eliminate-block-arguments is the workaround: it gives every non-entry
// block argument an emitc.variable, reads it at the top of its block, and
// writes it on each incoming edge, so every read is issued before any write,
// which is what a parallel copy means.
//
// The second RUN line is the real test. Reading the emitted C++ and agreeing
// that it looks right is what would have accepted the original bug: that code
// compiles, and `v8 = v9; v9 = v8;` looks like a swap. So this compiles the
// output, runs it, and lets the program's own exit status be the assertion.
// Without the pass it prints 20 where 10 is correct and exits 1; with it, 0.
//
// The loop swaps (10, 20) once per iteration, so the answer is 10 for even n
// and 20 for odd - a fact about the IR, not about the emitter.

// RUN: ctjs-opt %s --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top \
// RUN:   | FileCheck %s

// RUN: ctjs-opt %s --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp \
// RUN:   && %cxx_exe %t.cpp -o %t.exe && %t.exe

// The includes go first, so they use emitc.include; a trailing emitc.verbatim
// would land after the function that needs the declarations.
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

// The reads, at the top of the block that reads them. Both loads are issued
// before control reaches any edge, which is what makes the writes below safe in
// either order.
// CHECK:      [[LOOP:label[0-9]+]]:
// CHECK-NEXT: [[A:v[0-9]+]] = [[VA:v[0-9]+]];
// CHECK-NEXT: [[B:v[0-9]+]] = [[VB:v[0-9]+]];

// The writes, on an edge of their own. Each names a variable as its
// destination and a value loaded earlier as its source, never a variable this
// same edge has just assigned; that is the difference from the hazard test.
//
// CHECK:      [[VA]] = [[B]];
// CHECK-NEXT: [[VB]] = [[A]];

// --- The same successor twice, which is why edges are split -----------------
//
// `cf.cond_br %c, ^join(%x), ^join(%y)` is legal, and the two edges carry
// different values into the same block. Assigning both sets before the branch
// (the obvious placement, and correct for every single-successor terminator)
// performs both assignments on whichever path is taken, so the second wins
// unconditionally and `pick` returns %y for every input.
//
// The swap above does not catch this: it has one successor carrying operands,
// so in-place assignment works there. The case needs its own function, and the
// program below calls it with both values of the condition.
// CHECK-LABEL: int32_t pick(
emitc.func @pick(%c: i1, %x: i32, %y: i32) -> i32 {
  cf.cond_br %c, ^join(%x : i32), ^join(%y : i32)
^join(%v: i32):
  emitc.return %v : i32
}

// The program that checks both. An answer of 20 for an even n is the bug.
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
