// Part 24 Phase 63 Step 7: a variable nothing reads is erased with its
// assigns and whatever that leaves dead; a variable that is read stays.
//
// RUN: ctjs-opt --ctnative-prune-dead-stores %s | FileCheck %s
// RUN: ctjs-opt '--ctnative-prune-dead-stores=report=true' %s 2>&1 | FileCheck --check-prefix=REPORT %s

emitc.func @f(%a: f64) -> f64 {
  // read after being set: stays, with its assign
  %kept = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<f64>
  // set on a path, never read: goes, and the product feeding it goes with it
  %dead = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<f64>
  // declared, never touched: goes
  %unused = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<f64>
  emitc.assign %a : f64 to %kept : <f64>
  %two = "emitc.constant"() {value = 2.0 : f64} : () -> f64
  %prod = emitc.mul %a, %two : (f64, f64) -> f64
  emitc.assign %prod : f64 to %dead : <f64>
  %r = emitc.load %kept : <f64>
  emitc.return %r : f64
}

// CHECK-LABEL: emitc.func @f
// CHECK-NEXT: %[[KEPT:.*]] = "emitc.variable"()
// CHECK-NEXT: {{(emitc\.)?}}assign %arg0 : f64 to %[[KEPT]]
// CHECK-NEXT: %[[R:.*]] = {{(emitc\.)?}}load %[[KEPT]]
// CHECK-NEXT: {{(emitc\.)?}}return %[[R]]
// CHECK-NOT: emitc.variable
// CHECK-NOT: mul

// REPORT: remark: pruned 2 variable(s) and 2 operation(s)
