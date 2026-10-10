// A captured register(element, key, item) helper is inlined before its inner Map is chosen.
//--- class-map-record-nested-captured-direct.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    register("element", "bs.item", first);
    const savedChild = outer.get("element"), savedFirst = savedChild.get("bs.item");
    register("element", "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 1000 + savedChild.get("bs.item").n * 100
        + outer.get("element").get("bs.item").n * 10 + outer.size;
}
var a = probe();

// Each call substitutes its own literal keys; the two inner Maps stay separate.
//--- class-map-record-nested-captured-distinct.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    register("left", "bs.left", first);
    register("right", "bs.right", second);
    const savedLeft = outer.get("left").get("bs.left");
    const savedRight = outer.get("right").get("bs.right");
    savedLeft.n = 5;
    savedRight.n = 9;
    return first.n * 1000 + second.n * 100 + savedLeft.n * 10 + savedRight.n + outer.size;
}
var a = probe();

// Every Data method is called; saved inner Maps and records stay valid across remove/set.
//--- class-map-record-nested-captured-holder.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        get(element, key) { return outer.get(element).get(key); },
        remove(element) { outer.delete(element); }
    };
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(3);
    Data.set("element", "bs.item", first);
    const savedChild = outer.get("element"), savedFirst = Data.get("element", "bs.item");
    Data.remove("element");
    Data.set("element", "bs.item", second);
    savedFirst.n = 5;
    return savedFirst.n * 1000 + savedChild.size * 100
        + Data.get("element", "bs.item").n * 10 + outer.size;
}
var a = probe();

// `||` and `&&` regions that only touch Maps keep their results after inlining.
//--- class-map-record-nested-captured-shortcircuit.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        outer.has(element) || outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(2);
    register("element", "bs.item", item);
    const savedChild = outer.has("element") && outer.get("element");
    register("element", "bs.item", item);
    savedChild.get("bs.item").n = 5;
    return item.n * 100 + outer.get("element").get("bs.item").n * 10 + outer.size;
}
var a = probe();

// A caller-controlled flag decides whether the helper replaces the inner Map.
//--- class-map-record-nested-captured-dynamic-branch.js
function probe(flag) {
    const outer = new Map();
    outer.set("element", new Map());
    function register(element, key, item) {
        if (flag) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register("element", "bs.item", item);
    return outer.get("element").get("bs.item").n;
}
var a = probe(true) * 10 + probe(false);

// Deleting the outer entry means a later outer.get() is not proved present.
//--- class-map-record-nested-captured-missing-outer.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register("element", "bs.item", item);
    outer.delete("element");
    return (outer.get("element") === void 0) * 100 + item.n;
}
var a = probe();

// A successful helper registration proves only its exact inner key.
//--- class-map-record-nested-captured-missing-inner.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register("element", "bs.item", item);
    return (outer.get("element").get("missing") === void 0) * 100 + item.n;
}
var a = probe();

// Inlining the captured helper cannot remove an outer Map that escapes probe().
//--- class-map-record-nested-captured-outer-escaped.js
var retained;
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register("element", "bs.item", item);
    retained = outer;
    return item.n;
}
var a = probe();

// An escaped inner Map still holds a record owned by probe()'s frame.
//--- class-map-record-nested-captured-child-escaped.js
var retained;
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register("element", "bs.item", item);
    retained = outer.get("element");
    return item.n;
}
var a = probe();

// The record itself cannot escape its owning frame through the inlined helper.
//--- class-map-record-nested-captured-record-escaped.js
var retained;
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register("element", "bs.item", item);
    retained = outer.get("element").get("bs.item");
    return item.n;
}
var a = probe();

// A Data holder that escapes probe() keeps every method: none may be removed.
//--- class-map-record-nested-captured-holder-escaped.js
var retained;
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        get(element, key) { return outer.get(element).get(key); }
    };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    Data.set("element", "bs.item", item);
    retained = Data;
}
probe();
var a = retained.get("element", "bs.item").n;

// The proof scans the whole helper body, including a call in a branch that never runs.
//--- class-map-record-nested-captured-dead-call.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.has(element) || outer.set(element, missingFactory());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register("element", "bs.item", item);
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Coercion in a branch that never runs still counts as an effect beyond Map operations.
//--- class-map-record-nested-captured-dead-coercion.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.has(element) || outer.set(element, +new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register("element", "bs.item", item);
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// A recursive register() call is not a fixed, ordered call in the Map's owning function.
//--- class-map-record-nested-captured-reentry.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) {
            outer.set(element, new Map());
            register(element, key, item);
        }
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register("element", "bs.item", item);
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// A record created in a nested block cannot outlive that block through the helper.
//--- class-map-record-nested-captured-region-owner.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    if (true) {
        const item = new Item(7);
        register("element", "bs.item", item);
    }
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// A constructor registering `this` through the helper needs its own inner-Map proof.
//--- class-map-record-nested-captured-constructor.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(n) {
            this.n = n;
            register("element", "bs.item", this);
        }
    }
    const item = new Item(7);
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Helper publication followed by a throw retains the exception observation.
//--- class-map-record-nested-captured-exception.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
        throw 9;
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    try { register("element", "bs.item", item); }
    catch (error) { return error + outer.get("element").get("bs.item").n; }
}
var a = probe();

// Element identity remains unproved when the outer key is a record.
//--- class-map-record-nested-captured-object-key.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register(item, "bs.item", item);
    return outer.get(item).get("bs.item").n;
}
var a = probe();

// Inner keys must retain literal String identity after argument substitution.
//--- class-map-record-nested-captured-number-key.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    register("element", 1, item);
    return outer.get("element").get(1).n;
}
var a = probe();
