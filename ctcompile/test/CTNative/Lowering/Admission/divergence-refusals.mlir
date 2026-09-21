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
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/equality.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=EQUALITY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/relational.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=RELATIONAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/typeof.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=TYPEOF
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/bitwise.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=BITWISE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/concat.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=CONCAT
// RUN: cmake -DTRANSLATE=ctjs-translate -DOPT=ctjs-opt -DSOURCE=%t/globalstring.js -DOUTPUT=%t/globalstring.mlir -P %S/../../Checks/pipeline.cmake
// RUN: FileCheck %s --check-prefix=GLOBALSTRING --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func < %t/globalstring.mlir
// RUN: ctjs-translate --mlir-to-cpp %t/globalstring.mlir > %t/globalstring.cpp
// RUN: %gxx -O2 -ffp-contract=off %t/globalstring.cpp -o %t/globalstring-gcc
// RUN: %t/globalstring-gcc | FileCheck %s --check-prefix=GLOBALSTRINGOUT --match-full-lines
// RUN: %clangxx -O2 -ffp-contract=off %t/globalstring.cpp -o %t/globalstring-clang
// RUN: %t/globalstring-clang | FileCheck %s --check-prefix=GLOBALSTRINGOUT --match-full-lines
// RUN: nm -C %t/globalstring-gcc | FileCheck %s --check-prefix=GLOBALSTRINGBIN --implicit-check-not=ctbrowser::script

// Tagged optional scalars distinguish absent values from present NaN. Equality,
// numeric ordering and typeof now lower; the source differential fixture
// checks their answers independently of these structural assertions.
//
// EQUALITY: emitc.func @equality_1() -> !emitc.opaque<"ctnative::js_num">
// EQUALITY: call_opaque "ctnative::scalar_strict_equal"
// EQUALITY-NOT: ctnative.not_native
// RELATIONAL: emitc.func @relational_1() -> !emitc.opaque<"ctnative::js_num">
// RELATIONAL-NOT: ctnative.not_native
// TYPEOF: emitc.func @kind_1({{.*}}) -> !emitc.opaque<"ctnative::js_num">
// TYPEOF-NOT: ctnative.not_native

// --- ND-9: A BITWISE OPERATOR IS ToInt32, WHICH IS NOT A C++ CAST -----------
//
// `x | 0` is ToInt32(x): truncate toward zero, then take the value modulo
// 2^32 as a signed 32-bit integer. `2147483648 | 0` is -2147483648 in
// JavaScript, and `static_cast<int32_t>(2147483648.0)` is UNDEFINED BEHAVIOUR
// in C++ - not merely a different number, an unbounded one. NaN and the
// infinities are 0 under ToInt32 and are undefined behaviour under the cast
// too. The typed Number operator now uses the public Core conversion shared
// with the VM, including the wrap and non-finite cases.
//
// BITWISE: bitwise_or
// BITWISE-SAME: !emitc.opaque<"ctnative::js_num">
// BITWISE-NOT: ctnative.not_native

// Exact Number/String concatenation uses the typed String overload and public
// Core formatting; string-coercions.test checks the executable results.
// CONCAT: emitc.func @concat_1({{.*}}!emitc.opaque<"ctnative::js_num">{{.*}}) -> !emitc.opaque<"ctnative::js_string">
// CONCAT: add {{.*}} : (!emitc.opaque<"ctnative::js_num">, !emitc.opaque<"ctnative::js_string">) -> !emitc.opaque<"ctnative::js_string">
// CONCAT-NOT: ctnative.not_native

// The unchanged global String load returns through its owning optional carrier.
// Its actual String tag survives the call and is printed without narrowing.
// GLOBALSTRING: emitc.func @main()
// GLOBALSTRING: call_opaque "ctnative::print_scalar"
// GLOBALSTRING: emitc.func @readlabel_1()
// GLOBALSTRINGOUT: f="n"
// GLOBALSTRINGOUT-NEXT: label="n"
// GLOBALSTRINGOUT-NOT: {{.}}
// GLOBALSTRINGBIN: {{.*}} T main

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
