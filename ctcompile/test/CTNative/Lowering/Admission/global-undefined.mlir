// Global observations preserve each proved scalar alternative's actual tag.
// Every source store contributes to the census; a constant last write or an
// observation request cannot erase earlier Number/Boolean or absent alternatives.
// Unsupported String/Number and String/Boolean joins still refuse.
//
// ONE PROGRAM PER FILE, VIA split-file, for the reason divergence-refusals.mlir
// gives: admission reports the FIRST refusal per function and every global
// store in these programs lives in `_script_$0`, so two refusals in one file
// would pin whichever came first and be green on the other for the wrong
// reason.
//
// RUN: split-file %s %t
// RUN: python3 %S/nullable-global-output.py --translate ctjs-translate --opt ctjs-opt --work %t/execute --node %node --reference %native_reference
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

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=STRING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string-optional.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=STRING_OPTIONAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string-mixed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=STRING_MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string-writes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=STRING_WRITES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string-callee.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=STRING_CALLEE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string-overwrite.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=STRING_OVERWRITE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string-early.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' \
// RUN:   | FileCheck %s --check-prefix=STRING_EARLY

// Hoisted globals, conditionally assigned cells and undominated field reads
// retain Undefined. The output carrier must print their tags without narrowing
// the underlying inference. Exact dominance proofs still keep numeric returns.
// HOISTED-NOT: ctnative.not_native
// HOISTED: emitc.global static @g_u : !emitc.opaque<"ctnative::nullable_scalar">
// HOISTED: call_opaque "ctnative::print_scalar"
// HOISTED: call_opaque "ctnative::print_scalar"
// HOISTED-NOT: ctnative.not_native
// PICK-NOT: ctnative.not_native
// PICK: call_opaque "ctnative::print_scalar"
// PICK: emitc.func @pick_1({{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// PICK-NOT: ctnative.not_native
// OUTERSTORE-NOT: ctnative.not_native
// OUTERSTORE: call_opaque "ctnative::print_scalar"
// OUTERSTORE: emitc.func @pick2_1({{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// OUTERSTORE-NOT: ctnative.not_native
// DOMINATES-NOT: ctnative.not_native
// DOMINATES: emitc.global static @g_c : !emitc.opaque<"ctnative::nullable_scalar">
// DOMINATES: emitc.func @counter_1() -> f64
// DOMINATES-NOT: ctnative.not_native
// FIELD-NOT: ctnative.not_native
// FIELD: emitc.global static @g_f : !emitc.opaque<"ctnative::nullable_scalar">
// FIELD: emitc.func @held_1() -> f64
// FIELD-NOT: ctnative.not_native
// READBEFORE-NOT: ctnative.not_native
// READBEFORE: call_opaque "ctnative::print_scalar"
// READBEFORE: emitc.func @early_1() -> !emitc.opaque<"ctnative::nullable_scalar">
// READBEFORE-NOT: ctnative.not_native
// ONEPATH-NOT: ctnative.not_native
// ONEPATH: call_opaque "ctnative::print_scalar"
// ONEPATH: emitc.func @maybe_1({{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// ONEPATH-NOT: ctnative.not_native

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
// BOOLEAN_OPTIONAL-NOT: ctnative.not_native
// BOOLEAN_OPTIONAL: call_opaque "ctnative::print_scalar"
// BOOLEAN_OPTIONAL-NOT: ctnative.not_native
// BOOLEAN_MIXED-NOT: ctnative.not_native
// BOOLEAN_MIXED: call_opaque "ctnative::print_scalar"
// BOOLEAN_MIXED-NOT: ctnative.not_native
// BOOLEAN_WRITES-NOT: ctnative.not_native
// BOOLEAN_WRITES: call_opaque "ctnative::print_scalar"
// BOOLEAN_WRITES-NOT: ctnative.not_native
// BOOLEAN_CALLEE-NOT: ctnative.not_native
// BOOLEAN_CALLEE: call_opaque "ctnative::print_scalar"
// BOOLEAN_CALLEE-NOT: ctnative.not_native

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

// String storage is owning, and the full store census decides its tag.
// Early typeof observes initial Undefined without borrowing a later String.
// STRING-NOT: ctnative.not_native
// STRING: emitc.global static @g_empty : !emitc.opaque<"ctnative::nullable_string">
// STRING: call_opaque "ctnative::global_string"
// STRING: call_opaque "ctnative::print_string"
// STRING-NOT: ctnative.not_native
// STRING_OPTIONAL-NOT: ctnative.not_native
// STRING_OPTIONAL: emitc.global static @g_result : !emitc.opaque<"ctnative::nullable_string">
// STRING_OPTIONAL: call_opaque "ctnative::print_scalar"
// STRING_OPTIONAL-NOT: ctnative.not_native
// STRING_MIXED: ctnative.not_native = "store to global `result` is {{.*}}; native global observations require a supported scalar type"
// STRING_WRITES: ctnative.not_native = "store to global `result` has inconsistent global observation types"
// STRING_CALLEE: ctnative.not_native = "store to global `result` has inconsistent global observation types"
// STRING_OVERWRITE-NOT: ctnative.not_native
// STRING_OVERWRITE: emitc.global static @g_result : !emitc.opaque<"ctnative::nullable_string">
// STRING_OVERWRITE: call_opaque "ctnative::global_string"
// STRING_OVERWRITE: emitc.func @overwrite_1
// STRING_OVERWRITE-NOT: ctnative.not_native
// STRING_EARLY-NOT: ctnative.not_native
// STRING_EARLY: emitc.global static @g_fixed : !emitc.opaque<"ctnative::nullable_string">
// STRING_EARLY: call_opaque "ctnative::string_typeof"
// STRING_EARLY: call_opaque "ctnative::global_string"
// STRING_EARLY-NOT: ctnative.not_native

//--- string.js
var empty = '';
var text = 'owned';
var count = 2;

//--- string-optional.js
function choose(flag) { if (flag) { return 'owned'; } }
var result = choose(false);

//--- string-mixed.js
function choose(flag) { return flag ? 'owned' : false; }
var result = choose(false);

//--- string-writes.js
var result = 'owned';
result = 1;

//--- string-callee.js
function overwrite() { result = false; }
var result = 'owned';
overwrite();

//--- string-overwrite.js
function overwrite() { result = 'later'; }
var result = 'first';
overwrite();

//--- string-early.js
var tag = typeof fixed;
var fixed = 'x';
