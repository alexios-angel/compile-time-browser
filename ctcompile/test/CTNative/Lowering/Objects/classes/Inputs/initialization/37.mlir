// The constructor's last call, `Data.put(this)`, stores the finished instance in the Map.
//--- class-map-record-constructor-holder-direct.js
function constructor_holder_direct() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    const saved = values.get("item");
    return saved.n * 100 + item.n * 10 + values.size;
}
var a = constructor_holder_direct();

// Overwrite and delete do not change which instance each earlier get result refers to.
//--- class-map-record-constructor-holder-overwrite-delete.js
function constructor_holder_overwrite_delete() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const first = new Item(2);
    const savedFirst = values.get("item");
    const second = new Item(7);
    const savedSecond = values.get("item");
    const removed = values.delete("item");
    savedFirst.n = 5;
    savedSecond.n = 9;
    return first.n * 10000 + second.n * 1000 + savedFirst.n * 100 + savedSecond.n * 10 + removed * 2 + values.size;
}
var a = constructor_holder_overwrite_delete();

// The key and the computed numeric field come from the actual source, not fixed values.
//--- class-map-record-constructor-holder-numeric.js
function constructor_holder_numeric() {
    const values = new Map();
    const Data = { put(value) { values.set("component", value); } };
    class Item {
        constructor(n) { this.n = n + 2; Data.put(this); }
    }
    const item = new Item(5);
    const saved = values.get("component");
    return saved.n * 100 + item.n * 10 + values.size;
}
var a = constructor_holder_numeric();

// Refused: returning `Data` keeps its method and the captured Map alive past the function.
//--- class-map-record-constructor-holder-escaped.js
function constructor_holder_escaped() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    return Data;
}
var holder = constructor_holder_escaped();
var a = typeof holder.put === "function" ? 7 : 0;

// Refused: `alias.put` is a second use of the holder, even though it stores the same object.
//--- class-map-record-constructor-holder-alias.js
function constructor_holder_alias() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    const alias = Data;
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    alias.put(item);
    return values.get("item").n;
}
var a = constructor_holder_alias();

// Refused: a second closure captures `Data`, so it is not used only by the constructor.
//--- class-map-record-constructor-holder-captured-observer.js
function constructor_holder_captured_observer() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    function republish(value) { Data.put(value); }
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    republish(item);
    return values.get("item").n;
}
var a = constructor_holder_captured_observer();

// Refused: the holder has a second method, even unused; only one-method holders handled.
//--- class-map-record-constructor-holder-second-slot.js
function constructor_holder_second_slot() {
    const values = new Map();
    const Data = {
        put(value) { values.set("item", value); },
        count() { return values.size; }
    };
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    return values.get("item").n;
}
var a = constructor_holder_second_slot();

// Refused: two classes share the holder; each caller would need checking.
//--- class-map-record-constructor-holder-second-constructor.js
function constructor_holder_second_constructor() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    class Other {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const first = new Item(2);
    const second = new Other(7);
    return first.n * 100 + values.get("item").n;
}
var a = constructor_holder_second_constructor();

// Refused: `Data.put(this)` runs before the fields are written, not as the last statement.
//--- class-map-record-constructor-holder-early.js
function constructor_holder_early() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    class Item {
        constructor(n) { Data.put(this); this.n = n; }
    }
    const item = new Item(7);
    return values.get("item").n * 100 + item.n;
}
var a = constructor_holder_early();

// Refused: a throw after storing `this` leaves a half-built object in the Map.
//--- class-map-record-constructor-holder-throw-after.js
function constructor_holder_throw_after() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    class Item {
        constructor(n) { this.n = n; Data.put(this); throw 9; }
    }
    try {
        const item = new Item(7);
        return item.n;
    } catch (error) {
        return values.get("item").n * 100 + error;
    }
}
var a = constructor_holder_throw_after();

// Refused: `put` reads `this`, so inlining it must keep the holder object.
//--- class-map-record-constructor-holder-receiver.js
function constructor_holder_receiver() {
    const values = new Map();
    const Data = { put(value) { values.set("item", this && value); } };
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    return values.get("item").n;
}
var a = constructor_holder_receiver();

// Refused: `Data.put` is reassigned, so the constructor calls a different function.
//--- class-map-record-constructor-holder-replaced.js
function constructor_holder_replaced() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    Data.put = function(value) { values.set("other", value); };
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    return values.get("other").n;
}
var a = constructor_holder_replaced();

// Refused: another closure also reads the Map, so the holder is not its only user.
//--- class-map-record-constructor-holder-map-observer.js
function constructor_holder_map_observer() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    function inspect() { return values.get("item").n; }
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    return inspect() * 100 + item.n;
}
var a = constructor_holder_map_observer();

// Refused: `put` also writes a field of the stored object after storing it.
//--- class-map-record-constructor-holder-helper-write.js
function constructor_holder_helper_write() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); value.n = value.n + 1; } };
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    return values.get("item").n * 100 + item.n;
}
var a = constructor_holder_helper_write();

// Refused: `put` returns its argument; only methods returning undefined are handled.
//--- class-map-record-constructor-holder-helper-return.js
function constructor_holder_helper_return() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); return value; } };
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    return values.get("item").n * 100 + item.n;
}
var a = constructor_holder_helper_return();

// Refused: an alias of `Data` used in a closure is missed by the scan of direct uses.
//--- class-map-record-constructor-holder-alias-capture.js
function constructor_holder_alias_capture() {
    const values = new Map();
    const Data = { put(value) { values.set("item", value); } };
    const alias = Data;
    function republish(value) { alias.put(value); }
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    republish(item);
    return values.get("item").n;
}
var a = constructor_holder_alias_capture();

// Refused: a second name for the Map lets a closure read it after `put` is inlined.
//--- class-map-record-constructor-holder-map-alias-capture.js
function constructor_holder_map_alias_capture() {
    const values = new Map();
    const alias = values;
    const Data = { put(value) { values.set("item", value); } };
    function inspect() { return alias.get("item").n; }
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    const item = new Item(7);
    return inspect() * 100 + item.n;
}
var a = constructor_holder_map_alias_capture();

// Refused: `Data.put` is assigned after the class that calls it is defined.
//--- class-map-record-constructor-holder-late-slot.js
function constructor_holder_late_slot() {
    const values = new Map();
    const Data = {};
    class Item {
        constructor(n) { this.n = n; Data.put(this); }
    }
    Data.put = function(value) { values.set("item", value); };
    const item = new Item(7);
    return values.get("item").n;
}
var a = constructor_holder_late_slot();
