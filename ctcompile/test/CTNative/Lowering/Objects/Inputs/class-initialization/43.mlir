// Nested unused yields preserve the selected child and skip repeated creation.
//--- class-map-record-nested-shortcircuit-repeat.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    outer.has("element") || (outer.has("element") || outer.set("element", new Map()));
    const child = outer.has("element") && outer.get("element");
    child.set("bs.item", first);
    const savedFirst = child.get("bs.item");
    outer.has("element") || outer.set("element", new Map());
    const sameChild = outer.has("element") && outer.get("element");
    sameChild.set("bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 1000 + sameChild.get("bs.item").n * 100
        + child.get("bs.item").n * 10 + outer.size;
}
var a = probe();

// Result-carrying branches route each literal key to its distinct child owner.
//--- class-map-record-nested-shortcircuit-distinct.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    outer.has("left") || outer.set("left", new Map());
    outer.has("right") || outer.set("right", new Map());
    const left = outer.has("left") && outer.get("left");
    const right = outer.has("right") && outer.get("right");
    left.set("bs.item", first);
    right.set("bs.item", second);
    const savedLeft = left.get("bs.item"), savedRight = right.get("bs.item");
    savedLeft.n = 5;
    savedRight.n = 9;
    return first.n * 1000 + second.n * 100 + savedLeft.n * 10 + savedRight.n + outer.size;
}
var a = probe();

// Cached selected results survive outer cleanup without changing saved owners.
//--- class-map-record-nested-shortcircuit-cached.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(3);
    const initiallyPresent = outer.has("element");
    initiallyPresent || outer.set("element", new Map());
    const present = outer.has("element");
    const savedChild = present && outer.get("element");
    savedChild.set("bs.item", first);
    const savedFirst = savedChild.get("bs.item");
    outer.delete("element");
    const absent = outer.has("element") && outer.get("element");
    absent || outer.set("element", new Map());
    outer.get("element").set("bs.item", second);
    savedFirst.n = 5;
    return savedFirst.n * 1000 + savedChild.size * 100
        + outer.get("element").get("bs.item").n * 10 + (absent === false);
}
var a = probe();

// Caller-controlled truth cannot select child topology at compile time.
//--- class-map-record-nested-shortcircuit-dynamic-branch.js
function probe(flag) {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    outer.has("element") || outer.set("element", new Map());
    flag || outer.set("element", new Map());
    outer.get("element").set("bs.item", item);
    return outer.get("element").get("bs.item").n;
}
var a = probe(true) * 10 + probe(false);

// A selected set result carries the outer identity beyond its owning entry.
//--- class-map-record-nested-shortcircuit-returned-outer.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    const child = new Map();
    child.set("bs.item", item);
    return outer.has("element") || outer.set("element", child);
}
var a = probe().get("element").get("bs.item").n;

// Boxing an outer set result is observable identity transport, not a dead yield.
//--- class-map-record-nested-shortcircuit-boxed-outer.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    const child = new Map();
    child.set("bs.item", item);
    const holder = { value: outer.has("element") || outer.set("element", child) };
    return holder.value.get("element").get("bs.item").n;
}
var a = probe();

// Even a dead set result cannot enter an observed branch-result identity union.
//--- class-map-record-nested-shortcircuit-observed-dead-set.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    outer.has("element") || outer.set("element", new Map());
    outer.get("element").set("bs.item", item);
    const result = outer.has("element") || outer.set("element", new Map());
    return (result === true) * 100 + item.n;
}
var a = probe();

// An unselected registration branch still cannot call user code.
//--- class-map-record-nested-shortcircuit-dead-call.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    function make() { return new Map(); }
    const item = new Item(7);
    outer.has("element") || outer.set("element", new Map());
    outer.get("element").set("bs.item", item);
    outer.has("element") || outer.set("element", make());
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Dead branch coercion cannot disappear before the complete effect census.
//--- class-map-record-nested-shortcircuit-dead-coercion.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    outer.has("element") || outer.set("element", new Map());
    outer.get("element").set("bs.item", item);
    outer.has("element") || outer.set("element", +new Map());
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// A selected missing get does not become a child owner through a branch result.
//--- class-map-record-nested-shortcircuit-missing-outer.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    outer.has("element") || outer.set("element", new Map());
    outer.get("element").set("bs.item", item);
    outer.delete("element");
    const absent = outer.has("element") || outer.get("element");
    return (absent === void 0) * 100 + item.n;
}
var a = probe();

// A present child is not proof that its requested record exists.
//--- class-map-record-nested-shortcircuit-missing-inner.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    outer.has("element") || outer.set("element", new Map());
    const child = outer.has("element") && outer.get("element");
    child.set("bs.item", item);
    const absent = child.get("missing");
    return (absent === void 0) * 100 + item.n;
}
var a = probe();

// Captured observers need their own order proof across conditional registration.
//--- class-map-record-nested-shortcircuit-captured-observer.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    function read() { return outer.get("element").get("bs.item").n; }
    const item = new Item(7);
    outer.has("element") || outer.set("element", new Map());
    outer.get("element").set("bs.item", item);
    return read();
}
var a = probe();

// Flattening a selected arm must not promote a record's region lifetime.
//--- class-map-record-nested-shortcircuit-region-owner.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    outer.has("element") || outer.set("element", new Map());
    outer.has("element") && outer.get("element").set("bs.item", new Item(7));
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// A record reached through a selected child still cannot leave its owner.
//--- class-map-record-nested-shortcircuit-record-escaped.js
var retained;
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    outer.has("element") || outer.set("element", new Map());
    const child = outer.has("element") && outer.get("element");
    child.set("bs.item", item);
    retained = child.get("bs.item");
    return item.n;
}
var a = probe();
