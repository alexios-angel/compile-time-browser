// Current complete own-contents evidence admits direct local Number overwrites.
// The called fixture also runs through the native differential/compile-clean gate
// under both optimization policies. array.mlir's original uncalled indexed.js
// remains a separate control; no body or call was changed there.
//
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../Fixtures/Objects/array-overwrite.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=STORED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../Fixtures/Objects/array-overwrite.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=STORED
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/sparse.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=INDEX
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/fractional.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=INDEX
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/negative.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=INDEX
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unknown.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=INDEX
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nullable-value.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=READ
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nullable-index.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=READ
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/late-call.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=INDEX
//
// One by-value vector and an assignment on that same storage; no helper or copy.
// STORED-NOT: ctnative.not_native
// STORED-LABEL: emitc.func @stored_1()
// STORED: %[[A:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
// STORED: call_opaque "ctnative::vec_push"(%[[A]],
// STORED: call_opaque "ctnative::vec_push"(%[[A]],
// STORED: verbatim "{}[static_cast<std::vector<double>::size_type>({})] = {};" args %[[A]],
// STORED: call_opaque "ctnative::vec_at"(%[[A]],
// STORED-NOT: ctnative.not_native
//
// INDEX: ctjs.func private @blocked$1
// INDEX-SAME: ctnative.not_native = "an array literal written through an index
// MIXED: ctjs.func private @blocked$1
// MIXED-SAME: ctnative.not_native = "an array whose elements are !ctnative.opt<!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>>, not numbers"
// The same original read-fed value/index sources now have complete own reads.
// READ-NOT: ctnative.not_native
// READ-LABEL: emitc.func @blocked_1()
// READ: call_opaque "ctnative::vec_at"
// READ: verbatim "{}[static_cast<std::vector<double>::size_type>({})] = {};"
// READ-NOT: ctnative.not_native

//--- sparse.js
function blocked() {
  var a = [1, 2];
  a[2] = 9;
  return a[0];
}
var observed = blocked();

//--- fractional.js
function blocked() {
  var a = [1, 2];
  a[0.5] = 9;
  return a[0];
}
var observed = blocked();

//--- negative.js
function blocked() {
  var a = [1, 2];
  a[-1] = 9;
  return a[0];
}
var observed = blocked();

//--- unknown.js
function blocked(index) {
  var a = [1, 2];
  a[index] = 9;
  return a[0];
}
var observed = blocked(0);

//--- mixed.js
function blocked() {
  var a = [1, 2];
  a[0] = "changed";
  return a[0];
}
var observed = blocked();

//--- nullable-value.js
function blocked() {
  var a = [1, 2];
  a[0] = a[1];
  return a[0];
}
var observed = blocked();

//--- nullable-index.js
function blocked() {
  var a = [0, 1];
  var index = a[0];
  a[index] = 9;
  return a[0];
}
var observed = blocked();

//--- late-call.js
function blocked() {
  var a = [1, 2];
  a[0] = 9;
  var called = later();
  return a[0] + called;
}
function later() { return 1; }
var observed = blocked();

