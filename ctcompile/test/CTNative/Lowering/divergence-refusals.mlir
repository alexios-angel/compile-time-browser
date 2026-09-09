// THE DECLARED DIVERGENCES THE TIER REFUSES - part 24 Phase 63 Step 5, the
// REFUSED half.
//
// ctcompile/docs/native-divergences.md is one row per place the native tier's
// answer differs, or must not differ, from the interpreter's. Each row says
// whether the tier emits a GUARD (the two sides agree, and the divergence
// gate proves it - native-divergence-fixture.js) or REFUSES (no program can
// witness it, so the refusal TEXT is the only thing a test can hold onto).
// This file is the second kind. Every string below is quoted verbatim in the
// document beside the JavaScript that produces it.
//
// ONE PROGRAM PER FILE, VIA split-file. A refusal is contagious in both
// directions - a refused callee refuses its caller and a refused `main`
// refuses everything it calls - so a file that pinned two of these would pin
// whichever the walk reached first and would be green on the other for the
// wrong reason. shape-field-names.mlir says the same thing and learned it the
// same way.
//
// WHAT IS PINNED ELSEWHERE, and is therefore NOT repeated here: an inherited
// `Object.prototype` name and a field name that is a C++ keyword or a <cmath>
// macro (shape-field-names.mlir); an array of booleans and an array of mixed
// element types (native-array.mlir). The document cites those files by name.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/equality.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EQUALITY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/relational.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=RELATIONAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/typeof.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=TYPEOF
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/bitwise.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=BITWISE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/concat.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CONCAT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/globalstring.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=GLOBALSTRING

// Tagged optional scalars distinguish absent values from present NaN. Equality,
// numeric ordering and typeof now lower; the source differential fixture
// checks their answers independently of these structural assertions.
//
// EQUALITY: emitc.func @equality_1() -> f64
// EQUALITY: call_opaque "ctnative::scalar_strict_equal"
// EQUALITY-NOT: ctnative.not_native
// RELATIONAL: emitc.func @relational_1() -> f64
// RELATIONAL-NOT: ctnative.not_native
// TYPEOF: emitc.func @kind_1({{.*}}) -> f64
// TYPEOF-NOT: ctnative.not_native

// --- ND-9: A BITWISE OPERATOR IS ToInt32, WHICH IS NOT A C++ CAST -----------
//
// `x | 0` is ToInt32(x): truncate toward zero, then take the value modulo
// 2^32 as a signed 32-bit integer. `2147483648 | 0` is -2147483648 in
// JavaScript, and `static_cast<int32_t>(2147483648.0)` is UNDEFINED BEHAVIOUR
// in C++ - not merely a different number, an unbounded one. NaN and the
// infinities are 0 under ToInt32 and are undefined behaviour under the cast
// too. So the operator is refused until it is emitted with the wrap written
// out, which is the same shape the `**` guard has.
//
// BITWISE: ctnative.not_native = "a static bitwise operator is not native yet"

// CONCAT: ctnative.not_native = "binary operand is !ctnative.str<utf8>, not a number"

// --- AN UNPROVED CALL RESULT CANNOT BECOME A GLOBAL OBSERVATION ------------
//
// The historical source is unchanged: the definite String store to `label`
// now has owning storage, but the direct call's return value is not yet proved
// definite at the store to `f`. Pin that remaining refusal by the destination
// global, rather than keeping the obsolete String-load refusal.
//
// GLOBALSTRING: ctjs.func @_script_$0
// GLOBALSTRING-SAME: ctnative.not_native = "store to global `f` requires a Number, Boolean or String global"

//--- equality.js
function equality() {
  var o = { seen: 1 };
  return o.later === 5 ? 1 : 0;
}
var a = equality();

//--- relational.js
function relational() {
  var o = { seen: 1 };
  return o.later < 5 ? 1 : 0;
}
var b = relational();

//--- typeof.js
function kind(x) {
  return typeof x === "number" ? 1 : 0;
}
var c = kind(1);

//--- bitwise.js
function bits(x) {
  return x | 0;
}
var d = bits(2147483648);

//--- concat.js
function concat(x) {
  return x + "!";
}
var e = concat(1);

//--- globalstring.js
var label = "n";
function readlabel() {
  return label;
}
var f = readlabel();
