// THE CLOSED SHAPE, LOWERED - AND THE OPEN ONE, REFUSED BY NAME.
//
// Part 24 Phase 56 gate 1: the same object literal used two ways in one
// program, once closed and once escaping. The closed one becomes a class
// with public fields and a local of that class; the escaping one - here it
// is returned, which is the escape every other route reduces to for the
// lowering - is a diagnostic naming the function, because under §2 the
// native tier has no boxed value to give it.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s

function closed() {
  var p = { x: 1, y: 2 };
  p.x = p.x + 3;
  return p.x * p.y;
}
function open() {
  var p = { x: 1, y: 2 };
  return p;
}
function looped() {
  var acc = { total: 0 };
  var i = 0;
  while (i < 3) { acc.total = acc.total + i; i = i + 1; }
  return acc.total;
}
var a = closed();

// --- one class per shape, fields as doubles, the local by value ------------
//
// CHECK: emitc.class @ctn_shape_
// CHECK-NEXT: emitc.field @x : f64
// CHECK-NEXT: emitc.field @y : f64
// CHECK-LABEL: emitc.func @closed_1() -> f64
// CHECK: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_shape_
// CHECK: member = "x"
// CHECK: assign
// CHECK: member = "x"
// CHECK: load
// CHECK-NOT: ctjs.

// --- the escaping literal is a diagnostic, not a heap object ---------------
//
// The literal is visited before the return that leaks it, so the shape is
// what the diagnostic names; "returns !ctnative.boxed" would be the symptom.
//
// CHECK: ctjs.func private @open$2
// CHECK-SAME: ctnative.not_native = "an object literal whose shape is not closed - it is stored, passed, returned, or reached through a dynamic key"

// --- AND THE LOOP-CARRIED ONE, refused today by name - obligation O-3 ------
//
// The lift threads every register live across the loop through the loop's
// block arguments, so `acc` reaches the scf.while itself and its shape reads
// as open. Admitting it means following one object through those arguments
// to one stack slot (part 24 Phase 56B's O-3). Pinned as the next step. It is
// uncalled and parameterless so the object, not a caller, is what refuses it.
//
// CHECK: ctjs.func private @looped$3
// CHECK-SAME: ctnative.not_native = "an object literal whose shape is not closed
