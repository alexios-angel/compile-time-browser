// One exact holder slot publishes the completed receiver through its captured Map.
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

// Replacing and deleting the entry does not move either saved alias's concrete owner.
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

// The proof follows the actual literal key and completed numeric field expression.
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

// Returning the holder keeps its helper and captured Map reachable after the frame exits.
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

// A second holder alias remains an independent use even when it republishes the same owner.
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

// A second closure capturing the holder prevents its sole-constructor retirement.
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

// Even an unused second slot is outside the single publication-helper proof.
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

// Sharing the holder between constructor families needs a complete caller-family proof.
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

// A holder call before receiver initialization is not a terminal publication.
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

// A later throw leaves the published receiver reachable without a completed owner.
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

// Expansion must not erase a publication helper's observable receiver.
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

// A later slot replacement changes the exact callable captured by the constructor.
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

// A separate captured Map observer remains outside the exclusive publication proof.
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

// Publishing does not authorize a later helper write to the receiver.
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

// Returning the receiver exceeds the publication helper's undefined-return contract.
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

// A captured alias cell is not visible in the direct holder source-use census.
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

// A second Map cell preserves an observer after the publication helper retires.
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

// This slice requires the own slot to be complete before the constructor captures it.
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
