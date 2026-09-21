// Both constructor key parameters retain each exact new's literal arguments.
//--- class-map-record-nested-constructor-direct.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    const first = new Item("left", "bs.item", 2);
    const savedChild = outer.get("left"), savedFirst = savedChild.get("bs.item");
    const second = new Item("left", "bs.item", 7);
    const third = new Item("right", "bs.other", 6);
    const fourth = new Item("left", "bs.other", 4);
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 100000 + savedChild.get("bs.item").n * 10000
        + second.n * 1000 + outer.get("right").get("bs.other").n * 100
        + savedChild.get("bs.other").n * 10 + outer.size;
}
var a = probe();

// Failed helper expansion must roll back a constructor publication rewrite.
//--- class-map-record-nested-constructor-helper-effect.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.has(element) || outer.set(element, missingFactory());
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(n) {
            this.n = n;
            register("element", "bs.item", this);
        }
    }
    const item = new Item(7);
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Called holder slots preserve old child and record aliases after recreation.
//--- class-map-record-nested-constructor-holder.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        get(element, key) { return outer.get(element).get(key); },
        remove(element) { outer.delete(element); }
    };
    class Item {
        constructor(n, element, key) {
            this.n = n;
            Data.set(element, key, this);
        }
    }
    const first = new Item(2, "element", "bs.item");
    const savedChild = outer.get("element"), savedFirst = Data.get("element", "bs.item");
    Data.remove("element");
    const second = new Item(3, "element", "bs.item");
    savedFirst.n = 5;
    return savedFirst.n * 1000 + savedChild.size * 100
        + Data.get("element", "bs.item").n * 10 + outer.size;
}
var a = probe();

// Only the first constructor allocates a child through the short-circuit helper.
//--- class-map-record-nested-constructor-shortcircuit.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        outer.has(element) || outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(n) {
            this.n = n;
            register("element", "bs.item", this);
        }
    }
    const first = new Item(2);
    const savedChild = outer.has("element") && outer.get("element");
    const savedFirst = savedChild.get("bs.item");
    const second = new Item(7);
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 1000 + savedChild.get("bs.item").n * 100
        + second.n * 10 + outer.size;
}
var a = probe();

// A read after publication must not move ahead of its registration.
//--- class-map-record-nested-constructor-observer.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        get(element, key) { return outer.get(element).get(key); }
    };
    class Item {
        constructor(n) {
            this.n = n;
            Data.set("element", "bs.item", this);
            this.n = Data.get("element", "bs.item").n + 1;
        }
    }
    const item = new Item(7);
    return Data.get("element", "bs.item").n;
}
var a = probe();

// An exception leaves the published receiver observable to the catch handler.
//--- class-map-record-nested-constructor-throw.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(n) {
            this.n = n;
            register("element", "bs.item", this);
            throw 9;
        }
    }
    try { new Item(7); }
    catch (error) { return error + outer.get("element").get("bs.item").n; }
}
var a = probe();

// A replacement return is distinct from the receiver stored in the child Map.
//--- class-map-record-nested-constructor-return-object.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(n) {
            this.n = n;
            register("element", "bs.item", this);
            return { n: 9 };
        }
    }
    const item = new Item(7);
    return outer.get("element").get("bs.item").n * 100 + item.n;
}
var a = probe();

// A caller parameter is not a literal key at the exact construction site.
//--- class-map-record-nested-constructor-dynamic-key.js
function probe(element) {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    const item = new Item(element, "bs.item", 7);
    return outer.get(element).get("bs.item").n;
}
var a = probe("element");

// Element object identity is outside the literal String routing proof.
//--- class-map-record-nested-constructor-object-key.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    const element = {};
    const item = new Item(element, "bs.item", 7);
    return outer.get(element).get("bs.item").n;
}
var a = probe();

// Constructor sinking cannot extend a child Map's borrowed record lifetime.
//--- class-map-record-nested-constructor-child-escaped.js
var retained;
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(n) {
            this.n = n;
            register("element", "bs.item", this);
        }
    }
    const item = new Item(7);
    retained = outer.get("element");
    return item.n;
}
var a = probe();

// A record reached through the published child still needs its local owner.
//--- class-map-record-nested-constructor-record-escaped.js
var retained;
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(n) {
            this.n = n;
            register("element", "bs.item", this);
        }
    }
    const item = new Item(7);
    retained = outer.get("element").get("bs.item");
    return item.n;
}
var a = probe();

// Recursive helper entry cannot be replaced with a finite entry call sequence.
//--- class-map-record-nested-constructor-reentry.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) {
            outer.set(element, new Map());
            register(element, key, item);
        }
        outer.get(element).set(key, item);
    }
    class Item {
        constructor(n) {
            this.n = n;
            register("element", "bs.item", this);
        }
    }
    const item = new Item(7);
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// An uncalled holder slot remains part of the complete effect census.
//--- class-map-record-nested-constructor-unused-slot.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        unsafe() { return missingObserver(outer); }
    };
    class Item {
        constructor(n) {
            this.n = n;
            Data.set("element", "bs.item", this);
        }
    }
    const item = new Item(7);
    return outer.get("element").get("bs.item").n;
}
var a = probe();

// Inherited construction still needs the complete family publication proof.
//--- class-map-record-nested-constructor-inherited.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(n) {
            this.n = n;
            register("element", "bs.item", this);
        }
    }
    class Item extends Base { constructor(n) { super(n); } }
    const item = new Item(7);
    return outer.get("element").get("bs.item").n;
}
var a = probe();
