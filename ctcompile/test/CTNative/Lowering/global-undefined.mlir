// Global observations require one definite Number or Boolean source type.
// Tagged scalar storage preserves early undefined reads, but absence and mixed
// tags remain refused at output. Every source store contributes to the census;
// neither a constant last write nor a requested observation manufactures a type.
// The exact runtime tag is checked again before printing Numbers or Booleans.
//
// ONE PROGRAM PER FILE, VIA split-file, for the reason divergence-refusals.mlir
// gives: admission reports the FIRST refusal per function and every global
// store in these programs lives in `_script_$0`, so two refusals in one file
// would pin whichever came first and be green on the other for the wrong
// reason.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/hoisted.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=HOISTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/pick.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=PICK
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/outerstore.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OUTERSTORE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dominates.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DOMINATES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/field.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FIELD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/readbefore.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=READBEFORE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/onepath.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ONEPATH

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boolean.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=BOOLEAN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boolean-optional.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=BOOLEAN_OPTIONAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boolean-mixed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=BOOLEAN_MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boolean-writes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=BOOLEAN_WRITES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boolean-callee.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=BOOLEAN_CALLEE

// --- THE SHAPE WITH NO CLOSURE IN IT ---------------------------------------
//
// `var u; var z = u;` is the whole defect, and it is older than any closure
// rule: `u` is a global bound to the `undefined` the declaration hoists, so
// the value stored under it is `opt<>` and the value stored under `z` is the
// same one read back. Before this clause both stores were admitted, and the
// binary printed `u=nan z=nan` where the interpreter prints
// `u=undefined z=undefined`.
//
// THE FIRST STORE IS THE ONE PINNED. Both refuse, admission reports the first,
// and `u` is it.
//
// HOISTED: ctnative.not_native = "store to global `u` may be null or undefined; native global observations require a definite Number or Boolean"

// --- AND THE SHAPE SLICE 2 STEP 2 INTRODUCED -------------------------------
//
// `v` is a shared binding carried by pointer, written on ONE path of an `if`
// and read in a closure called after it. There is a path to `get()` on which
// nothing was assigned, so the variable holds the NaN it was initialised with
// and `pick(-1)` is `undefined` in the interpreter. Step 2 stopped refusing the
// program - correctly, the arithmetic on it is exact - and the value then
// reached a global, where it printed `nan`.
//
// THE NARROWING MUST NOT TAKE THIS ONE, and that is the whole hazard of the
// other half of this slice: typing `v` as `num` here would not refuse anything,
// it would PRINT A NUMBER where the interpreter prints `undefined`.
//
// PICK: ctnative.not_native = "store to global `out` may be null or undefined; native global observations require a definite Number or Boolean"

// --- THE STORE, NOT THE VALUE, AND THAT DISTINCTION HAS BEEN WRONG BEFORE --
//
// `var t = k * 2; if (k > 0) { v = t; }`. The stored VALUE is computed before
// the branch and dominates every call of `get`; the STORE dominates none of
// them. Slice 2 step 1 shipped a wrong answer by asking about the value, and
// this program is what caught it: compiled by value, `pick2(-1)` answered -2
// where the interpreter says `undefined`.
//
// IT IS PINNED HERE SEPARATELY FROM PICK because the two fail different
// clauses of the same rule. PICK has no value dominating anything either, so a
// narrowing that asked the OLD, wrong question would still refuse PICK and be
// green on it - a guard passing on a witness that cannot fail it.
//
// THAT MISTAKE HAS A NAME AND THIS CITATION USED TO GET IT WRONG. It was the
// `loopwrite` program in closure-refusals.mlir, whose pin named a dominance
// condition and passed while that condition was a no-op, because it stored a
// value defined INSIDE the loop body and so tripped the earlier
// value-dominates clause first. `outerstore` - a value computed ABOVE the
// branch - is what only the store clause can refuse. Both moved into
// native-shared-cell-fixture.js when slice 2 step 2 made them compile, so
// neither is a check-prefix in closure-refusals.mlir any more; the lesson is
// why this file pins PICK and OUTERSTORE separately.
//
// OUTERSTORE: ctnative.not_native = "store to global `out2` may be null or undefined; native global observations require a definite Number or Boolean"

// --- AND THE SHAPES THE NARROWING EXISTS FOR -------------------------------
//
// A CARRIED CELL. `var n = 0` is a write that dominates every read of the
// binding: the frame's own `return n`, and - through the calls of `tick`, which
// it also dominates - the read inside the closure. No read can see the hoisted
// `undefined`, so the binding is `num` and the global is a Number.
//
// WITHOUT THE NARROWING THIS IS RED. `c` would be `opt<num>` and refused by the
// clause the three programs above pin, which is what makes this a gate on the
// narrowing rather than a description of it. `@g_c : f64` is the assertion that
// the global is a Number, and `@counter_1` that the frame it came out of is
// claimed too.
//
// DOMINATES-NOT: ctnative.not_native
// DOMINATES: emitc.global static @g_c : !emitc.opaque<"ctnative::nullable_scalar">
// DOMINATES: emitc.func @counter_1() -> f64
// DOMINATES-NOT: ctnative.not_native

// --- AND A FIELD, WHICH IS THE SAME IMPRECISION ONE OPERAND ALONG ----------
//
// A closed-shape field read is the join over the stores of that key STARTING
// FROM `undefined`, "because nothing orders the read after a store". That is
// true of a field in general and false of this read: `{n: 8}` compiles to a
// `ctjs.set_property` in the frame, on this SSA value, dominating the
// `ctjs.get_property` after it. So the seed is dropped and `p.n` is `num`.
//
// FIELD-NOT: ctnative.not_native
// FIELD: emitc.global static @g_f : !emitc.opaque<"ctnative::nullable_scalar">
// FIELD: emitc.func @held_1() -> f64
// FIELD-NOT: ctnative.not_native

// --- A READ BEFORE THE STORE, WHICH KEEPS ITS undefined --------------------
//
// `var before = o.later; o.later = 5;`. The store is in the same function on
// the same value and does NOT dominate the read, so the seed stays and
// `before` is `opt<num>` - which is the honest answer: the interpreter says
// `undefined` there. This is the field half's dominance clause, and it is the
// one witness that separates "a store exists" from "a store comes first".
//
// READBEFORE: ctnative.not_native = "store to global `e` may be null or undefined; native global observations require a definite Number or Boolean"

// --- AND A STORE ON ONE PATH OF AN `if` ------------------------------------
//
// The store to `o.hit` is inside the `scf.if` and dominates nothing after it,
// so `maybe(-1)` reads a field nothing wrote. Pinned separately from
// READBEFORE because the two fail dominance in different directions - one is
// program order without dominance, the other is a store that never ran - and a
// rule that asked for "a store anywhere in this function" would pass the
// second while getting the first right.
//
// ONEPATH: ctnative.not_native = "store to global `mm` may be null or undefined; native global observations require a definite Number or Boolean"

//--- hoisted.js
var u;
var z = u;

//--- pick.js
function pick(k) {
  var v;
  if (k > 0) { v = 5; }
  function get() { return v; }
  return get();
}
var out = pick(0 - 1);

//--- outerstore.js
function pick2(k) {
  var v;
  var t = k * 2;
  if (k > 0) { v = t; }
  function get() { return v; }
  return get();
}
var out2 = pick2(0 - 1);

//--- dominates.js
function counter() {
  var n = 0;
  function tick() { n = n + 1; return n; }
  tick();
  tick();
  return n;
}
var c = counter();

//--- field.js
function held() {
  var p = { n: 8 };
  return p.n;
}
var f = held();

//--- readbefore.js
function early() {
  var o = { seen: 1 };
  var before = o.later;
  o.later = 5;
  return before;
}
var e = early();

//--- onepath.js
function maybe(k) {
  var o = { seen: 1 };
  if (k > 0) { o.hit = 1; }
  return o.hit;
}
var mm = maybe(0 - 1);

// Both Boolean values use exact Boolean observations; a neighboring Number
// still calls global_number. Optional or mixed returns retain their real types.
// BOOLEAN-NOT: ctnative.not_native
// BOOLEAN: emitc.global static @g_off : !emitc.opaque<"ctnative::nullable_scalar">
// BOOLEAN: call_opaque "ctnative::global_number"
// BOOLEAN: call_opaque "ctnative::global_boolean"
// BOOLEAN: call_opaque "ctnative::global_boolean"
// BOOLEAN-NOT: ctnative.not_native
// BOOLEAN_OPTIONAL: ctnative.not_native = "store to global `result` may be null or undefined; native global observations require a definite Number or Boolean"
// BOOLEAN_MIXED: ctnative.not_native = "store to global `result` is !ctnative.variant<!ctnative.bool, !ctnative.num<i32>>; native global observations require a definite Number or Boolean"
// BOOLEAN_WRITES: ctnative.not_native = "store to global `result` has inconsistent global observation types"
// BOOLEAN_CALLEE: ctnative.not_native = "store to global `result` has inconsistent global observation types"

//--- boolean.js
var off = false;
var on = true;
var count = 2;

//--- boolean-optional.js
function choose(flag) { if (flag) { return true; } }
var result = choose(false);

//--- boolean-mixed.js
function choose(flag) { return flag ? true : 1; }
var result = choose(false);

//--- boolean-writes.js
var result = false;
result = 1;

//--- boolean-callee.js
function overwrite() { result = 1; }
var result = false;
overwrite();
