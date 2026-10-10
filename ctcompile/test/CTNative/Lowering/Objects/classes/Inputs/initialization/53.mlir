// A derived class's own static getters may read inherited ones when no own getter
// shadows an inherited key: every `this.X` read resolves exactly as before.
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

// Bootstrap's BaseComponent shape: an own getter (DATA_KEY) whose inherited dependency
// (NAME) always throws ends the constructor branch that reads it.
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

// An own getter that shadows an inherited one changes what the inherited getter's
// `this.NAME` read finds, so each class needs its own copy of that getter.
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

// An inherited getter cannot read a key that only its derived class defines.
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

// A static method may call another static method on `this`: the call runs the same
// body that `Shape.base(n)` does.
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

// The call on `this` may pass fewer arguments and read an own getter too.
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

// A method read off `this` and called detached loses its receiver.
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

// Calling the same method with another receiver is not a call on `this`.
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

// Bootstrap's getOrCreateInstance shape: a static call on `this` beside `new this()`.
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

// Bootstrap's component shape: W's NAME throws, B's own DATA_KEY and EVENT_KEY read
// it through `this`, and the leaf Alert overrides NAME. Alert gets its own copies
// of B's getters, so its EVENT_KEY is ".bs.alert".
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

// Base keeps its own getters while Leaf reads its copies: the same Base constructor
// read finds "bs.base" for a Base and "bs.leaf" for a Leaf.
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

// An inherited method reads `this.constructor.NAME`; when a subclass overrides
// NAME, the method needs a separate body per class.
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

// Copies belong to one class: a class derived from Leaf would inherit the shared
// definition instead.
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

// Copying KEY for Leaf would also reach the throwing BAD getter.
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

// An inherited method reads the overridden getter through this.constructor:
// instances of each class run a body that finds that class's NAME.
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

// A class derived from Leaf overrides NAME again and gets its own method body.
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

// Bootstrap's shape again: B's method reads DATA_KEY, which is copied for Alert,
// so Alert's copy of the method sees "bs.alert".
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
