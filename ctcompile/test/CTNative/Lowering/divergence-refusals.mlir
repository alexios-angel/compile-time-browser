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

// --- ND-7: EQUALITY IS WHERE undefined-AS-NaN STOPS BEING EXACT -------------
//
// The whole `opt` representation rests on NaN behaving like undefined, and it
// does - in arithmetic, in relational comparison and in truthiness. It does
// NOT in equality, and it fails there in BOTH directions at once:
// `undefined === undefined` is true where `NaN === NaN` is false, and
// `undefined === 0` is false where a NaN compared to 0 is also false but for
// an unrelated reason. `!=` and `!==` import as this same op with a negate
// flag, so refusing the op covers all four spellings.
//
// EQUALITY: ctnative.not_native = "equality on a value that may be undefined - NaN would not compare the way undefined does"

// --- AND THE SAME VALUE UNDER `<` IS ADMITTED -------------------------------
//
// The refusal above is the narrow one it claims to be, not a blanket ban on
// reading a field that was never written. `undefined < 1` is false and
// `NaN < 1` is false, so the relational operators carry the representation
// exactly and stay admitted. If this half goes red the rule has become "any
// `opt` is refused", and native-divergence-fixture.js's u_lt/u_le/u_gt/u_ge
// globals would be unreachable.
//
// RELATIONAL: emitc.func @relational_1() -> f64
// RELATIONAL-NOT: ctnative.not_native

// --- `typeof` DISTINGUISHES undefined FROM A NUMBER, AND NaN IS A NUMBER ----
//
// `typeof undefined` is "undefined"; `typeof NaN` is "number". A tier that
// carries the first as the second cannot answer this operator at all, so it
// is refused rather than approximated. `void` and `~` share the refusal: void
// yields undefined as a VALUE (not as a missing one) and `~` is ToInt32, which
// is the ND-8 modular wrap below.
//
// TYPEOF: ctnative.not_native = "typeof, void and ~ are not native yet"

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
// THE REFUSAL COMES FROM THE STATIC FAMILY, and pinning the other string was
// this test's own first failure. BytecodeImport.cpp's `binary_rows` marks all
// six bitwise opcodes NON-re-entering, so `&`, `|`, `^`, `<<`, `>>` and `>>>`
// import as `ctjs.binary_static` and never as `ctjs.binary`. The `ctjs.binary`
// arm's "a bitwise or string operator is not native yet" is reachable only
// through `BinaryKind::Concat`, and a concatenation needs a string, and a
// string in a native candidate is refused at its CONSTANT first (the next
// case) - so that sentence has no minimal witness today. Said here rather
// than pinned with a program that does not produce it.
//
// BITWISE: ctnative.not_native = "a static bitwise operator is not native yet"

// --- `+` ON A STRING IS CONCATENATION, AND A STRING IS NOT A CARRIER -------
//
// The refusal lands on the constant rather than on the `+`, which is the
// earlier and more useful site: it names the value that has no representation
// instead of the operator that could not use it.
//
// CONCAT: ctnative.not_native = "a constant that is not a number, a boolean or undefined"

// --- A GLOBAL THAT IS NOT A NUMBER IS REFUSED WHERE IT IS READ -------------
//
// The type in the message is whatever the inference proved, so only the
// prefix is pinned: the assertion is that the refusal names the GLOBAL, which
// is the thing a reader has to go and look at.
//
// GLOBALSTRING: ctnative.not_native = "global `label` is

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
  return typeof x;
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
