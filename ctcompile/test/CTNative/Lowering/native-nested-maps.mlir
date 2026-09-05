// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-nested-map-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/self.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CYCLE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/cycle.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CYCLE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/absent.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/branch.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/deleted.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/cleared-by-call.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/distinct-instance.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/cross-function.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/snapshot.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SNAPSHOT

// NATIVE: emitc.func @main() -> i32
// NATIVE-DAG: call_opaque "ctnative::make_map<std::string, std::shared_ptr<ctnative::number_map<std::string>>>"
// NATIVE-DAG: call_opaque "ctnative::map_get_present"
// CYCLE: ctnative.not_native = "native Map payload schemas contain an ownership cycle"
// MIXED: ctnative.not_native = "native Map flow contains a non-Map producer `ctjs.constant`"
// PRESENCE: ctnative.not_native = "nested native Map get requires presence on every reaching path for the same instance and key; has observations must survive intervening effects"
// SNAPSHOT: ctnative.not_native = "native Map snapshot requires confined numeric elements"

//--- self.js
function probe() { var map = new Map(); map.set(1, map); return map.size; }
probe();

//--- cycle.js
function probe() { var a = new Map(); var b = new Map(); a.set(1, b); b.set(1, a); return a.size; }
probe();

//--- mixed.js
function probe() { var map = new Map(); map.set(1, new Map()); map.set(2, 42); return map.size; }
probe();

//--- absent.js
function probe() { var map = new Map(); map.set(1, new Map()); return map.get(2).size; }
probe();

//--- branch.js
function probe(flag) {
    var map = new Map();
    if (flag) { map.set(1, new Map()); }
    return map.get(1).size;
}
probe(false);

//--- deleted.js
function probe() { var map = new Map(); map.set(1, new Map()); map.delete(1); return map.get(1).size; }
probe();

//--- cleared-by-call.js
function clear(map) { map.clear(); }
function probe() { var map = new Map(); map.set(1, new Map()); clear(map); return map.get(1).size; }
probe();

//--- distinct-instance.js
// These Maps share a C++ schema through merge(), but only a contains key 1.
function merge(map) { return map.size; }
function probe() {
    var a = new Map(); var b = new Map();
    a.set(1, new Map());
    merge(a); merge(b);
    return b.get(1).size;
}
probe();

//--- cross-function.js
function put(map) { map.set(1, new Map()); }
function read(map) { return map.get(1).size; }
function probe() { var map = new Map(); put(map); return read(map); }
probe();

//--- snapshot.js
function probe() { var map = new Map(); map.set(1, new Map()); return map.values().length; }
probe();
