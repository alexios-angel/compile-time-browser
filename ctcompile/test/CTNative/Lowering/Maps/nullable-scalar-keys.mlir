// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/values.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=VALUES --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/values.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=true | FileCheck %s --check-prefix=VALUES --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/keys.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=KEYS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/keys.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=true | FileCheck %s --check-prefix=KEYS

// VALUES-LABEL: emitc.func @probe_1
// VALUES: call_opaque "ctnative::make_number_map<ctnative::nullable_scalar>"
// VALUES: call_opaque "ctnative::map_values"
// VALUES-SAME: !emitc.opaque<"std::vector<double>">
// VALUES: call_opaque "ctnative::map_delete"
// KEYS: ctjs.func private @probe$1
// KEYS-SAME: ctnative.not_native = "nullable scalar Map key snapshot needs a tagged element carrier"

//--- values.js
// Numeric values remain ordinary snapshots even when the keys need scalar tags.
function probe() {
    const map = new Map();
    map.set(void 0, 3);
    map.set(null, 5);
    map.set(0, 7);
    const values = Array.from(map.values());
    map.set(void 0, 11);
    map.delete(null);
    return values[0] * 100 + values[1] * 10 + values[2] + values.length;
}
var a = probe();

//--- keys.js
// A homogeneous numeric snapshot cannot preserve Undefined/Null/Number tags.
function probe() {
    const map = new Map();
    map.set(void 0, 3);
    map.set(null, 5);
    map.set(0, 7);
    return Array.from(map.keys()).length;
}
var a = probe();
