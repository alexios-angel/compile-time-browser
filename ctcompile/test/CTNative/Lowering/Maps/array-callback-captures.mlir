// A Map captured by an array-method callback (ClosureLifting/ArrayCallbacks.cpp)
// reaches the callback as one of the leading arguments of the specialised
// call, which the runtime binds to the callback's capture parameters. The
// Map proof follows it there instead of counting the call as an escape.
// Popper's flip modifier keeps such a Map (`A`, read by a find callback).
//
// With the per-family identity proof the family passes, so no Map reason is
// left: the functions next stop at the Map's key type (the inferred key is
// boxed) and the callbacks at their Map capture's carrier. proof.mlir keeps
// the identity walk's refusal.
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/captured.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true | FileCheck %s --check-prefix=CAPTURED --implicit-check-not="escapes or is mutated" --implicit-check-not=ctnative.map_reason
//
// reduce's initial value is an argument of the call, not a capture: a Map
// passed there still escapes.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/initial.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true | FileCheck %s --check-prefix=INITIAL

// CAPTURED-DAG: @pick$1({{.*}}ctnative.not_native = "native Map needs supported keys and numeric, boolean, closed mixed, owning-string, object-identity union or acyclic Map values; inferred !ctnative.map<!ctnative.boxed, !ctnative.num<i32>>"
// CAPTURED-DAG: @fn$2({{.*}}ctnative.not_native = "capture 0 is !ctnative.map<!ctnative.boxed, !ctnative.num<i32>>, which has no native carrier yet"
// CAPTURED-DAG: @firstLarge$3({{.*}}ctnative.not_native = "native Map needs supported keys
// CAPTURED-DAG: @fn$4({{.*}}ctnative.not_native = "capture 0 is !ctnative.map<
// INITIAL: ctnative.map_reason = "native Map instance escapes or is mutated through `ctjs.call`"

//--- captured.js
function pick() {
    var m = new Map();
    m.set(1, 5);
    m.set(2, 0);
    return [1, 2, 3].filter(function (k) {
        return m.has(k) && m.get(k) > 1;
    }).length;
}
function firstLarge() {
    var sizes = new Map();
    sizes.set(3, 30);
    sizes.set(4, 40);
    return [1, 3, 4].find(function (k) {
        return sizes.has(k) && sizes.get(k) > 35;
    });
}
var picked = pick();
var found = firstLarge();

//--- initial.js
function tally() {
    return [1, 2, 2].reduce(function (seen, k) {
        seen.set(k, 1);
        return seen;
    }, new Map()).size;
}
var tallied = tally();
