// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-object-key-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/field.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=FIELDS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inspect.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=INSPECT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=OPTIONAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/publish.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/capture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/snapshot.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SNAPSHOT

// NATIVE-DAG: struct identity_object {}
// NATIVE-DAG: call_opaque "std::make_shared<ctnative::identity_object>"
// NATIVE-DAG: call_opaque "ctnative::make_number_map<std::shared_ptr<ctnative::identity_object>>"
// NATIVE-DAG: emitc.func @makeKey_1({{.*}}) -> !emitc.opaque<"std::shared_ptr<ctnative::identity_object>">
// FIELDS: ctnative.not_native =
// FIELDS: identity-only Map key has an unsupported use through `ctjs.set_property`
// INSPECT: ctnative.not_native =
// INSPECT: identity-only Map key has an unsupported use through `ctjs.compare`
// MIXED: ctnative.not_native =
// MIXED: identity-only Map key flow contains a non-object producer
// OPTIONAL: ctnative.not_native = "native Map needs supported keys and definite numeric or acyclic Map values; inferred !ctnative.map<!ctnative.boxed,
// REFUSED: ctnative.not_native =
// SNAPSHOT: ctnative.not_native = "native Map snapshot requires confined numeric elements"

//--- field.js
function probe() { const key = {x: 1}; const map = new Map(); map.set(key, 42); return map.size; }
probe();

//--- inspect.js
function probe() { const key = {}; const map = new Map(); map.set(key, 42); return key === key; }
probe();

//--- mixed.js
function read(map, key) { return map.get(key) + 0; }
function probe() { const key = {}; const map = new Map(); map.set(key, 42); read(map, key); return read(map, 1); }
probe();

//--- optional.js
function key(flag) { if (flag) { return {}; } }
function probe() { const map = new Map(); map.set(key(false), 42); return map.size; }
probe();

//--- publish.js
var published;
function probe() { const key = {}; const map = new Map(); map.set(key, 42); published = key; return map.size; }
probe();

//--- capture.js
function probe() { const key = {}; const map = new Map(); map.set(key, 42); return function() { return map.get(key) + 0; }; }
const read = probe(); read();

//--- snapshot.js
function probe() { const key = {}; const map = new Map(); map.set(key, 42); return map.keys().length; }
probe();
