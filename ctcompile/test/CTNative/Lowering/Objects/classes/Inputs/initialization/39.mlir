// A holder with get/set/remove/count methods; the other methods are inlined around `set`.
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

// Get results keep referring to their own instances after overwrite and delete.
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

// Base stores `this` through the holder before Leaf writes its constant field.
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

// `set` without a key parameter works alongside the called get/remove/count methods.
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

// Refused: `Data.get` is taken as a value, making a second way to call it.
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

// Refused: `Data.get` is reassigned, so it is no longer the holder's original method.
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

// Refused: returning `Data` keeps its methods alive past the function.
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

// Refused: another closure captures `Data`, so the holder cannot be inlined away.
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

// Refused: the constructor reads the stored entry back before it finishes.
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

// Refused: get after delete may return undefined, even though the code checks for it.
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

// Refused: an unused holder method calls an unknown global; every method is checked.
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

// Refused: `Data.get` is added after the class is defined, not in the object literal.
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
