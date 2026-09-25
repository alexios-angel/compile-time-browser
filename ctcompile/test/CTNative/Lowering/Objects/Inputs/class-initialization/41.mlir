// Finite entry-block outer Maps route literal keys to existing child Map owners.
//--- class-map-record-nested-direct.js
function probe() {
    const outer = new Map(), values = new Map();
    const alias = outer;
    class Item { constructor(n) { this.n = n; values.set("bs.item", this); } }
    const item = new Item(7);
    alias.set("element", values);
    const size = alias.size;
    const present = alias.has("element");
    const saved = alias.get("element").get("bs.item");
    outer.clear();
    return saved.n * 100 + present * 10 + size + alias.size;
}
var a = probe();

// Distinct child Maps keep the same inner key bound to distinct record owners.
//--- class-map-record-nested-distinct.js
function probe() {
    const outer = new Map(), left = new Map(), right = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    left.set("bs.item", first);
    right.set("bs.item", second);
    outer.set("left", left);
    outer.set("right", right);
    const savedLeft = outer.get("left").get("bs.item");
    const savedRight = outer.get("right").get("bs.item");
    savedLeft.n = 5;
    savedRight.n = 9;
    return first.n * 1000 + second.n * 100 + savedLeft.n * 10 + savedRight.n + outer.size;
}
var a = probe();

// Parent and child replacement/deletion never end saved child or record lifetimes.
//--- class-map-record-nested-overwrite-delete.js
function probe() {
    const outer = new Map(), left = new Map(), right = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    left.set("bs.item", first);
    right.set("bs.item", second);
    outer.set("element", left);
    const savedChild = outer.get("element");
    const savedFirst = savedChild.get("bs.item");
    outer.set("element", right);
    const savedSecond = outer.get("element").get("bs.item");
    savedChild.set("bs.item", second);
    const childRemoved = savedChild.delete("bs.item");
    const parentRemoved = outer.delete("element");
    const absent = outer.has("element");
    outer.set("element", savedChild);
    outer.clear();
    savedFirst.n = 5;
    savedSecond.n = 9;
    return first.n * 10000 + second.n * 1000 + savedFirst.n * 100 + savedSecond.n * 10
        + childRemoved * 4 + parentRemoved * 2 + outer.size + absent;
}
var a = probe();

// The parent cannot escape with borrows of entry-local child Maps.
//--- class-map-record-nested-outer-escaped.js
var retained;
function probe() {
    const outer = new Map(), values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("bs.item", item);
    outer.set("element", values);
    retained = outer;
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Folding a parent get cannot hide a child Map published beyond its owner frame.
//--- class-map-record-nested-child-escaped.js
var retained;
function probe() {
    const outer = new Map(), values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("bs.item", item);
    outer.set("element", values);
    retained = outer.get("element");
    return item.n;
}
var a = probe();

// A record obtained through both keys still cannot outlive its entry-frame owner.
//--- class-map-record-nested-record-escaped.js
var retained;
function probe() {
    const outer = new Map(), values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("bs.item", item);
    outer.set("element", values);
    retained = outer.get("element").get("bs.item");
    return item.n;
}
var a = probe();

// Absent parent keys need nullable child-map ownership beyond exact routing.
//--- class-map-record-nested-missing-outer.js
function probe() {
    const outer = new Map(), values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("bs.item", item);
    outer.set("element", values);
    const absent = outer.get("missing");
    return (absent === void 0) * 100 + item.n;
}
var a = probe();

// A present child does not prove the requested inner record exists.
//--- class-map-record-nested-missing-inner.js
function probe() {
    const outer = new Map(), values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("bs.item", item);
    outer.set("element", values);
    const absent = outer.get("element").get("missing");
    return (absent === void 0) * 100 + item.n;
}
var a = probe();

// Conditional child allocation remains the next Bootstrap Data boundary.
//--- class-map-record-nested-branch-child.js
function probe(flag) {
    const outer = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    if (flag) {
        const values = new Map();
        values.set("bs.item", item);
        outer.set("element", values);
    }
    return outer.get("element").get("bs.item").n;
}
var a = probe(true);

// Capturing the parent requires a complete observer and call-order proof.
//--- class-map-record-nested-captured-observer.js
function probe() {
    const outer = new Map(), values = new Map();
    class Item { constructor(n) { this.n = n; } }
    function read() { return outer.get("element").get("bs.item").n; }
    const item = new Item(7);
    values.set("bs.item", item);
    outer.set("element", values);
    return read();
}
var a = probe();

// An arbitrary caller key is not a literal outer topology edge.
//--- class-map-record-nested-dynamic-outer-key.js
function probe(key) {
    const outer = new Map(), values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("bs.item", item);
    outer.set(key, values);
    return outer.get(key).get("bs.item").n;
}
var a = probe("element");
