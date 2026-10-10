// The derived constructor stores `this` in the Map after all inherited fields are set.
//--- class-map-inherited-leaf-direct.js
function leaf_direct() {
    const values = new Map();
    class Base { constructor(n) { this.n = n; } }
    class Item extends Base {
        constructor(n) { super(n); this.extra = 9; values.set("slot", this); }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}
var a = leaf_direct();

// After the entry is replaced, each get result still refers to its own instance.
//--- class-map-inherited-leaf-overwrite.js
function leaf_overwrite() {
    const values = new Map();
    class Base { constructor(n) { this.n = n; } }
    class Item extends Base {
        constructor(n) { super(n); this.extra = n + 1; values.set("slot", this); }
    }
    const first = new Item(2);
    const savedFirst = values.get("slot");
    const second = new Item(7);
    const savedSecond = values.get("slot");
    savedFirst.n = 5;
    savedSecond.extra = 9;
    return first.n * 1000 + second.extra * 100 + savedFirst.n * 10 + savedSecond.extra;
}
var a = leaf_overwrite();

// Deleting the Map entry does not destroy the instance the caller holds.
//--- class-map-inherited-leaf-delete.js
function leaf_delete() {
    const values = new Map();
    class Base { constructor(n) { this.n = n; } }
    class Item extends Base {
        constructor(n) { super(n); this.extra = n + 1; values.set("slot", this); }
    }
    const item = new Item(2);
    const saved = values.get("slot");
    const removed = values.delete("slot");
    saved.n = 5;
    item.extra = 4;
    return item.n * 1000 + item.extra * 100 + saved.n * 10 + removed * 2 + values.size;
}
var a = leaf_delete();

// An inherited method called on a get result runs on the same instance.
//--- class-map-inherited-leaf-method.js
function leaf_method() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; }
        bump() { this.n = this.n + 1; return this.n; }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = 9; values.set("slot", this); }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    const result = saved.bump();
    return result * 100 + item.n * 10 + saved.extra;
}
var a = leaf_method();

// Refused: a field write after storing `this` means the store is not the last statement.
//--- class-map-inherited-leaf-write-after.js
function leaf_write_after() {
    const values = new Map();
    class Base { constructor(n) { this.n = n; } }
    class Item extends Base {
        constructor(n) { super(n); values.set("slot", this); this.extra = 9; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}
var a = leaf_write_after();

// Refused: a throw after storing `this` leaves a half-built object visible in the Map.
//--- class-map-inherited-leaf-throw-after.js
function leaf_throw_after() {
    const values = new Map();
    class Base { constructor(n) { this.n = n; } }
    class Item extends Base {
        constructor(n) { super(n); values.set("slot", this); throw 9; }
    }
    try {
        const item = new Item(7);
        return item.n;
    } catch (error) {
        return values.get("slot").n * 100 + error;
    }
}
var a = leaf_throw_after();
