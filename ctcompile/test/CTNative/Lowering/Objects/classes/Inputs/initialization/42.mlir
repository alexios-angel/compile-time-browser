// A repeated `if (!outer.has(...))` keeps the first inner Map and skips the second `new Map`.
//--- class-map-record-nested-conditional-repeat.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", first);
        outer.set("element", values);
    }
    const saved = outer.get("element").get("bs.item");
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", second);
        outer.set("element", values);
    }
    saved.n = 5;
    return saved.n * 100 + outer.get("element").get("bs.item").n * 10 + outer.size;
}
var a = probe();

// Each branch known to run creates a separate inner Map for its literal outer key.
//--- class-map-record-nested-conditional-distinct.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    if (outer.size === 0) {
        if (outer.has("left")) {
            outer.clear();
        } else {
            const left = new Map();
            left.set("bs.item", first);
            outer.set("left", left);
        }
    }
    if (!outer.has("right")) {
        const right = new Map();
        right.set("bs.item", second);
        outer.set("right", right);
    }
    const savedLeft = outer.get("left").get("bs.item");
    const savedRight = outer.get("right").get("bs.item");
    savedLeft.n = 5;
    savedRight.n = 9;
    return first.n * 1000 + second.n * 100 + savedLeft.n * 10 + savedRight.n + outer.size;
}
var a = probe();

// Refused: a branch known never to run still contains `+values`, which may call user code.
//--- class-map-record-nested-conditional-dead-coercion.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", item);
        outer.set("element", values);
    }
    if (!outer.has("element")) {
        const values = new Map();
        +values;
        outer.set("element", values);
    }
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Clearing and recreating outer entries does not invalidate saved inner Maps or instances.
//--- class-map-record-nested-conditional-recreate.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7), third = new Item(3);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", first);
        outer.set("element", values);
    }
    const savedChild = outer.get("element");
    const savedFirst = savedChild.get("bs.item");
    if (outer.delete("element")) {
        const values = new Map();
        values.set("bs.item", second);
        outer.set("element", values);
    }
    const savedSecond = outer.get("element").get("bs.item");
    outer.clear();
    if (outer.size === 0) {
        const values = new Map();
        values.set("bs.item", third);
        outer.set("element", values);
    }
    savedFirst.n = 5;
    savedSecond.n = 9;
    savedChild.delete("bs.item");
    return savedFirst.n * 1000 + savedSecond.n * 100
        + outer.get("element").get("bs.item").n * 10 + savedChild.size + outer.size;
}
var a = probe();

// Refused: a later branch depends on a caller's flag, so its outer entry is not known.
//--- class-map-record-nested-conditional-dynamic-branch.js
function probe(flag) {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(7), second = new Item(9);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", first);
        outer.set("element", values);
    }
    if (flag) {
        const values = new Map();
        values.set("bs.item", second);
        outer.set("element", values);
    }
    return outer.get("element").get("bs.item").n;
}
var a = probe(true) * 10 + probe(false);

// Refused: the outer Map escapes with an inner Map created inside a branch.
//--- class-map-record-nested-conditional-outer-escaped.js
var retained;
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", item);
        outer.set("element", values);
    }
    retained = outer;
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Refused: a closure reading the outer Map needs its calls and their order checked.
//--- class-map-record-nested-conditional-captured-observer.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    function read() { return outer.get("element").get("bs.item").n; }
    const item = new Item(7);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", item);
        outer.set("element", values);
    }
    return read();
}
var a = probe();

// Refused: the outer key is a parameter, even inside a `has` check.
//--- class-map-record-nested-conditional-dynamic-key.js
function probe(key) {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    if (!outer.has(key)) {
        const values = new Map();
        values.set("bs.item", item);
        outer.set(key, values);
    }
    return outer.get(key).get("bs.item").n;
}
var a = probe("element");

// Refused: get after delete returns undefined; it must not produce an inner Map.
//--- class-map-record-nested-conditional-missing-outer.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", item);
        outer.set("element", values);
    }
    outer.delete("element");
    const absent = outer.get("element");
    return (absent === void 0) * 100 + item.n;
}
var a = probe();

// Refused: the inner Map exists, but the inner key does not.
//--- class-map-record-nested-conditional-missing-inner.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", item);
        outer.set("element", values);
    }
    const absent = outer.get("element").get("missing");
    return (absent === void 0) * 100 + item.n;
}
var a = probe();

// Refused: an inner Map escapes into a global; folding the outer Map must not hide it.
//--- class-map-record-nested-conditional-child-escaped.js
var retained;
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", item);
        outer.set("element", values);
    }
    retained = outer.get("element");
    return item.n;
}
var a = probe();

// Refused: an instance read through branch-created inner Maps escapes its function.
//--- class-map-record-nested-conditional-record-escaped.js
var retained;
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", item);
        outer.set("element", values);
    }
    retained = outer.get("element").get("bs.item");
    return item.n;
}
var a = probe();

// Refused: a throw inside the known branch leaves through the catch handler.
//--- class-map-record-nested-conditional-exception.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    try {
        if (!outer.has("element")) {
            const values = new Map();
            values.set("bs.item", item);
            outer.set("element", values);
            throw 1;
        }
    } catch (error) {
        item.n += error;
    }
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Refused: a branch known never to run still stores a Map into a global.
//--- class-map-record-nested-conditional-dead-escape.js
var retained;
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    if (!outer.has("element")) {
        const values = new Map();
        values.set("bs.item", item);
        outer.set("element", values);
    }
    if (!outer.has("element")) {
        const values = new Map();
        retained = values;
        outer.set("element", values);
    }
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Refused: the instance is created inside the branch, so it cannot outlive the branch.
//--- class-map-record-nested-conditional-region-owner.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    if (!outer.has("element")) {
        const values = new Map();
        const item = new Item(7);
        values.set("bs.item", item);
        outer.set("element", values);
    }
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Refused: `register` calls itself, so the nested Maps are not built in one call.
//--- class-map-record-nested-conditional-reentry.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    function register() {
        if (!outer.has("element")) {
            const values = new Map();
            outer.set("element", values);
            register();
            values.set("bs.item", item);
        }
    }
    register();
    return outer.get("element").get("bs.item").n;
}
var a = probe();
