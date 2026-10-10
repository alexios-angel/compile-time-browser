// `save` takes two parameters but gets one argument; it is padded before the Map capture.
//--- class-map-helper-constructor.js
function class_map_helper_constructor() {
    const values = new Map();
    const save = (n, unused) => { values.set("slot", n + 1); return values.get("slot"); };
    class Shape { constructor(n) { this.n = save(n); } }
    const first = new Shape(2), second = new Shape(7);
    return first.n * 100 + second.n * 10 + values.get("slot");
}
var a = class_map_helper_constructor();

// Methods of a `Data` object literal and class methods mutate the same captured Map.
//--- class-map-helper-holder.js
function class_map_helper_holder() {
    const values = new Map();
    const Data = {
        set: n => { values.set("slot", n); return values.size; },
        get: () => values.get("slot"),
        remove: () => values.delete("slot")
    };
    class Shape {
        constructor(n) { this.n = n; Data.set(n); }
        read() { return Data.get(); }
        remove() {
            const before = Data.get(), removed = Data.remove();
            return before * 100 + removed * 10 + values.has("slot");
        }
    }
    const first = new Shape(2), second = new Shape(7);
    const before = first.read() * 10 + second.read();
    const removed = first.remove();
    return before * 1000 + removed * 10 + values.size;
}
var a = class_map_helper_holder();

// The top-level code, constructors and methods all call the helpers with the same captures.
//--- class-map-helper-shared-callers.js
function class_map_helper_shared_callers() {
    const values = new Map();
    const storeValue = n => { values.set("slot", n); return values.get("slot"); };
    const readValue = () => values.get("slot");
    class First {
        constructor(n) { this.n = storeValue(n); }
        read() { return readValue(); }
    }
    class Second {
        constructor(n) { this.n = n; }
        update(n) { return storeValue(n); }
        read() { return readValue(); }
    }
    const first = new First(2), second = new Second(7);
    const before = first.read(), changed = second.update(9), direct = storeValue(4);
    return before * 10000 + changed * 1000 + direct * 100
        + first.read() * 10 + second.read();
}
var a = class_map_helper_shared_callers();

// Dropping the `adjust` capture must not merge or renumber the two distinct Maps.
//--- class-map-helper-distinct-mixed.js
function class_map_helper_distinct_mixed() {
    const adjust = n => n + 1;
    const left = new Map(), right = new Map();
    const save = n => {
        left.set("slot", adjust(n));
        right.set("slot", n + 2);
        return left.get("slot") * 10 + right.get("slot");
    };
    const replaceLeft = n => { left.set("slot", n); return right.get("slot"); };
    class Shape {
        constructor(n) { this.n = save(n); }
        read() { return left.get("slot") * 10 + right.get("slot"); }
        update(n) { return replaceLeft(n); }
    }
    const first = new Shape(2), second = new Shape(7);
    const observed = second.update(5);
    return first.n * 100000 + second.n * 1000 + observed * 100 + first.read();
}
var a = class_map_helper_distinct_mixed();

// A chain of helpers passes the captured Map through each direct call.
//--- class-map-helper-chain.js
function class_map_helper_chain() {
    const values = new Map();
    const leaf = n => { values.set("slot", n + 1); return values.get("slot"); };
    const middle = n => leaf(n) + 1;
    const outer = n => middle(n) + 2;
    class Shape {
        constructor(n) { this.n = outer(n); }
        update(n) { return outer(n); }
    }
    const first = new Shape(2), second = new Shape(7);
    const changed = first.update(4);
    return first.n * 10000 + second.n * 1000 + changed * 100 + values.get("slot");
}
var a = class_map_helper_chain();

// Inlined base constructors and leaf methods keep the Map captured by `save`.
//--- class-map-inherited-helper-callers.js
function class_map_inherited_helper_callers() {
    const values = new Map();
    const save = n => { values.set("slot", n + 1); return values.get("slot"); };
    class Base { constructor(n) { this.n = save(n); } }
    class Leaf extends Base {
        constructor(n) { super(n); }
        update(n) { return save(n); }
    }
    const first = new Base(2), second = new Leaf(7), third = new Leaf(4);
    const changed = second.update(9);
    return first.n * 100000 + second.n * 10000 + third.n * 1000
        + changed * 10 + values.get("slot");
}
var a = class_map_inherited_helper_callers();

// Rewriting the helper's loop keeps its captured Map next to its number argument.
//--- class-map-helper-loop.js
function class_map_helper_loop() {
    const values = new Map();
    const calculateSum = n => {
        let total = 0;
        for (let i = 0; i < n; i++) {
            values.set("slot", i + 1);
            total = total + values.get("slot");
        }
        return total * 10 + values.size;
    };
    class Shape {
        constructor(n) { this.n = n; }
        sum() { return calculateSum(this.n); }
    }
    return new Shape(3).sum();
}
var a = class_map_helper_loop();

// The helper sees the Map later assigned to `values`, not the first one.
//--- class-map-helper-reassigned-cell.js
function class_map_helper_reassigned_cell() {
    let values = new Map();
    const readValue = () => values.get("slot");
    class Shape {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return readValue(); }
    }
    const instance = new Shape(7);
    values = new Map();
    values.set("slot", 9);
    return instance.read();
}
var a = class_map_helper_reassigned_cell();

// `save` is reassigned between calls, so calls to it cannot be bound early.
//--- class-map-helper-replaced.js
function class_map_helper_replaced() {
    const values = new Map();
    let save = n => { values.set("slot", n + 1); return values.get("slot"); };
    class Shape { constructor(n) { this.n = save(n); } }
    const first = new Shape(7);
    save = n => 9;
    const second = new Shape(7);
    return first.n * 10 + second.n;
}
var a = class_map_helper_replaced();

// `Data.get` is replaced, so the method no longer calls the Map-reading helper.
//--- class-map-helper-holder-replaced.js
function class_map_helper_holder_replaced() {
    const values = new Map();
    const Data = {get: () => values.get("slot")};
    class Shape {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return Data.get(); }
    }
    const instance = new Shape(7);
    Data.get = () => 9;
    return instance.read();
}
var a = class_map_helper_holder_replaced();

// An own `get` on the Map instance changes what the helper's get call runs.
//--- class-map-helper-member-replaced.js
function class_map_helper_member_replaced() {
    const values = new Map();
    const readValue = () => values.get("slot");
    class Shape {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return readValue(); }
    }
    const instance = new Shape(7);
    values.get = () => 9;
    return instance.read();
}
var a = class_map_helper_member_replaced();

// Replacing Map.prototype.get changes the helper's get even though the Map is known.
//--- class-map-helper-prototype-replaced.js
function class_map_helper_prototype_replaced() {
    const values = new Map();
    const readValue = () => values.get("slot");
    class Shape {
        constructor(n) { this.n = n; values.set("slot", n + 1); }
        read() { return readValue(); }
    }
    const instance = new Shape(7);
    Map.prototype.get = () => 9;
    return instance.read();
}
var a = class_map_helper_prototype_replaced();

// An unused `Data` method calling an unknown global still counts: every body is checked.
//--- class-map-helper-unused-ambient.js
function class_map_helper_unused_ambient() {
    const values = new Map();
    const Data = {
        set: n => { values.set("slot", n); },
        get: () => values.get("slot"),
        unused: () => unknown(values.size)
    };
    class Shape {
        constructor(n) { this.n = n; Data.set(n + 1); }
        read() { return Data.get(); }
    }
    return new Shape(7).read();
}
var a = class_map_helper_unused_ambient();

// `Data` storing `this` in the Map needs object-valued Map entries.
//--- class-map-helper-stored-receiver.js
function class_map_helper_stored_receiver() {
    const values = new Map();
    const Data = {
        set: instance => { values.set("slot", instance); },
        get: () => values.get("slot"),
        remove: () => values.delete("slot")
    };
    class Shape {
        constructor(n) { this.n = n; Data.set(this); }
        read() { return Data.get().n; }
        dispose() { return Data.remove(); }
    }
    return new Shape(7).read();
}
var a = class_map_helper_stored_receiver();

// `save` escapes into a global, so not all of its callers are known.
//--- class-map-helper-published.js
var retained;
function class_map_helper_published() {
    const values = new Map();
    const save = n => { values.set("slot", n + 1); return values.get("slot"); };
    retained = save;
    class Shape { constructor(n) { this.n = save(n); } }
    const instance = new Shape(7);
    return instance.n * 10 + retained(4);
}
var a = class_map_helper_published();

// A helper-captured Map keyed by a Number argument instead of a string literal.
//--- class-map-helper-numeric-key.js
function class_map_helper_numeric_key() {
    const values = new Map();
    const Data = {
        set: (key, n) => { values.set(key, n); },
        get: key => values.get(key)
    };
    class Shape {
        constructor(n) { this.n = n; Data.set(n, n + 1); }
        read() { return Data.get(this.n); }
    }
    return new Shape(7).read();
}
var a = class_map_helper_numeric_key();

// A static method gets no instance capture environment by calling a helper.
//--- class-map-helper-static.js
function class_map_helper_static() {
    const values = new Map();
    const readValue = n => { values.set("slot", n + 1); return values.get("slot"); };
    class Shape { static read(n) { return readValue(n); } }
    return Shape.read(7);
}
var a = class_map_helper_static();

// The VM wrongly resolves `read()` inside method `read` to the method itself; keep it out.
//--- class-map-helper-method-name.js
function class_map_helper_method_name() {
    const values = new Map();
    values.set("slot", 8);
    const read = () => values.get("slot");
    class Shape { read() { return read(); } }
    return new Shape().read();
}
var a = class_map_helper_method_name();

// Calling `save` with an extra argument from top-level code keeps the arity refusal.
//--- class-map-helper-excess-arguments.js
function class_map_helper_excess_arguments() {
    const values = new Map();
    const save = n => { values.set("slot", n + 1); return values.get("slot"); };
    class Shape { constructor(n) { this.n = save(n); } }
    const instance = new Shape(7);
    return instance.n * 10 + save(4, 9);
}
var a = class_map_helper_excess_arguments();
