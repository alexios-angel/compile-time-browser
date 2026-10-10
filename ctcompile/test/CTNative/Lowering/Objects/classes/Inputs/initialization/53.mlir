// A derived class's own static getters may read inherited ones when no own
// getter shadows an inherited key: every receiver read resolves as it did.
//--- inherited-static-getters.js
function inherited_static_getters() {
    class Base {
        constructor() { this.n = 3; }
        static get NAME() { return "base"; }
    }
    class Leaf extends Base {
        constructor() { super(); this.k = (this.constructor.KEY === "bs.base") * 4; }
        static get KEY() { return "bs." + this.NAME; }
    }
    const leaf = new Leaf();
    return leaf.k + (leaf.n === 3) * 3;
}
var a = inherited_static_getters();

// Bootstrap B's shape: an own getter whose inherited dependency always throws
// ends the constructor arm that reads it.
//--- inherited-own-fields-iterate-throwing-arm-own-getter.js
function inherited_own_fields_iterate_throwing_arm_own_getter(u) {
    class Base {
        constructor() { this.n = 3; }
        static get NAME() { throw new Error("NAME required"); }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
        }
    }
    class Leaf extends Base {
        constructor(t) { super(), t && (this.x = t, this.y = this.constructor.DATA_KEY) }
        static get DATA_KEY() { return `bs.${this.NAME}` }
    }
    const leaf = new Leaf(u);
    leaf.dispose();
    return (leaf.n === null) * 7;
}
var a = inherited_own_fields_iterate_throwing_arm_own_getter();

// An own getter that shadows an inherited one changes what an inherited body's
// receiver read selects; that still needs receiver-specific bodies.
//--- inherited-static-getters-shadow.js
function inherited_static_getters_shadow() {
    class Base {
        constructor() { this.n = 3; }
        static get NAME() { return "base"; }
        static get KEY() { return "bs." + this.NAME; }
    }
    class Leaf extends Base {
        constructor() { super(); this.k = (this.constructor.KEY === "bs.leaf") * 4; }
        static get NAME() { return "leaf"; }
    }
    const leaf = new Leaf();
    return leaf.k + (leaf.n === 3) * 3;
}
var a = inherited_static_getters_shadow();

// An inherited getter cannot read a key only its derived class defines.
//--- inherited-static-getters-base-reads-leaf.js
function inherited_static_getters_base_reads_leaf() {
    class Base {
        constructor() { this.n = 3; }
        static get FULL() { return "x" + this.KEY; }
    }
    class Leaf extends Base {
        constructor() { super(); this.k = (this.constructor.FULL === "xk") * 4; }
        static get KEY() { return "k"; }
    }
    const leaf = new Leaf();
    return leaf.k + (leaf.n === 3) * 3;
}
var a = inherited_static_getters_base_reads_leaf();

// A static method may call an own static method on its exact receiver: the
// call selects the same body an exact local `Shape.base(n)` does.
//--- static-call-this-method.js
function static_call_this_method() {
    class Shape {
        static base(n) { return n + 1; }
        static read(n) { return this.base(n) * 2; }
    }
    var instance = new Shape();
    return Shape.read(2) + 1;
}
var a = static_call_this_method();

// The receiver call may pass fewer arguments and read an own getter too.
//--- static-call-this-getter-chain.js
function static_call_this_getter_chain() {
    class Shape {
        static get BASE() { return 3; }
        static add(n, m) { return m ? n + m : n + this.BASE; }
        static read(n) { return this.add(n) + this.add(n, 1); }
    }
    var instance = new Shape();
    return Shape.read(1);
}
var a = static_call_this_getter_chain();

// A detached receiver method loses its receiver; it is not the exact call.
//--- static-call-this-detached.js
function static_call_this_detached() {
    class Shape {
        static base(n) { return n + 1; }
        static read(n) { const f = this.base; return f(n) * 2; }
    }
    var instance = new Shape();
    return Shape.read(2) + 1;
}
var a = static_call_this_detached();

// A different receiver for the same body is still not the exact receiver.
//--- static-call-this-foreign.js
function static_call_this_foreign() {
    class Shape {
        static base(n) { return n + 1; }
        static read(n) { return this.base.call(undefined, n) * 2; }
    }
    var instance = new Shape();
    return Shape.read(2) + 1;
}
var a = static_call_this_foreign();

// Bootstrap B's getOrCreateInstance: a receiver method call beside new this.
//--- static-call-this-new-this.js
function static_call_this_new_this() {
    class Shape {
        static find(n) { return n ? 7 : 0; }
        static make(n) { return this.find(n) || new this(); }
    }
    var instance = new Shape();
    return Shape.make(1);
}
var a = static_call_this_new_this();

// Bootstrap's component shape: W's NAME throws, B's own DATA_KEY and
// EVENT_KEY read it through `this`, and a leaf overrides NAME. The leaf gets
// its own copies of B's getters, so its EVENT_KEY is ".bs.alert".
//--- inherited-static-getters-component.js
function inherited_static_getters_component() {
    class W {
        static get NAME() { throw new Error("NAME required"); }
    }
    class B extends W {
        constructor() { super(); this.n = 3; }
        static get DATA_KEY() { return `bs.${this.NAME}`; }
        static get EVENT_KEY() { return `.${this.DATA_KEY}`; }
    }
    class Alert extends B {
        constructor() { super(); this.k = (this.constructor.EVENT_KEY === ".bs.alert") * 4; }
        static get NAME() { return "alert"; }
    }
    const alert = new Alert();
    return alert.k + (alert.n === 3) * 3;
}
var a = inherited_static_getters_component();

// The base keeps its own resolution while the leaf reads its copy: the same
// base constructor read selects "bs.base" for Base and "bs.leaf" for Leaf.
//--- inherited-static-getters-shadow-both.js
function inherited_static_getters_shadow_both() {
    class Base {
        constructor() {
            this.k = this.constructor.KEY === "bs.base" ? 1 : this.constructor.KEY === "bs.leaf" ? 2 : 0;
        }
        static get NAME() { return "base"; }
        static get KEY() { return "bs." + this.NAME; }
    }
    class Leaf extends Base {
        constructor() { super(); }
        static get NAME() { return "leaf"; }
    }
    return new Base().k * 3 + new Leaf().k * 2;
}
var a = inherited_static_getters_shadow_both();

// A shared inherited method resolves `this.constructor.NAME` once for every
// class; an override needs per-class method bodies.
//--- inherited-static-getters-shadow-method.js
function inherited_static_getters_shadow_method() {
    class Base {
        constructor() { this.n = 3; }
        read() { return (this.constructor.NAME === "leaf") * 4; }
        static get NAME() { return "base"; }
    }
    class Leaf extends Base {
        constructor() { super(); }
        static get NAME() { return "leaf"; }
    }
    const leaf = new Leaf();
    return leaf.read() + (leaf.n === 3) * 3;
}
var a = inherited_static_getters_shadow_method();

// Copies belong to one class: a class derived from the leaf would inherit
// the shared definition instead.
//--- inherited-static-getters-shadow-extended.js
function inherited_static_getters_shadow_extended() {
    class Base {
        constructor() { this.n = 3; }
        static get NAME() { return "base"; }
        static get KEY() { return "bs." + this.NAME; }
    }
    class Leaf extends Base {
        constructor() { super(); this.k = (this.constructor.KEY === "bs.leaf") * 4; }
        static get NAME() { return "leaf"; }
    }
    class Sub extends Leaf {}
    const leaf = new Leaf();
    return leaf.k + (leaf.n === 3) * 3 + (new Sub().k === 0) * 0;
}
var a = inherited_static_getters_shadow_extended();

// A copy that reaches a throwing getter would need its own closure transport.
//--- inherited-static-getters-shadow-throwing.js
function inherited_static_getters_shadow_throwing() {
    class Base {
        constructor() { this.n = 3; }
        static get NAME() { return "base"; }
        static get BAD() { throw new Error("bad"); }
        static get KEY() { return this.NAME + this.BAD; }
    }
    class Leaf extends Base {
        constructor() { super(); this.k = (this.constructor.NAME === "leaf") * 4; }
        static get NAME() { return "leaf"; }
    }
    const leaf = new Leaf();
    return leaf.k + (leaf.n === 3) * 3;
}
var a = inherited_static_getters_shadow_throwing();

// A shared inherited method reads the overridden getter through
// this.constructor: each class's instances run a body that selects its own.
//--- inherited-static-getters-shadow-method-both.js
function inherited_static_getters_shadow_method_both() {
    class Base {
        constructor() { this.n = 3; }
        read() { return this.constructor.NAME === "leaf" ? 2 : this.constructor.NAME === "base" ? 1 : 0; }
        static get NAME() { return "base"; }
    }
    class Leaf extends Base {
        constructor() { super(); }
        static get NAME() { return "leaf"; }
    }
    return new Base().read() * 3 + new Leaf().read() * 2;
}
var a = inherited_static_getters_shadow_method_both();

// A class derived from the copy's class overrides again and gets its own.
//--- inherited-static-getters-shadow-method-chain.js
function inherited_static_getters_shadow_method_chain() {
    class Base {
        constructor() { this.n = 3; }
        read() { return this.constructor.NAME === "sub" ? 5 : this.constructor.NAME === "leaf" ? 2 : 0; }
        static get NAME() { return "base"; }
    }
    class Leaf extends Base {
        constructor() { super(); }
        static get NAME() { return "leaf"; }
    }
    class Sub extends Leaf {
        constructor() { super(); }
        static get NAME() { return "sub"; }
    }
    return new Leaf().read() + new Sub().read();
}
var a = inherited_static_getters_shadow_method_chain();

// Bootstrap's shape again: B's method reads DATA_KEY, which is copied for the
// component, so the component's copy of the method sees "bs.alert".
//--- inherited-static-getters-component-method.js
function inherited_static_getters_component_method() {
    class W {
        static get NAME() { throw new Error("NAME required"); }
    }
    class B extends W {
        constructor() { super(); this.n = 3; }
        keyed() { return (this.constructor.DATA_KEY === "bs.alert") * 4; }
        static get DATA_KEY() { return `bs.${this.NAME}`; }
    }
    class Alert extends B {
        constructor() { super(); }
        static get NAME() { return "alert"; }
    }
    const alert = new Alert();
    return alert.keyed() + (alert.n === 3) * 3;
}
var a = inherited_static_getters_component_method();
