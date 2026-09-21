// Each exact holder method borrows its captured Map in the record owner's entry block.
//--- class-map-record-captured-holder-direct.js
function captured_holder_direct() {
    const values = new Map();
    const data = {
        put(value) { values.set("slot", value); },
        read() { return values.get("slot"); },
        count() { return values.size; }
    };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    data.put(item);
    const saved = data.read();
    return saved.n * 100 + item.n * 10 + data.count();
}
var a = captured_holder_direct();

// Saved aliases still belong to their original records after replacement and deletion.
//--- class-map-record-captured-holder-overwrite-delete.js
function captured_holder_overwrite_delete() {
    const values = new Map();
    const data = {
        put(value) { values.set("slot", value); },
        read() { return values.get("slot"); },
        erase() { return values.delete("slot"); }
    };
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    data.put(first);
    const savedFirst = data.read();
    data.put(second);
    const savedSecond = data.read();
    const removed = data.erase();
    savedFirst.n = 5;
    savedSecond.n = 9;
    return first.n * 10000 + second.n * 1000 + savedFirst.n * 100 + savedSecond.n * 10 + removed * 2 + values.size;
}
var a = captured_holder_overwrite_delete();

// Different methods of one holder may capture distinct Map allocations.
//--- class-map-record-captured-holder-distinct.js
function captured_holder_distinct() {
    const left = new Map(), right = new Map();
    const data = {
        putLeft(value) { left.set("slot", value); },
        putRight(value) { right.set("slot", value); },
        readLeft() { return left.get("slot"); },
        readRight() { return right.get("slot"); }
    };
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    data.putLeft(first);
    data.putRight(second);
    const savedLeft = data.readLeft(), savedRight = data.readRight();
    savedLeft.n = 5;
    return first.n * 1000 + second.n * 100 + savedLeft.n * 10 + savedRight.n;
}
var a = captured_holder_distinct();

// The holder can read the receiver only after terminal constructor publication completes.
//--- class-map-record-captured-holder-constructor.js
function captured_holder_constructor() {
    const values = new Map();
    const data = { read() { return values.get("slot"); } };
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    const item = new Item(7);
    const saved = data.read();
    return saved.n * 100 + item.n * 10 + values.size;
}
var a = captured_holder_constructor();

// An escaped holder retains both the captured Map and its borrowed record beyond this frame.
//--- class-map-record-captured-holder-escaped.js
function captured_holder_escaped() {
    const values = new Map();
    const data = { read() { return values.get("slot"); } };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    return data;
}
var holder = captured_holder_escaped();
var a = holder.read().n;

// A later slot assignment defeats the holder's exact method identity.
//--- class-map-record-captured-holder-replaced.js
function captured_holder_replaced() {
    const values = new Map();
    const data = { read() { return values.get("slot"); } };
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(7), second = new Item(9);
    values.set("slot", first);
    values.set("other", second);
    data.read = function () { return values.get("other"); };
    return data.read().n;
}
var a = captured_holder_replaced();

// Extracting a method is a separate callable observer, even without receiver use.
//--- class-map-record-captured-holder-extracted.js
function captured_holder_extracted() {
    const values = new Map();
    const data = { read() { return values.get("slot"); } };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    const read = data.read;
    return read().n;
}
var a = captured_holder_extracted();

// Native expansion must not discard the observable method receiver.
//--- class-map-record-captured-holder-receiver.js
function captured_holder_receiver() {
    const values = new Map();
    const data = { read() { return this && values.get("slot"); } };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    return data.read().n;
}
var a = captured_holder_receiver();

// Constructor-time holder calls cannot publish a receiver before its own writes complete.
//--- class-map-record-captured-holder-early-publication.js
function captured_holder_early_publication() {
    const values = new Map();
    const data = {
        put(value) { values.set("slot", value); },
        read() { return values.get("slot"); }
    };
    class Item { constructor(n) { data.put(this); this.n = n; } }
    const item = new Item(7);
    return data.read().n * 100 + item.n;
}
var a = captured_holder_early_publication();

// Region calls require control-flow and membership proofs beyond the entry block.
//--- class-map-record-captured-holder-region.js
function captured_holder_region(flag) {
    const values = new Map();
    const data = { read() { return values.get("slot"); } };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    if (flag) return data.read().n;
    return 0;
}
var a = captured_holder_region(true);

// The direct call does not authorize retirement of a separately observed holder alias.
//--- class-map-record-captured-holder-alias-observer.js
function captured_holder_alias_observer() {
    const values = new Map();
    const data = { read() { return values.get("slot"); } };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    const alias = data;
    function inspect() { return alias.read().n; }
    const saved = data.read();
    return saved.n * 100 + inspect();
}
var a = captured_holder_alias_observer();

// Expansion cannot restore Map membership after deletion.
//--- class-map-record-captured-holder-read-after-delete.js
function captured_holder_read_after_delete() {
    const values = new Map();
    const data = {
        read() { return values.get("slot"); },
        erase() { return values.delete("slot"); }
    };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    data.erase();
    return (data.read() === void 0) * 100 + item.n;
}
var a = captured_holder_read_after_delete();

// A used slot does not license discarding another slot with an unproved ambient effect.
//--- class-map-record-captured-holder-unused-ambient.js
function captured_holder_unused_ambient() {
    const values = new Map();
    const data = {
        read() { return values.get("slot"); },
        ambient() { return unknown(values); }
    };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    return data.read().n;
}
var a = captured_holder_unused_ambient();

// Every unused slot remains part of the holder census, including a receiver observer.
//--- class-map-record-captured-holder-unused-receiver.js
function captured_holder_unused_receiver() {
    const values = new Map();
    const data = {
        read() { return values.get("slot"); },
        owner() { return this; }
    };
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    return data.read().n;
}
var a = captured_holder_unused_receiver();
