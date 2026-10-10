// A captured three-argument helper expands before selecting its nested child.
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

// Each invocation substitutes both literal keys without merging child owners.
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

// All called holder slots retain saved child/record owners through recreation.
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

// Map-only short-circuit regions retain their results after helper expansion.
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

// Caller-controlled truth cannot select a captured helper's child topology.
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

// Removing a routed child does not prove a later outer get present.
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

// Captured expansion cannot erase an outer Map that escapes its entry.
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

// A saved child still borrows a record whose declaring frame is local.
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

// The record itself cannot outlive its owner through the expanded route.
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

// A holder observed after its frame cannot have its live slots retired.
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

// The complete body census includes a user call in an unselected region.
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

// Dead coercion is not a Map-only effect even when no runtime call reaches it.
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

// Recursive reentry is not an ordered call in the captured Map's owner block.
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

// An entry helper proof cannot extend a nested region's record lifetime.
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

// Three-argument constructor publication needs its own selected-owner proof.
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
