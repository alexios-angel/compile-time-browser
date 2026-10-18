// The instance registry proof (lib/CTNative/Analysis/Inference/Instances.cpp):
// a class whose constructor stores `this` into a Map can be owned through
// `std::shared_ptr<C>` when every instance read back is checked before use
// and no field can hold the Map or an instance. The test-only pass
// --ctnative-test-instance-registries writes the answer on the constructor.
//
// Each program is its own file: the proof stops at its first refusal, so two
// clauses in one program would test only the first.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proved.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=PROVED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proved.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=LIFT --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proved.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt "--pass-pipeline=builtin.module(ctjs-resolve-globals, ctjs-lift-to-scf, ctnative-lower-to-emitc, emitc.func(canonicalize, convert-scf-to-emitc, convert-arith-to-emitc, canonicalize, ctnative-prune-dead-stores, canonicalize))" \
// RUN:              --mlir-print-debuginfo -o %t/proved.emitc.mlir
// RUN: %compilation_unit --module %t/proved.emitc.mlir --js %t/proved.js --work %t/proved-native --name proved
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/tells.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=TELLS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/undefined-global.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNDEFINED-GLOBAL --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/undefined-global.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt "--pass-pipeline=builtin.module(ctjs-resolve-globals, ctjs-lift-to-scf, ctnative-lower-to-emitc, emitc.func(canonicalize, convert-scf-to-emitc, convert-arith-to-emitc, canonicalize, ctnative-prune-dead-stores, canonicalize))" \
// RUN:              --mlir-print-debuginfo -o %t/undefined-global.emitc.mlir
// RUN: %compilation_unit --module %t/undefined-global.emitc.mlir --js %t/undefined-global.js --work %t/undefined-global-native --name undefined_global
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unchecked.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=UNCHECKED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/strict-undefined.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=STRICT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/cycle.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=CYCLE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/object-field.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=OBJECT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/outside.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=OUTSIDE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nulling-miss.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=NULLING-MISS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/captor.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=CAPTOR
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/method-value.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=METHOD-VALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/iterate.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=ITERATE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/joined.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=JOINED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-escapes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-test-instance-registries \
// RUN:   | FileCheck %s --check-prefix=THIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/table.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=TABLE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/table-other.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=TABLE-OTHER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/table-kept.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=TABLE-KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nested.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=NESTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/family.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=FAMILY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/family-same-key.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=SAME-KEY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nested-unchecked.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=NESTED-UNCHECKED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nested-removed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=NESTED-REMOVED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/new-this.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=NEW-THIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/new-this-native.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NEW-THIS-NATIVE --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/new-this-native.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt "--pass-pipeline=builtin.module(ctjs-resolve-globals, ctjs-lift-to-scf, ctnative-lower-to-emitc, emitc.func(canonicalize, convert-scf-to-emitc, convert-arith-to-emitc, canonicalize, ctnative-prune-dead-stores, canonicalize))" \
// RUN:              --mlir-print-debuginfo -o %t/new-this-native.emitc.mlir
// RUN: %compilation_unit --module %t/new-this-native.emitc.mlir --js %t/new-this-native.js --work %t/new-this-native --name new_this_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/new-this-stored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=NEW-THIS-STORED

// --- The accepted shape ----------------------------------------------------
// `getInstance` reads back with `|| null`, so `!== null` is a check; a
// truthiness test and `!= undefined` are checks for any read; `sizeOf` is
// only ever passed a checked instance, so its parameter needs no check; and
// `dispose` removes the entry and goes on using `this`.
// PROVED: ctjs.func @sizeOf{{.*}}ctnative.instance_non_null = array<i32: 0>
// PROVED: ctjs.func @Widget{{.*}}ctnative.instance_registry = "proved"

// In the lowering, the closure lifter marks the proved constructor, lifts it
// although it passes `this` to the Map, and resolves the method calls on the
// instances read back, so `grow` and `dispose` become direct calls. Nothing
// else in the program can reach Map.prototype (mapRealmUnproved in
// Analysis/NativeMap.cpp), so the Map, and with it the whole program, is
// native; the %compilation_unit run checks its answer against the
// interpreter.
// LIFT-DAG: emitc.class @Widget
// LIFT-DAG: call_opaque "std::make_shared<Widget>"
// LIFT-DAG: call @Widget_{{[0-9]+}}(
// LIFT-DAG: emitc.call @grow_{{[0-9]+}}(
// LIFT-DAG: emitc.call @dispose_{{[0-9]+}}(

// The accepted program runs natively end to end in
// ../Fixtures/Objects/instance-registry.js. Two things its carrier, a
// `std::shared_ptr<Widget>` that is null for both null and undefined, cannot
// say are refused in the lowering:
// TELLS: ctnative.not_native = "a comparison of an instance with something that tells null from undefined, which one null pointer stands for"
// Reading the global `undefined` beside the Map no longer keeps the Map
// unproved: it cannot reach Map.prototype, so the program is native (and
// agrees with the interpreter). ../Lowering/Maps/realm-identity.mlir keeps
// the global reads that still refuse every Map.
// UNDEFINED-GLOBAL: emitc.func @run_{{[0-9]+}}

// --- A read used without a check -------------------------------------------
// UNCHECKED: ctnative.instance_registry = "a value read from the registry Map is used at `ctjs.get_property` without a check - `get` answers undefined for a missing key"

// --- `!== null` on a read that may be undefined is no check ----------------
// STRICT: ctnative.instance_registry = "a value read from the registry Map is used at `ctjs.get_property` without a check - `get` answers undefined for a missing key"

// --- An instance stored into an object ---------------------------------------
// CYCLE: ctnative.instance_registry = "a value read from the registry Map is stored into an object, so shared ownership could form a cycle no destructor breaks"

// --- Fields holding objects ------------------------------------------------
// No value that can hold an instance is ever stored into an object, so an
// object field cannot reach one, whatever it holds: Bootstrap's `_element`
// and `_config`.
// OBJECT: ctnative.instance_registry = "proved"

// --- A field written outside the class ---------------------------------------
// Only the constructor and the methods write fields, so each field's native
// type is the join of their stores.
// OUTSIDE: ctnative.instance_registry = "a field of an instance read from the registry Map is written outside its class's constructor and methods"

// --- An own-property loop over a field that may not exist yet ---------------
// `for (const k of Object.getOwnPropertyNames(this)) this[k] = null` becomes
// one store per field the constructor writes before anything else runs; a
// field only a method writes may not be an own property yet.
// NULLING-MISS: ctnative.instance_registry = "the own-property loop of `Widget` would miss `extra`, a field its constructor may not have written yet"

// --- A field holding a function that captures the Map ----------------------
// The binding `count` holds it, so whoever reads the binding holds it too.
// CAPTOR: ctnative.instance_registry = "a function that captures the registry Map is used at `ctjs.cell_set` as a value, so an instance holding it could own the Map - a cycle no destructor breaks"

// --- A method read from an instance and kept --------------------------------
// METHOD-VALUE: ctnative.instance_registry = "a value read from the registry Map reaches `ctjs.get_property`"

// --- Iterating while `delete` can remove entries ---------------------------
// ITERATE: ctnative.instance_registry = "the registry Map is iterated, and `delete` removes entries from it - a removal during the iteration changes what it visits"

// --- A Map holding something besides instances -----------------------------
// MIXED: ctnative.instance_registry = "the registry Map also holds a value that is not a new `Widget`"

// --- A read joined with another kind of value ------------------------------
// JOINED: ctnative.instance_registry = "a value read from the registry Map is joined with a value that is not an instance of `Widget`"

// --- `this` handed to code the proof cannot see ----------------------------
// THIS: ctnative.instance_registry = "`this` in the method `leak` of `Widget` reaches `ctjs.store_global`"

// --- Through a table method, as Bootstrap's Data.set --------------------
// The module function runs once, so the Map and the table become static
// variables and `Data.set(key, this)` a direct call (ClosureLifting/
// Singletons.cpp); `this` reaches the Map through the parameter `instance`.
// TABLE: ctnative.instance_registry = "proved"

// --- The table method also stores something else ---------------------------
// TABLE-OTHER: ctnative.instance_registry = "the registry Map also holds a value that is not a new `Widget`"

// --- The table method keeps the instance elsewhere too ---------------------
// The importer names an object-literal method `fn`.
// TABLE-KEPT: ctnative.instance_registry = "`this` of `Widget`, passed on as a parameter of `fn`, also reaches `ctjs.store_global`"

// --- A Map of Maps, as Bootstrap's Data -------------------------------------
// The outer Map, keyed by element, owns one inner Map per element, keyed by
// component name, which holds the instances. An inner Map is read only where
// `has` just found it or `has(k) || set(k, new Map())` just made it, and
// `remove` drops the outer entry once the inner Map is empty.
// NESTED: ctnative.instance_registry = "proved"

// --- A class family, as Bootstrap's BaseComponent and its subclasses ---------
// Inheritance flattening (ClosureLifting/ClassInheritance.cpp) gives each
// subclass its own copy of Base's constructor, which hands `this` to
// Data.set with `this.constructor.DATA_KEY`. Each class is proved on its own;
// its key is evaluated through its static getter, and each `getInstance`
// copy's Data.get, keyed with `this.DATA_KEY`, is marked with the one class
// its key selects. Base itself is never constructed and holds no instance.
// FAMILY-DAG: ctjs.call_direct {{.*}}ctnative.instance_key_class = "Alert$
// FAMILY-DAG: ctjs.call_direct {{.*}}ctnative.instance_key_class = "Button$
// FAMILY-DAG: ctjs.func @Alert${{[0-9]+}}({{.*}}ctnative.instance_key = "bs.alert", ctnative.instance_registry = "proved"
// FAMILY-DAG: ctjs.func @Button${{[0-9]+}}({{.*}}ctnative.instance_key = "bs.button", ctnative.instance_registry = "proved"

// --- Two classes of a family under one key ----------------------------------
// SAME-KEY: ctnative.instance_registry = "the classes `{{Alert|Button}}` and `{{Alert|Button}}` both register under the key `bs.shared`, so a read cannot know which it finds"

// --- An inner Map read without `has` ---------------------------------------
// NESTED-UNCHECKED: ctnative.instance_registry = "an inner Map is read from the outer registry Map where `has` did not just find or make it - `get` answers undefined for a missing key"

// --- An inner Map read after a call that could remove it --------------------
// NESTED-REMOVED: ctnative.instance_registry = "an inner Map is read from the outer registry Map where `has` did not just find or make it - `get` answers undefined for a missing key"

// --- Instances made by a static member's `new this`, as Bootstrap's -------
// `getOrCreateInstance`: `this.getInstance(element) || new this(element)`.
// The class statics rewrite (ClosureLifting/ClassStatics.cpp) makes the
// `new this` of each class's copy a marked construct of that class; the
// proof follows the fresh instance as it follows a read, through the join
// with the read and the return, and nothing else constructs either class.
// The family keys are read through the static getters that rewrite made
// direct calls.
// NEW-THIS-DAG: ctjs.func @Alert${{[0-9]+}}({{.*}}ctnative.instance_key = "bs.alert", ctnative.instance_registry = "proved"
// NEW-THIS-DAG: ctjs.func @Button${{[0-9]+}}({{.*}}ctnative.instance_key = "bs.button", ctnative.instance_registry = "proved"

// --- The whole program native, as one of Bootstrap's components --------------
// A run-once module whose class is made only by getOrCreateInstance's
// `getInstance(key) || new this(key, size)`: the registry Map is a static
// (ClosedValueFlow's run-once statics), nothing can reach Map.prototype
// (NativeMap.cpp's realm rule), the constructor lift takes the registry
// proof's word for the `new` that joins the read (Constructors.cpp), and
// that `new` is admitted as the std::shared_ptr it lowers to although its
// shape is open (Admission/Operations.cpp). The %compilation_unit run
// checks the answer, 123, against the interpreter.
// NEW-THIS-NATIVE: emitc.func @getOrCreateInstance_{{[0-9]+}}(
// NEW-THIS-NATIVE-SAME: -> !emitc.opaque<"std::shared_ptr<Widget>">
// NEW-THIS-NATIVE: call_opaque "std::make_shared<Widget>"

// --- A fresh instance stored into an object ----------------------------------
// Only the instance `new this` makes is stored; the read is returned as it
// is. Without following the fresh instance, the proof would only see the
// join of the read with it.
// NEW-THIS-STORED: ctnative.instance_registry = "a value read from the registry Map is stored into an object, so shared ownership could form a cycle no destructor breaks"

//--- proved.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) {
            this.key = key;
            this.size = size;
            registry.set(key, this);
        }
        grow() {
            this.size = this.size + 1;
            return this.size;
        }
        dispose() {
            registry.delete(this.key);
            this.size = 0;
            return this.size + registry.size;
        }
    }
    function getInstance(key) {
        return registry.get(key) || null;
    }
    function sizeOf(widget) {
        return widget.size;
    }
    new Widget("a", 3);
    new Widget("b", 10);
    let total = 0;
    const a = getInstance("a");
    if (a !== null) { total = total + a.grow() + sizeOf(a); }
    const b = getInstance("b");
    if (b) { total = total + b.size + b.dispose(); }
    if (getInstance("b") === null) { total = total + 100; }
    const c = registry.get("a");
    if (c != undefined) { total = total + c.size; }
    return total + registry.size;
}
var result = run();

//--- unchecked.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
    }
    new Widget("a");
    return registry.get("a").key;
}
var result = run();

//--- strict-undefined.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
    }
    new Widget("a");
    const a = registry.get("a");
    return a !== null ? a.key : "";
}
var result = run();

//--- cycle.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
    }
    new Widget("a");
    const holder = { item: null };
    const a = registry.get("a");
    if (a) { holder.item = a; }
    return registry.size;
}
var result = run();

//--- object-field.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(element, key, config) {
            this._element = element;
            this._config = { delay: config.delay, owner: null };
            registry.set(key, this);
        }
    }
    new Widget({ id: "x" }, "a", { delay: 5 });
    const a = registry.get("a");
    return a ? a._config.delay : 0;
}
var result = run();

//--- captor.js
function run() {
    const registry = new Map();
    const count = function () {
        return registry.size;
    };
    class Widget {
        constructor(key) {
            this.key = key;
            this.count = count;
            registry.set(key, this);
        }
    }
    new Widget("a");
    return registry.size;
}
var result = run();

//--- method-value.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
        size() {
            return registry.size;
        }
    }
    new Widget("a");
    const a = registry.get("a");
    let keep = null;
    if (a) { keep = a.size; }
    return keep === null ? 0 : 1;
}
var result = run();

//--- iterate.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
        dispose() {
            registry.delete(this.key);
        }
    }
    new Widget("a");
    let count = 0;
    for (const key of registry.keys()) { count = count + key.length; }
    return count;
}
var result = run();

//--- mixed.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
    }
    new Widget("a");
    registry.set("b", 1);
    return registry.size;
}
var result = run();

//--- joined.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
    }
    new Widget("a");
    const a = registry.get("a") || { key: "none" };
    return a.key;
}
var result = run();

//--- this-escapes.js
var kept = null;
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
        leak() {
            kept = this;
        }
    }
    new Widget("a");
    return registry.size;
}
var result = run();

//--- table.js
var result = (function () {
    const registry = new Map();
    const Data = {
        set(key, instance) {
            registry.set(key, instance);
        },
        get(key) {
            return registry.get(key) || null;
        },
        remove(key) {
            registry.delete(key);
        }
    };
    class Widget {
        constructor(key, size) {
            this.key = key;
            this.size = size;
            Data.set(key, this);
        }
        dispose() {
            Data.remove(this.key);
            return this.size;
        }
    }
    new Widget("a", 3);
    new Widget("b", 10);
    let total = 0;
    const a = Data.get("a");
    if (a !== null) { total = total + a.size; }
    const b = Data.get("b");
    if (b) { total = total + b.dispose(); }
    return total + registry.size;
})();

//--- table-other.js
var result = (function () {
    const registry = new Map();
    const Data = {
        set(key, instance) {
            registry.set(key, instance);
        }
    };
    class Widget {
        constructor(key) {
            this.key = key;
            Data.set(key, this);
        }
    }
    new Widget("a");
    Data.set("b", 2);
    return registry.size;
})();

//--- table-kept.js
var kept = null;
var result = (function () {
    const registry = new Map();
    const Data = {
        set(key, instance) {
            registry.set(key, instance);
            kept = instance;
        }
    };
    class Widget {
        constructor(key) {
            this.key = key;
            Data.set(key, this);
        }
    }
    new Widget("a");
    return registry.size;
})();

//--- nested.js
var result = (function () {
    const elements = new Map();
    const Data = {
        set(element, key, instance) {
            if (!elements.has(element)) {
                elements.set(element, new Map());
            }
            const instances = elements.get(element);
            if (!instances.has(key) && instances.size !== 0) {
                return;
            }
            instances.set(key, instance);
        },
        get(element, key) {
            return (elements.has(element) && elements.get(element).get(key)) || null;
        },
        remove(element, key) {
            if (!elements.has(element)) {
                return;
            }
            const instances = elements.get(element);
            instances.delete(key);
            if (instances.size === 0) {
                elements.delete(element);
            }
        }
    };
    class Widget {
        constructor(element, size) {
            this._element = element;
            this.size = size;
            Data.set(element, "widget", this);
        }
        dispose() {
            Data.remove(this._element, "widget");
            return this.size;
        }
    }
    const first = { id: 1 };
    const second = { id: 2 };
    new Widget(first, 3);
    new Widget(second, 10);
    let total = 0;
    const a = Data.get(first, "widget");
    if (a !== null) { total = total + a.size; }
    const b = Data.get(second, "widget");
    if (b) { total = total + b.dispose(); }
    if (Data.get(second, "widget") === null) { total = total + 100; }
    return total;
})();

//--- nested-unchecked.js
var result = (function () {
    const elements = new Map();
    const Data = {
        set(element, key, instance) {
            if (!elements.has(element)) {
                elements.set(element, new Map());
            }
            const instances = elements.get(element);
            if (!instances.has(key) && instances.size !== 0) {
                return;
            }
            instances.set(key, instance);
        },
        get(element, key) {
            return (elements.has(element) && elements.get(element).get(key)) || null;
        },
        remove(element, key) {
            elements.get(element).delete(key);
            const instances = elements.get(element);
            if (instances.size === 0) {
                elements.delete(element);
            }
        }
    };
    class Widget {
        constructor(element, size) {
            this._element = element;
            this.size = size;
            Data.set(element, "widget", this);
        }
        dispose() {
            Data.remove(this._element, "widget");
            return this.size;
        }
    }
    const first = { id: 1 };
    const second = { id: 2 };
    new Widget(first, 3);
    new Widget(second, 10);
    let total = 0;
    const a = Data.get(first, "widget");
    if (a !== null) { total = total + a.size; }
    const b = Data.get(second, "widget");
    if (b) { total = total + b.dispose(); }
    if (Data.get(second, "widget") === null) { total = total + 100; }
    return total;
})();

//--- nested-removed.js
var result = (function () {
    const elements = new Map();
    const Data = {
        set(element, key, instance) {
            if (!elements.has(element)) {
                elements.set(element, new Map());
            }
            Data.remove(element, key);
            const instances = elements.get(element);
            if (!instances.has(key) && instances.size !== 0) {
                return;
            }
            instances.set(key, instance);
        },
        get(element, key) {
            return (elements.has(element) && elements.get(element).get(key)) || null;
        },
        remove(element, key) {
            if (!elements.has(element)) {
                return;
            }
            const instances = elements.get(element);
            instances.delete(key);
            if (instances.size === 0) {
                elements.delete(element);
            }
        }
    };
    class Widget {
        constructor(element, size) {
            this._element = element;
            this.size = size;
            Data.set(element, "widget", this);
        }
        dispose() {
            Data.remove(this._element, "widget");
            return this.size;
        }
    }
    const first = { id: 1 };
    const second = { id: 2 };
    new Widget(first, 3);
    new Widget(second, 10);
    let total = 0;
    const a = Data.get(first, "widget");
    if (a !== null) { total = total + a.size; }
    const b = Data.get(second, "widget");
    if (b) { total = total + b.dispose(); }
    if (Data.get(second, "widget") === null) { total = total + 100; }
    return total;
})();

//--- family.js
var total = (function () {
    const elements = new Map();
    const Data = {
        set(element, key, instance) {
            if (!elements.has(element)) {
                elements.set(element, new Map());
            }
            elements.get(element).set(key, instance);
        },
        get(element, key) {
            return (elements.has(element) && elements.get(element).get(key)) || null;
        },
        remove(element, key) {
            if (!elements.has(element)) {
                return;
            }
            const instances = elements.get(element);
            instances.delete(key);
            if (instances.size === 0) {
                elements.delete(element);
            }
        }
    };
    class Base {
        constructor(element) {
            this._element = element;
            Data.set(element, this.constructor.DATA_KEY, this);
        }
        dispose() {
            Data.remove(this._element, this.constructor.DATA_KEY);
        }
        static getInstance(element) {
            return Data.get(element, this.DATA_KEY);
        }
    }
    class Alert extends Base {
        constructor(element) {
            super(element);
            this.kind = 1;
        }
        static get DATA_KEY() { return "bs.alert"; }
        close() { return 1; }
    }
    class Button extends Base {
        constructor(element) {
            super(element);
            this.kind = 2;
        }
        static get DATA_KEY() { return "bs.button"; }
        toggle() { return 2; }
    }
    const el = { id: 1 };
    new Alert(el);
    new Button(el);
    let t = 0;
    const a = Alert.getInstance(el);
    if (a) { t = t + a.close(); }
    const b = Button.getInstance(el);
    if (b) { t = t + b.toggle(); b.dispose(); }
    if (Button.getInstance(el) === null) { t = t + 10; }
    return t;
})();

//--- outside.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            this.size = 0;
            registry.set(key, this);
        }
    }
    new Widget("a");
    const a = registry.get("a");
    if (a) { a.size = 5; }
    return registry.size;
}
var result = run();

//--- tells.js
function run(flag) {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
    }
    new Widget("a");
    const found = flag ? registry.get("a") : null;
    return found === null ? 1 : 2;
}
var result = run(true);

//--- undefined-global.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
    }
    new Widget("a");
    const found = registry.get("a");
    return found != undefined ? found.key.length : 0;
}
var result = run();

//--- nulling-miss.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
        mark() {
            this.extra = 1;
        }
        dispose() {
            for (const name of Object.getOwnPropertyNames(this)) {
                this[name] = null;
            }
        }
    }
    new Widget("a");
    return registry.size;
}
var result = run();

//--- family-same-key.js
var total = (function () {
    const elements = new Map();
    const Data = {
        set(element, key, instance) {
            if (!elements.has(element)) {
                elements.set(element, new Map());
            }
            elements.get(element).set(key, instance);
        },
        get(element, key) {
            return (elements.has(element) && elements.get(element).get(key)) || null;
        },
        remove(element, key) {
            if (!elements.has(element)) {
                return;
            }
            const instances = elements.get(element);
            instances.delete(key);
            if (instances.size === 0) {
                elements.delete(element);
            }
        }
    };
    class Base {
        constructor(element) {
            this._element = element;
            Data.set(element, this.constructor.DATA_KEY, this);
        }
        dispose() {
            Data.remove(this._element, this.constructor.DATA_KEY);
        }
        static getInstance(element) {
            return Data.get(element, this.DATA_KEY);
        }
    }
    class Alert extends Base {
        constructor(element) {
            super(element);
            this.kind = 1;
        }
        static get DATA_KEY() { return "bs.shared"; }
        close() { return 1; }
    }
    class Button extends Base {
        constructor(element) {
            super(element);
            this.kind = 2;
        }
        static get DATA_KEY() { return "bs.shared"; }
        toggle() { return 2; }
    }
    const el = { id: 1 };
    new Alert(el);
    new Button(el);
    let t = 0;
    const a = Alert.getInstance(el);
    if (a) { t = t + a.close(); }
    const b = Button.getInstance(el);
    if (b) { t = t + b.toggle(); b.dispose(); }
    if (Button.getInstance(el) === null) { t = t + 10; }
    return t;
})();

//--- new-this.js
var total = (function () {
    const elements = new Map();
    const Data = {
        set(element, key, instance) {
            if (!elements.has(element)) {
                elements.set(element, new Map());
            }
            elements.get(element).set(key, instance);
        },
        get(element, key) {
            return (elements.has(element) && elements.get(element).get(key)) || null;
        },
        remove(element, key) {
            if (!elements.has(element)) {
                return;
            }
            const instances = elements.get(element);
            instances.delete(key);
            if (instances.size === 0) {
                elements.delete(element);
            }
        }
    };
    class Base {
        constructor(element) {
            this._element = element;
            Data.set(element, this.constructor.DATA_KEY, this);
        }
        dispose() {
            Data.remove(this._element, this.constructor.DATA_KEY);
        }
        static getInstance(element) {
            return Data.get(element, this.DATA_KEY);
        }
        static getOrCreateInstance(element) {
            return this.getInstance(element) || new this(element);
        }
    }
    class Alert extends Base {
        constructor(element) {
            super(element);
            this.kind = 1;
        }
        static get DATA_KEY() { return "bs.alert"; }
        close() { return 1; }
    }
    class Button extends Base {
        constructor(element) {
            super(element);
            this.kind = 2;
        }
        static get DATA_KEY() { return "bs.button"; }
        toggle() { return 2; }
    }
    const el = { id: 1 };
    let t = 0;
    t = t + Alert.getOrCreateInstance(el).close();
    t = t + Button.getOrCreateInstance(el).toggle();
    const a = Alert.getInstance(el);
    if (a) { t = t + a.close(); }
    const b = Button.getInstance(el);
    if (b) { t = t + b.toggle(); b.dispose(); }
    if (Button.getInstance(el) === null) { t = t + 10; }
    return t;
})();

//--- new-this-stored.js
var total = (function () {
    const elements = new Map();
    const kept = {};
    const Data = {
        set(element, key, instance) {
            if (!elements.has(element)) {
                elements.set(element, new Map());
            }
            elements.get(element).set(key, instance);
        },
        get(element, key) {
            return (elements.has(element) && elements.get(element).get(key)) || null;
        },
        remove(element, key) {
            if (!elements.has(element)) {
                return;
            }
            const instances = elements.get(element);
            instances.delete(key);
            if (instances.size === 0) {
                elements.delete(element);
            }
        }
    };
    class Base {
        constructor(element) {
            this._element = element;
            Data.set(element, this.constructor.DATA_KEY, this);
        }
        dispose() {
            Data.remove(this._element, this.constructor.DATA_KEY);
        }
        static getInstance(element) {
            return Data.get(element, this.DATA_KEY);
        }
        static getOrCreateInstance(element) {
            const found = this.getInstance(element);
            if (found) {
                return found;
            }
            const made = new this(element);
            kept.last = made;
            return made;
        }
    }
    class Alert extends Base {
        constructor(element) {
            super(element);
            this.kind = 1;
        }
        static get DATA_KEY() { return "bs.alert"; }
        close() { return 1; }
    }
    class Button extends Base {
        constructor(element) {
            super(element);
            this.kind = 2;
        }
        static get DATA_KEY() { return "bs.button"; }
        toggle() { return 2; }
    }
    const el = { id: 1 };
    let t = 0;
    t = t + Alert.getOrCreateInstance(el).close();
    t = t + Button.getOrCreateInstance(el).toggle();
    const a = Alert.getInstance(el);
    if (a) { t = t + a.close(); }
    const b = Button.getInstance(el);
    if (b) { t = t + b.toggle(); b.dispose(); }
    if (Button.getInstance(el) === null) { t = t + 10; }
    return t;
})();

//--- new-this-native.js
var total = (function () {
    const registry = new Map();
    class Widget {
        constructor(key, size) {
            this.key = key;
            this.size = size;
            registry.set(key, this);
        }
        static getInstance(key) {
            return registry.get(key) || null;
        }
        static getOrCreateInstance(key, size) {
            return this.getInstance(key) || new this(key, size);
        }
        grow() {
            this.size = this.size + 1;
            return this.size;
        }
    }
    const a = Widget.getOrCreateInstance(1, 3).grow();
    const b = Widget.getOrCreateInstance(1, 9).grow();
    const c = Widget.getOrCreateInstance(2, 7).grow();
    return a * 100 + b * 10 + c + registry.size;
})();
