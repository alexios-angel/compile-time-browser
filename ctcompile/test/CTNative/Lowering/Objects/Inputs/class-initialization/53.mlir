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
function inherited_own_fields_iterate_throwing_arm_own_getter() {
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
    const leaf = new Leaf(null);
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
