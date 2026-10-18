// Declared native-vs-interpreter divergences that used to be refused and now
// lower: tagged optional scalars (equality, ordering, typeof), ToInt32 for
// bitwise operators, Number/String concatenation, and a global String load.
//
// ctcompile/docs/native-divergences.md has one row per place the native
// lowering's answer differs, or must not differ, from the interpreter's. The
// runtime answers are checked by Fixtures/Scalars/divergence.js and
// ../Scalars/string/coercions.test, so this file only asserts that each case
// lowers (no ctnative.not_native) and has the expected native signature.
//
// One program per split-file input. A refusal spreads in both directions (a
// refused callee refuses its caller and a refused `main` refuses everything
// it calls), so a file holding two cases would report whichever the walk
// reached first and could pass on the other for the wrong reason.
// Objects/shape-field-names.mlir explains the same rule.
//
// Covered elsewhere and not repeated here: an inherited `Object.prototype`
// name and a field name that is a C++ keyword or a <cmath> macro
// (Objects/shape-field-names.mlir); arrays of booleans and of mixed element
// types (Objects/array.mlir).
//
// Labels such as ND-9 are row ids in ctcompile/docs/native-divergences.md.
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

// Tagged optional scalars distinguish an absent value from a present NaN, so
// equality, numeric ordering and typeof lower; Fixtures/Scalars/divergence.js
// checks their runtime answers independently of these structural assertions.
//
// EQUALITY: emitc.func @equality_1() -> !emitc.opaque<"ctnative::js_num">
// EQUALITY: cmp eq, {{.*}} : (!emitc.opaque<"ctnative::nullable_scalar">, !emitc.opaque<"ctnative::js_num">) -> i1
// EQUALITY-NOT: ctnative.not_native
// RELATIONAL: emitc.func @relational_1() -> !emitc.opaque<"ctnative::js_num">
// RELATIONAL-NOT: ctnative.not_native
// TYPEOF: emitc.func @kind_1({{.*}}) -> !emitc.opaque<"ctnative::js_num">
// TYPEOF-NOT: ctnative.not_native

// --- ND-9: a bitwise operator is ToInt32, not a C++ cast ---------------------
//
// `x | 0` is ToInt32(x): truncate toward zero, then take the value modulo
// 2^32 as a signed 32-bit integer. `2147483648 | 0` is -2147483648 in
// JavaScript, while `static_cast<int32_t>(2147483648.0)` is undefined
// behaviour in C++, not merely a different number. NaN and the infinities are
// 0 under ToInt32 and undefined behaviour under the cast too. The native
// operator calls the same ToInt32 conversion the VM uses (in ctbrowser's Core
// library), including the wrap and non-finite cases.
//
// BITWISE: bitwise_or
// BITWISE-SAME: !emitc.opaque<"ctnative::js_num">
// BITWISE-NOT: ctnative.not_native

// Number/String concatenation uses the typed String overload and the Number
// formatting the VM uses; ../Scalars/string/coercions.test runs the results.
// CONCAT: emitc.func @concat_1({{.*}}!emitc.opaque<"ctnative::js_num">{{.*}}) -> !emitc.opaque<"ctnative::js_string">
// CONCAT: add {{.*}} : (!emitc.opaque<"ctnative::js_num">, !emitc.opaque<"ctnative::js_string">) -> !emitc.opaque<"ctnative::js_string">
// CONCAT-NOT: ctnative.not_native

// A global String load returns through an optional value that owns the string.
// Its String tag survives the call and is printed as a string.
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
