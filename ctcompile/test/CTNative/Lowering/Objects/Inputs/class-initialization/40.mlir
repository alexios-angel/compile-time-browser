// Exact captured String keys keep the original record owner and call order.
//--- class-map-record-constructor-captured-key-literal.js
function probe() {
    const key = "slot";
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = probe();

//--- class-map-record-constructor-captured-key-repeated.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = probe("slot") * 10 + probe("slot");

//--- class-map-record-constructor-captured-key-reversed.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { const savedKey = key; this.n = n; values.set(savedKey, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = probe("slot");

//--- class-map-record-constructor-captured-key-overwrite-delete.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const first = new Item(2);
    const savedFirst = values.get(key);
    const second = new Item(7);
    const savedSecond = values.get(key);
    const removed = values.delete(key);
    savedFirst.n = 5;
    savedSecond.n = 9;
    return first.n * 1000 + second.n * 100 + savedFirst.n * 10 + savedSecond.n + removed;
}
var a = probe("slot");

//--- class-map-record-constructor-captured-key-different.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = probe("first") * 10 + probe("second");

//--- class-map-record-constructor-captured-key-missing.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = probe();

//--- class-map-record-constructor-captured-key-number.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = probe(7);

//--- class-map-record-constructor-captured-key-object.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = probe({name: "slot"});

//--- class-map-record-constructor-captured-key-computed.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
function makeKey() { return "slot"; }
var a = probe(makeKey());

//--- class-map-record-constructor-captured-key-escaped.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var retained = probe;
var a = probe("slot");

//--- class-map-record-constructor-captured-key-changing.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    key = "other";
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = probe("slot");

//--- class-map-record-constructor-captured-key-ambient.js
function probe(key) {
    if (false) unknown();
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = probe("slot");

//--- class-map-record-constructor-captured-key-after-publication.js
function probe(key) {
    const values = new Map();
    class Item {
        constructor(n) { values.set(key, this); this.n = n; }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = probe("slot");
