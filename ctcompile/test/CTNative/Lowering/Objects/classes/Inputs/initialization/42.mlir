// Repeated registration must preserve the first child and skip the second allocation.
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

// Each selected branch creates a distinct child for its literal outer key.
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

// Dead branches still need a non-coercing complete effect census.
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

// Parent cleanup and recreation do not invalidate saved child or record owners.
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

// A known first branch must not hide later topology controlled by a caller.
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

// A conditional child cannot leave its owning entry through the outer Map.
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

// Captured outer observers require their own complete call-order proof.
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

// Caller-controlled keys do not provide literal topology, even in a has guard.
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

// Cleanup cannot turn an absent outer get into an invented child owner.
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

// Selecting a child allocation does not establish an absent inner record.
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

// Erasing the outer topology must not conceal a child escape.
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

// A record reached through conditional children still belongs to its entry.
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

// A known guard cannot bypass exception and cleanup control flow.
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

// Even an unselected branch must pass the original complete effect census.
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

// Selecting Map-only arms cannot move a record allocation out of its own region.
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

// Recursive registration remains outside the same-entry topology proof.
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
