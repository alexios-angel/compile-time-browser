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

// Two classes and repeated instances observe the same mutable captured identity.
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

// Two captured cells remain distinct even when their keys and lifetimes coincide.
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

// Retiring the preceding class/getter slot must renumber the surviving Map slot.
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

// Normalizing a method loop must keep its surviving captured Map argument.
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

// Captures observe a later cell assignment, not the first Map allocation.
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

// Declaring initial Map identity cannot authorize a replacement constructor.
//--- class-map-global-replaced.js
function class_map_global_replaced() {
    Map = function Replacement() { return {get() { return 9; }}; };
    const values = new Map();
    class Shape { read() { return values.get(7); } }
    return new Shape().read();
}
var a = class_map_global_replaced();

// Prototype writes change the operation invoked by the original method body.
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

// An own member masks the declared intrinsic's original prototype method.
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

// Publishing a captured Map needs an ownership proof beyond local invocations.
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

// An unused method still belongs to the original class effect census.
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

// Storing the receiver requires typed payload ownership, not scalar Map admission.
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

// A helper/holder chain needs its own complete Map capture and invocation proof.
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

// Static calls do not inherit the direct instance-method capture proof.
//--- class-map-static.js
function class_map_static() {
    const values = new Map();
    values.set(7, 8);
    class Shape { static read(n) { return values.get(n); } }
    return Shape.read(7);
}
var a = class_map_static();

// Base expansion cannot erase a Map capture before proving its leaf invocations.
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

// Literal keys isolate capture transport from optional numeric-key representation.
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


// Literal keys isolate capture transport from optional numeric-key representation.
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


// Literal keys isolate capture transport from optional numeric-key representation.
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

