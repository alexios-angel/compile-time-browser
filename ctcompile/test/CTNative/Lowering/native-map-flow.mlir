// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-map-flow-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed-argument.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed-schema.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SCHEMA
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-result.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=OPTIONAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/phi.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PHI
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/exported-methods.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EXPORT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reassigned-capture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CELL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=STORED

// NATIVE: emitc.verbatim {{.*}}template <class K, class V> struct map_storage
// NATIVE: emitc.declare_func @makeStore_1
// NATIVE: emitc.func @makeStore_1({{.*}}) -> !emitc.opaque<"std::shared_ptr<ctnative::number_map<std::string>>">
// NATIVE: emitc.func @initialize_2({{.*}}!emitc.opaque<"std::shared_ptr<ctnative::number_map<std::string>>">
// NATIVE: emitc.func @identity_5({{.*}}) -> !emitc.opaque<"std::shared_ptr<ctnative::number_map<std::string>>">
// NATIVE: emitc.func @differentSlots_18({{.*}}!emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">{{.*}}!emitc.opaque<"std::shared_ptr<ctnative::number_map<std::string>>">
// NATIVE: emitc.func @unusedStore_24
// NATIVE: call_opaque "static_cast<void>"
// MIXED: ctjs.func private @probe$2
// MIXED-SAME: ctnative.not_native = "native Map flow contains a non-Map producer `ctjs.constant`"
// SCHEMA: ctjs.func private @probe$2
// SCHEMA-SAME: ctnative.not_native = "native Map needs primitive keys and definite numeric or acyclic Map values; inferred !ctnative.map<!ctnative.variant<
// OPTIONAL: ctjs.func private @maybe$1
// OPTIONAL-SAME: ctnative.not_native = "native Map instance escapes or is mutated through `scf.yield`"
// PHI: ctjs.func private @probe$1
// PHI-SAME: ctnative.not_native = "native Map instance escapes or is mutated through `scf.yield`"
// EXPORT: ctjs.func private @probe$1
// EXPORT-SAME: ctnative.not_native = "a closure used as a value: returned method table field has no visible invocation
// EXPORT: ctjs.construct {{.*}}ctnative.map_reason = "native Map instance escapes or is mutated through `ctjs.cell_set`"
// CELL: ctjs.func private @probe$1
// CELL-SAME: ctnative.not_native = "a shared binding of type !ctnative.boxed, which has no native carrier
// CELL: ctjs.construct {{.*}}ctnative.map_reason = "native Map instance escapes or is mutated through `ctjs.cell_set`"
// STORED: ctjs.func private @make$1
// STORED-SAME: ctnative.not_native = "native Map instance escapes or is mutated through `ctjs.set_property`"

//--- mixed-argument.js
// A Map at one call site does not prove that all incoming values are Maps.
function size(store) { return store.size; }
function probe() { size(new Map()); return size(0); }
probe();

//--- mixed-schema.js
// Different instances passed to one formal require one compatible schema.
function insert(store, key) { store.set(key, 1); return store.size; }
function probe() {
    var numbers = new Map();
    var strings = new Map();
    insert(numbers, 1);
    insert(strings, "1");
    return numbers.size;
}
probe();

//--- optional-result.js
function maybe(flag) { if (flag > 0) { return new Map(); } }
function probe() { var store = maybe(1); return store.size; }
probe();

//--- phi.js
// This slice does not prove Map identity through structured merge values.
function probe(flag) {
    var store = new Map();
    if (flag > 0) { store = new Map(); }
    return store.size;
}
probe(1);

//--- exported-methods.js
// The returned field has no invocation to prove its argument signature.
function probe() {
    var store = new Map();
    return { get(key) { return store.get(key) + 0; } };
}
probe();

//--- reassigned-capture.js
// Mutating entries is supported; reassigning a shared Map binding is not.
function probe() {
    var store = new Map();
    function reset() { store = new Map(); return store.size; }
    reset();
    return store.size;
}
probe();

//--- stored.js
function make() { return new Map(); }
function probe() { var object = { store: make() }; return object.store.size; }
probe();
