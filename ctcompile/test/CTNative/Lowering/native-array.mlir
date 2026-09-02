// THE DENSE ARRAY, LOWERED - AND EVERY WAY OF MAKING ONE SPARSE, REFUSED BY
// NAME.
//
// Part 24 Phase 57 Stage A. An array literal built by its own appends and then
// only read - through an index or through `length` - becomes one frame-scope
// `std::vector<double>` and three calls to three helpers. Anything that could
// put a hole in it, rename an element or let it leave the frame is a
// diagnostic, because under §2 the native tier has no boxed value to give it.
//
// ONE PROGRAM PER FILE, and that is not tidiness. A refusal is contagious in
// both directions - a refused callee refuses its caller, and a refused `main`
// refuses everything it calls - so the positive program could not be written
// beside a refusal that has to be CALLED. The four structural refusals need no
// types at all and could have shared a file; the two that depend on the
// element type could not, because an uncalled private function is dead code to
// MLIR's DeadCodeAnalysis and every type in it reads `<unvisited>`.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dense.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DENSE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/length.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=LENGTH
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/indexed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=INDEXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/deleted.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escaping.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ESCAPING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/booleans.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=BOOLEANS

// --- the include and the preamble, and ONLY when there is an array ----------
//
// The other native lits pin line positions and the printing gate reports byte
// counts, so an include and a preamble emitted unconditionally would move both
// for every program that has no array in it. `native-numeric.mlir` is the
// witness: it has no `<vector>` line to find.
//
// DENSE: emitc.include <"vector">
// DENSE: emitc.verbatim
// DENSE-SAME: inline double vec_at
// DENSE-SAME: inline double vec_length
// DENSE-SAME: inline void vec_push

// --- one variable, one push per element, one call per read ------------------
//
// %[[A]] under every access is the assertion that there is ONE vector and not
// a copy per use: an `emitc.load` of it would be a `std::vector` by value
// passed by value, and a second `emitc.variable` would be a second array.
//
// DENSE-LABEL: emitc.func @sum_of_three_1() -> f64
// DENSE: %[[A:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
// DENSE-NOT: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
// DENSE: call_opaque "ctnative::vec_push"(%[[A]],
// DENSE: call_opaque "ctnative::vec_push"(%[[A]],
// DENSE: call_opaque "ctnative::vec_push"(%[[A]],
// DENSE: call_opaque "ctnative::vec_at"(%[[A]],
// DENSE: call_opaque "ctnative::vec_at"(%[[A]],
// DENSE: call_opaque "ctnative::vec_at"(%[[A]],
// DENSE: return

// --- `a.length` is `size()`, and the key constant lowers to nothing ---------
//
// The `"length"` string constant is NOT a closed object's member name, so
// `isKeyOnlyString` is false for it and without a vector arm of its own it
// falls through to "a constant that is not a number, a boolean or undefined"
// and refuses this function outright. The DENSE-NOT below is that arm's test:
// no string constant survives into the emitted function.
//
// DENSE-LABEL: emitc.func @counted_2() -> f64
// DENSE: %[[C:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
// DENSE: call_opaque "ctnative::vec_length"(%[[C]])
// DENSE-NOT: ctjs.

// --- two widths are one vector<double>, not a vector of a union -------------
//
// DENSE-LABEL: emitc.func @widened_3() -> f64
// DENSE: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
// DENSE-NOT: ctjs.

// --- AND THE SIX THAT REFUSE, each by its own route -------------------------
//
// The four structural ones are uncalled and parameterless, so the array and
// not a parameter is what refuses them. Each names the route rather than the
// symptom: "returns !ctnative.boxed" would be true of all six and would tell a
// reader nothing.
//
// `a.length = 5` RESIZES. `a.length = 5` on a one-element array gives four
// holes; `a.length = 0` throws the elements away. Either way `length` stops
// being `size()`, which is the one equation this stage is built on.
//
// LENGTH: ctjs.func private @grow$1
// LENGTH-SAME: ctnative.not_native = "an array literal whose `length` is assigned - that resizes it, and a resize leaves holes no `std::vector` can hold"

// `a[0] = 9` IS THE CASE THE WRITTEN DESIGN FOR THIS STAGE ADMITTED, and part
// 24 Stage 57A's own rule is why it does not: "a JavaScript array can be
// sparse: `a[100] = 1` on an empty array gives `length` 101 with one element.
// Prove density, or box." An index store proves nothing about the index, so
// admitting it would have made both `length` and the element join wrong - the
// join sees appends, and a stored value it does not see is a value a later
// read returns.
//
// INDEXED: ctjs.func private @stored$1
// INDEXED-SAME: ctnative.not_native = "an array literal written through an index - `a[100] = 1` gives `length` 101 with one element, so density is not proved"

// `delete a[0]` punches the hole directly.
//
// DELETED: ctjs.func private @drop$1
// DELETED-SAME: ctnative.not_native = "an array literal with an element deleted - `delete a[0]` punches a hole in it, so density is not proved"

// A RETURNED ARRAY OUTLIVES ITS FRAME. This is the default arm of
// isDenseVectorSite doing its job: a return is not an append and not a read,
// so it opens the site, and there is nothing else in the rule to delete.
//
// ESCAPING: ctjs.func private @leak$1
// ESCAPING-SAME: ctnative.not_native = "an array literal that escapes - it is returned"

// MIXED ELEMENTS AND BOOLEAN ELEMENTS BOTH NEED TYPES, so both are CALLED and
// both take `main` down with them - a refused callee refuses its caller. The
// callee keeps its own reason, which is what these pin.
//
// The element type is the join over the appends from `undefined`, so a number
// and a string give an optional of a two-alternative variant, which has no
// native carrier. `vector<dyn<double, str>>` is part 24's answer for it and is
// Stage 53H's work, not this one's.
//
// MIXED: ctjs.func private @mixed$1
// MIXED-SAME: ctnative.not_native = "an array whose elements are !ctnative.opt<!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>>, not numbers"

// `std::vector<bool>` IS NOT A CONTAINER OF bool. Its `operator[]` returns a
// proxy that aliases the container and converts differently from `bool`, which
// is on Stage 53E's never-deduce list and which this stage is what would
// generate. Refused rather than special-cased.
//
// BOOLEANS: ctjs.func private @flags$1
// BOOLEANS-SAME: ctnative.not_native = "an array of booleans - `std::vector<bool>` is a bit-packed specialisation whose elements are a proxy, not a `bool`"

//--- dense.js
function sum_of_three() {
  var a = [1, 2, 3];
  return a[0] + a[1] + a[2];
}
function counted() {
  var a = [4, 5];
  return a.length;
}
function widened() {
  var a = [1, 2.5];
  return a[0] + a[1];
}
var s = sum_of_three();
var c = counted();
var w = widened();

//--- length.js
function grow() {
  var a = [1];
  a.length = 5;
  return a[0];
}

//--- indexed.js
function stored() {
  var a = [1, 2];
  a[0] = 9;
  return a[0];
}

//--- deleted.js
function drop() {
  var a = [1, 2];
  delete a[0];
  return a[1];
}

//--- escaping.js
function leak() {
  var a = [1, 2];
  return a;
}

//--- mixed.js
function mixed() {
  var a = [1, "x"];
  return a[0];
}
var m = mixed();

//--- booleans.js
function flags() {
  var a = [true, false];
  return a[1];
}
var f = flags();
