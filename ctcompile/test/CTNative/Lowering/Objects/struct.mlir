// A closed-shape object literal lowers to a class; open ones are refused by name.
//
// The same object literal used several ways in one program: closed, escaping
// and loop-carried. The closed one becomes a class with public fields and a
// local of that class, and so does the one updated inside a loop (it stays one
// slot however many SSA values reach it). The escaping ones are refused with a
// diagnostic naming the function and the escape route, because native output
// has no boxed (heap, garbage-collected) value to fall back on.

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
// `looped` must be called. MLIR's DeadCodeAnalysis treats a private function
// nothing calls as dead, so no value in it is visited and every type reads
// `<unvisited>`: uncalled, `looped` is refused "field `total` is stored a
// <unvisited>, not a number or a boolean" however closed its shape is. The
// refused functions (open, reassigned, leaked, switching) stay uncalled: they
// are refused by their shape, which needs no types, and a refused callee
// refuses its caller, which would take main with it.
var b = looped();

// --- one class per shape, fields as doubles, the local by value ------------
//
// CHECK: emitc.class @ctn_x_y
// CHECK-NEXT: emitc.field @x : !emitc.opaque<"ctnative::js_num">
// CHECK-NEXT: emitc.field @y : !emitc.opaque<"ctnative::js_num">
// CHECK-LABEL: emitc.func @closed_1() -> !emitc.opaque<"ctnative::js_num">
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


// --- The one updated in a loop: one slot, nothing copied --------------------
//
// The importer hands the loop header the whole register file, so without help
// `acc` would reach the scf.while as an iteration argument, fed by the literal
// on the entry edge and by itself on the back edge, and every access in the
// loop would go through it, making the shape read as open. --ctjs-lift-to-scf
// removes that trivial phi first (Braun et al. 2013 §3.1), so every access
// inside and after
// the loop is on the literal itself. What the CHECKs prove: exactly ONE
// emitc.variable of the class in the function, a loop that carries a double
// and nothing of that class, and the same %[[ACC]] under every `total`.
//
// CHECK-LABEL: emitc.func @looped_3() -> !emitc.opaque<"ctnative::js_num">
// CHECK: %[[ACC:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_total">>
// CHECK-NOT: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_total">>
// CHECK: scf.while ({{.*}}) : (!emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num"> {
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

// --- And the three that still refuse, each by its own route ------------------
//
// All three are uncalled and parameterless, so the object, not a parameter, is
// what refuses them; and a refused callee refuses its caller, which would take
// main and the two lowered functions with it.
//
// `reassigned` makes a second literal inside the loop and stores it in the same
// variable, so two values (two shapes, two slots) reach the header. Deleting
// the `incoming != common` guard does not make this one lower: the literal
// made inside the loop does not dominate the header, so the dominance test
// refuses the replacement instead. Here the backup check holds, not the guard,
// and the count of dropped arguments does not change.
//
// `switching` is the case the dominance test cannot catch, so it is the one
// that proves the guard: both literals are made before the loop, so both
// dominate the header, and only "two different values reach it" stands
// between the pass and a miscompile. With the guard deleted, the same program
// with a parameter and a call lowers to C++ that reads `hi` unconditionally
// (`acc = lo` before the loop is gone), so it answers 2 even for n = 0, where
// the loop never runs and the answer is 1. Here, uncalled, deleting the guard
// turns this line into the `<unvisited>` refusal instead, which fails the
// test either way.
//
// `leaked` returns the literal after the loop. With the trivial phi removed
// that is a plain escape rather than a loop-carried value, and hasClosedShape's
// "anything else opens it" arm refuses it; delete that arm and the shape reads
// as closed, the message changes, and this line goes red.
//
// CHECK: ctjs.func private @reassigned$4
// CHECK-SAME: ctnative.not_native = "an object literal that is loop-carried - more than one value reaches the variable that holds it (assigned again inside a loop, or on only one path before it)"
// CHECK: ctjs.func private @leaked$5
// CHECK-SAME: ctnative.not_native = "an object literal that escapes - it is returned"
// CHECK: ctjs.func private @switching$6
// CHECK-SAME: ctnative.not_native = "an object literal that is loop-carried - more than one value reaches the variable that holds it (assigned again inside a loop, or on only one path before it)"
