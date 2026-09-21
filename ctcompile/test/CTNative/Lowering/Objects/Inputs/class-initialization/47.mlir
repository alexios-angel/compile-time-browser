// Child membership and size distinguish partial removal from empty-parent cleanup.
//--- class-map-record-nested-cleanup-direct.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    function remove(element, key) {
        if (outer.has(element)) {
            const child = outer.get(element);
            if (child.has(key)) child.delete(key);
            if (child.size === 0) outer.delete(element);
        }
    }
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7), other = new Item(4);
    register("left", "bs.first", first);
    register("left", "bs.second", second);
    register("right", "bs.first", other);
    const savedChild = outer.get("left"), savedFirst = savedChild.get("bs.first");
    remove("left", "missing");
    remove("left", "bs.first");
    const remaining = savedChild.size, retained = outer.has("left");
    remove("left", "bs.second");
    remove("left", "bs.first");
    const absent = outer.has("left");
    register("left", "bs.third", other);
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 100000 + second.n * 10000
        + outer.get("right").get("bs.first").n * 1000
        + outer.get("left").get("bs.third").n * 100
        + remaining * 10 + retained + absent + savedChild.size + outer.size;
}
var a = probe();

// A second outer identity can mutate the same child before the first observes it.
//--- class-map-record-nested-cleanup-shared-outer-child.js
function probe() {
    const outer = new Map(), otherOuter = new Map(), child = new Map();
    outer.set("element", child);
    otherOuter.set("other", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    otherOuter.get("other").delete("bs.item");
    if (outer.get("element").size === 0) outer.delete("element");
    return outer.size * 1000 + otherOuter.size * 100 + child.size * 10 + item.n;
}
var a = probe();

// Cleanup cannot merge object-key identity with a literal String membership proof.
//--- class-map-record-nested-cleanup-object-key.js
function probe() {
    const outer = new Map(), child = new Map(), key = {};
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set(key, item);
    if (child.has(key)) child.delete(key);
    if (child.size === 0) outer.delete("element");
    return outer.size * 100 + child.size * 10 + item.n;
}
var a = probe();

// Number keys remain distinct from String keys at the child membership boundary.
//--- class-map-record-nested-cleanup-number-key.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set(1, item);
    if (child.has("1")) child.delete("1");
    if (child.size === 0) outer.delete("element");
    return outer.size * 100 + child.size * 10 + item.n;
}
var a = probe();

// Deleting a saved borrow does not let a record outlive its defining region.
//--- class-map-record-nested-cleanup-region-owner.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    if (true) {
        const item = new Item(7);
        child.set("bs.item", item);
    }
    const saved = child.get("bs.item");
    if (child.delete("bs.item")) outer.delete("element");
    return saved.n;
}
var a = probe();

// Every Data slot is called; constructor publication and saved aliases survive cleanup.
//--- class-map-record-nested-cleanup-holder.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        get(element, key) { return outer.get(element).get(key); },
        remove(element, key) {
            if (outer.has(element)) {
                const child = outer.get(element);
                child.delete(key);
                if (child.size === 0) outer.delete(element);
            }
        }
    };
    class Item {
        constructor(n) {
            this.n = n;
            Data.set("element", "bs.item", this);
        }
    }
    const first = new Item(2);
    const savedChild = outer.get("element"), savedFirst = Data.get("element", "bs.item");
    Data.remove("element", "missing");
    const present = savedChild.has("bs.item");
    Data.remove("element", "bs.item");
    Data.remove("element", "bs.item");
    const second = new Item(7);
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 1000 + Data.get("element", "bs.item").n * 100
        + present * 10 + savedChild.size + outer.size;
}
var a = probe();

// A failed child delete cannot clean up; cached child observations retain their time.
//--- class-map-record-nested-cleanup-shortcircuit.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        outer.has(element) || outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    function remove(element, key) {
        if (outer.has(element)) {
            const child = outer.get(element);
            child.delete(key) && (child.size === 0 && outer.delete(element));
        }
    }
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    register("element", "bs.item", first);
    const savedChild = outer.get("element"), savedFirst = savedChild.get("bs.item");
    const wasPresent = savedChild.has("bs.item"), wasSize = savedChild.size;
    remove("element", "missing");
    remove("element", "bs.item");
    const absent = savedChild.has("bs.item");
    register("element", "bs.item", second);
    savedFirst.n = 5;
    return savedFirst.n * 1000 + outer.get("element").get("bs.item").n * 100
        + wasPresent * 10 + wasSize + absent + savedChild.size + outer.size;
}
var a = probe();

// Child contents must not be inferred from a caller-controlled key.
//--- class-map-record-nested-cleanup-dynamic-key.js
function probe(key) {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set(key, item);
    if (child.has("bs.item")) child.delete("bs.item");
    if (child.size === 0) outer.delete("element");
    return outer.size * 10 + item.n;
}
var a = probe("bs.item") * 100 + probe("other");

// An unknown branch can change child membership before parent cleanup.
//--- class-map-record-nested-cleanup-dynamic-branch.js
function probe(flag) {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    if (flag) child.delete("bs.item");
    if (child.size === 0) outer.delete("element");
    return outer.size * 10 + item.n;
}
var a = probe(true) * 100 + probe(false);

// Emptying a child does not authorize an escaped outer identity.
//--- class-map-record-nested-cleanup-outer-escaped.js
var retained;
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    if (child.delete("bs.item")) outer.delete("element");
    retained = outer;
    return item.n;
}
var a = probe();

// An escaped child must retain its owner proof even after its last record is removed.
//--- class-map-record-nested-cleanup-child-escaped.js
var retained;
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    retained = child;
    if (child.delete("bs.item")) outer.delete("element");
    return item.n;
}
var a = probe();

// Removing the last stored borrow does not extend the record's stack lifetime.
//--- class-map-record-nested-cleanup-record-escaped.js
var retained;
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    retained = child.get("bs.item");
    if (child.delete("bs.item")) outer.delete("element");
    return item.n;
}
var a = probe();

// A dead child branch still contains a call outside the Map-only proof.
//--- class-map-record-nested-cleanup-dead-call.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    if (child.has("missing")) {
        unknownObserver(child);
        outer.delete("element");
    }
    child.delete("bs.item");
    if (child.size === 0) outer.delete("element");
    return item.n;
}
var a = probe();

// Coercing a child is an effect even when a known nonempty size skips the branch.
//--- class-map-record-nested-cleanup-dead-coercion.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    if (child.size === 0) outer.set("other", +child);
    child.delete("bs.item");
    if (child.size === 0) outer.delete("element");
    return item.n;
}
var a = probe();

// An unused Data slot remains an observer of the captured Map.
//--- class-map-record-nested-cleanup-unused-slot.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        remove(element, key) {
            if (outer.has(element)) {
                const child = outer.get(element);
                child.delete(key);
                if (child.size === 0) outer.delete(element);
            }
        },
        inspect() { return unknownObserver(outer); }
    };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    Data.set("element", "bs.item", item);
    Data.remove("element", "bs.item");
    return item.n;
}
var a = probe();

// A live holder cannot have its called slots retired when it escapes.
//--- class-map-record-nested-cleanup-holder-escaped.js
var retained;
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        remove(element, key) {
            if (outer.has(element)) {
                const child = outer.get(element);
                child.delete(key);
                if (child.size === 0) outer.delete(element);
            }
        }
    };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    Data.set("element", "bs.item", item);
    Data.remove("element", "bs.item");
    retained = Data;
    return item.n;
}
var a = probe();

// A throw between child deletion and parent cleanup leaves distinct catch observations.
//--- class-map-record-nested-cleanup-exception.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function remove(element, key) {
        if (outer.has(element)) {
            const selected = outer.get(element);
            selected.delete(key);
            throw 9;
            if (selected.size === 0) outer.delete(element);
        }
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    try { remove("element", "bs.item"); }
    catch (error) { return outer.size * 100 + child.size * 10 + error; }
}
var a = probe();

// Recursive removal is not an ordered helper call in the captured owner's entry.
//--- class-map-record-nested-cleanup-reentry.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function remove(element, key) {
        if (outer.has(element)) {
            const selected = outer.get(element);
            if (selected.delete(key)) remove(element, key);
            if (selected.size === 0) outer.delete(element);
        }
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    remove("element", "bs.item");
    return outer.size * 100 + child.size * 10 + item.n;
}
var a = probe();

// An observed lookup after removal needs a separate nullable-record proof.
//--- class-map-record-nested-cleanup-missing-inner.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    if (child.delete("bs.item")) outer.delete("element");
    return (child.get("bs.item") === void 0) * 100 + item.n;
}
var a = probe();

// A dead set result still observes the outer identity through its result union.
//--- class-map-record-nested-cleanup-observed-dead-set.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    const result = child.has("bs.item") || outer.set("other", child);
    child.delete("bs.item");
    if (child.size === 0) outer.delete("element");
    return (result === true) * 100 + item.n;
}
var a = probe();
