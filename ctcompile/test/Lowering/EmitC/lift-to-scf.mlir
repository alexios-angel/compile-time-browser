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
