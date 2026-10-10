// Constructor and instance methods share one Map through every scalar operation.
//--- class-map-direct.js
function class_map_direct() {
    const values = new Map();
    class Shape {
        constructor(n) { this.n = n; values.set(n, n + 1); }
        read() { return values.get(this.n); }
        remove() {
            const before = values.get(this.n);
            const removed = values.delete(this.n);
            return before * 100 + removed * 10 + values.has(this.n);
        }
        clear() { values.clear(); return values.size; }
        size() { return values.size; }
    }
    const first = new Shape(7), second = new Shape(2);
    const before = first.read() * 100 + second.read() * 10 + first.size();
    const removed = first.remove();
    return before * 10000 + removed * 10 + second.clear();
}
var a = class_map_direct();

// Two classes and repeated instances all share one mutable captured Map.
//--- class-map-shared.js
function class_map_shared() {
    const values = new Map();
    class First {
        constructor(n) { this.n = n; values.set("slot", n); }
        read() { return values.get("slot"); }
    }
    class Second {
        constructor(n) { this.n = n; }
        update(n) { values.set("slot", n); return values.get("slot"); }
    }
    const first = new First(2), second = new First(7), third = new Second(3);
    const before = first.read() * 100 + second.read();
    const changed = third.update(9);
    return before * 10000 + changed * 100 + first.read() * 10 + second.read();
}
var a = class_map_shared();

// Two captured Maps stay distinct even when they use the same keys and lifetimes.
//--- class-map-distinct.js
function class_map_distinct() {
    const left = new Map(), right = new Map();
    class Shape {
        constructor(n) { this.n = n; left.set(n, n + 1); right.set(n, n + 2); }
        read() { return left.get(this.n) * 10 + right.get(this.n); }
        update(n) { left.set(this.n, n); return right.get(this.n); }
    }
    const first = new Shape(7), second = new Shape(2);
    const before = first.read() * 100 + second.read();
    const observed = first.update(5);
    return before * 1000 + observed * 100 + first.read();
}
var a = class_map_distinct();

// Folding away the static getter's capture must renumber the remaining Map capture.
//--- class-map-mixed-captures.js
function class_map_mixed_captures() {
    const values = new Map();
    class Shape {
        static get SEED() { return 2; }
        constructor(n) { this.n = Shape.SEED + n; values.set(this.n, n); }
        read() { return Shape.SEED * 100 + values.get(this.n); }
    }
    const first = new Shape(7), second = new Shape(3);
    return first.read() * 1000 + second.read();
}
var a = class_map_mixed_captures();

// Rewriting the method's loop must keep the captured Map argument it still uses.
//--- class-map-method-loop.js
function class_map_method_loop() {
    const values = new Map();
    class Shape {
        constructor(n) { this.n = n; }
        sum() {
            let sum = 0;
            for (let i = 0; i < this.n; i++) {
                values.set(i, i + 1);
                sum = sum + values.get(i);
            }
            return sum * 10 + values.size;
        }
    }
    return new Shape(3).sum();
}
var a = class_map_method_loop();

// The method sees the Map later assigned to `values`, not the first allocation.
//--- class-map-reassigned-cell.js
function class_map_reassigned_cell() {
    let values = new Map();
    class Shape {
        constructor(n) { this.n = n; values.set(n, n + 1); }
        read() { return values.get(this.n); }
    }
    const instance = new Shape(7);
    values = new Map();
    values.set(7, 9);
    return instance.read();
}
var a = class_map_reassigned_cell();

// `Map` is reassigned before `new Map()`, so `values` is not an intrinsic Map.
//--- class-map-global-replaced.js
function class_map_global_replaced() {
    Map = function Replacement() { return {get() { return 9; }}; };
    const values = new Map();
    class Shape { read() { return values.get(7); } }
    return new Shape().read();
}
var a = class_map_global_replaced();

// Replacing Map.prototype.get changes what the method's `values.get` call runs.
//--- class-map-prototype-replaced.js
function class_map_prototype_replaced() {
    const values = new Map();
    class Shape {
        constructor(n) { this.n = n; values.set(n, n + 1); }
        read() { return values.get(this.n); }
    }
    const instance = new Shape(7);
    Map.prototype.get = function replacement(n) { return 9; };
    return instance.read();
}
var a = class_map_prototype_replaced();

// An own `get` property on the Map instance hides Map.prototype.get.
//--- class-map-member-replaced.js
function class_map_member_replaced() {
    const values = new Map();
    class Shape {
        constructor(n) { this.n = n; values.set(n, n + 1); }
        read() { return values.get(this.n); }
    }
    const instance = new Shape(7);
    values.get = function replacement(n) { return 9; };
    return instance.read();
}
var a = class_map_member_replaced();

// The Map escapes into a global, so local calls alone do not show who owns it.
//--- class-map-escaped.js
var retained;
function class_map_escaped() {
    const values = new Map();
    retained = values;
    class Shape {
        constructor(n) { this.n = n; values.set(n, n + 1); }
        read() { return values.get(this.n); }
    }
    return new Shape(7).read() + retained.size;
}
var a = class_map_escaped();

// An unused method calling an unknown global still counts: every method body is checked.
//--- class-map-unused-ambient.js
function class_map_unused_ambient() {
    const values = new Map();
    class Shape {
        constructor(n) { this.n = n; values.set(n, n + 1); }
        read() { return values.get(this.n); }
        unused() { unknown(values.size); }
    }
    return new Shape(7).read();
}
var a = class_map_unused_ambient();

// Storing `this` in the Map needs object-valued entries, not scalar ones.
//--- class-map-stored-receiver.js
function class_map_stored_receiver() {
    const values = new Map();
    class Shape {
        constructor(n) { this.n = n; values.set(n, this); }
        read() { return values.get(this.n).n; }
    }
    return new Shape(7).read();
}
var a = class_map_stored_receiver();

// A Map reached through a helper and a holder object is tracked through both calls.
//--- class-map-nested-holder.js
function class_map_nested_holder() {
    const values = new Map();
    const read = n => values.get(n);
    const H = {read: n => read(n)};
    class Shape {
        constructor(n) { this.n = n; values.set(n, n + 1); }
        read() { return H.read(this.n); }
    }
    return new Shape(7).read();
}
var a = class_map_nested_holder();

// A static method's Map capture is not covered by the instance-method handling.
//--- class-map-static.js
function class_map_static() {
    const values = new Map();
    values.set(7, 8);
    class Shape { static read(n) { return values.get(n); } }
    return Shape.read(7);
}
var a = class_map_static();

// Inlining the base constructor into Leaf must keep the base's Map capture.
//--- class-map-inherited.js
function class_map_inherited() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set(n, n + 1); }
        read() { return values.get(this.n); }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).read();
}
var a = class_map_inherited();

// String-literal keys: tests how the captured Map is passed, not numeric-key handling.
//--- class-map-direct-string-keys.js
function class_map_direct_string_keys() {
    const values = new Map();
    class Shape {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return values.get("slot"); }
        remove() {
            const before = values.get("slot");
            const removed = values.delete("slot");
            return before * 100 + removed * 10 + values.has("slot");
        }
        clear() { values.clear(); return values.size; }
        size() { return values.size; }
    }
    const first = new Shape(7), second = new Shape(2);
    const before = first.read() * 100 + second.read() * 10 + first.size();
    const removed = first.remove();
    return before * 10000 + removed * 10 + second.clear();
}
var a = class_map_direct_string_keys();


// Same as class-map-distinct with string-literal keys instead of numeric ones.
//--- class-map-distinct-string-keys.js
function class_map_distinct_string_keys() {
    const left = new Map(), right = new Map();
    class Shape {
        constructor(n) { this.n = n; left.set("slot", n + 1); right.set("slot", n + 2); }
        read() { return left.get("slot") * 10 + right.get("slot"); }
        update(n) { left.set("slot", n); return right.get("slot"); }
    }
    const first = new Shape(7), second = new Shape(2);
    const before = first.read() * 100 + second.read();
    const observed = first.update(5);
    return before * 1000 + observed * 100 + first.read();
}
var a = class_map_distinct_string_keys();


// A static getter read beside a captured Map does not disturb the Map capture.
//--- class-map-mixed-method.js
function class_map_mixed_method() {
    const values = new Map();
    class Shape {
        static get SEED() { return 2; }
        constructor(n) { this.n = n; values.set("slot", n); }
        read() { return Shape.SEED * 100 + values.get("slot"); }
    }
    const first = new Shape(7), second = new Shape(3);
    return first.read() * 1000 + second.read();
}
var a = class_map_mixed_method();

