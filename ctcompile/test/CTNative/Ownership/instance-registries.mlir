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
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nested-unchecked.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=NESTED-UNCHECKED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nested-removed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=NESTED-REMOVED

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

// --- Fields holding objects ------------------------------------------------
// No value that can hold an instance is ever stored into an object, so an
// object field cannot reach one, whatever it holds: Bootstrap's `_element`
// and `_config`.
// OBJECT: ctnative.instance_registry = "proved"

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
// FAMILY: ctnative.instance_registry = "the class `Base` is extended, or held in a binding through which subclasses and static methods reach it - a class family this proof does not follow yet"

// --- An inner Map read without `has` ---------------------------------------
// NESTED-UNCHECKED: ctnative.instance_registry = "an inner Map is read from the outer registry Map where `has` did not just find or make it - `get` answers undefined for a missing key"

// --- An inner Map read after a call that could remove it --------------------
// NESTED-REMOVED: ctnative.instance_registry = "an inner Map is read from the outer registry Map where `has` did not just find or make it - `get` answers undefined for a missing key"

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
var result = (function () {
    const registry = new Map();
    class Base {
        constructor(key) {
            this.key = key;
            registry.set(key, this);
        }
    }
    class Alert extends Base {
        close() {
            return this.key;
        }
    }
    new Alert("a");
    return registry.size;
})();
