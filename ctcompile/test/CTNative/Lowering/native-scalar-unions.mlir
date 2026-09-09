// Scalar-only unions have an exact tagged carrier. Wider unions and numeric
// publication boundaries remain refusals; the differential fixture checks the
// runtime distinction among booleans, numbers, null and undefined.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-scalar-unions-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/global.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=GLOBAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=STRING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/map-key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MAP-KEY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/map-value.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MAP-VALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/array.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ARRAY

// NATIVE-DAG: emitc.func @choose_{{[0-9]+}}({{.*}}i1{{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @chooseReturn_{{[0-9]+}}({{.*}}i1{{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @optional_{{[0-9]+}}({{.*}}f64{{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @forward_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::nullable_scalar">{{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @arithmetic_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::nullable_scalar">{{.*}}) -> f64
// NATIVE-DAG: emitc.func @retainedData_{{[0-9]+}}() -> f64
// NATIVE-DAG: emitc.field @value : !emitc.opaque<"ctnative::nullable_scalar">

// GLOBAL: ctjs.func @_script_$0
// GLOBAL-SAME: ctnative.not_native = "store to global `result` is !ctnative.variant<!ctnative.bool, !ctnative.num<i32>>; native global observations require a definite Number, Boolean or String"
// STRING: ctnative.not_native = "a Bool/String temporary needs a single proved return type"
// MAP-KEY: ctnative.not_native = "mixed native Map key needs one proved scalar alternative"
// MAP-VALUE: ctnative.not_native = "mixed native Map write needs one proved scalar alternative"
// ARRAY: ctnative.not_native = "an array whose elements are !ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>, not numbers"

//--- global.js
function choose(flag) { return flag ? false : 42; }
var result = choose(true);

//--- string.js
function choose(flag) { return flag ? false : "text"; }
choose(true);

//--- map-key.js
function probe(flag) {
    const map = new Map();
    map.set(flag ? false : 0, 42);
    return map.size;
}
probe(true);

//--- map-value.js
function probe(flag) {
    const map = new Map();
    map.set("value", flag ? false : 42);
    return map.size;
}
probe(true);

//--- array.js
function probe(flag) {
    const values = [flag ? false : 42];
    return values.length;
}
probe(true);
