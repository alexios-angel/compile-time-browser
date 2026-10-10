// A copied base constructor keeps its Map capture across repeated leaf calls.
//--- class-map-inherited-constructor.js
function class_map_inherited_constructor() {
    const values = new Map();
    class Base {
        constructor(n) { values.set("slot", n + 1); this.n = values.get("slot"); }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    const first = new Base(2), second = new Leaf(7), third = new Leaf(4);
    return first.n * 1000 + second.n * 100 + third.n * 10 + values.get("slot");
}
var a = class_map_inherited_constructor();

// Slot zero in each original constructor names a different Map cell.
//--- class-map-inherited-distinct.js
function class_map_inherited_distinct() {
    const left = new Map(), right = new Map();
    class Base {
        constructor(n) { left.set("slot", n + 1); this.n = left.get("slot"); }
    }
    class Leaf extends Base {
        constructor(n) { super(n); right.set("slot", n + 2); this.n = right.get("slot"); }
    }
    const first = new Base(2), second = new Leaf(7);
    return first.n * 10000 + second.n * 100 + left.get("slot") * 10 + right.get("slot");
}
var a = class_map_inherited_distinct();

// Base and leaf captures of the same cell retain one mutable identity.
//--- class-map-inherited-shared.js
function class_map_inherited_shared() {
    const values = new Map();
    class Base {
        constructor(n) { values.set("slot", n + 1); this.n = values.get("slot"); }
    }
    class Leaf extends Base {
        constructor(n) {
            super(n);
            values.set("slot", values.get("slot") + 3);
            this.n = values.get("slot");
        }
    }
    const first = new Base(2), second = new Leaf(7), third = new Leaf(4);
    return first.n * 10000 + second.n * 100 + third.n * 10 + values.get("slot");
}
var a = class_map_inherited_shared();

// Re-expanding a middle constructor keeps shared and distinct cells for siblings.
//--- class-map-inherited-chain.js
function class_map_inherited_chain() {
    const shared = new Map(), middle = new Map();
    class Base {
        constructor(n) { shared.set("slot", n + 1); this.n = shared.get("slot"); }
    }
    class Middle extends Base {
        constructor(n) { super(n); middle.set("slot", this.n * 10); this.n = middle.get("slot"); }
    }
    class Leaf extends Middle {
        constructor(n) { super(n); shared.set("slot", this.n + 3); this.n = shared.get("slot"); }
    }
    class Sibling extends Base { constructor(n) { super(n); } }
    const first = new Middle(2), second = new Leaf(7), third = new Sibling(4);
    return first.n * 1000000 + second.n * 10000 + third.n * 1000
        + shared.get("slot") * 100 + middle.get("slot");
}
var a = class_map_inherited_chain();

// Ordinary inherited method dispatch retains the same cell as construction.
//--- class-map-inherited-method.js
function class_map_inherited_method() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return values.get("slot"); }
        update(n) { values.set("slot", n); return values.get("slot"); }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    const first = new Base(2), second = new Leaf(7);
    const before = first.read() * 100 + second.read();
    const changed = second.update(9);
    return before * 1000 + changed * 100 + first.read() * 10 + second.read();
}
var a = class_map_inherited_method();

// Removing an earlier helper capture must not rename the copied Map capture.
//--- class-map-inherited-mixed-captures.js
function class_map_inherited_mixed_captures() {
    const adjust = n => n + 1;
    const values = new Map();
    class Base {
        constructor(n) { this.n = adjust(n); values.set("slot", this.n); }
    }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = values.get("slot") + 2; }
    }
    const first = new Base(2), second = new Leaf(7);
    return first.n * 100 + second.n * 10 + values.get("slot");
}
var a = class_map_inherited_mixed_captures();

// An early own-key snapshot cannot use the final constructor field count.
//--- class-map-inherited-early-snapshot.js
function class_map_inherited_early_snapshot() {
    const values = new Map();
    class Base {
        constructor() {
            this.first = 7;
            values.set("slot", this.first);
            this.second = this.seed();
        }
        seed() { return this.first; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base {
        constructor() { super(); }
        seed() { return this.count(); }
    }
    return new Base().count() * 100 + new Leaf().second * 10 + values.get("slot");
}
var a = class_map_inherited_early_snapshot();

// Copied captures observe a replaced cell, not the original allocation.
//--- class-map-inherited-reassigned-cell.js
function class_map_inherited_reassigned_cell() {
    let values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return values.get("slot"); }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    const instance = new Leaf(7);
    values = new Map();
    values.set("slot", 9);
    return instance.read();
}
var a = class_map_inherited_reassigned_cell();

// Original Map identity cannot authorize a replacement constructor.
//--- class-map-inherited-global-replaced.js
function class_map_inherited_global_replaced() {
    Map = function Replacement() { return {get() { return 9; }}; };
    const values = new Map();
    class Base { read() { return values.get("slot"); } }
    class Leaf extends Base { constructor() { super(); } }
    return new Leaf().read();
}
var a = class_map_inherited_global_replaced();

// A prototype write changes the inherited method's captured Map operation.
//--- class-map-inherited-prototype-replaced.js
function class_map_inherited_prototype_replaced() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return values.get("slot"); }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    const instance = new Leaf(7);
    Map.prototype.get = function replacement(n) { return 9; };
    return instance.read();
}
var a = class_map_inherited_prototype_replaced();

// An own Map member masks the operation even when capture identity is exact.
//--- class-map-inherited-member-replaced.js
function class_map_inherited_member_replaced() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return values.get("slot"); }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    const instance = new Leaf(7);
    values.get = function replacement(n) { return 9; };
    return instance.read();
}
var a = class_map_inherited_member_replaced();

// The full inherited class census includes methods this entry never calls.
//--- class-map-inherited-unused-ambient.js
function class_map_inherited_unused_ambient() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return values.get("slot"); }
        unused() { unknown(values.size); }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).read();
}
var a = class_map_inherited_unused_ambient();

// Storing an inherited receiver requires a separate typed ownership proof.
//--- class-map-inherited-stored-receiver.js
function class_map_inherited_stored_receiver() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
        read() { return values.get("slot").n; }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).read();
}
var a = class_map_inherited_stored_receiver();

// Capturing a helper does not prove the Map its body captures.
//--- class-map-inherited-helper.js
function class_map_inherited_helper() {
    const values = new Map();
    const read = n => { values.set("slot", n + 1); return values.get("slot"); };
    class Base { constructor(n) { this.n = read(n); } }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).n;
}
var a = class_map_inherited_helper();

// A holder method's Map needs its own complete capture and invocation proof.
//--- class-map-inherited-holder.js
function class_map_inherited_holder() {
    const values = new Map();
    const H = {read: n => { values.set("slot", n + 1); return values.get("slot"); }};
    class Base { constructor(n) { this.n = H.read(n); } }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).n;
}
var a = class_map_inherited_holder();

// A captured super.method target remains separate from constructor expansion.
//--- class-map-inherited-super-method.js
function class_map_inherited_super_method() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return values.get("slot"); }
    }
    class Leaf extends Base {
        constructor(n) { super(n); }
        read() { return super.read(); }
    }
    return new Leaf(7).read();
}
var a = class_map_inherited_super_method();
