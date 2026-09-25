// Both literal keys follow reordered super arguments; saved aliases retain their leaf owners.
//--- class-map-record-nested-inherited-direct.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = 3;
        }
    }
    const first = new Item(2, "bs.item", "left");
    const savedChild = outer.get("left"), savedFirst = savedChild.get("bs.item");
    const second = new Item(7, "bs.item", "left");
    const other = new Item(6, "bs.item", "right");
    const third = new Item(4, "bs.other", "left");
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 100000 + savedChild.get("bs.item").n * 10000
        + second.n * 1000 + outer.get("right").get("bs.item").n * 100
        + savedChild.get("bs.other").n * 10 + third.extra + outer.size;
}

var a = probe();

// Called Data slots preserve old child and leaf aliases after deletion and recreation.
//--- class-map-record-nested-inherited-holder.js
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
    class Base {
        constructor(element, key, n) {
            this.n = n;
            Data.set(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = n + 3;
        }
    }
    const first = new Item(2, "bs.item", "element");
    const savedChild = outer.get("element"), savedFirst = Data.get("element", "bs.item");
    Data.remove("element");
    const second = new Item(7, "bs.item", "element");
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 10000 + savedChild.size * 1000
        + Data.get("element", "bs.item").n * 100 + savedFirst.extra * 10
        + second.extra + outer.size;
}

var a = probe();

// Base registration reuses one selected child while the unique leaf initializes its suffix.
//--- class-map-record-nested-inherited-shortcircuit.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        outer.has(element) || outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = 3;
        }
    }
    const first = new Item(2, "bs.item", "element");
    const savedChild = outer.has("element") && outer.get("element");
    const savedFirst = savedChild.get("bs.item");
    const second = new Item(7, "bs.item", "element");
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 1000 + savedChild.get("bs.item").n * 100
        + savedFirst.extra * 10 + second.extra + outer.size;
}

var a = probe();

// The leaf can observe the receiver published by super before its suffix finishes.
//--- class-map-record-nested-inherited-observer.js
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
    class Base {
        constructor(element, key, n) {
            this.n = n;
            Data.set(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.n = Data.get(element, key).n + 1;
        }
    }
    const item = new Item(7, "bs.item", "element");
    return Data.get("element", "bs.item").n;
}

var a = probe();

// A throwing leaf leaves the base receiver visible to its catch handler.
//--- class-map-record-nested-inherited-throw.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            throw 9;
        }
    }
    try { new Item(7, "bs.item", "element"); }
    catch (error) { return error + outer.get("element").get("bs.item").n; }
}

var a = probe();

// Leaf reentry can replace the published record before the outer construction completes.
//--- class-map-record-nested-inherited-reentry.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            if (n) new Item(0, key, element);
        }
    }
    const item = new Item(7, "bs.item", "element");
    return outer.get("element").get("bs.item").n * 100 + item.n;
}

var a = probe();

// A replacement return differs from the base receiver already stored in Data.
//--- class-map-record-nested-inherited-return-object.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            return { n: 9 };
        }
    }
    const item = new Item(7, "bs.item", "element");
    return outer.get("element").get("bs.item").n * 100 + item.n;
}

var a = probe();

// A second concrete leaf prevents a unique family publication proof.
//--- class-map-record-nested-inherited-sibling.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = 3;
        }
    }
    class Other extends Base {
        constructor(n, key, element) { super(element, key, n); this.extra = 4; }
    }
    const first = new Item(2, "bs.item", "element");
    const second = new Other(7, "bs.other", "element");
    return outer.get("element").get("bs.item").n * 100
        + outer.get("element").get("bs.other").n;
}

var a = probe();

// A grandchild needs a separate complete ancestry proof.
//--- class-map-record-nested-inherited-deeper.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = 3;
        }
    }
    class Grandchild extends Item {
        constructor(n, key, element) { super(n, key, element); this.last = 4; }
    }
    const item = new Grandchild(7, "bs.item", "element");
    return outer.get("element").get("bs.item").n;
}

var a = probe();

// A valid first element String cannot authorize a later numeric element key.
//--- class-map-record-nested-inherited-outer-number.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = 3;
        }
    }
    const first = new Item(2, "bs.item", "element");
    const second = new Item(7, "bs.item", 4);
    return outer.get("element").get("bs.item").n * 100 + outer.get(4).get("bs.item").n;
}

var a = probe();

// A valid first component String cannot authorize a later numeric component key.
//--- class-map-record-nested-inherited-inner-number.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = 3;
        }
    }
    const first = new Item(2, "bs.item", "element");
    const second = new Item(7, 4, "element");
    return outer.get("element").get("bs.item").n * 100 + outer.get("element").get(4).n;
}

var a = probe();

// A computed super key is outside the exact literal actual proof.
//--- class-map-record-nested-inherited-computed-super-key.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key + "", n);
            this.extra = 3;
        }
    }
    const item = new Item(7, "bs.item", "element");
    return outer.get("element").get("bs.item").n;
}

var a = probe();

// An uncalled observer is still part of the original Data holder census.
//--- class-map-record-nested-inherited-unused-slot.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            outer.get(element).set(key, item);
        },
        get(element, key) { return outer.get(element).get(key); },
        remove(element) { outer.delete(element); },
        unsafe() { return missingObserver(outer); }
    };
    class Base {
        constructor(element, key, n) {
            this.n = n;
            Data.set(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = 3;
        }
    }
    const item = new Item(7, "bs.item", "element");
    return outer.get("element").get("bs.item").n;
}

var a = probe();

// Moving publication past super cannot extend the selected child owner lifetime.
//--- class-map-record-nested-inherited-child-escaped.js
var retained;
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = 3;
        }
    }
    const item = new Item(7, "bs.item", "element");
    retained = outer.get("element");
    return item.n;
}

var a = probe();

// A failed captured helper census must roll back base and leaf publication together.
//--- class-map-record-nested-inherited-helper-effect.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.has(element) || outer.set(element, missingFactory());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = 3;
        }
    }
    const item = new Item(7, "bs.item", "element");
    return outer.get("element").get("bs.item").n;
}

var a = probe();

// Coercion can inspect the partial receiver and replace its child entry reentrantly.
//--- class-map-record-nested-inherited-coercion.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, 7);
            this.extra = n + 1;
        }
    }
    const argument = {
        valueOf() {
            const observed = outer.get("element").get("bs.item").extra === void 0;
            new Item(2, "bs.item", "element");
            return observed ? 4 : 9;
        }
    };
    const item = new Item(argument, "bs.item", "element");
    const saved = outer.get("element").get("bs.item");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}

var a = probe();

// A captured base caller must retain its original registration after leaf normalization.
//--- class-map-record-nested-inherited-hidden-base.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = 3;
        }
    }
    function hidden() { return new Base("element", "bs.other", 7); }
    const first = new Item(2, "bs.item", "element");
    const second = hidden();
    return outer.get("element").get("bs.item").n * 100 + second.n;
}

var a = probe();

// A captured leaf caller is outside the complete same-entry construction census.
//--- class-map-record-nested-inherited-hidden-leaf.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        outer.get(element).set(key, item);
    }
    class Base {
        constructor(element, key, n) {
            this.n = n;
            register(element, key, this);
        }
    }
    class Item extends Base {
        constructor(n, key, element) {
            super(element, key, n);
            this.extra = n + 1;
        }
    }
    function hidden() { return new Item(7, "bs.other", "element"); }
    const first = new Item(2, "bs.item", "element");
    const second = hidden();
    return outer.get("element").get("bs.item").n * 100 + second.n;
}

var a = probe();
