// Both leaf construction sites retain their own receiver across Map replacement.
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

// Removing the entry leaves the saved alias borrowed from the completed leaf owner.
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

// Constant own-field writes may replace a base field and add primitive leaf fields.
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

// Constant own-field writes may replace a base field and add primitive leaf fields.
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

// A child Map read can observe the published receiver before its extra field exists.
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

// A throwing child leaves the base receiver reachable without a completed owner.
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

// An accessor in the child suffix exceeds the literal-write completion proof.
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

// A helper called by the child can observe the partial receiver through its Map.
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

// Reentrant construction replaces the entry before the outer child completes.
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

// A directly constructed base does not have the proved leaf's complete field set.
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

// Sibling leaves require a separate proof of the complete constructor family.
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

// A deeper chain is outside the single base and directly constructed leaf proof.
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

// Reading through a second Map alias is still a partial-initialization observer.
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

// An escaped Map would retain the receiver past its enclosing native owner.
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

// An explicit receiver return exceeds the child's fixed-write completion proof.
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

// A child replacement makes the returned owner differ from the published receiver.
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

// Arithmetic after publication needs a broader effect and exception proof.
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
