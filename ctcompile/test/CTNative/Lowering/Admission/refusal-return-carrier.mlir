// Scalar return alternatives join into one tagged signature. Unstructured
// control flow and unreachable values retain their existing refusal rules.
// The runtime scalar-union fixture covers both reachable alternatives.
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

// Mixed scalar returns are native after structuring. Their numeric observer
// below keeps the standalone global contract definite.
// MIXED: emitc.func @main
// MIXED: emitc.func @mixed_1({{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">

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
var r = +mixed(3);

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
