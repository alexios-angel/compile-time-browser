// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-map-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prototype.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PROTOTYPE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/assigned.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ASSIGNED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/host.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=HOST
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/detached.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DETACHED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/replaced.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REPLACED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arity.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ARITY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/seeded.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SEEDED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/returned.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=RETURNED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/passed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PASSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nested.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NESTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed-keys.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/object-key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=OBJECT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string-value.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=STRING --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-value.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=OPTIONAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/get-equality.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EQUALITY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string-keys.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=KEYS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/snapshot-write.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SNAPSHOT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/snapshot-return.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SNAPSHOT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/entries.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ENTRIES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/computed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=COMPUTED

// NATIVE: emitc.include <"memory">
// NATIVE: emitc.func @main() -> i32
// NATIVE-DAG: call_opaque "ctnative::make_number_map<double>"
// NATIVE-DAG: call_opaque "ctnative::make_string_to_number_map"
// NATIVE-DAG: call_opaque "ctnative::make_number_map<bool>"
// NATIVE-DAG: call_opaque "ctnative::map_set"
// NATIVE-DAG: call_opaque "ctnative::map_get"
// NATIVE-DAG: call_opaque "ctnative::map_has"
// NATIVE-DAG: call_opaque "ctnative::map_delete"
// NATIVE-DAG: call_opaque "ctnative::map_clear"
// NATIVE-DAG: call_opaque "ctnative::map_size"
// NATIVE-DAG: call_opaque "ctnative::map_keys"
// NATIVE-DAG: call_opaque "ctnative::map_values"
// PROTOTYPE: ctjs.func private @probe$1
// PROTOTYPE-SAME: ctnative.not_native = "standard Map constructor identity escapes or is inspected"
// ASSIGNED: ctjs.func private @probe$1
// ASSIGNED-SAME: ctnative.not_native = "standard Map binding is assigned in this program"
// HOST: ctjs.func private @probe$1
// HOST-SAME: ctnative.not_native = "standard Map identity is unproved with other host/global value reads"
// DETACHED: ctjs.func private @probe$1
// DETACHED-SAME: ctnative.not_native = "native Map method is detached, escapes, or has a different receiver"
// REPLACED: ctjs.func private @probe$1
// REPLACED-SAME: ctnative.not_native = "native Map instance escapes or is mutated through `ctjs.set_property`"
// ARITY: ctjs.func private @probe$1
// ARITY-SAME: ctnative.not_native = "native Map method requires its exact argument count"
// SEEDED: ctjs.func private @probe$1
// SEEDED-SAME: ctnative.not_native = "native Map requires an empty constructor"
// RETURNED: ctjs.func private @probe$1
// RETURNED-SAME: ctnative.not_native = "native Map instance escapes or is mutated through `ctjs.store_global`"
// PASSED: emitc.func @probe_1
// PASSED: emitc.func @consume_2
// PASSED: call_opaque "static_cast<void>"
// NESTED: emitc.func @probe_1
// NESTED: call_opaque "ctnative::make_map<std::string, std::shared_ptr<ctnative::number_map<double>>>"
// MIXED: ctjs.func private @probe$1
// MIXED-SAME: ctnative.not_native = "native Map needs supported keys and homogeneous numeric, boolean, owning-string, object-identity union or acyclic Map values; inferred !ctnative.map<!ctnative.variant<
// OBJECT: emitc.func @probe_1
// STRING-LABEL: emitc.func @probe_1() -> f64
// STRING: [[STRING_MAP:%[^ ]+]] = call_opaque "ctnative::make_map<std::string, std::string>"
// STRING-SAME: !emitc.opaque<"std::shared_ptr<ctnative::map_storage<std::string, std::string>>">
// STRING: call_opaque "ctnative::map_set"([[STRING_MAP]],
// STRING: [[STRING_SIZE:%[^ ]+]] = call_opaque "ctnative::map_size"([[STRING_MAP]])
// STRING: return [[STRING_SIZE]] : f64
// OPTIONAL: ctjs.func private @probe$1
// OPTIONAL-SAME: ctnative.not_native = "native Map needs supported keys and homogeneous numeric, boolean, owning-string, object-identity union or acyclic Map values; inferred !ctnative.map<!ctnative.num<i32>, !ctnative.opt<!ctnative.num<i32>>>"
// EQUALITY: emitc.func @probe_1() -> i1
// EQUALITY: call_opaque "ctnative::scalar_strict_equal"
// EQUALITY-NOT: ctnative.not_native
// KEYS: emitc.func @probe_1
// KEYS: call_opaque "ctnative::map_keys"
// KEYS: !emitc.opaque<"std::vector<std::string>">
// KEYS-NOT: ctnative.not_native
// SNAPSHOT: ctjs.func private @probe$1
// SNAPSHOT-SAME: ctnative.not_native = "native Map snapshot requires confined numeric or string elements"
// ENTRIES: ctjs.func private @probe$1
// ENTRIES-SAME: ctnative.not_native = "native Map property is not a supported constant method or size"
// COMPUTED: ctjs.func private @probe$1
// COMPUTED-SAME: ctnative.not_native = "native Map property is not a supported constant method or size"

//--- prototype.js
function probe() { var map = new Map(); return map.size; }
Map.prototype.has = 0;
probe();

//--- assigned.js
function probe() { var map = new Map(); return map.size; }
Map = 0;
probe();

//--- host.js
function probe() { var map = new Map(); return map.size; }
globalThis.Map = 0;
probe();

//--- detached.js
function probe() { var map = new Map(); var has = map.has; return has(1); }
probe();

//--- replaced.js
function probe() { var map = new Map(); map.has = 0; return map.size; }
probe();

//--- arity.js
function probe() { var map = new Map(); map.set(1); return map.size; }
probe();

//--- seeded.js
function probe() { var map = new Map([]); return map.size; }
probe();

//--- returned.js
// A closed return is supported; publication through a global still is not.
function probe() { return new Map(); }
var exported = probe();

//--- passed.js
function probe() { var map = new Map(); consume(map); return map.size; }
function consume(value) { return 1; }
probe();

//--- nested.js
function probe() { var map = new Map(); map.set("nested", new Map()); return map.size; }
probe();

//--- mixed-keys.js
// A read with a different primitive carrier also widens the key schema.
function probe() { var map = new Map(); map.set(1, 2); return map.has("1"); }
probe();

//--- object-key.js
function probe() { var map = new Map(); map.set({}, 2); return map.size; }
probe();

//--- string-value.js
function probe() { var map = new Map(); map.set("key", "value"); return map.size; }
probe();

//--- optional-value.js
function probe(mode) {
    var map = new Map();
    var value;
    if (mode > 0) { value = 2; }
    map.set(1, value);
    return map.size;
}
probe(0);

//--- get-equality.js
function probe() { var map = new Map(); map.set(1, 2); return map.get(1) === 2; }
probe();

//--- string-keys.js
function probe() { var map = new Map(); map.set("key", 2); var keys = Array.from(map.keys()); return keys.length; }
probe();

//--- snapshot-write.js
function probe() { var map = new Map(); map.set(1, 2); var values = Array.from(map.values()); values[0] = 3; return values[0]; }
probe();

//--- snapshot-return.js
function probe() { var map = new Map(); return Array.from(map.values()); }
probe();

//--- entries.js
function probe() { var map = new Map(); map.entries(); return map.size; }
probe();

//--- computed.js
function probe(method) { var map = new Map(); return map[method](1); }
probe("has");
