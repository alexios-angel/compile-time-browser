// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-string-snapshots-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/assigned.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ASSIGNED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/replaced.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REPLACED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/detached.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DETACHED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/callback.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CALLBACK
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/other-array.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=OTHER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prototype.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REPLACED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/host.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=HOST
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutate-original.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ITERATOR
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutate-copy.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SNAPSHOT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escape.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SNAPSHOT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/numeric-coercion.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NUMERIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed-equality.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-add.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ADD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boolean-keys.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SNAPSHOT

// NATIVE: emitc.func @diagnostic_1
// NATIVE: call_opaque "ctnative::map_keys"
// NATIVE: !emitc.opaque<"std::vector<std::string>">
// NATIVE: call_opaque "ctnative::vec_at"
// NATIVE: !emitc.opaque<"ctnative::nullable_string">
// NATIVE: call_opaque "ctnative::string_text"
// NATIVE: emitc.func @stringFlags_
// NATIVE-DAG: call_opaque "ctnative::string_strict_equal"
// NATIVE-DAG: call_opaque "ctnative::string_equal"
// NATIVE-DAG: call_opaque "ctnative::string_truthy"
// NATIVE-DAG: call_opaque "ctnative::string_typeof"
// ASSIGNED: ctjs.func private @probe$1
// ASSIGNED-SAME: ctnative.not_native = "standard Array binding is assigned in this program"
// REPLACED: ctjs.func private @probe$1
// REPLACED-SAME: ctnative.not_native = "standard Array.from identity escapes, is inspected or is mutated"
// DETACHED: ctjs.func private @probe$1
// DETACHED-SAME: ctnative.not_native = "standard Array.from requires its exact Array receiver"
// CALLBACK: ctjs.func private @probe$1
// CALLBACK-SAME: ctnative.not_native = "native Array.from snapshot copy requires exactly one argument"
// OTHER: ctjs.func private @probe$1
// OTHER-SAME: ctnative.not_native = "native Array.from requires a proved Map keys or values snapshot"
// HOST: ctjs.func private @probe$1
// HOST-SAME: ctnative.not_native = "standard Map identity is unproved with other host/global value reads"
// ITERATOR: ctjs.func private @probe$1
// ITERATOR-SAME: ctnative.not_native = "native Map iterator requires one immediate Array.from consumption"
// SNAPSHOT: ctjs.func private @probe$1
// SNAPSHOT-SAME: ctnative.not_native = "native Map snapshot requires confined numeric or string elements"
// NUMERIC: ctjs.func private @probe$1
// NUMERIC-SAME: ctnative.not_native = "unary operand is !ctnative.opt<!ctnative.str<utf8>>, not a number"
// MIXED: ctjs.func private @probe$1
// MIXED-SAME: ctnative.not_native = "equality operand is !ctnative.opt<!ctnative.str<utf8>>, not a number"
// ADD: ctjs.func private @probe$1
// ADD-SAME: ctnative.not_native = "binary operand is !ctnative.opt<!ctnative.str<utf8>>, not a number"

//--- assigned.js
function probe() { const map = new Map(); map.set("bs.modal", 1); return Array.from(map.keys())[0]; }
Array = 0;
probe();

//--- replaced.js
function probe() { const map = new Map(); map.set("bs.modal", 1); return Array.from(map.keys())[0]; }
Array.from = 0;
probe();

//--- detached.js
function probe() { const map = new Map(); map.set("bs.modal", 1); const copy = Array.from; return copy(map.keys())[0]; }
probe();

//--- callback.js
function probe() { const map = new Map(); map.set("bs.modal", 1); return Array.from(map.keys(), 0)[0]; }
probe();

//--- other-array.js
function probe() { const map = new Map(); map.set("bs.modal", 1); return Array.from(["bs.modal"])[0]; }
probe();

//--- prototype.js
function probe() { const map = new Map(); map.set("bs.modal", 1); return Array.from(map.keys())[0]; }
Array.prototype[0] = "polluted";
probe();

//--- host.js
function probe() { const map = new Map(); map.set("bs.modal", 1); return Array.from(map.keys())[0]; }
globalThis.Array = 0;
probe();

//--- mutate-original.js
function probe() { const map = new Map(); map.set("bs.modal", 1); const keys = map.keys(); keys[0] = "bs.alert"; return Array.from(keys)[0]; }
probe();

//--- mutate-copy.js
function probe() { const map = new Map(); map.set("bs.modal", 1); const keys = Array.from(map.keys()); keys[0] = "bs.alert"; return keys[0]; }
probe();

//--- escape.js
function probe() { const map = new Map(); map.set("bs.modal", 1); return Array.from(map.keys()); }
probe();

//--- numeric-coercion.js
function probe() { const map = new Map(); map.set("1", 1); return +Array.from(map.keys())[0]; }
probe();

//--- mixed-equality.js
function probe() { const map = new Map(); map.set("1", 1); return Array.from(map.keys())[0] == 1; }
probe();

//--- optional-add.js
function probe() { const map = new Map(); map.set("1", 1); const keys = Array.from(map.keys()); return keys[0] + keys[1]; }
probe();

//--- boolean-keys.js
function probe() { const map = new Map(); map.set(true, 1); return Array.from(map.keys())[0]; }
probe();
