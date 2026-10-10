// The dense array lowered, and each way of making one sparse refused with its
// own reason.
//
// An array literal that is filled only by its own appends and then only read
// (by index or `length`) lowers to one frame-local `std::vector<double>` plus
// calls to ctnative::vec_push / vec_at / vec_length. Anything that could make
// it sparse, change an element's type, or let it outlive the frame is a
// diagnostic: the native target has no boxed fallback value to use instead.
//
// One program per split file, and that is not tidiness. A refusal is contagious in
// both directions (a refused callee refuses its caller, and a refused `main`
// refuses everything it calls), so the positive program could not be written
// beside a refusal that has to be called. The uncalled refusals need no
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

// --- The include, and only when there is an array ----------------------------
//
// The vector helpers live in Runtime/ctnative.hpp, which the generated
// program pulls in with a single include. This checks that a dense array
// reaches the helpers through that include, by name, and by no other route
// (no helper definition is spelled in the program).
//
// DENSE: emitc.include "ctcompile/CTNative/Runtime/ctnative.hpp"

// --- One variable, one push per element, one call per read -------------------
//
// %[[A]] under every access is the assertion that there is one vector and not
// a copy per use: an `emitc.load` of it would be a `std::vector` by value
// passed by value, and a second `emitc.variable` would be a second array.
//
// DENSE-LABEL: emitc.func @sum_of_three_1() -> !emitc.opaque<"ctnative::js_num">
// DENSE: %[[A:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
// DENSE-NOT: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
// DENSE: call_opaque "ctnative::vec_push"(%[[A]],
// DENSE: call_opaque "ctnative::vec_push"(%[[A]],
// DENSE: call_opaque "ctnative::vec_push"(%[[A]],
// DENSE: call_opaque "ctnative::vec_at"(%[[A]],
// DENSE: call_opaque "ctnative::vec_at"(%[[A]],
// DENSE: call_opaque "ctnative::vec_at"(%[[A]],
// DENSE: return

// --- `a.length` is `size()`, and the key constant lowers to nothing ----------
//
// The `"length"` string constant is not a closed object's member name, so
// `isKeyOnlyString` is false for it and without a vector arm of its own it
// falls through to "a constant that is not a number, a boolean or undefined"
// and refuses this function outright. The DENSE-NOT below is that arm's test:
// no string constant survives into the emitted function.
//
// DENSE-LABEL: emitc.func @counted_2() -> !emitc.opaque<"ctnative::js_num">
// DENSE: %[[C:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
// DENSE: call_opaque "ctnative::vec_length"(%[[C]])
// DENSE-NOT: ctjs.

// --- Two widths are one vector<double>, not a vector of a union --------------
//
// DENSE-LABEL: emitc.func @widened_3() -> !emitc.opaque<"ctnative::js_num">
// DENSE: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
// DENSE-NOT: ctjs.

// --- The programs that refuse, each with its own reason ----------------------
//
// Each refusal checks a specific reason string; a generic "returns
// !ctnative.boxed" would be true of all of them and prove nothing. length.js,
// indexed.js, deleted.js and escaping.js are uncalled and parameterless, so
// the array itself, not a parameter, is what refuses them.
//
// `a.length = 5` on a one-element array gives four holes. Only a complete
// non-growing contents proof admits length assignments; array/shrink.mlir
// covers the retained dense prefix and clearing to zero.
//
// LENGTH: ctjs.func private @grow$1
// LENGTH-SAME: ctnative.not_native = "an array literal whose assigned `length` lacks a complete non-growing contents proof"

// `a[0] = 9` is an index store. JavaScript arrays can be sparse (`a[100] = 1`
// on an empty array gives length 101 with one element), so an index store is
// admitted only when density is proved; otherwise both `length` and the
// element-type join (which sees only appends) would be wrong. This uncalled
// copy proves density but is still refused, because DeadCodeAnalysis never
// visits it and its element types read `<unvisited>`.
// The called copy in ../../Fixtures/Objects/array/overwrite.js emits code.
//
// INDEXED: ctjs.func private @stored$1
// The reason is the unvisited element type, not the index store, which this
// copy proves dense.
// INDEXED-SAME: ctnative.not_native = "an array whose elements are <unvisited>, not numbers"

// `delete a[0]` lowers to the opaque __ctbrowser_delete host call, so the array escapes.
//
// DELETED: ctjs.func private @drop$1
// DELETED-SAME: ctnative.not_native = "an array literal that escapes - it reaches `ctjs.call`"

// A returned array outlives its frame. This is the default arm of
// isDenseVectorSite doing its job: a return is not an append and not a read,
// so it opens the site, and there is nothing else in the rule to delete.
//
// ESCAPING: ctjs.func private @leak$1
// ESCAPING-SAME: ctnative.not_native = "an array literal that escapes - it is returned"

// Mixed elements and boolean elements both need types, so both are called and
// both take `main` down with them, since a refused callee refuses its caller. The
// callee keeps its own reason, which is what these pin.
//
// The element type is the join over the appends from `undefined`, so a number
// and a string give an optional of a two-alternative variant, which has no
// native carrier. A typed `vector<dyn<double, str>>` carrier would handle
// this; it is not implemented yet.
//
// MIXED: ctjs.func private @mixed$1
// MIXED-SAME: ctnative.not_native = "an array whose elements are !ctnative.opt<!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>>, not numbers"

// `std::vector<bool>` is not a container of bool. Its `operator[]` returns a
// proxy that aliases the container and converts differently from `bool`.
// Lowering never deduces that type, so boolean arrays are refused rather than
// special-cased.
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
