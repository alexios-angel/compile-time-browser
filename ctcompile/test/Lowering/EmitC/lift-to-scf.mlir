// RECOVERING THE LOOPS AND BRANCHES THE BYTECODE THREW AWAY.
//
// The importer emits a CFG because bytecode is jumps, and the C++ that falls
// out is `goto label9`. --ctjs-lift-to-scf puts the structure back, using
// upstream's transformCFGToSCF - Bahmann et al.'s reconstruction algorithm,
// which handles even irreducible control flow by inserting edge multiplexers.
//
// WHY THE PASS IS OURS WHEN THE ALGORITHM IS NOT. Upstream ships this as
// --lift-cf-to-scf, and that pass does `op->walk([](func::FuncOp))` - it
// matches by TYPE, not by FunctionOpInterface. `ctjs.func` IS a
// FunctionOpInterface and is NOT a func::FuncOp, so upstream's pass walks the
// module, matches nothing, reports success and changes nothing. Measured on
// this very file's first case: twelve cf operations before, twelve after, zero
// scf. Our pass is that walk and nothing else.
//
// MEASURED ON THE CORPORA, because a structuring pass that only works on
// hand-written IR is worth nothing:
//
//   bootstrap   570 functions   273 already straight-line ->  552 structured (96.8%)
//   p5         4309             2029                       -> 4125 (95.7%)
//   phaser     7628             4523                       -> 7575 (99.3%)
//
// IT IS NOT IN THE COMPILE PIPELINE. The EmitC backend reads a CFG and has no
// conversion for scf, so running this before it would refuse every function it
// structured. It is the foundation of the lexical backend and it earns its
// place by being measurable on its own.

// RUN: ctjs-opt %s --ctjs-lift-to-scf | FileCheck %s

// --- A BRANCH BECOMES AN IF -------------------------------------------------
//
// Both arms return, so the whole diamond collapses into one scf.if yielding a
// value - which is what makes the emitted C++ an `if/else` with a result
// rather than two labels and a join.
//
// CHECK-LABEL: ctjs.func @pick
// CHECK-NOT: cf.cond_br
// CHECK: scf.if
// CHECK: scf.yield
// CHECK: else
// CHECK: scf.yield
ctjs.func @pick(%receiver: !ctjs.value, %new_target: !ctjs.value,
                %callee: !ctjs.value, %c: !ctjs.value, %a: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %bit = ctjs.truthy %c
  cf.cond_br %bit, ^yes(%a : !ctjs.value), ^no(%c : !ctjs.value)
^yes(%x: !ctjs.value):
  ctjs.return %x
^no(%y: !ctjs.value):
  ctjs.return %y
}

// --- A BACK EDGE BECOMES A LOOP ---------------------------------------------
//
// The shape every `while` compiles to: a header that tests, a body that jumps
// back. transformCFGToSCF turns it into an scf.while, which is a real loop a
// C++ compiler can reason about rather than a cycle in a goto graph.
//
// SCF.WHILE AND NOT SCF.FOR, and that is not a shortcoming to fix: the trip
// count is not recoverable from bytecode, because `for` and `while` compile to
// the same jumps. Claiming a `for` here would be inventing information.
//
// CHECK-LABEL: ctjs.func @counter
// CHECK-NOT: cf.br
// CHECK: scf.while
ctjs.func @counter(%receiver: !ctjs.value, %new_target: !ctjs.value,
                   %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  cf.br ^head(%n : !ctjs.value)
^head(%i: !ctjs.value):
  %bit = ctjs.truthy %i
  cf.cond_br %bit, ^body(%i : !ctjs.value), ^done(%i : !ctjs.value)
^body(%j: !ctjs.value):
  %next = ctjs.unary neg %j
  cf.br ^head(%next : !ctjs.value)
^done(%k: !ctjs.value):
  ctjs.return %k
}

// --- A VALUE THE LOOP ONLY CARRIES IS NOT CARRIED ---------------------------
//
// The importer's loop header takes the whole register file, so a variable
// assigned before the loop and never inside it arrives as an argument fed by
// itself on the back edge and by the variable on the entry edge - the one
// shape simplifyRegions' dropRedundantArguments leaves alone, because the two
// operands differ. Lifted as it stands it becomes an iteration argument of
// the scf.while, and a result too when it is read after the loop: four values
// through a loop with one counter. For a number that is a copy; for an object
// literal it is a refusal, because its shape is no longer closed (part 24
// Phase 56B, obligation O-3). The pass drops the argument first - a trivial
// phi, Braun et al. 2013 §3.1 - so the loop carries the counter and its
// post-loop export and nothing else.
//
// PROVED LOAD-BEARING: with dropSelfCarriedArguments removed from the pass,
// this scf.while has four operands and four results, and this line is red.
//
// Measured with --mlir-pass-statistics, self-carried-args-dropped: this file 1
// (only %inv), native-struct.mlir's program 3 (`acc` in looped, leaked - and
// in reassigned NOTHING, its `acc` is a real phi - plus `i`'s twin in none of
// them: `i` is assigned inside every loop), native-struct-fixture.js 2 (`acc`
// and `n` in accumulate), native-fixture.js 0.
//
// CHECK-LABEL: ctjs.func @invariant
// CHECK-NOT: cf.br
// CHECK: scf.while ({{.*}}) : (!ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value)
ctjs.func @invariant(%receiver: !ctjs.value, %new_target: !ctjs.value,
                     %callee: !ctjs.value, %n: !ctjs.value, %m: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  cf.br ^head(%n, %m : !ctjs.value, !ctjs.value)
^head(%i: !ctjs.value, %inv: !ctjs.value):
  %bit = ctjs.truthy %i
  cf.cond_br %bit, ^body(%i, %inv : !ctjs.value, !ctjs.value),
                   ^done(%i, %inv : !ctjs.value, !ctjs.value)
^body(%j: !ctjs.value, %jinv: !ctjs.value):
  %next = ctjs.unary neg %j
  cf.br ^head(%next, %jinv : !ctjs.value, !ctjs.value)
^done(%k: !ctjs.value, %kinv: !ctjs.value):
  %sum = ctjs.binary add %k, %kinv
  ctjs.return %sum
}

// --- AND WHAT IT REFUSES, WHICH IS RECORDED RATHER THAN FATAL ---------------
//
// ctjs.push_handler is a TERMINATOR WITH SIDE EFFECTS - it installs a handler -
// and transformCFGToSCF refuses any region containing one, because it reorders
// and duplicates terminators freely.
//
// A REFUSAL MUST NOT FAIL THE RUN. The utility reports one by emitting a
// diagnostic, and an emitted error would fail the whole ctjs-opt invocation -
// so a single try/catch anywhere in a bundle would produce no output at all.
// The pass swallows the diagnostic and records the reason on the function,
// where this project's work lists already live. The function keeps its CFG and
// the CFG backend still compiles it.
//
// CHECK-LABEL: ctjs.func @guarded
// CHECK-SAME: ctjs.not_structured = "{{.*}}side effects{{.*}}"
// CHECK: ctjs.push_handler
ctjs.func @guarded(%receiver: !ctjs.value, %new_target: !ctjs.value,
                   %callee: !ctjs.value, %f: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  ctjs.push_handler ^body(%f : !ctjs.value) catch ^pad(%f : !ctjs.value)
^body(%b: !ctjs.value):
  ctjs.pop_handler
  ctjs.return %b
^pad(%p: !ctjs.value):
  ctjs.return %p
}
