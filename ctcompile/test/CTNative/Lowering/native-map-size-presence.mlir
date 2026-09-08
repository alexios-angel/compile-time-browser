// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nonempty.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/saved.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/has.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/before-seed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/after-delete.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/positive-key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/distinct-instance.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stale-has.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/branch.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/recursive-clear.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED

// These nested reads require definite presence; nullable numeric get results
// cannot hide a missing proof. Size reads are checked by the native Map query,
// independently of the published-method host proof and its input annotations.
// PRESENT: emitc.func @main() -> i32
// PRESENT: call_opaque "ctnative::map_get_present"
// REFUSED: ctnative.not_native = "nested native Map get requires presence on every reaching path for the same instance and key; has observations must survive intervening effects"

//--- nonempty.js
function probe() {
    var map = new Map();
    map.set(-0, new Map());
    map.delete(map.size);
    return map.get(0).size;
}
probe();

//--- saved.js
function probe() {
    var map = new Map();
    map.set(1, new Map());
    const key = map.size;
    map.delete(1);
    map.set(0, new Map());
    map.delete(key);
    return map.get(0).size;
}
probe();

//--- has.js
function read(map) {
    if (map.has(0)) {
        const key = map.size;
        map.delete(key);
        return map.get(0).size;
    }
    return 0;
}
function probe() { var map = new Map(); map.set(0, new Map()); return read(map); }
probe();

//--- before-seed.js
function probe() {
    var map = new Map();
    const key = map.size;
    map.set(0, new Map());
    map.delete(key);
    return map.get(0).size;
}
probe();

//--- after-delete.js
function probe() {
    var map = new Map();
    map.set(0, new Map());
    map.delete(0);
    const key = map.size;
    map.set(0, new Map());
    map.delete(key);
    return map.get(0).size;
}
probe();

//--- positive-key.js
function probe() {
    var map = new Map();
    map.set(1, new Map());
    map.delete(map.size);
    return map.get(1).size;
}
probe();

//--- distinct-instance.js
// Both Maps share a schema through measure, but only a is still nonempty.
function measure(map) { return map.size; }
function probe() {
    var a = new Map(); var b = new Map();
    a.set(0, new Map()); b.set(1, new Map()); b.delete(1);
    measure(a); measure(b);
    const key = b.size;
    a.delete(key);
    return a.get(0).size;
}
probe();

//--- stale-has.js
function probe() {
    var map = new Map();
    map.set(0, new Map());
    const had = map.has(0);
    map.delete(0);
    if (had) {
        const key = map.size;
        map.set(0, new Map());
        map.delete(key);
        return map.get(0).size;
    }
    return 0;
}
probe();

//--- branch.js
function probe(flag) {
    var map = new Map();
    if (flag) { map.set(0, new Map()); }
    const key = map.size;
    map.set(0, new Map());
    map.delete(key);
    return map.get(0).size;
}
probe(false);

//--- recursive-clear.js
function clear(map, again) { if (again) { clear(map, false); } else { map.clear(); } }
function probe() {
    var map = new Map();
    map.set(0, new Map());
    clear(map, true);
    const key = map.size;
    map.set(0, new Map());
    map.delete(key);
    return map.get(0).size;
}
probe();
