// Base stores `this` in the Map; each Leaf construction keeps its own instance.
//--- class-map-inherited-base-overwrite.js
function base_overwrite() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = 3; }
    }
    const first = new Item(2);
    const savedFirst = values.get("slot");
    const second = new Item(7);
    const savedSecond = values.get("slot");
    savedFirst.n = 5;
    savedSecond.extra = 9;
    return first.n * 1000 + second.extra * 100 + savedFirst.n * 10 + savedSecond.extra;
}
var a = base_overwrite();

// Deleting the entry leaves the get result referring to the finished Leaf instance.
//--- class-map-inherited-base-delete.js
function base_delete() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = 3; }
    }
    const item = new Item(2);
    const saved = values.get("slot");
    const removed = values.delete("slot");
    saved.n = 5;
    item.extra = 4;
    return item.n * 1000 + saved.extra * 100 + saved.n * 10 + removed * 2 + values.size;
}
var a = base_delete();

// After Base stores `this`, Leaf may overwrite a base field and add constant fields.
//--- class-map-inherited-base-constant-overwrite.js
function base_constant_overwrite() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.n = 9; this.label = 3; this.ready = true; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n * 10 + (saved.label === 3) * 2 + saved.ready;
}
var a = base_constant_overwrite();

// Same with a String-valued field; preparation keeps it but native lowering refuses it.
//--- class-map-inherited-base-string.js
function base_string() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.n = 9; this.label = "ready"; this.ready = true; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n * 10 + (saved.label === "ready") * 2 + saved.ready;
}
var a = base_string();

// Refused: Leaf reads `this` back from the Map before writing its `extra` field.
//--- class-map-inherited-base-observer.js
function base_observer() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) {
            super(n);
            this.observed = values.get("slot").extra === void 0;
            this.extra = 9;
        }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return item.observed * 1000 + saved.n * 10 + saved.extra;
}
var a = base_observer();

// Refused: Leaf throws after Base stored `this`, leaving a half-built object in the Map.
//--- class-map-inherited-base-throw.js
function base_throw() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); throw 9; }
    }
    try {
        const item = new Item(7);
        return item.n;
    } catch (error) {
        return values.get("slot").n * 100 + error;
    }
}
var a = base_throw();

// Refused: Leaf's field write calls a getter; only constant field writes are handled.
//--- class-map-inherited-base-getter.js
function base_getter() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = this.current; }
        get current() { return this.n; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}
var a = base_getter();

// Refused: a helper called by Leaf can read the half-built object from the Map.
//--- class-map-inherited-base-call.js
function base_call() {
    const values = new Map();
    function observe() { return values.get("slot").extra === void 0; }
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.observed = observe(); this.extra = 9; }
    }
    const item = new Item(7);
    return item.observed * 1000 + item.extra;
}
var a = base_call();

// Refused: a nested `new Item` replaces the entry before the outer Leaf finishes.
//--- class-map-inherited-base-reentry.js
function base_reentry() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); if (n === 7) { new Item(2); } this.extra = 9; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n * 10 + saved.extra;
}
var a = base_reentry();

// Refused: a direct `new Base` stores an object lacking Leaf's `extra` field.
//--- class-map-inherited-base-direct-base.js
function base_direct_base() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = 9; }
    }
    const base = new Base(2);
    const item = new Item(7);
    const saved = values.get("slot");
    return base.n * 1000 + saved.n * 100 + item.extra;
}
var a = base_direct_base();

// Refused: two sibling subclasses of Base would each need checking.
//--- class-map-inherited-base-siblings.js
function base_siblings() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = 9; }
    }
    class Other extends Base {
        constructor(n) { super(n); this.extra = 8; }
    }
    const first = new Item(2);
    const second = new Other(7);
    const saved = values.get("slot");
    return first.n * 1000 + saved.n * 10 + second.extra;
}
var a = base_siblings();

// Refused: only one Base and one directly constructed subclass are handled, not a chain.
//--- class-map-inherited-base-deep.js
function base_deep() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Middle extends Base {
        constructor(n) { super(n); this.middle = 3; }
    }
    class Item extends Middle {
        constructor(n) { super(n); this.extra = 9; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + saved.middle * 10 + item.extra;
}
var a = base_deep();

// Refused: reading through a second name for the Map still sees the half-built object.
//--- class-map-inherited-base-map-alias.js
function base_map_alias() {
    const values = new Map();
    const mirror = values;
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) {
            super(n);
            this.observed = mirror.get("slot").extra === void 0;
            this.extra = 9;
        }
    }
    const item = new Item(7);
    return item.observed * 1000 + item.extra;
}
var a = base_map_alias();

// Refused: the Map escapes into a global and would keep the instance past its owner.
//--- class-map-inherited-base-map-escaped.js
var retained;
function base_map_escaped() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = 9; }
    }
    const item = new Item(7);
    retained = values;
    return item.extra;
}
var a = base_map_escaped();
a = retained.get("slot").n * 100 + a;

// Refused: an explicit `return this` is not one of the constant field writes handled.
//--- class-map-inherited-base-return-receiver.js
function base_return_receiver() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = 9; return this; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.extra;
}
var a = base_return_receiver();

// Refused: Leaf returns another object, so `new` differs from the stored `this`.
//--- class-map-inherited-base-return-object.js
function base_return_object() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); return {n: 9}; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n;
}
var a = base_return_object();

// Leaf computes a field with arithmetic after Base stored `this` in the Map.
//--- class-map-inherited-base-arithmetic.js
function base_arithmetic() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = n + 1; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}
var a = base_arithmetic();
