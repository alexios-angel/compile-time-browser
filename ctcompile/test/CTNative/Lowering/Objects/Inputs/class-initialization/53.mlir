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
