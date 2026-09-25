// Null and undefined have distinct tags even when their lattice type agrees.
// The standalone differential fixture tests the semantics; this gate checks
// that optional signatures survive lowering and unsupported payloads refuse.
// Keep both source alternatives: default precomputation can erase the union.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nullable-map-roundtrip.js | ctjs-opt --pass-pipeline="builtin.module(ctjs-resolve-globals,ctjs-lift-to-scf,ctnative-lower-to-emitc{optimize=false},emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))" -o %t/roundtrip.mlir
// RUN: %compilation_unit --module %t/roundtrip.mlir --js %t/nullable-map-roundtrip.js --work %t/roundtrip --name nullable_map_roundtrip
// RUN: %node -e "const vm = require('node:vm'), fs = require('node:fs'); const cx = vm.createContext({}); vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), cx); if (cx.nullValue !== null || cx.undefinedValue !== undefined || cx.presentValue !== 42 || cx.missingValue !== undefined) process.exit(1);" %t/nullable-map-roundtrip.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../Fixtures/Scalars/optional-scalars.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-string.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=STRING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed-present.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=MIXED --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-map-payload.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PAYLOAD --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-map.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=MAP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-array-payload.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=ARRAY

// NATIVE-DAG: emitc.func @absent_{{[0-9]+}}() -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @maybeNumber_{{[0-9]+}}({{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @maybeBoolean_{{[0-9]+}}({{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @forwardNumber_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::nullable_scalar">{{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @forwardBoolean_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::nullable_scalar">{{.*}}) -> !emitc.opaque<"ctnative::nullable_scalar">
// NATIVE-DAG: emitc.func @numberFlags_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::nullable_scalar">{{.*}}) -> !emitc.opaque<"ctnative::js_num">
// NATIVE-DAG: emitc.func @booleanFlags_{{[0-9]+}}({{.*}}!emitc.opaque<"ctnative::nullable_scalar">{{.*}}) -> !emitc.opaque<"ctnative::js_num">
// NATIVE-DAG: emitc.func @retainedData_{{[0-9]+}}() -> !emitc.opaque<"ctnative::js_num">
// NATIVE-DAG: #emitc.opaque<"ctnative::nullable_scalar{ctnative::js_null_t{}}">
// NATIVE-DAG: #emitc.opaque<"ctnative::nullable_scalar{ctnative::undefined_t{}}">
// Numeric coercion and arithmetic share the Number value carrier.
// NATIVE-DAG: [[NUMBER:%[^ ]+]] = call_opaque "ctnative::to_number"{{.*}} -> !emitc.opaque<"ctnative::js_num">

// STRING: emitc.func @choose_1({{.*}}) -> !emitc.opaque<"ctnative::nullable_string">
// STRING: call_opaque "ctnative::to_nullable_string"
// STRING-NOT: ctnative.not_native
// MIXED: emitc.func @choose_1({{.*}}) -> !emitc.opaque<"ctnative::number_string">
// PAYLOAD: emitc.func @probe_1({{.*}}) -> !emitc.opaque<"ctnative::js_num">
// PAYLOAD: [[PAYLOAD_MAP:%[^ ]+]] = call_opaque "ctnative::make_map<std::string, ctnative::nullable_scalar>"
// PAYLOAD: call_opaque "ctnative::map_set"([[PAYLOAD_MAP]],
// PAYLOAD-SAME: !emitc.opaque<"ctnative::nullable_scalar">
// PAYLOAD: call_opaque "ctnative::map_size"([[PAYLOAD_MAP]])
// MAP: ctjs.func private @choose$1
// MAP-SAME: ctnative.not_native = "native Map instance escapes or is mutated through `scf.yield`"
// ARRAY: ctjs.func private @probe$1
// ARRAY-SAME: ctnative.not_native = "dense array storage requires definite numbers"

//--- optional-string.js
// Optional strings have an owning carrier with separate null/undefined tags.
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
// Preserve the former refusal with its owning String/Number representation.
function choose(flag) { return flag ? "value" : 42; }
choose(false);

//--- optional-map-payload.js
// Tagged Map storage preserves nullable numeric payloads independently of misses.
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

//--- nullable-map-roundtrip.js
// The stored absence tags survive get; an unallocated key is independently missing.
function roundtrip(mode) {
    const map = new Map();
    let value;
    if (mode === 0) { value = null; }
    if (mode === 1) { value = 42; }
    map.set("key", value);
    return map.get(mode === 2 ? "missing" : "key");
}
var nullValue = roundtrip(0);
var undefinedValue = roundtrip(-1);
var presentValue = roundtrip(1);
var missingValue = roundtrip(2);
