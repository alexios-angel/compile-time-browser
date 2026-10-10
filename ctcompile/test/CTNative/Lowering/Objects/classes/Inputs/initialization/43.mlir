// Nested `has(...) || set(...)` with unused results keeps the first inner Map; no re-create.
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

// `has(k) && get(k)` results give each literal outer key its own inner Map.
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

// `has`/`get` results saved in variables survive deleting the outer entry.
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

// Refused: `flag || set(...)` depends on a caller's flag, unknown at compile time.
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

// Refused: returning the `set` result returns the outer Map past this function.
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

// Refused: storing the `set` result in an object keeps the outer Map, so it is used.
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

// Refused: a never-taken `set` is still a possible value of the used `||` result.
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

// Refused: a never-taken `||` branch still calls the user function `make()`.
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

// Refused: a never-taken `||` branch still contains `+new Map()`, a conversion.
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

// Refused: get after delete returns undefined; the `||` must not produce an inner Map.
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

// Refused: the inner Map exists, but the inner key does not.
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

// Refused: a closure reading the outer Map needs its calls and their order checked.
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

// Refused: the instance is created inside a `&&` branch, so it cannot outlive it.
//--- class-map-record-nested-shortcircuit-region-owner.js
function probe() {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    outer.has("element") || outer.set("element", new Map());
    outer.has("element") && outer.get("element").set("bs.item", new Item(7));
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Refused: an instance read through an `&&`-selected inner Map escapes into a global.
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
