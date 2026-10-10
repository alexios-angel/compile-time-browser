// Each constructor's capture slot zero names its own helper after expansion.
//--- inherited-helper-distinct.js
function inherited_helper_distinct() {
    const base = n => n + 1;
    const leaf = n => n * 10;
    class Base { constructor(n) { this.n = base(n); } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = leaf(this.n); }
    }
    return new Leaf(7).n;
}
var a = inherited_helper_distinct();

// A previously expanded base must retain all three original helper identities.
//--- inherited-helper-chain.js
function inherited_helper_chain() {
    const base = n => n + 1;
    const middle = n => n * 10;
    const leaf = n => n + 3;
    class Base { constructor(n) { this.n = base(n); } }
    class Middle extends Base {
        constructor(n) { super(n); this.n = middle(this.n); }
    }
    class Leaf extends Middle {
        constructor(n) { super(n); this.n = leaf(this.n); }
    }
    return new Leaf(7).n;
}
var a = inherited_helper_chain();

// Reusing one captured base body cannot overwrite the first leaf's targets.
//--- inherited-helper-siblings.js
function inherited_helper_siblings() {
    const base = n => n + 1;
    const left = n => n * 10;
    const right = n => n + 100;
    class Base { constructor(n) { this.n = base(n); } }
    class Left extends Base {
        constructor(n) { super(n); this.n = left(this.n); }
    }
    class Right extends Base {
        constructor(n) { super(n); this.n = right(this.n); }
    }
    return new Left(2).n * 1000 + new Right(3).n;
}
var a = inherited_helper_siblings();

// Original helper effects run once, before super, in the base, then in the leaf.
//--- inherited-helper-order.js
function inherited_helper_order() {
    const before = box => { box.n = box.n * 10 + 1; return box.n; };
    const base = box => { box.n = box.n * 10 + 2; return box.n; };
    const after = box => { box.n = box.n * 10 + 3; return box.n; };
    class Base {
        constructor(box, value) { this.before = value; this.base = base(box); }
    }
    class Leaf extends Base {
        constructor(box) { super(box, before(box)); this.after = after(box); }
    }
    var box = {n: 0};
    var leaf = new Leaf(box);
    return leaf.before * 1000000 + leaf.base * 1000 + leaf.after;
}
var a = inherited_helper_order();

//--- inherited-helper-changing.js
function inherited_helper_changing() {
    let plus = n => n + 1;
    class Base { constructor(n) { this.n = plus(n); } }
    class Leaf extends Base { constructor(n) { super(n); } }
    plus = n => n + 2;
    return new Leaf(7).n;
}
var a = inherited_helper_changing();

// Cloning a base does not turn an observed helper identity into a direct call.
//--- inherited-helper-identity.js
function inherited_helper_identity() {
    const plus = n => n + 1;
    class Base { constructor() { this.helper = plus; } }
    class Leaf extends Base { constructor() { super(); } }
    return new Leaf().helper === plus ? 1 : 0;
}
var a = inherited_helper_identity();

// The complete helper body needs proof even when its ambient arm is not taken.
//--- inherited-helper-effect.js
function inherited_helper_effect() {
    const unsafe = n => { if (n) unknown(n); return 7; };
    class Base { constructor(n) { this.n = unsafe(n); } }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(0).n;
}
var a = inherited_helper_effect();

//--- inherited-helper-receiver.js
function inherited_helper_receiver() {
    const read = function(n) { if (n) return this.n; return 7; };
    class Base { constructor(n) { this.n = read(n); } }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(0).n;
}
var a = inherited_helper_receiver();

//--- inherited-helper-newtarget.js
function inherited_helper_newtarget() {
    const plus = function(n) { return new.target === undefined ? n + 1 : 0; };
    class Base { constructor(n) { this.n = plus(n); } }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).n;
}
var a = inherited_helper_newtarget();
//--- inherited-helper-order-values.js
// Value dependencies retain before-super, base and post-super call order.
// The object-mutating original above keeps its separate ownership refusal.
function inherited_helper_order_values() {
    const before = n => n + 1;
    const base = n => n * 10;
    const after = n => n + 3;
    class Base {
        constructor(n) { this.before = n; this.base = base(n); }
    }
    class Leaf extends Base {
        constructor(n) { super(before(n)); this.after = after(this.base); }
    }
    var leaf = new Leaf(0);
    return leaf.before * 1000000 + leaf.base * 1000 + leaf.after;
}
var a = inherited_helper_order_values();
