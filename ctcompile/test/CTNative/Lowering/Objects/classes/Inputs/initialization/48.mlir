// Bootstrap's `get(...) || null` getter keeps saved records valid across replace and cleanup.
//--- class-map-record-nested-nullable-direct.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    function remove(element, key) {
        if (outer.has(element)) {
            const child = outer.get(element);
            child.delete(key);
            if (child.size === 0) outer.delete(element);
        }
    }
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7), other = new Item(4);
    const absentBefore = get("left", "bs.item");
    register("left", "bs.item", first);
    register("right", "bs.item", other);
    const savedChild = outer.get("left"), savedFirst = get("left", "bs.item");
    const missingInner = get("left", "missing");
    register("left", "bs.item", second);
    const savedSecond = get("left", "bs.item");
    remove("left", "bs.item");
    const absentAfter = get("left", "bs.item");
    register("left", "bs.item", other);
    savedFirst.n = 5;
    savedSecond.n = 9;
    return savedFirst.n * 100000 + savedSecond.n * 10000
        + get("right", "bs.item").n * 1000 + get("left", "bs.item").n * 100
        + (absentBefore === null) * 10 + (missingInner === null)
        + (absentAfter === null) + savedChild.size + outer.size;
}
var a = probe();

// Every Data method is called, including missing lookups before and after cleanup.
//--- class-map-record-nested-nullable-holder.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            outer.has(element) || outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        get(element, key) {
            return outer.has(element) && outer.get(element).get(key) || null;
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
    const first = new Item(2), second = new Item(7);
    Data.set("element", "bs.item", first);
    const savedChild = outer.get("element"), savedFirst = Data.get("element", "bs.item");
    const missing = Data.get("element", "missing");
    Data.remove("element", "bs.item");
    const absent = Data.get("element", "bs.item");
    Data.set("element", "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + Data.get("element", "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + outer.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();

// Constructor registration and `|| null` reads share the same ordinary record owners.
//--- class-map-record-nested-nullable-constructor.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        get(element, key) {
            return outer.has(element) && outer.get(element).get(key) || null;
        },
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
    const absentBefore = Data.get("element", "bs.item");
    const first = new Item(2);
    const savedChild = outer.get("element"), savedFirst = Data.get("element", "bs.item");
    const missing = Data.get("element", "missing");
    Data.remove("element", "bs.item");
    const absentAfter = Data.get("element", "bs.item");
    const second = new Item(7);
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 10000 + Data.get("element", "bs.item").n * 1000
        + (absentBefore === null) * 100 + (missing === null) * 10
        + (absentAfter === null) + savedChild.size + outer.size;
}
var a = probe();

// `|| null` cannot hide a missing get() whose raw undefined is also read.
//--- class-map-record-nested-nullable-raw-undefined.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    const raw = child.get("missing"), nullable = raw || null;
    return (raw === void 0) * 1000 + (nullable === null) * 100
        + (raw === null) * 10 + (raw === false) + (nullable === void 0) + item.n;
}
var a = probe();

// A stored null value is not a record that owns anything.
//--- class-map-record-nested-nullable-null-payload.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    child.set("empty", null);
    return (get("element", "empty") === null) * 100 + item.n;
}
var a = probe();

// An explicitly stored undefined is not the same as a missing record.
//--- class-map-record-nested-nullable-undefined-payload.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    child.set("empty", void 0);
    return (get("element", "empty") === null) * 100 + item.n;
}
var a = probe();

// A caller-controlled inner key cannot reuse the literal-key lookup proof.
//--- class-map-record-nested-nullable-dynamic-key.js
function probe(key) {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    return (get("element", key) === null) * 100 + item.n;
}
var a = probe("missing") * 1000 + probe("bs.item");

// A caller-controlled element key can find either nothing (null) or the inner Map.
//--- class-map-record-nested-nullable-dynamic-element.js
function probe(element) {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    return (get(element, "bs.item") === null) * 100 + item.n;
}
var a = probe("missing") * 1000 + probe("element");

// An object element key needs an identity proof, not the literal String-key proof.
//--- class-map-record-nested-nullable-object-key.js
function probe() {
    const outer = new Map(), child = new Map(), element = {};
    outer.set(element, child);
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    return get(element, "bs.item").n;
}
var a = probe();

// Another outer Map can change the shared inner Map before the `|| null` read.
//--- class-map-record-nested-nullable-shared-child.js
function probe() {
    const outer = new Map(), other = new Map(), child = new Map();
    outer.set("element", child);
    other.set("other", child);
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    other.get("other").delete("bs.item");
    return (get("element", "bs.item") === null) * 100 + item.n;
}
var a = probe();

// A `|| null` read cannot drop an inner Map that has escaped probe().
//--- class-map-record-nested-nullable-child-escaped.js
var retained;
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    retained = child;
    return get("element", "bs.item").n;
}
var a = probe();

// A record found by get() still cannot escape the function that owns it.
//--- class-map-record-nested-nullable-record-escaped.js
var retained;
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    retained = get("element", "bs.item");
    return item.n;
}
var a = probe();

// A found record cannot outlive the block that created it, even when its key exists.
//--- class-map-record-nested-nullable-region-owner.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    class Item { constructor(n) { this.n = n; } }
    if (true) {
        const item = new Item(7);
        child.set("bs.item", item);
    }
    return get("element", "bs.item").n;
}
var a = probe();

// Data's unused `inspect` method still reads the captured Map.
//--- class-map-record-nested-nullable-unused-slot.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    const Data = {
        get(element, key) {
            return outer.has(element) && outer.get(element).get(key) || null;
        },
        inspect() { return unknownObserver(outer); }
    };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    return Data.get("element", "bs.item").n;
}
var a = probe();

// A Data holder that escapes probe() must keep its getter and the Map it captures.
//--- class-map-record-nested-nullable-holder-escaped.js
var retained;
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    const Data = {
        get(element, key) {
            return outer.has(element) && outer.get(element).get(key) || null;
        }
    };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    retained = Data;
    return Data.get("element", "bs.item").n;
}
var a = probe();

// An unknown call in the skipped found-branch must not disappear with the getter.
//--- class-map-record-nested-nullable-dead-present-call.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function get(element, key) {
        return outer.has(element)
            && (unknownObserver(outer), outer.get(element).get(key)) || null;
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    return (get("missing", "bs.item") === null) * 100 + item.n;
}
var a = probe();

// An unknown call in the skipped fallback branch must not disappear with the getter.
//--- class-map-record-nested-nullable-dead-fallback-call.js
function probe() {
    const outer = new Map(), child = new Map();
    outer.set("element", child);
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key)
            || (unknownObserver(outer), null);
    }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    child.set("bs.item", item);
    return get("element", "bs.item").n;
}
var a = probe();
