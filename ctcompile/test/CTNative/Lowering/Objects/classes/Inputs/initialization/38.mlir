// Every `new Item` passes a string-literal key on to the holder's only method, `put`.
//--- class-map-record-constructor-keyed-holder.js
function keyed_holder() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); } };
    class Item {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const savedFirst = values.get("first");
    const savedSecond = values.get("second");
    return savedFirst.n * 1000 + savedSecond.n * 100 + first.n * 10 + second.n;
}
var a = keyed_holder();

// Overwrite and delete do not change which instance each earlier get result refers to.
//--- class-map-record-constructor-keyed-overwrite-delete.js
function keyed_overwrite_delete() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); } };
    class Item {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    const first = new Item("item", 2);
    const savedFirst = values.get("item");
    const second = new Item("item", 7);
    const savedSecond = values.get("item");
    const removed = values.delete("item");
    savedFirst.n = 5;
    savedSecond.n = 9;
    return first.n * 10000 + second.n * 1000 + savedFirst.n * 100 + savedSecond.n * 10 + removed * 2 + values.size;
}
var a = keyed_overwrite_delete();

// The key may sit at a different argument position in the constructor than in the helper.
//--- class-map-record-constructor-keyed-helper.js
function keyed_helper() {
    const values = new Map();
    function put(key, value) { values.set(key, value); }
    class Item {
        constructor(n, key) { this.n = n; put(key, this); }
    }
    const first = new Item(2, "first");
    const second = new Item(7, "second");
    return values.get("first").n * 100 + values.get("second").n * 10 + values.size;
}
var a = keyed_helper();

// A single subclass passes each key through `super` before writing constant fields.
//--- class-map-inherited-keyed-holder.js
function inherited_keyed_holder() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); } };
    class Base {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    class Item extends Base {
        constructor(key, n) { super(key, n); this.extra = 3; }
    }
    const first = new Item("first", 2);
    const second = new Item("second", 7);
    const savedFirst = values.get("first");
    const savedSecond = values.get("second");
    return savedFirst.n * 1000 + savedSecond.n * 100 + first.extra * 10 + second.extra;
}
var a = inherited_keyed_holder();

// Refused: a later `new Item(4, 7)` passes a Number key after a valid String one.
//--- class-map-record-constructor-keyed-later-number.js
function keyed_later_number() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); } };
    class Item {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    const first = new Item("item", 2);
    const second = new Item(4, 7);
    return values.get("item").n * 100 + values.get(4).n;
}
var a = keyed_later_number();

// Refused: the second `new Item` omits the key, so it is undefined.
//--- class-map-record-constructor-keyed-missing.js
function keyed_missing() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); } };
    class Item {
        constructor(n, key) { this.n = n; Data.put(key, this); }
    }
    const first = new Item(2, "item");
    const second = new Item(7);
    return values.get("item").n * 100 + values.get(undefined).n;
}
var a = keyed_missing();

// Refused: the key `"i" + "tem"` is computed, not a string literal.
//--- class-map-record-constructor-keyed-computed.js
function keyed_computed() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); } };
    class Item {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    const item = new Item("i" + "tem", 7);
    return values.get("item").n * 100 + item.n;
}
var a = keyed_computed();

// Refused: the key comes from `nextKey()`, whose side effect must not be dropped.
//--- class-map-record-constructor-keyed-effect.js
function keyed_effect() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); } };
    let count = 0;
    function nextKey() { count = count + 1; return "item"; }
    class Item {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    const item = new Item(nextKey(), 7);
    return count * 100 + values.get("item").n;
}
var a = keyed_effect();

// Refused: `put` reassigns its `key` parameter, so the entry lands under another key.
//--- class-map-record-constructor-keyed-helper-modified-key.js
function keyed_helper_modified_key() {
    const values = new Map();
    const Data = { put(key, value) { key = "other"; values.set(key, value); } };
    class Item {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    const item = new Item("item", 7);
    return values.get("other").n;
}
var a = keyed_helper_modified_key();

// Refused: `put` returns the key; only methods returning undefined are handled.
//--- class-map-record-constructor-keyed-helper-return-key.js
function keyed_helper_return_key() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); return key; } };
    class Item {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    const item = new Item("item", 7);
    return values.get("item").n;
}
var a = keyed_helper_return_key();

// Refused: `put` reads `this`, so it is not a plain pass-through to the Map.
//--- class-map-record-constructor-keyed-helper-receiver.js
function keyed_helper_receiver() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, this && value); } };
    class Item {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    const item = new Item("item", 7);
    return values.get("item").n;
}
var a = keyed_helper_receiver();

// Refused: `Data.put` runs before the fields are written, even with a literal key.
//--- class-map-record-constructor-keyed-early.js
function keyed_early() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); } };
    class Item {
        constructor(key, n) { Data.put(key, this); this.n = n; }
    }
    const item = new Item("item", 7);
    return values.get("item").n * 100 + item.n;
}
var a = keyed_early();

// Refused: the get result is returned and would outlive its stack-allocated instance.
//--- class-map-record-constructor-keyed-owner-escaped.js
function keyed_owner_escaped() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); } };
    class Item {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    const item = new Item("item", 7);
    return values.get("item");
}
var saved = keyed_owner_escaped();
var a = saved.n;

// Refused: the key check must cover every subclass construction; here one passes a Number.
//--- class-map-inherited-keyed-later-number.js
function inherited_keyed_later_number() {
    const values = new Map();
    const Data = { put(key, value) { values.set(key, value); } };
    class Base {
        constructor(key, n) { this.n = n; Data.put(key, this); }
    }
    class Item extends Base {
        constructor(key, n) { super(key, n); }
    }
    const first = new Item("item", 2);
    const second = new Item(4, 7);
    return values.get("item").n * 100 + values.get(4).n;
}
var a = inherited_keyed_later_number();
