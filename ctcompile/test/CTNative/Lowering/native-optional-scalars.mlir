// Null and undefined have distinct tags even when their lattice type agrees.
// The standalone differential fixture tests the semantics; this gate checks
// that optional signatures survive lowering and unsupported payloads refuse.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-optional-scalars-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-string.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=STRING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed-present.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-map-payload.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PAYLOAD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-map.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MAP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-array-payload.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ARRAY

// NATIVE-DAG: emitc.func @absent_{{[0-9]+}}() -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @maybeNumber_{{[0-9]+}}({{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @maybeBoolean_{{[0-9]+}}({{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @forwardNumber_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::nullable_scalar">{{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @forwardBoolean_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::nullable_scalar">{{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @numberFlags_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::nullable_scalar">{{.*}}) -> f64
// NATIVE-DAG: emitc.func @booleanFlags_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::nullable_scalar">{{.*}}) -> f64
// NATIVE-DAG: emitc.func @retainedData_{{[0-9]+}}() -> f64

// STRING: ctjs.func @_script_$0
// STRING-SAME: ctnative.not_native = "a value of type !ctnative.opt<!ctnative.str<utf8>>
// MIXED: ctjs.func private @choose$1
// MIXED-SAME: ctnative.not_native = "a value of type !ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>> from `scf.if`"
// PAYLOAD: ctjs.func private @probe$1
// PAYLOAD-SAME: ctnative.not_native = "native Map needs supported keys and definite numeric or acyclic Map values; inferred !ctnative.map<!ctnative.str<utf8>, !ctnative.opt<!ctnative.num<i32>>>"
// MAP: ctjs.func private @choose$1
// MAP-SAME: ctnative.not_native = "native Map instance escapes or is mutated through `scf.yield`"
// ARRAY: ctjs.func private @probe$1
// ARRAY-SAME: ctnative.not_native = "dense array storage requires definite numbers"

//--- optional-string.js
// Nullable numeric/boolean carriers do not represent an optional string.
function choose(flag) { return flag ? "value" : null; }
choose(false);

//--- optional-array-payload.js
// A tagged array miss does not make vector<double> suitable for stored null.
function probe(flag) {
    var values = [flag ? 42 : null, 1];
    return values.length;
}
probe(false);

//--- mixed-present.js
// A string/number union still needs a representation of its own.
function choose(flag) { return flag ? "value" : 42; }
choose(false);

//--- optional-map-payload.js
// Distinguishing a missing numeric entry does not admit nullable stored values.
function probe(flag) {
    var map = new Map();
    map.set("key", flag ? 42 : null);
    return map.size;
}
probe(false);

//--- optional-map.js
// A nullable Map is outside the scalar carrier and the Map presence proof.
function choose(flag) { if (flag) { return new Map(); } return null; }
choose(false);
