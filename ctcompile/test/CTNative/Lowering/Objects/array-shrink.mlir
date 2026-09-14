// A complete current contents certificate permits only literal non-growing
// length assignments. Ownership requires local entry-block arrays; selected
// aliases borrow their owners. An unsupported operation invalidates the certificate.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../Fixtures/Objects/array-shrink.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SHRINK
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../Fixtures/Objects/array-shrink.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=SHRINK
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/growth.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=REFUSE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/fraction.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=REFUSE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/negative.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=REFUSE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=REFUSE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unknown.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=REFUSE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/missing.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=REFUSE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/late-call.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=REFUSE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/alias.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=ALIAS
// ALIAS-NOT: ctnative.not_native
// ALIAS-LABEL: emitc.func @blocked_1(
// ALIAS: scf.if
// ALIAS-SAME: !emitc.ptr<!emitc.opaque<"std::vector<double>">>
// ALIAS: emitc.address_of
// ALIAS: emitc.address_of
// ALIAS: verbatim "{}->resize(static_cast<std::vector<double>::size_type>({}));"
// ALIAS-NOT: ctnative.not_native
// SHRINK-NOT: ctnative.not_native
// SHRINK-LABEL: emitc.func @shrink_1()
// SHRINK: %[[A:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
// SHRINK: call_opaque "ctnative::vec_length"(%[[A]])
// SHRINK: verbatim "{}.resize(static_cast<std::vector<double>::size_type>({}));" args %[[A]],
// SHRINK: call_opaque "ctnative::vec_at"(%[[A]],
// SHRINK-LABEL: emitc.func @clear_2()
// SHRINK: verbatim "{}.resize(static_cast<std::vector<double>::size_type>({}));"
// SHRINK-LABEL: emitc.func @unchanged_3()
// SHRINK: verbatim "{}.resize(static_cast<std::vector<double>::size_type>({}));"
// SHRINK-NOT: ctnative.not_native
// REFUSE: ctnative.not_native =
// REFUSE-NOT: emitc.func

//--- growth.js
function blocked() {
  var a = [1, 2];
  a.length = 3;
  return a.length;
}
var observed = blocked();

//--- fraction.js
function blocked() {
  var a = [1, 2];
  a.length = 0.5;
  return a.length;
}
var observed = blocked();

//--- negative.js
function blocked() {
  var a = [1, 2];
  a.length = -1;
  return a.length;
}
var observed = blocked();

//--- string.js
function blocked() {
  var a = [1, 2];
  a.length = "0";
  return a.length;
}
var observed = blocked();

//--- unknown.js
function blocked(length) {
  var a = [1, 2];
  a.length = length;
  return a.length;
}
var observed = blocked(0);

//--- missing.js
function blocked() {
  var a = [1, 2];
  a.length = 0;
  return a[0];
}
var observed = blocked();

//--- late-call.js
function blocked() {
  var a = [1, 2];
  a.length = 0;
  var effect = later();
  return a.length + effect;
}
function later() { return 1; }
var observed = blocked();

//--- alias.js
function blocked(flag) {
  var a = [1, 2], b = [3, 4];
  var alias = flag ? a : b;
  alias.length = 0;
  return a.length + b.length;
}
var observed = blocked(true);
