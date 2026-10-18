// A Map held in a run-once static (the variable a binding of a function that
// runs once becomes, ClosureLifting/RunOnce.cpp), written once and read only
// after that write, is a native Map: Bootstrap's Data keeps its element map
// this way, read by its set and get methods through the static.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/held.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=HELD --implicit-check-not=ctnative.not_native --implicit-check-not=ctnative.map_reason
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/held.js 2>/dev/null | ctjs-opt --pass-pipeline="builtin.module(ctjs-resolve-globals,ctjs-lift-to-scf,ctnative-lower-to-emitc,emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))" -o %t/held.mlir
// RUN: %compilation_unit --module %t/held.mlir --js %t/held.js --work %t/held --name run_once_map
// RUN: not %compilation_unit --module %t/held.mlir --js %t/held.js --work %t/held --name run_once_map_off --mutate total 2>&1 | FileCheck %s --check-prefix=HELD-OFF
//
// A second write (another Map, or a number after the Map), or a read that
// can run before the write, keeps the Map boxed: the static may hold the
// other value or the undefined it was hoisted with.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/twice.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/rebound.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED

// HELD: emitc.global static @static_binding : !emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">
// HELD-OFF: global 'total' differs
// REFUSED: ctnative.map_reason = "native Map instance escapes or is mutated through `ctjs.cell_set`"

//--- held.js
var total = (function () {
    const m = new Map();
    const Data = {
        set(key, value) {
            m.set(key, value);
        },
        get(key) {
            return m.has(key) ? m.get(key) : 0;
        }
    };
    class Item {
        constructor(key, value) {
            Data.set(key, value);
        }
        static read(key) {
            return Data.get(key);
        }
    }
    new Item(1, 10);
    new Item(2, 32);
    return Item.read(1) + Item.read(2) * 100 + Item.read(3) * 10000;
})();
var base = 1;

//--- twice.js
var total = (function () {
    let m = new Map();
    const Data = {
        set(key, value) {
            m.set(key, value);
        },
        get(key) {
            return m.has(key) ? m.get(key) : 0;
        }
    };
    class Item {
        constructor(key, value) {
            Data.set(key, value);
        }
        static read(key) {
            return Data.get(key);
        }
    }
    new Item(1, 10);
    m = new Map();
    new Item(2, 32);
    return Item.read(1) + Item.read(2);
})();

//--- early.js
var total = (function () {
    function peek() {
        return m === undefined ? -1 : m.size;
    }
    const Data = {
        set(key, value) {
            m.set(key, value);
        },
        get(key) {
            return m.has(key) ? m.get(key) : 0;
        }
    };
    class Item {
        constructor(key, value) {
            Data.set(key, value);
        }
        static read(key) {
            return Data.get(key);
        }
    }
    var before = peek();
    var m = new Map();
    new Item(1, 10);
    new Item(2, 32);
    return Item.read(1) + Item.read(2) + before;
})();

//--- rebound.js
var total = (function () {
    let m = new Map();
    const Data = {
        set(key, value) {
            m.set(key, value);
        },
        get(key) {
            return m.has(key) ? m.get(key) : 0;
        }
    };
    class Item {
        constructor(key, value) {
            Data.set(key, value);
        }
        static read(key) {
            return Data.get(key);
        }
    }
    new Item(1, 10);
    const first = Item.read(1);
    m = 7;
    return first;
})();
