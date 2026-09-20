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

// THE O-3 STACK SLOT MUST SURVIVE THIS PASS. A closed-shape object literal is
// one emitc.variable of a class type whose uses are emitc.member, never a
// direct assign - so "every use is an assign to it" is false and it is not
// write-only. Pinned because the failure mode is silent: prune the slot and
// the program still compiles and prints a wrong number.
emitc.func @slot(%a: f64) -> f64 {
  %obj = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_shape_0">>
  %w = "emitc.member"(%obj) <{member = "total"}> : (!emitc.lvalue<!emitc.opaque<"ctn_shape_0">>) -> !emitc.lvalue<f64>
  emitc.assign %a : f64 to %w : <f64>
  %r = "emitc.member"(%obj) <{member = "total"}> : (!emitc.lvalue<!emitc.opaque<"ctn_shape_0">>) -> !emitc.lvalue<f64>
  %v = emitc.load %r : <f64>
  emitc.return %v : f64
}

// CHECK-LABEL: emitc.func @f
// CHECK-NEXT: %[[KEPT:.*]] = "emitc.variable"()
// CHECK-NEXT: {{(emitc\.)?}}assign %arg0 : f64 to %[[KEPT]]
// CHECK-NEXT: %[[R:.*]] = {{(emitc\.)?}}load %[[KEPT]]
// CHECK-NEXT: {{(emitc\.)?}}return %[[R]]
// CHECK-NOT: emitc.variable
// CHECK-NOT: mul

// AND THE SLOT, WHICH IS THE ONE VARIABLE THIS PASS MUST NEVER TOUCH.
//
// CHECK-LABEL: emitc.func @slot
// CHECK: %[[OBJ:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_shape_0">>
// CHECK: "emitc.member"(%[[OBJ]]) <{member = "total"}>
// CHECK: assign
// CHECK: "emitc.member"(%[[OBJ]]) <{member = "total"}>
// CHECK: load
// CHECK: return

// Number extraction is pure. Erasing it exposes the conversion's unused result
// to statement marking; arbitrary receivers or methods retain their effects.
emitc.func @number_extraction(%value: !emitc.opaque<"ctnative::nullable_scalar">) {
  %number = emitc.call_opaque "ctnative::to_number"(%value) : (!emitc.opaque<"ctnative::nullable_scalar">) -> !emitc.opaque<"ctnative::js_num">
  %dead = emitc.member_call_opaque %number "value"() : !emitc.opaque<"ctnative::js_num">, () -> f64
  emitc.return
}
emitc.func @unknown_members(%number: !emitc.opaque<"ctnative::js_num">, %unknown: !emitc.opaque<"Unknown">) {
  %receiver = emitc.member_call_opaque %unknown "value"() : !emitc.opaque<"Unknown">, () -> f64
  %method = emitc.member_call_opaque %number "other"() : !emitc.opaque<"ctnative::js_num">, () -> f64
  emitc.return
}

// CHECK-LABEL: emitc.func @number_extraction
// CHECK: call_opaque "ctnative::to_number"
// CHECK-SAME: ctnative.statement
// CHECK-NEXT: return
// CHECK-LABEL: emitc.func @unknown_members
// CHECK: member_call_opaque {{.*}} "value"() : !emitc.opaque<"Unknown">
// CHECK: member_call_opaque {{.*}} "other"() : !emitc.opaque<"ctnative::js_num">
// REPORT: remark: pruned 2 variable(s) and 3 operation(s), marked 1 call(s) as statements
