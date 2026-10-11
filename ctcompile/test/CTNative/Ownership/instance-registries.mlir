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

// --- The accepted shape ----------------------------------------------------
// `getInstance` reads back with `|| null`, so `!== null` is a check; a
// truthiness test and `!= undefined` are checks for any read; `sizeOf` is
// only ever passed a checked instance, so its parameter needs no check; and
// `dispose` removes the entry and goes on using `this`.
// PROVED: ctjs.func @sizeOf{{.*}}ctnative.instance_non_null = array<i32: 0>
// PROVED: ctjs.func @Widget{{.*}}ctnative.instance_registry = "proved"

// --- A read used without a check -------------------------------------------
// UNCHECKED: ctnative.instance_registry = "a value read from the registry Map is used at `ctjs.get_property` without a check - `get` answers undefined for a missing key"

// --- `!== null` on a read that may be undefined is no check ----------------
// STRICT: ctnative.instance_registry = "a value read from the registry Map is used at `ctjs.get_property` without a check - `get` answers undefined for a missing key"

// --- An instance stored into another instance ------------------------------
// CYCLE: ctnative.instance_registry = "a value read from the registry Map is stored into an object, so shared ownership could form a cycle no destructor breaks"

// --- A field holding an object, which could hold an instance ---------------
// OBJECT: ctnative.instance_registry = "the field `box` of `Widget` can hold an object, so shared ownership could form a cycle no destructor breaks"

// --- Iterating while `delete` can remove entries ---------------------------
// ITERATE: ctnative.instance_registry = "the registry Map is iterated, and `delete` removes entries from it - a removal during the iteration changes what it visits"

// --- A Map holding something besides instances -----------------------------
// MIXED: ctnative.instance_registry = "the registry Map also holds a value that is not a new `Widget`"

// --- A read joined with another kind of value ------------------------------
// JOINED: ctnative.instance_registry = "a value read from the registry Map is joined with a value that is not an instance of `Widget`"

// --- `this` handed to code the proof cannot see ----------------------------
// THIS: ctnative.instance_registry = "`this` in the method `leak` of `Widget` reaches `ctjs.store_global`"

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
            this.peer = null;
            registry.set(key, this);
        }
    }
    new Widget("a");
    const b = new Widget("b");
    const a = registry.get("a");
    if (a) { b.peer = a; }
    return registry.size;
}
var result = run();

//--- object-field.js
function run() {
    const registry = new Map();
    class Widget {
        constructor(key) {
            this.key = key;
            this.box = { owner: null };
            registry.set(key, this);
        }
    }
    new Widget("a");
    return registry.size;
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
