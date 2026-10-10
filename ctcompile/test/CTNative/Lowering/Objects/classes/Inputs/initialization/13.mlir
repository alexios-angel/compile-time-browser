// Ordinary calls after super still require the complete immutable-holder proof.
//--- inherited-post-super-holder.js
const H = {read: n => n + 1};
function inherited_post_super_holder() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = H.read(this.n); }
    }
    return new Leaf(7).n;
}
var a = inherited_post_super_holder();

// Base constructors used directly and cloned into a leaf retain their holder calls.
//--- inherited-post-super-holder-shared.js
const H = {read: n => n + 1, scale: n => n * 10};
function inherited_post_super_holder_shared() {
    class Base { constructor(n) { this.n = H.read(n); } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = H.scale(this.n); }
    }
    return new Base(2).n * 100 + new Leaf(7).n;
}
var a = inherited_post_super_holder_shared();

// Each ancestry level resolves its own callable slot after cloning.
//--- inherited-post-super-holder-chain.js
const H = {read: n => n + 1, scale: n => n * 10, add: (left, right) => left + right};
function inherited_post_super_holder_chain() {
    class Base { constructor(n) { this.n = H.read(n); } }
    class Middle extends Base {
        constructor(n) { super(n); this.n = H.scale(this.n); }
    }
    class Leaf extends Middle {
        constructor(n) { super(n); this.n = H.add(this.n, 3); }
    }
    return new Middle(2).n * 100 + new Leaf(7).n;
}
var a = inherited_post_super_holder_chain();

// Both runtime arms invoke the holder; nested branches cannot lose a call.
//--- inherited-post-super-holder-branches.js
const H = {read: n => n + 1, scale: n => n * 10};
function inherited_post_super_holder_branches() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) {
            super(n);
            if (n) {
                if (n > 1) this.n = H.scale(this.n);
                else this.n = H.read(this.n);
            } else this.n = H.read(3);
        }
    }
    return new Leaf(2).n * 10000 + new Leaf(1).n * 100 + new Leaf(0).n;
}
var a = inherited_post_super_holder_branches();

// Preserve super effects and left-to-right holder arguments in the same local cell.
//--- inherited-post-super-holder-order.js
const H = {combine: (left, right) => left * 10 + right};
function inherited_post_super_holder_order() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) {
            super(n = n + 1);
            this.n = H.combine(n = n + 1, n = n + 1) * 100 + this.n * 10 + n;
        }
    }
    return new Leaf(0).n * 10000 + new Leaf(3).n;
}
var a = inherited_post_super_holder_order();

// A constant true arm does not authorize skipping the uncalled unknown global.
//--- inherited-post-super-holder-dead-ambient.js
const H = {read: n => n + 1};
function inherited_post_super_holder_dead_ambient() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) {
            super(n);
            if (true) this.n = H.read(this.n);
            else this.n = unknown(this.n);
        }
    }
    return new Leaf(7).n;
}
var a = inherited_post_super_holder_dead_ambient();

// Every holder slot requires a closed body, including the uncalled slot.
//--- inherited-post-super-holder-unused-ambient.js
const H = {read: n => n + 1, unused: n => unknown(n)};
function inherited_post_super_holder_unused_ambient() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = H.read(this.n); }
    }
    return new Leaf(7).n;
}
var a = inherited_post_super_holder_unused_ambient();

//--- inherited-post-super-holder-replaced.js
var H = {read: n => n + 1};
H = {read: n => n + 2};
function inherited_post_super_holder_replaced() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = H.read(this.n); }
    }
    return new Leaf(7).n;
}
var a = inherited_post_super_holder_replaced();

//--- inherited-post-super-holder-slot-replaced.js
const H = {read: n => n + 1};
H.read = n => n + 2;
function inherited_post_super_holder_slot_replaced() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = H.read(this.n); }
    }
    return new Leaf(7).n;
}
var a = inherited_post_super_holder_slot_replaced();

// Method syntax alone cannot prove that the holder receiver is unused.
//--- inherited-post-super-holder-receiver.js
const H = {read(n) { return this ? n + 1 : 0; }};
function inherited_post_super_holder_receiver() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = H.read(this.n); }
    }
    return new Leaf(7).n;
}
var a = inherited_post_super_holder_receiver();

// Surplus argument frames remain outside the bounded callable-holder proof.
//--- inherited-post-super-holder-surplus.js
const H = {read: n => n + 1};
function inherited_post_super_holder_surplus() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = H.read(this.n, 9); }
    }
    return new Leaf(7).n;
}
var a = inherited_post_super_holder_surplus();

// A closed helper can borrow the initialized receiver for ordinary field reads.
//--- inherited-post-super-holder-argument-receiver.js
const H = {read: receiver => receiver.n + 1};
function inherited_post_super_holder_argument_receiver() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = H.read(this); }
    }
    return new Leaf(7).n;
}
var a = inherited_post_super_holder_argument_receiver();

// The post-super allowance cannot make an uninitialized receiver available early.
//--- inherited-post-super-holder-before.js
const H = {read: receiver => receiver.n + 1};
function inherited_post_super_holder_before() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor() { const n = H.read(this); super(n); }
    }
    return new Leaf().n;
}
var a = inherited_post_super_holder_before();

// Primitive branch arguments avoid transporting holder targets around receiver guards.
//--- inherited-post-super-holder-branch-values.js
const H = {read: n => n + 1, scale: n => n * 10};
function inherited_post_super_holder_branch_values() {
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) {
            super(n);
            if (n) {
                if (n > 1) this.n = H.scale(n);
                else this.n = H.read(n);
            } else this.n = H.read(3);
        }
    }
    return new Leaf(2).n * 10000 + new Leaf(1).n * 100 + new Leaf(0).n;
}
var a = inherited_post_super_holder_branch_values();
