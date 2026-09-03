// ONE SHAPE IS ONE DEFINITION - part 24 Phase 56, stage C.
//
// 56B emitted one `emitc.class` per creation SITE, so the shipped struct
// fixture had seven classes for six shapes and two of them were byte for byte
// the same. A definition is keyed on the SHAPE instead - the ordered list of
// (field name, field type) - so two sites with the same key get the same type
// and the SAME NAME, which is what makes a generated struct passable between
// functions at all. Where the names match and a type differs, the definition is
// one class TEMPLATE and each site is an instantiation.
//
// ASSERTED AT THE IR LEVEL, WITH CHECK-COUNT AND CHECK-NOT, and deliberately
// NOT by counting `class` lines in the emitted C++ with a regex: an indent-
// anchored regex over generated C++ under-counts the moment a definition moves,
// and obligation O-3 already puts creation sites inside branches and loops.
// CHECK-COUNT-1 followed by CHECK-NOT is exact and needs no indent at all. One
// RUN does go to C++, and only to pin the two things the IR cannot show: the
// `template <class T0>` line itself and the provenance comment above it.
//
// ONE PROGRAM PER FILE, for the reason native-array.mlir gives: a refusal is
// contagious in both directions, so one bad function in a shared module would
// take every other case in it down with it.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/same.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SAME
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/varying.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=VARYING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ordered.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ORDERED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/collide.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=COLLIDE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shadow.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SHADOW
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/empty.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EMPTY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/conflict.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CONFLICT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/varying.js 2>/dev/null | ctjs-opt "--pass-pipeline=builtin.module(ctjs-resolve-globals, ctjs-lift-to-scf, ctnative-lower-to-emitc, emitc.func(canonicalize, convert-scf-to-emitc, convert-arith-to-emitc, canonicalize, ctnative-prune-dead-stores, canonicalize))" | ctjs-translate --mlir-to-cpp | FileCheck %s --check-prefix=CPP

// --- THREE SITES, ONE CLASS ------------------------------------------------
//
// `{x, y}` in two functions and three literals. Before 56C this module held
// ctn_shape_0, ctn_shape_1 and ctn_shape_2, three distinct types with
// identical members, and `dot` could not have passed `a` to a helper that took
// `b`. The COUNT-3 is the other half of the claim: all three creations declare
// a variable of the ONE type, so the canonicalisation reaches the sites and not
// only the definitions.
//
// SAME-NOT:  emitc.class
// SAME:      emitc.class @ctn_x_y
// SAME-NEXT: emitc.field @x : f64
// SAME-NEXT: emitc.field @y : f64
// SAME-NOT:  emitc.class
// SAME-COUNT-3: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"ctn_x_y">>
// SAME-NOT:  emitc.class

// --- THE NAMES MATCH AND A TYPE DIFFERS: ONE TEMPLATE, TWO INSTANTIATIONS ---
//
// `at` is a number at both sites and stays a `double`; `hit` is a boolean at
// one and a number at the other, and only IT becomes a parameter. A position
// the whole program agrees on keeps its concrete type, which is more
// information and not less.
//
// NOT A VARIANT FIELD. Each site here is monomorphic - `flag` never puts a
// number in `hit` and `count` never puts a boolean there - and only the union
// of the two sites is not, so a `std::variant` member would put a `std::visit`
// in front of every read at both sites to pay for a polymorphism neither has.
// Part 24 Phase 56C step 3 draws that line; this is the case on its near side.
//
// VARYING:      emitc.class @ctn_at_hit
// VARYING-SAME: 2 sites, 2 instantiations
// VARYING-SAME: ctnative.template_params = ["T0"]
// VARYING-NEXT: emitc.field @at : f64
// VARYING-NEXT: emitc.field @hit : !emitc.opaque<"T0">
// VARYING-NOT:  emitc.class
// VARYING-DAG: () -> !emitc.lvalue<!emitc.opaque<"ctn_at_hit<bool>">>
// VARYING-DAG: () -> !emitc.lvalue<!emitc.opaque<"ctn_at_hit<double>">>

// --- AND THE ORDER THE PROGRAM WROTE THE KEYS IN IS NOT PART OF THE KEY -----
//
// `{x: 1, y: 2}` and `{y: 4, x: 3}` are the same shape. The fields are sorted
// before the key is formed, so they canonicalise together.
//
// A NOTE ON WHAT THIS DOES *NOT* PROVE. Deleting the sort does not make this
// test fail, and it was tried: the field vector comes out of a StringMap whose
// bucket layout depends on the key SET and not on the insertion order, so both
// sites produce the same order with or without it. A "delete the sort and watch
// it go red" test would have been vacuous, so there is not one. The sort stays
// because the emitted field order should not depend on a hash.
//
// ORDERED-NOT: emitc.class
// ORDERED:     emitc.class @ctn_x_y
// ORDERED-NOT: emitc.class

// --- TWO SHAPES THAT WANT ONE NAME -----------------------------------------
//
// `{a_b}` and `{a, b}` both join to `ctn_a_b`, because every character a field
// name may contain is also the character the separator is. Without the
// uniquing counter the module holds two `emitc.class @ctn_a_b` and the
// symbol-table verifier rejects it - which is how this guard is proved.
//
// COLLIDE:      emitc.class @ctn_a_b
// COLLIDE-NEXT: emitc.field @a_b : f64
// COLLIDE:      emitc.class @ctn_a_b_2
// COLLIDE-NEXT: emitc.field @a : f64
// COLLIDE-NEXT: emitc.field @b : f64

// --- A FIELD MAY BE CALLED T0, AND A TEMPLATE PARAMETER MAY NOT -------------
//
// `template <class T0> class C { double T0; };` is "declaration of 'T0'
// shadows template parameter" on both toolchains, and `{T0: 1}` is a perfectly
// ordinary JavaScript object. The parameter names skip every field name in the
// family, so this one is T1.
//
// SHADOW:      emitc.class @ctn_T0_y
// SHADOW-SAME: ctnative.template_params = ["T1"]
// SHADOW-NEXT: emitc.field @T0 : f64
// SHADOW-NEXT: emitc.field @y : !emitc.opaque<"T1">

// --- THE SHAPE WITH NO FIELDS IS A PLAIN CLASS -----------------------------
//
// `var e = {};` is admitted: hasClosedShape's loop over the uses of a literal
// with none is vacuously true. It has no field, so it has nothing to
// parametrise, and `template <>` is the explicit-specialisation syntax rather
// than a declaration - ill-formed on a primary template. It must therefore
// arrive at the emitter with NO parameter attribute at all, not an empty one;
// template-empty.mlir pins what happens if it ever does.
//
// EMPTY:     emitc.class @ctn_empty
// EMPTY-NOT: ctnative.template_params
// EMPTY-NOT: emitc.field

// --- A FIELD STORED TWO CARRIERS IS REFUSED --------------------------------
//
// `o.v = 1; o.v = true;` with `v` never read. Where the field IS read the join
// of its stores is `!ctnative.boxed` and the read is refused for having no
// carrier; where it is not, both stores are admitted one at a time and the
// field used to take whichever the use-list handed over first. That was
// unobservable while the class was per site. It is not now: the shape key IS
// the field types, so two sites of one shape could disagree about this field
// and split into a template that says nothing about the program.
//
// CONFLICT: ctnative.not_native = "field `v` is stored a number on one path and a boolean on another"

// --- AND WHAT THE TEMPLATE LOOKS LIKE AS C++ -------------------------------
//
// The provenance comment names the sites and says how many there are, and the
// `template <class T0>` line sits between it and the class - which is why
// check-compile-clean.cmake looks back two lines and not one.
//
// CPP:      // ctcompile: object literal at
// CPP-SAME: (2 sites, 2 instantiations)
// CPP-NEXT: template <class T0>
// CPP-NEXT: class ctn_at_hit {
// CPP-NEXT: public:
// CPP-NEXT: double at;
// CPP-NEXT: T0 hit;
// CPP-NEXT: };
// CPP-DAG: ctn_at_hit<bool> v
// CPP-DAG: ctn_at_hit<double> v

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
