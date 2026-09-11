// THE CLOSED SHAPE, LOWERED - AND THE OPEN ONES, REFUSED BY NAME.
//
// Part 24 Phase 56 gate 1: the same object literal used several ways in one
// program, closed, escaping and loop-carried. The closed one becomes a class
// with public fields and a local of that class, and so does the one updated
// inside a loop (obligation O-3 - one slot, however many SSA values reach
// it); the escaping ones are a diagnostic naming the function AND the route,
// because under §2 the native tier has no boxed value to give them.

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
function reassigned() {
  var acc = { total: 0 };
  var i = 0;
  while (i < 3) { acc = { total: i }; i = i + 1; }
  return acc.total;
}
function leaked() {
  var acc = { total: 0 };
  var i = 0;
  while (i < 3) { acc.total = acc.total + i; i = i + 1; }
  return acc;
}
function switching() {
  var lo = { total: 1 };
  var hi = { total: 2 };
  var acc = lo;
  var i = 0;
  while (i < 3) { acc = hi; i = i + 1; }
  return acc.total;
}
var a = closed();
// CALLED, AND IT HAS TO BE. A private function nothing calls is dead code to
// MLIR's DeadCodeAnalysis, so no value in it is ever visited and every type
// reads `<unvisited>`: measured, `looped` uncalled is refused "field `total`
// is stored a <unvisited>, not a number or a boolean" no matter how closed
// its shape is. The two negatives below stay uncalled because they are
// refused STRUCTURALLY, by the shape, which needs no types at all - and
// because a refused callee refuses its caller, which would take main with it.
var b = looped();

// --- one class per shape, fields as doubles, the local by value ------------
//
// CHECK: emitc.class @ctn_x_y
// CHECK-NEXT: emitc.field @x : f64
// CHECK-NEXT: emitc.field @y : f64
// CHECK-LABEL: emitc.func @closed_1() -> f64
// CHECK: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_x_y">>
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
// CHECK-SAME: ctnative.not_native = "an object literal that escapes - it is returned"


// --- THE ONE UPDATED IN A LOOP: one slot, nothing copied - obligation O-3 --
//
// The importer hands the loop header the whole register file, so `acc` used
// to arrive at the scf.while as an iteration argument - fed by the literal on
// the entry edge and by itself on the back edge - and every access inside the
// loop went through that argument: the shape read as open, and this function
// was pinned as a refusal. --ctjs-lift-to-scf now drops that argument first
// (a trivial phi, Braun et al. 2013 §3.1), so every access inside and after
// the loop is on the literal itself. What the CHECKs prove: exactly ONE
// emitc.variable of the class in the function, a loop that carries a double
// and nothing of that class, and the same %[[ACC]] under every `total`.
//
// CHECK-LABEL: emitc.func @looped_3() -> f64
// CHECK: %[[ACC:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_total">>
// CHECK-NOT: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_total">>
// CHECK: scf.while ({{.*}}) : (f64) -> f64 {
// CHECK-NOT: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_total">>
// CHECK: "emitc.member"(%[[ACC]]) <{member = "total"}>
// CHECK-NEXT: emitc.load
// CHECK: "emitc.member"(%[[ACC]]) <{member = "total"}>
// CHECK-NEXT: emitc.assign
// CHECK-NOT: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_total">>
// CHECK: scf.yield
// CHECK-NOT: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_total">>
// CHECK: "emitc.member"(%[[ACC]]) <{member = "total"}>
// CHECK-NEXT: load
// CHECK-NEXT: return
// CHECK-NOT: ctjs.

// --- AND THE THREE THAT STILL REFUSE, each by its own route ------------------
//
// All three are uncalled and parameterless, so the object and not a parameter
// is what refuses them - and a refused callee refuses its caller, which would
// take main and the two lowered functions with it.
//
// `reassigned` makes a SECOND literal inside the loop and stores it in the
// same variable, so two values - two shapes, two slots - reach the header.
// MEASURED: deleting the `incoming != common` guard does NOT make this one
// lower. The literal made inside the loop does not dominate the header, so
// the dominance test refuses the replacement instead - it is the belt, not
// the guard, that holds here, and the drop count does not move.
//
// `switching` is the case the dominance test CANNOT catch, and so the one
// that proves the guard: both literals are made before the loop, so both
// dominate the header, and only "two different values reach it" stands
// between the pass and a miscompile. MEASURED with the guard deleted: the
// same program with a parameter and a call lowers to C++ that reads `hi`
// unconditionally - `acc = lo` before the loop is gone, so it answers 2 even
// for n = 0, where the loop never runs and the answer is 1. Here, uncalled,
// deleting the guard flips this line to the `<unvisited>` refusal instead,
// which is red either way.
//
// `leaked` returns the literal after the loop; with O-3 that is no longer a
// loop-carried value but a plain escape, and hasClosedShape's "anything else
// opens it" arm is what refuses it - delete that arm and the shape reads as
// closed, the message changes, and this line goes red.
//
// CHECK: ctjs.func private @reassigned$4
// CHECK-SAME: ctnative.not_native = "an object literal that is loop-carried - more than one value reaches the variable that holds it (assigned again inside a loop, or on only one path before it)"
// CHECK: ctjs.func private @leaked$5
// CHECK-SAME: ctnative.not_native = "an object literal that escapes - it is returned"
// CHECK: ctjs.func private @switching$6
// CHECK-SAME: ctnative.not_native = "an object literal that is loop-carried - more than one value reaches the variable that holds it (assigned again inside a loop, or on only one path before it)"
