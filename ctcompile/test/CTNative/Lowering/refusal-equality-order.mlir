// WHICH OF TWO REACHABLE REFUSALS WINS, AND WHY IT IS THE ORDER AND NOT THE
// PROGRAM THAT DECIDES.
//
// The compare arm's equality case is four checks in one expression:
//
//     numeric(lhs, "equality") && numeric(rhs, "equality") &&
//     defined(lhs, "equality") && defined(rhs, "equality")
//
// `&&` short-circuits and `admission::refuse` keeps the FIRST reason it is
// given, so for a value that fails both - not a number AND possibly undefined
// - the message a reader sees is decided by the order these are written in,
// not by anything about the program. Swap the two pairs while "refactoring"
// and every such program starts saying something else; reorder them inside a
// table of independent rules and the answer is whatever the table iterates in.
//
// Nothing pinned that. This file does: one program per message, and the pair
// is the assertion.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/undefined.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNDEF
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/notanumber.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NAN

// --- ONLY `defined` can fire: a number that may be undefined ----------------
//
// `u` is assigned on one path, so its type is the opt row over a number, whose
// CARRIER IS number - NaN is exact for arithmetic. `numeric` therefore passes,
// and the only check left to fail is `defined`. This is the message the tier
// exists to give: NaN is not undefined where equality can see the difference.
//
// UNDEF: ctjs.func {{.*}}@eqnum$1
// UNDEF-SAME: ctnative.not_native = "equality on a value that may be undefined - NaN would not compare the way undefined does"

// --- BOTH can fire, and `numeric` runs first --------------------------------
//
// The only change is `true` for `1`. `u` is now the opt row over a boolean:
// its carrier is boolean, so `numeric` fails - and it may still be undefined,
// so `defined` would fail too. The message below is the one the ORDER picks.
//
// IF THIS FILE EVER REPORTS THE OTHER MESSAGE HERE, the checks were reordered.
// That is not automatically wrong - "may be undefined" is arguably the more
// useful thing to say about this program - but it is a change to what the
// compiler tells 12,900 refused functions, and it should be a decision rather
// than a side effect of moving code.
//
// NAN: ctjs.func {{.*}}@eqbool$1
// NAN-SAME: ctnative.not_native = "equality operand is !ctnative.opt<!ctnative.bool>, not a number"
// NAN-NOT: may be undefined

//--- undefined.js
function eqnum(n) {
  var u;
  if (n > 0) { u = 1; }
  return u === 1;
}
var r = eqnum(3);

//--- notanumber.js
function eqbool(n) {
  var u;
  if (n > 0) { u = true; }
  return u === true;
}
var r = eqbool(3);
