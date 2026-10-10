// Local helper functions read and write the captured Map holding a finished instance.
//--- class-map-record-captured-helper-direct.js
function captured_helper_direct() {
    const values = new Map();
    function put(value) { values.set("slot", value); }
    function read() { return values.get("slot"); }
    function count() { return values.size; }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    put(item);
    const saved = read();
    return saved.n * 100 + item.n * 10 + count();
}
var a = captured_helper_direct();

// A captured reader reads `this` after the constructor stored it as its last statement.
//--- class-map-record-captured-helper-constructor.js
function captured_helper_constructor() {
    const values = new Map();
    function read() { return values.get("slot"); }
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    const item = new Item(7);
    const saved = read();
    return saved.n * 100 + item.n * 10 + values.size;
}
var a = captured_helper_constructor();

// Repeated helper calls keep both instances valid through overwrite and delete.
//--- class-map-record-captured-helper-overwrite-delete.js
function captured_helper_overwrite_delete() {
    const values = new Map();
    function put(value) { values.set("slot", value); }
    function read() { return values.get("slot"); }
    function erase() { return values.delete("slot"); }
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    put(first);
    const savedFirst = read();
    put(second);
    const savedSecond = read();
    const removed = erase();
    savedFirst.n = 5;
    savedSecond.n = 9;
    return first.n * 10000 + second.n * 1000 + savedFirst.n * 100 + savedSecond.n * 10 + removed * 2 + values.size;
}
var a = captured_helper_overwrite_delete();

// The same key in two captured Maps names two separate entries.
//--- class-map-record-captured-helper-distinct.js
function captured_helper_distinct() {
    const left = new Map(), right = new Map();
    function putLeft(value) { left.set("slot", value); }
    function putRight(value) { right.set("slot", value); }
    function readLeft() { return left.get("slot"); }
    function readRight() { return right.get("slot"); }
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    putLeft(first);
    putRight(second);
    const savedLeft = readLeft(), savedRight = readRight();
    savedLeft.n = 5;
    return first.n * 1000 + second.n * 100 + savedLeft.n * 10 + savedRight.n;
}
var a = captured_helper_distinct();

// Refused: returning the reader lets the Map and instance outlive this function.
//--- class-map-record-captured-helper-escaped.js
function captured_helper_escaped() {
    const values = new Map();
    function read() { return values.get("slot"); }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    return read;
}
var reader = captured_helper_escaped();
var a = reader().n;

// `read` is called directly and through `holder`; it must not be removed as unused.
//--- class-map-record-captured-helper-alias-observer.js
function captured_helper_alias_observer() {
    const values = new Map();
    function read() { return values.get("slot"); }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    const holder = {read};
    const saved = read();
    return saved.n * 100 + holder.read().n;
}
var a = captured_helper_alias_observer();

// Refused: the helper is called inside an `if`, outside the function's first block.
//--- class-map-record-captured-helper-region.js
function captured_helper_region(flag) {
    const values = new Map();
    function read() { return values.get("slot"); }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    if (flag) return read().n;
    return 0;
}
var a = captured_helper_region(true);

// Refused: a helper's get after delete returns undefined even though the key existed.
//--- class-map-record-captured-helper-read-after-delete.js
function captured_helper_read_after_delete() {
    const values = new Map();
    function read() { return values.get("slot"); }
    function erase() { return values.delete("slot"); }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    erase();
    return (read() === void 0) * 100 + item.n;
}
var a = captured_helper_read_after_delete();

// Refused: the key is a function parameter, even after it is passed into the helper.
//--- class-map-record-captured-helper-dynamic-key.js
function captured_helper_dynamic_key(key) {
    const values = new Map();
    function put(name, value) { values.set(name, value); }
    function read(name) { return values.get(name); }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    put(key, item);
    const saved = read(key);
    return saved.n;
}
var a = captured_helper_dynamic_key("slot");

// Refused: a later `put(3)` stores a Number in the Map that also holds an instance.
//--- class-map-record-captured-helper-mixed-payload.js
function captured_helper_mixed_payload() {
    const values = new Map();
    function put(value) { values.set("slot", value); }
    function read() { return values.get("slot"); }
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    put(item);
    put(3);
    return item.n * 100 + read();
}
var a = captured_helper_mixed_payload();

// Refused: returning the class keeps its helper and the captured Map alive.
//--- class-map-record-captured-helper-returned-class.js
function captured_helper_returned_class() {
    const values = new Map();
    function read() { return values.get("slot"); }
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); }
        inspect() { return read().n; }
    }
    return Item;
}
var Item = captured_helper_returned_class();
var item = new Item(7);
var a = item.inspect();
