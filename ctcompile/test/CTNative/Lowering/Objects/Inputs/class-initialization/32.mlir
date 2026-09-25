// Both construction sites publish through the same helper and retain distinct owners.
//--- class-map-record-constructor-helper-overwrite.js
function helper_overwrite() {
    const values = new Map();
    function publish(value) { values.set("slot", value); }
    class Item {
        constructor(n) { this.n = n; publish(this); }
    }
    const first = new Item(2);
    const savedFirst = values.get("slot");
    const second = new Item(7);
    const savedSecond = values.get("slot");
    savedFirst.n = 5;
    savedSecond.n = 9;
    return first.n * 1000 + second.n * 100 + savedFirst.n * 10 + savedSecond.n;
}
var a = helper_overwrite();

// Removing the entry keeps the saved alias borrowed from the caller-owned record.
//--- class-map-record-constructor-helper-delete.js
function helper_delete() {
    const values = new Map();
    function publish(value) { values.set("slot", value); }
    class Item {
        constructor(n) { this.n = n; publish(this); }
    }
    const item = new Item(2);
    const saved = values.get("slot");
    const removed = values.delete("slot");
    saved.n = 5;
    return item.n * 100 + saved.n * 10 + removed * 2 + values.size;
}
var a = helper_delete();

// A helper writing the published receiver is not an effect-free terminal publication.
//--- class-map-record-constructor-helper-write-after.js
function helper_write_after() {
    const values = new Map();
    function publish(value) { values.set("slot", value); value.n = value.n + 1; }
    class Item {
        constructor(n) { this.n = n; publish(this); }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n;
}
var a = helper_write_after();

// Reading the published entry inside the helper requires an observer proof.
//--- class-map-record-constructor-helper-observer.js
function helper_observer() {
    const values = new Map();
    function publish(value) {
        values.set("slot", value);
        value.observed = values.get("slot").n;
    }
    class Item {
        constructor(n) { this.n = n; this.observed = 0; publish(this); }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.observed * 100 + item.n;
}
var a = helper_observer();

// Returning the receiver exceeds the helper's exact undefined-return contract.
//--- class-map-record-constructor-helper-return-value.js
function helper_return_value() {
    const values = new Map();
    function publish(value) { values.set("slot", value); return value; }
    class Item {
        constructor(n) { this.n = n; publish(this); }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n;
}
var a = helper_return_value();

// A second closure capturing the Map is outside the exclusive observer proof.
//--- class-map-record-constructor-helper-other-observer.js
function helper_other_observer() {
    const values = new Map();
    function publish(value) { values.set("slot", value); }
    function observe() { return values.get("slot").n; }
    class Item {
        constructor(n) { this.n = n; publish(this); }
    }
    const item = new Item(7);
    return observe() * 100 + item.n;
}
var a = helper_other_observer();

// Sharing the helper with another constructor requires a complete caller-family proof.
//--- class-map-record-constructor-helper-shared-callers.js
function helper_shared_callers() {
    const values = new Map();
    function publish(value) { values.set("slot", value); }
    class Item {
        constructor(n) { this.n = n; publish(this); }
    }
    class Other {
        constructor(n) { this.n = n; publish(this); }
    }
    const first = new Item(2);
    const second = new Other(7);
    const saved = values.get("slot");
    return first.n * 100 + saved.n;
}
var a = helper_shared_callers();

// Escaping the Map retains its published receiver past the caller-owned record's lifetime.
//--- class-map-record-constructor-helper-map-escaped.js
var retained;
function helper_map_escaped() {
    const values = new Map();
    function publish(value) { values.set("slot", value); }
    class Item {
        constructor(n) { this.n = n; publish(this); }
    }
    const item = new Item(7);
    retained = values;
    return item.n;
}
var a = helper_map_escaped();
a = retained.get("slot").n * 100 + a;

// A pure helper does not make a subsequent constructor field write terminal.
//--- class-map-record-constructor-helper-constructor-write.js
function helper_constructor_write() {
    const values = new Map();
    function publish(value) { values.set("slot", value); }
    class Item {
        constructor(n) { this.n = n; publish(this); this.n = n + 1; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n;
}
var a = helper_constructor_write();

// Throwing after the helper leaves a reachable receiver without a completed owner.
//--- class-map-record-constructor-helper-constructor-throw.js
function helper_constructor_throw() {
    const values = new Map();
    function publish(value) { values.set("slot", value); }
    class Item {
        constructor(n) { this.n = n; publish(this); throw 9; }
    }
    try {
        const item = new Item(7);
        return item.n;
    } catch (error) {
        return values.get("slot").n * 100 + error;
    }
}
var a = helper_constructor_throw();

// Recursive publication is not an exact single-call helper even when this input terminates.
//--- class-map-record-constructor-helper-recursive.js
function helper_recursive() {
    const values = new Map();
    function publish(value) {
        values.set("slot", value);
        if (value.n < 2) { value.n = value.n + 1; publish(value); }
    }
    class Item {
        constructor(n) { this.n = n; publish(this); }
    }
    const item = new Item(1);
    const saved = values.get("slot");
    return saved.n * 100 + item.n;
}
var a = helper_recursive();

// An alias captured by another closure keeps the helper reachable through its cell.
//--- class-map-record-constructor-helper-alias-cell.js
function helper_alias_cell() {
    const values = new Map();
    function publish(value) { values.set("slot", value); }
    const alias = publish;
    function republish(value) { alias(value); }
    class Item {
        constructor(n) { this.n = n; publish(this); }
    }
    const item = new Item(7);
    republish(item);
    const saved = values.get("slot");
    return saved.n * 100 + item.n;
}
var a = helper_alias_cell();
