// A GLOBAL IS WHERE undefined-AS-NaN STOPS BEING EXACT - part 24 Phase 59
// slice 2 step 3, and ND-7's printing row in
// ctcompile/docs/native-divergences.md.
//
// The tier carries `undefined` as NaN and that is exact in arithmetic, in
// relational comparison and in truthiness. It is not exact in a PRINT: the
// convention is `%.17g` of the double, which has no spelling for `undefined`
// at all, so a global holding a possibly-undefined value prints `nan` where the
// interpreter prints `undefined`. That is a WRONG ANSWER and not a refusal, and
// no gate outside the differential comparison can see it - the differential
// reference skips a global that is not a Number, so the binary prints a line
// the reference does not have.
//
// SO THE STORE REFUSES, AND THE REFUSAL NAMES THE CONVENTION. A reader meeting
// it has to know why a global is different from every other use of the same
// value, and "may be undefined" does not say that: `o.later + 1` is admitted on
// the same type, one line away.
//
// AND THE OTHER HALF OF THE SLICE IS WHY THAT COSTS NOTHING. A carried shared
// binding is emitted as a frame-scope variable holding the box's hoisted
// `undefined`, so its type was `opt<num>` FLOW-INSENSITIVELY - at a read a
// write dominates just as much as at one it does not. DOMINATES below is that
// shape and it must COMPILE; PICK and OUTERSTORE are the two shapes where no
// write dominates the read and they must not.
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
// HOISTED: ctnative.not_native = "store to global `u` may be undefined, and a global is where a value becomes an observable: this tier prints a Number as `%.17g` of the double, so undefined carried as NaN prints `nan` where the interpreter prints `undefined`"

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
// PICK: ctnative.not_native = "store to global `out` may be undefined, and a global is where a value becomes an observable: this tier prints a Number as `%.17g` of the double, so undefined carried as NaN prints `nan` where the interpreter prints `undefined`"

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
// OUTERSTORE: ctnative.not_native = "store to global `out2` may be undefined, and a global is where a value becomes an observable: this tier prints a Number as `%.17g` of the double, so undefined carried as NaN prints `nan` where the interpreter prints `undefined`"

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
// DOMINATES: emitc.global static @g_c : f64
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
// FIELD: emitc.global static @g_f : f64
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
// READBEFORE: ctnative.not_native = "store to global `e` may be undefined, and a global is where a value becomes an observable: this tier prints a Number as `%.17g` of the double, so undefined carried as NaN prints `nan` where the interpreter prints `undefined`"

// --- AND A STORE ON ONE PATH OF AN `if` ------------------------------------
//
// The store to `o.hit` is inside the `scf.if` and dominates nothing after it,
// so `maybe(-1)` reads a field nothing wrote. Pinned separately from
// READBEFORE because the two fail dominance in different directions - one is
// program order without dominance, the other is a store that never ran - and a
// rule that asked for "a store anywhere in this function" would pass the
// second while getting the first right.
//
// ONEPATH: ctnative.not_native = "store to global `mm` may be undefined, and a global is where a value becomes an observable: this tier prints a Number as `%.17g` of the double, so undefined carried as NaN prints `nan` where the interpreter prints `undefined`"

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
