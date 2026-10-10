// The constructor's last statement stores the finished `this` in a captured local Map.
//--- class-map-record-constructor-direct.js
function class_map_record_constructor_direct() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n * 10 + values.size;
}
var a = class_map_record_constructor_direct();

// A second construction overwrites the entry; both get results stay distinct objects.
//--- class-map-record-constructor-overwrite.js
function class_map_record_constructor_overwrite() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    const first = new Item(2);
    const savedFirst = values.get("slot");
    const second = new Item(7);
    const savedSecond = values.get("slot");
    savedFirst.n = 5;
    savedSecond.n = 9;
    return first.n * 1000 + second.n * 100 + savedFirst.n * 10 + savedSecond.n;
}
var a = class_map_record_constructor_overwrite();

// dispose() lists all fields after the constructor has stored `this` in the Map.
//--- class-map-record-constructor-snapshot-dispose.js
function class_map_record_constructor_snapshot_dispose() {
    const values = new Map();
    class Item {
        constructor(n) { this.first = n; this.second = 9; values.set("slot", this); }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return (this.first === null) * 10 + (this.second === null);
        }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    const disposed = saved.dispose();
    return values.size * 1000 + disposed * 100 + (item.first === null) * 10 + (item.second === null);
}
var a = class_map_record_constructor_snapshot_dispose();

// Deleting the entry after construction leaves the get result usable.
//--- class-map-record-constructor-delete.js
function class_map_record_constructor_delete() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    const item = new Item(2);
    const saved = values.get("slot");
    const removed = values.delete("slot");
    saved.n = 5;
    return item.n * 100 + saved.n * 10 + removed * 2 + values.size;
}
var a = class_map_record_constructor_delete();

// Refused: the constructor reads `this` back from the Map before `n` is written.
//--- class-map-record-constructor-observer.js
function class_map_record_constructor_observer() {
    const values = new Map();
    class Item {
        constructor(n) {
            values.set("slot", this);
            const early = values.get("slot");
            this.observed = early.n === void 0;
            this.n = n;
        }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return item.observed * 100 + saved.n;
}
var a = class_map_record_constructor_observer();

// Refused: a throw after storing `this` leaves a half-built object reachable in the Map.
//--- class-map-record-constructor-throw.js
function class_map_record_constructor_throw() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); throw 9; }
    }
    try {
        const item = new Item(7);
        return item.n;
    } catch (error) {
        const saved = values.get("slot");
        return saved.n * 100 + error;
    }
}
var a = class_map_record_constructor_throw();

// Refused: the Map escapes into a global and keeps the instance after the function returns.
//--- class-map-record-constructor-map-escaped.js
var retained;
function class_map_record_constructor_map_escaped() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    const item = new Item(7);
    retained = values;
    return item.n;
}
var a = class_map_record_constructor_map_escaped();
a = retained.get("slot").n * 100 + a;

// The constructor stores `this` through a helper function instead of calling set directly.
//--- class-map-record-constructor-helper.js
function class_map_record_constructor_helper() {
    const values = new Map();
    function publish(value) { values.set("slot", value); }
    class Item {
        constructor(n) { this.n = n; publish(this); }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n;
}
var a = class_map_record_constructor_helper();

// Refused: the constructor returns another object, so `new` differs from the stored `this`.
//--- class-map-record-constructor-return-object.js
function class_map_record_constructor_return_object() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); return {n: 9}; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n;
}
var a = class_map_record_constructor_return_object();

// Base stores `this` as its last statement, before the derived constructor adds a field.
//--- class-map-record-constructor-inherited.js
function class_map_record_constructor_inherited() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = 9; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}
var a = class_map_record_constructor_inherited();

// The constructor stores `this` under a String key passed in as a parameter.
//--- class-map-record-constructor-dynamic-key.js
function class_map_record_constructor_dynamic_key(key) {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set(key, this); }
    }
    const item = new Item(7);
    const saved = values.get(key);
    return saved.n * 100 + item.n;
}
var a = class_map_record_constructor_dynamic_key("slot");
