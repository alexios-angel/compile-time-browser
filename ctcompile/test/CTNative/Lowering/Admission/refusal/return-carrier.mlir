// Returns of different scalar types join into one tagged return type.
// Unstructured control flow and unreachable values are still refused.
// Fixtures/Scalars/scalar-unions.js checks the runtime answers.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/agreed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=AGREED --implicit-check-not=ctnative.not_native
// RUN: ctjs-opt --ctnative-lower-to-emitc %t/two-returns.mlir \
// RUN:   | FileCheck %s --check-prefix=TWORETURNS
// RUN: ctjs-opt --ctnative-lower-to-emitc %t/unreachable-return.mlir \
// RUN:   | FileCheck %s --check-prefix=UNVISITED

// Mixed scalar returns are native once structured. The unary `+` on the call
// gives the global `r` a definite Number type.
// MIXED: emitc.func @main
// MIXED: emitc.func @mixed_1({{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">

// --- the same shape with both returns of one type ---------------------------
//
// Two returns of the same type lower to one plain C++ return type, with no
// tagged value, and nothing is refused. This guards against a change that
// refuses every function with more than one `return` while the other checks
// still pass.
//
// AGREED: emitc.func @main
// AGREED: emitc.func @agreed_1

// --- hand-written IR: two reachable returns of different types, still in
// --- unstructured `cf` form -------------------------------------------------
//
// It has no parameters and a constant condition, so nothing is boxed and the
// only refusal that can apply is the one for unstructured control flow.
// The importer never emits this form once --ctjs-lift-to-scf has run. It is
// function index 0, the script entry, so reachability keeps it as a root
// instead of removing a refused function nothing calls.
//
// TWORETURNS: ctjs.func {{.*}}@two_returns
// TWORETURNS-SAME: ctnative.not_native = "unstructured control flow - run --ctjs-lift-to-scf first"
// TWORETURNS-NOT: on one path

// --- an unreachable second return is unvisited, not admitted ----------------
//
// Function index 0 again, so reachability keeps it.
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
ctjs.func @two_returns$0(%receiver: !ctjs.value, %new_target: !ctjs.value,
                       %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %k = ctjs.constant #ctjs.boolean<true>
  %bit = ctjs.truthy %k
  cf.cond_br %bit, ^number, ^boolean
^number:
  %n = ctjs.constant #ctjs.number<4617315517961601024>  // 5.0
  ctjs.return %n
^boolean:
  %b = ctjs.constant #ctjs.boolean<true>
  ctjs.return %b
}

//--- unreachable-return.mlir
ctjs.func @unreachable_return$0(%receiver: !ctjs.value, %new_target: !ctjs.value,
                              %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %n = ctjs.constant #ctjs.number<4617315517961601024>  // 5.0
  ctjs.return %n
^boolean:
  %b = ctjs.constant #ctjs.boolean<true>
  ctjs.return %b
}
