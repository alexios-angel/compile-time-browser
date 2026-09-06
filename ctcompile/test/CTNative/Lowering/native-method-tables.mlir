// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-method-table-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/parameter_write.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PARAMETERWRITE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic_key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DYNAMICKEY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/detached.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DETACHED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/identity.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=IDENTITY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/conditional_initialization.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CONDITIONALINITIALIZATION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early_helper.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EARLYHELPER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutable_capture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTABLECAPTURE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/duplicate_fields.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DUPLICATEFIELDS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/closure_also_direct.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CLOSUREALSODIRECT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/property_names.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PROPERTYNAMES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed_producer.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXEDPRODUCER

// NATIVE-DAG: using ctn_env_fn_2 = std::function<js_num(js_num)>
// NATIVE-DAG: struct method_table_0
// NATIVE-DAG: cap0 = std::move(cap0)
// NATIVE-DAG: call_opaque "ctnative::method_set<&ctnative::method_table_0::m_get>"
// NATIVE-DAG: call_opaque "ctnative::method_get<&ctnative::method_table_0::m_get>"
// NATIVE-DAG: call_opaque "ctnative::invoke_callable"
// PARAMETERWRITE: returned method table is written through an alias or stored into another object
// DYNAMICKEY: returned method table read needs a supported constant key
// DETACHED: returned method table field is detached, inspected or called with another receiver
// IDENTITY: returned method table escapes or is inspected through `ctjs.compare`
// CONDITIONALINITIALIZATION: returned method table field initialization does not dominate every read or publication
// EARLYHELPER: returned method table field initialization does not dominate every read or publication
// MUTABLECAPTURE: returned closure capture 0 is a mutable or late-initialized binding
// DUPLICATEFIELDS: returned method table field requires one closure created and stored only here
// CLOSUREALSODIRECT: returned method table field requires one closure created and stored only here
// PROPERTYNAMES: returned method table field needs a supported constant key
// MIXEDPRODUCER: it is a method field of an object whose shape is not closed

//--- parameter_write.js
function make(seed) { return { get: function() { return seed; } }; }
function replace(table) { table.get = function() { return 99; }; return table; }
function run() { return replace(make(42)).get(); }
var trace = run();

//--- dynamic_key.js
function make(seed) { return { get: function() { return seed; } }; }
function read(table, key) { return table[key](); }
function run() { return read(make(42), "get"); }
var trace = run();

//--- detached.js
function make(seed) { return { get: function() { return seed; } }; }
function run() { const table = make(42); const getter = table.get; return getter(); }
var trace = run();

//--- identity.js
function make(seed) { return { get: function() { return seed; } }; }
function run() { const a = make(42); const b = make(42);
  return a === b ? 0 : (a === a && a.get() === b.get() ? 1 : 0); }
var trace = run();

//--- conditional_initialization.js
function make(flag, seed) {
  const table = {};
  if (flag) { table.get = function() { return seed; }; }
  return table;
}
function run() { return make(true, 42).get(); }
var trace = run();

//--- early_helper.js
function inspect(table, now) { if (now) return table.get(); return 0; }
function make(seed) {
  const table = {}; inspect(table, false);
  table.get = function() { return seed; }; return table;
}
function run() { return make(42).get(); }
var trace = run();

//--- mutable_capture.js
function make(seed) {
  return { get: function() { return seed; },
           increment: function() { seed = seed + 1; return seed; } };
}
function run() { const table = make(42); table.increment(); return table.get(); }
var trace = run();

//--- duplicate_fields.js
function make(seed) {
  const fn = function(value) { return seed + value; };
  return { left: fn, right: fn };
}
function run() { const table = make(10); return table.left(10) + table.right(12); }
var trace = run();

//--- closure_also_direct.js
function make(seed) {
  const fn = function(value) { return seed + value; }; const before = fn(1);
  return { get: fn, first: function() { return before; } };
}
function run() { const table = make(10); return table.get(20) + table.first() + 1; }
var trace = run();

//--- property_names.js
function make(seed) {
  return { "a-b": function() { return seed; },
           a_b: function() { return seed + 10; },
           "": function() { return seed + 2; } };
}
function run() { const table = make(10); return table["a-b"]() + table.a_b() + table[""](); }
var trace = run();

//--- mixed_producer.js
function make(flag, seed) {
  if (flag) return { get: function() { return seed; } }; return 7;
}
function run() { return make(true, 42).get(); }
var trace = run();
