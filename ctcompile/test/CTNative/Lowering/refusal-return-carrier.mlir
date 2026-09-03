// THE ONE REFUSAL THAT IS A FACT ABOUT TWO OPERATIONS - AND THE MEASUREMENT
// THAT SAYS NOTHING CAN REACH IT.
//
// Every other rule in `admission` is a question about ONE operation and the
// types on its operands. This one is not: `admission::returns` is state that
// SURVIVES from one ctjs.return to the next - the first return seen fixes the
// function's carrier, and a later return with a different one is
//
//     "returns a number on one path and a boolean on another"
//
// which is why it was picked out as the case that would tell an INTERFACE from
// a PATTERN if this pass is ever restructured: a rewrite that gives every
// operation its own independent rule has nowhere to put it.
//
// IT IS DEAD CODE TODAY, and this file is the evidence rather than a claim.
// To reach it a function needs TWO REACHABLE ctjs.return operations with
// different carriers and NO operation that `admission::op` refuses first -
// and `refuse()` keeps the FIRST reason it is given. Those two demands are
// contradictory:
//
//   * ctjs.return is a terminator with no successors, so a second reachable
//     return needs a successor-bearing terminator somewhere before it. The
//     only ones there are are cf.br, cf.cond_br, cf.switch (refused by the
//     `cf` arm) and ctjs.check, ctjs.push_handler (refused by the default
//     arm). Follow the reachability chain back and one of them is in the
//     ENTRY block, which the walk reaches before any later block.
//   * after --ctjs-lift-to-scf there is no cf left to refuse, because the
//     lift has funnelled the returns into one, fed by a phi - and the phi's
//     type is a variant, which the carrier check refuses at the scf.if.
//   * put the second return in an UNREACHABLE block to dodge both and
//     DeadCodeAnalysis never visits it: measured, every value there reads
//     `<unvisited>` and the refusal is "a value of type <unvisited> from
//     `ctjs.constant`".
//
// MEASURED ON THE THREE CORPORA, after --ctjs-resolve-globals and
// --ctjs-lift-to-scf: of 13,000 imported functions, 28 have two or more
// ctjs.return (bootstrap 1, p5 18, phaser 9) and ZERO of those 28 are free of
// a cf operation. So the rule fires nowhere on real code either.
//
// WHAT THIS FILE THEREFORE PINS is what a two-carrier function ACTUALLY says,
// on both routes, plus a CHECK-NOT on the string that does not appear. If a
// change ever makes that string reachable, these go red and somebody looks -
// which is the outcome worth having, and is more than "pin the string" would
// have got, because the string cannot be produced.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/agreed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=AGREED --implicit-check-not=ctnative.not_native
// RUN: ctjs-opt --ctnative-lower-to-emitc %t/two-returns.mlir \
// RUN:   | FileCheck %s --check-prefix=TWORETURNS
// RUN: ctjs-opt --ctnative-lower-to-emitc %t/unreachable-return.mlir \
// RUN:   | FileCheck %s --check-prefix=UNVISITED

// --- the JavaScript route: the lift funnels the returns, so the VARIANT is
// --- what is named, at the scf.if and not at the return ---------------------
//
// CALLED, AND IT HAS TO BE. An uncalled private function is dead to
// DeadCodeAnalysis and every type in it reads `<unvisited>`; a refusal that
// needs TYPES needs a caller.
//
// MIXED: ctjs.func {{.*}}@mixed$1
// MIXED-SAME: ctnative.not_native = "a value of type !ctnative.variant<!ctnative.bool, !ctnative.num<i32>> from `scf.if`"
// MIXED-NOT: on one path

// --- and the same shape with the carriers agreeing is lowered ---------------
//
// The precision the rule costs is meant to be nil: two returns of the same
// carrier are one C++ return type and nothing is refused. Without this half, a
// change that refused every function with more than one `return` would leave
// the checks above green while narrowing the tier.
//
// AGREED: emitc.func @main
// AGREED: emitc.func @agreed_1

// --- the hand-written route: two reachable returns, two carriers, and the
// --- `cf` arm gets there first ---------------------------------------------
//
// No parameters, so the parameter rule cannot fire; a constant condition, so
// nothing is boxed. This is as close to the two-carrier rule as an input can
// get, and the answer is still the branch.
//
// TWORETURNS: ctjs.func {{.*}}@two_returns
// TWORETURNS-SAME: ctnative.not_native = "unstructured control flow - run --ctjs-lift-to-scf first"
// TWORETURNS-NOT: on one path

// --- and the dodge: an unreachable second return is unvisited, not admitted -
//
// UNVISITED: ctjs.func {{.*}}@unreachable_return
// UNVISITED-SAME: ctnative.not_native = "a value of type <unvisited> from `ctjs.constant`"
// UNVISITED-NOT: on one path

//--- mixed.js
function mixed(n) {
  if (n > 0) { return 1; }
  return true;
}
var r = mixed(3);

//--- agreed.js
function agreed(n) {
  if (n > 0) { return 1; }
  return 2;
}
var r = agreed(3);

//--- two-returns.mlir
ctjs.func @two_returns(%receiver: !ctjs.value, %new_target: !ctjs.value,
                       %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %k = ctjs.constant #ctjs.boolean<true>
  %bit = ctjs.truthy %k
  cf.cond_br %bit, ^number, ^boolean
^number:
  %n = ctjs.constant #ctjs.number<4617315517961601024>
  ctjs.return %n
^boolean:
  %b = ctjs.constant #ctjs.boolean<true>
  ctjs.return %b
}

//--- unreachable-return.mlir
ctjs.func @unreachable_return(%receiver: !ctjs.value, %new_target: !ctjs.value,
                              %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %n = ctjs.constant #ctjs.number<4617315517961601024>
  ctjs.return %n
^boolean:
  %b = ctjs.constant #ctjs.boolean<true>
  ctjs.return %b
}
