// One object shape produces one class definition.
//
// A class is keyed on the object's shape, the sorted list of (field name,
// field type), not on its creation site. Two sites with the same key get the
// same type and the same name, which is what lets a generated struct be passed
// between functions. Where the names match and a type differs, the definition
// is one class template and each site an instantiation.
//
// Creation sites can sit inside branches and loops (object identity, obligation
// O-3 in ctcompile/docs/native-divergences.md), so the test is
// ASSERTED AT THE IR LEVEL, WITH CHECK-COUNT AND CHECK-NOT, and deliberately
// not by counting `class` lines in the emitted C++ with a regex: an
// indent-anchored regex over generated C++ under-counts as soon as a
// definition moves, and those creation sites move.
// CHECK-COUNT-1 followed by CHECK-NOT is exact and needs no indent at all. One
// RUN does go to C++, only to pin the two things the IR cannot show: the
// `template <class T0>` line itself and the provenance comment above it.
//
// One program per split file: a refusal is contagious in both directions, so
// one bad function in a shared module would take every other case down with it.


// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/same.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SAME
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/varying.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=VARYING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ordered.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ORDERED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/collide.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=COLLIDE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shadow.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SHADOW
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/empty.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EMPTY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/conflict.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CONFLICT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/varying.js 2>/dev/null | ctjs-opt "--pass-pipeline=builtin.module(ctjs-resolve-globals, ctjs-lift-to-scf, ctnative-lower-to-emitc, emitc.func(canonicalize, convert-scf-to-emitc, convert-arith-to-emitc, canonicalize, ctnative-prune-dead-stores, canonicalize))" | ctjs-translate --mlir-to-cpp | FileCheck %s --check-prefix=CPP

// --- Three sites, one class ------------------------------------------------
//
// `{x, y}` in two functions and three literals must yield one class, ctn_x_y.
// With a class per site there would be three distinct types with identical
// members, and `dot` could not pass `a` to a helper that took `b`. The COUNT-3
// checks that all three creations declare a variable of that one type, so the
// canonicalization reaches the sites and not only the definitions.
//
//
// SAME-NOT:  emitc.class
// SAME:      emitc.class @ctn_x_y
// SAME-NEXT: emitc.field @x : !emitc.opaque<"ctnative::js_num">
// SAME-NEXT: emitc.field @y : !emitc.opaque<"ctnative::js_num">
// SAME-NOT:  emitc.class
// SAME-COUNT-3: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_x_y">>
// SAME-NOT:  emitc.class

// --- Same names, one type differs: one template, two instantiations --------
//
// `at` is a number at both sites and stays a `ctnative::js_num`; `hit` is a
// boolean at one and a number at the other, and only it becomes a parameter. A
// position the whole program agrees on keeps its concrete type.
//
// This is not a variant field. Each site is monomorphic (`flag` never puts a
// number in `hit` and `count` never puts a boolean there); only the union of
// the two is not. A `std::variant` member would put a `std::visit` before
// every read at both sites to pay for polymorphism neither has. A variant field
// is used only when a single site stores more than one type.
//
//
// VARYING:      emitc.class @ctn_at_hit
// VARYING-SAME: 2 sites, 2 instantiations
// VARYING-SAME: ctnative.template_params = ["T0"]
// VARYING-NEXT: emitc.field @at : !emitc.opaque<"ctnative::js_num">
// VARYING-NEXT: emitc.field @hit : !emitc.opaque<"T0">
// VARYING-NOT:  emitc.class
// VARYING-DAG: () -> !emitc.lvalue<!emitc.opaque<"ctn_at_hit<ctnative::js_boolean_t>">>
// VARYING-DAG: () -> !emitc.lvalue<!emitc.opaque<"ctn_at_hit<ctnative::js_num>">>

// --- The order the program wrote the keys in is not part of the key ---------
//
// `{x: 1, y: 2}` and `{y: 4, x: 3}` are the same shape. The fields are sorted
// before the key is formed, so they canonicalize together.
//
// This test cannot catch a removed sort: the fields come from a StringMap whose
// order depends only on the key set, so both sites agree anyway. The sort stays
// so the emitted field order never depends on a hash.



//
// ORDERED-NOT: emitc.class
// ORDERED:     emitc.class @ctn_x_y
// ORDERED-NOT: emitc.class

// --- Two shapes that want one name -----------------------------------------
//
// `{a_b}` and `{a, b}` both join to `ctn_a_b`, because every character a field
// name may contain is also the character the separator is. Without the
// uniquing counter the module holds two `emitc.class @ctn_a_b` and the
// symbol-table verifier rejects it - which is how this guard is proved.
//
// COLLIDE:      emitc.class @ctn_a_b
// COLLIDE-NEXT: emitc.field @a_b : !emitc.opaque<"ctnative::js_num">
// COLLIDE:      emitc.class @ctn_a_b_2
// COLLIDE-NEXT: emitc.field @a : !emitc.opaque<"ctnative::js_num">
// COLLIDE-NEXT: emitc.field @b : !emitc.opaque<"ctnative::js_num">

// --- A field may be called T0, and a template parameter may not -------------
//
// `template <class T0> class C { double T0; };` is "declaration of 'T0'
// shadows template parameter" on both toolchains, and `{T0: 1}` is a perfectly
// ordinary JavaScript object. The parameter names skip every field name in the
// family, so this one is T1.
//
// SHADOW:      emitc.class @ctn_T0_y
// SHADOW-SAME: ctnative.template_params = ["T1"]
// SHADOW-NEXT: emitc.field @T0 : !emitc.opaque<"ctnative::js_num">
// SHADOW-NEXT: emitc.field @y : !emitc.opaque<"T1">

// --- The shape with no fields is a plain class -----------------------------
//
// `var e = {};` is admitted: hasClosedShape's loop over the uses of a literal
// with none is vacuously true. It has no field, so nothing to parametrize, and
// `template <>` is explicit-specialization syntax, ill-formed on a primary
// template. So it must reach the emitter with no parameter attribute at all,
// not an empty one; ../Emission/template-empty.mlir pins what happens if it
// ever does.
//
// EMPTY:     emitc.class @ctn_empty
// EMPTY-NOT: ctnative.template_params
// EMPTY-NOT: emitc.field

// --- A field stored as both scalar kinds uses tagged storage ----------------
//
// `o.v = 1; o.v = true;` with `v` never read still joins every write before
// choosing its storage, so the order of a use list cannot pick bool or double
// and make otherwise identical sites disagree about the shape key.
//
// CONFLICT: emitc.include "ctcompile/CTNative/Runtime/ctnative.hpp"
// CONFLICT: emitc.class @ctn_v
// CONFLICT-NEXT: emitc.field @v : !emitc.opaque<"ctnative::nullable_scalar">
// CONFLICT: emitc.func @twofaced_1

// --- What the template looks like as C++ ----------------------------------
//
// The provenance comment names the sites and says how many there are, and the
// `template <class T0>` line sits between it and the class, which is why
// CTNative/Checks/compile-clean.py looks back two lines and not one.
//
// CPP:      // ctcompile: object literal at
// CPP-SAME: (2 sites, 2 instantiations)
// CPP-NEXT: template <class T0>
// CPP-NEXT: class ctn_at_hit {
// CPP-NEXT: public:
// CPP-NEXT: ctnative::js_num at;
// CPP-NEXT: T0 hit;
// CPP-NEXT: };
// CPP-DAG: ctn_at_hit<ctnative::js_boolean_t> v
// CPP-DAG: ctn_at_hit<ctnative::js_num> v

//--- same.js
function area() {
  var p = { x: 1, y: 2 };
  return p.x * p.y;
}
function dot() {
  var a = { x: 3, y: 4 };
  var b = { x: 5, y: 6 };
  return a.x * b.x + a.y * b.y;
}
var one = area();
var two = dot();

//--- varying.js
function flag(x) {
  var h = { hit: false, at: 0 };
  h.at = x;
  if (x > 10) { h.hit = true; }
  return h.hit ? h.at : 0;
}
function count(x) {
  var h = { hit: 0, at: 0 };
  h.at = x;
  h.hit = h.hit + 1;
  return h.hit * h.at;
}
var a = flag(20);
var b = count(7);

//--- ordered.js
function first() {
  var p = { x: 1, y: 2 };
  return p.x + p.y;
}
function second() {
  var p = { y: 4, x: 3 };
  return p.x - p.y;
}
var a = first();
var b = second();

//--- collide.js
function joined() {
  var p = { a_b: 1 };
  return p.a_b;
}
function split() {
  var p = { a: 2, b: 3 };
  return p.a + p.b;
}
var a = joined();
var b = split();

//--- shadow.js
function one() {
  var p = { T0: 1, y: 2 };
  return p.T0 + p.y;
}
function two() {
  var p = { T0: 3, y: false };
  return p.y ? p.T0 : 0;
}
var a = one();
var b = two();

//--- empty.js
function nothing() {
  var e = {};
  return 1;
}
var a = nothing();

//--- conflict.js
function twofaced() {
  var o = { v: 1 };
  o.v = true;
  return 0;
}
var a = twofaced();
