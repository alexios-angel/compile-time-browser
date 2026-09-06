// Owning fields preserve aliases and require a receiver proof at every access.
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-object-fields-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unguarded.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=RECEIVER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/scalar-truthy.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=RECEIVER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/loose-guard.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=RECEIVER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/different-lookup.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=RECEIVER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/wrong-arm.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=RECEIVER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string-leaf.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=LEAF
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/cycle.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic-key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inherited.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prototype-write.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/accessor.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED

// NATIVE-DAG: owning identity with proved scalar fields
// NATIVE-DAG: nullable_scalar field_76616c7565;
// NATIVE-DAG: nullable_scalar field_782d79;
// NATIVE-DAG: call_opaque "ctnative::object_get_field_76616c7565"
// NATIVE-DAG: call_opaque "ctnative::object_set_field_76616c7565"
// RECEIVER: ctnative.not_native = "an owning field receiver may be scalar, null or undefined;
// LEAF: ctnative.not_native = "owning object fields require number/boolean/null/undefined values"
// REFUSED: ctnative.not_native =

//--- unguarded.js
function probe() { const map = new Map(); map.set("x", {value:1}); map.clear(); return +map.get("x").value; } probe();

//--- scalar-truthy.js
function probe() { const map = new Map(); map.set("x", {value:1}); map.set("n", 1); const value = map.get("n"); if (value) { return +value.value; } return 0; } probe();

//--- loose-guard.js
function probe() { const map = new Map(); const first = {value:1}; map.set("x", first); const value = map.get("x"); if (value == first) { return +value.value; } return 0; } probe();

//--- different-lookup.js
function probe() { const map = new Map(); const first = {value:1}; map.set("x", first); if (map.get("x") === first) { map.clear(); return +map.get("x").value; } return 0; } probe();

//--- wrong-arm.js
function probe() { const map = new Map(); const first = {value:1}; map.set("x", first); const value = map.get("missing"); if (value !== first) { return +value.value; } return 0; } probe();

//--- string-leaf.js
function probe() { const map = new Map(); const first = {value:"text"}; map.set("x", first); return map.size; } probe();

//--- cycle.js
function probe() { const map = new Map(); const first = {}; first.next = first; map.set("x", first); return map.size; } probe();

//--- dynamic-key.js
function make(key) { const first = {}; first[key] = 1; return first; } function probe() { const map = new Map(); map.set("x", make("value")); return map.size; } probe();

//--- inherited.js
function probe() { const map = new Map(); const first = {}; map.set("x", first); return first.constructor === first; } probe();

//--- prototype-write.js
function probe() { const map = new Map(); const first = {value:1}; const other = {}; other.__proto__ = first; map.set("x", first); return +first.value; } probe();

//--- accessor.js
function probe() { const map = new Map(); const first = { get value() { return 1; } }; map.set("x", first); return +first.value; } probe();
