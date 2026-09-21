// Every called sibling expands before the existing constructor publication proof.
//--- class-map-record-constructor-multislot-keyed.js
function multi() {
    const values = new Map();
    const Data = {
        get(key) { return values.get(key); },
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; }
    };
    class Item {
        constructor(key, n) { this.n = n; Data.set(key, this); }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const savedFirst = Data.get("first");
    const savedSecond = Data.get("second");
    const removed = Data.remove("first");
    return savedFirst.n * 1000 + savedSecond.n * 100 + removed * 10 + Data.count();
}
var a = multi();

// Saved aliases retain distinct owners across replacement and deletion.
//--- class-map-record-constructor-multislot-overwrite.js
function multi() {
    const values = new Map();
    const Data = {
        get(key) { return values.get(key); },
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; }
    };
    class Item {
        constructor(key, n) { this.n = n; Data.set(key, this); }
    }
    const first = new Item("item", 2);
    const savedFirst = Data.get("item");
    const second = new Item("item", 7);
    const savedSecond = Data.get("item");
    const removed = Data.remove("item");
    savedFirst.n = 5;
    savedSecond.n = 9;
    return first.n * 10000 + second.n * 1000 + savedFirst.n * 100 + savedSecond.n * 10 + removed * 2 + Data.count();
}
var a = multi();

// One base publishes through the holder before literal leaf initialization.
//--- class-map-inherited-multislot-holder.js
function multi() {
    const values = new Map();
    const Data = {
        get(key) { return values.get(key); },
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; }
    };
    class Base {
        constructor(key, n) { this.n = n; Data.set(key, this); }
    }
    class Item extends Base {
        constructor(key, n) { super(key, n); this.extra = 3; }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const savedFirst = Data.get("first");
    const savedSecond = Data.get("second");
    const removed = Data.remove("first");
    return savedFirst.n * 1000 + savedSecond.n * 100 + first.extra * 100 + second.extra * 10 + removed + Data.count();
}
var a = multi();

// Unkeyed pure registration composes with called getter/removal/count siblings.
//--- class-map-record-constructor-multislot-literal.js
function multi() {
    const values = new Map();
    const Data = {
        get() { return values.get("item"); },
        set(value) { values.set("item", value); },
        remove() { return values.delete("item"); },
        count() { return values.size; }
    };
    class Item {
        constructor(n) { this.n = n; Data.set(this); }
    }
    const item = new Item(7);
    const saved = Data.get();
    const removed = Data.remove();
    return saved.n * 100 + item.n * 10 + removed + Data.count();
}
var a = multi();

// An extracted method remains an observable callable identity.
//--- class-map-record-constructor-multislot-extracted.js
function multi() {
    const values = new Map();
    const Data = {
        get(key) { return values.get(key); },
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; }
    };
    class Item {
        constructor(key, n) { this.n = n; Data.set(key, this); }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const getter = Data.get;
    const savedFirst = getter("first");
    const savedSecond = Data.get("second");
    const removed = Data.remove("first");
    return savedFirst.n * 1000 + savedSecond.n * 100 + removed * 10 + Data.count();
}
var a = multi();

// A later replacement invalidates the unique own slot.
//--- class-map-record-constructor-multislot-replaced.js
function multi() {
    const values = new Map();
    const Data = {
        get(key) { return values.get(key); },
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; }
    };
    class Item {
        constructor(key, n) { this.n = n; Data.set(key, this); }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    Data.get = function(key) { return values.get(key); };
    const savedFirst = Data.get("first");
    const savedSecond = Data.get("second");
    const removed = Data.remove("first");
    return savedFirst.n * 1000 + savedSecond.n * 100 + removed * 10 + Data.count();
}
var a = multi();

// A returned holder keeps its closures alive beyond the owner frame.
//--- class-map-record-constructor-multislot-escaped.js
function multi() {
    const values = new Map();
    const Data = {
        get(key) { return values.get(key); },
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; }
    };
    class Item {
        constructor(key, n) { this.n = n; Data.set(key, this); }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const savedFirst = Data.get("first");
    const savedSecond = Data.get("second");
    const removed = Data.remove("first");
    return Data;
}
var saved = multi();
var a = saved.count();

// An additional nonconstructor holder capture is not projected away.
//--- class-map-record-constructor-multislot-captured-observer.js
function multi() {
    const values = new Map();
    const Data = {
        get(key) { return values.get(key); },
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; }
    };
    function inspect(key) { return Data.get(key); }
    class Item {
        constructor(key, n) { this.n = n; Data.set(key, this); }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const savedFirst = inspect("first");
    const savedSecond = Data.get("second");
    const removed = Data.remove("first");
    return savedFirst.n * 1000 + savedSecond.n * 100 + removed * 10 + Data.count();
}
var a = multi();

// A constructor-time reader observes registration before completion.
//--- class-map-record-constructor-multislot-constructor-observer.js
function multi() {
    const values = new Map();
    const Data = {
        get(key) { return values.get(key); },
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; }
    };
    class Item {
        constructor(key, n) { this.n = n; Data.set(key, this); this.n = Data.get(key).n + 1; }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const savedFirst = Data.get("first");
    const savedSecond = Data.get("second");
    const removed = Data.remove("first");
    return savedFirst.n * 1000 + savedSecond.n * 100 + removed * 10 + Data.count();
}
var a = multi();

// A post-delete read needs a nullable record proof, even if guarded.
//--- class-map-record-constructor-multislot-missing.js
function multi() {
    const values = new Map();
    const Data = {
        get(key) { return values.get(key); },
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; }
    };
    class Item {
        constructor(key, n) { this.n = n; Data.set(key, this); }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const savedFirst = Data.get("first");
    const savedSecond = Data.get("second");
    const removed = Data.remove("first");
    const missing = Data.get("first");
    return missing === undefined ? savedFirst.n * 100 + Data.count() : 0;
}
var a = multi();

// Every unused slot body remains in the complete source census.
//--- class-map-record-constructor-multislot-unused-effect.js
function multi() {
    const values = new Map();
    const Data = {
        get(key) { return values.get(key); },
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; },
        unused() { return unknownEffect(); }
    };
    class Item {
        constructor(key, n) { this.n = n; Data.set(key, this); }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const savedFirst = Data.get("first");
    const savedSecond = Data.get("second");
    const removed = Data.remove("first");
    return savedFirst.n * 1000 + savedSecond.n * 100 + removed * 10 + Data.count();
}
var a = multi();

// A slot installed after constructor creation stays outside the ordered holder proof.
//--- class-map-record-constructor-multislot-late-slot.js
function multi() {
    const values = new Map();
    const Data = {
        set(key, value) { values.set(key, value); },
        remove(key) { return values.delete(key); },
        count() { return values.size; }
    };
    class Item {
        constructor(key, n) { this.n = n; Data.set(key, this); }
    }
    Data.get = function(key) { return values.get(key); };
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const savedFirst = Data.get("first");
    const savedSecond = Data.get("second");
    const removed = Data.remove("first");
    return savedFirst.n * 1000 + savedSecond.n * 100 + removed * 10 + Data.count();
}
var a = multi();
