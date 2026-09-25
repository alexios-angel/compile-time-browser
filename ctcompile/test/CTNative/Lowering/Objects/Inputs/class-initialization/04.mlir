// Ordinary calls after super retain their receiver and evaluation order.
//--- inherited-post-super-method.js
function post_super_method() {
    class Base {
        constructor(n) { this.n = n; }
        read(t) { return this.n + t; }
    }
    class Derived extends Base {
        constructor(n) { super(n); this.result = this.read(4); }
    }
    return new Derived(3).result;
}
var a = post_super_method();

//--- inherited-post-super-override.js
function post_super_override() {
    class Base {
        constructor(n) { this.n = n; }
        read(t) { return 999; }
    }
    class Derived extends Base {
        constructor(n) { super(n); this.result = this.read(4); }
        read(t) { return this.n * 2 + t; }
    }
    return new Derived(3).result;
}
var a = post_super_override();

//--- inherited-post-super-order.js
function post_super_order() {
    class Base {
        constructor() { this.n = 0; }
        step(t) { this.n = this.n * 10 + t; return this.n; }
        mix(a, b) { return a * 100 + b; }
    }
    class Derived extends Base {
        constructor() {
            super();
            this.result = this.mix(this.step(1), this.step(2));
            this.result = this.result * 100 + this.n;
        }
    }
    return new Derived().result;
}
var a = post_super_order();

// Each inlined constructor must still dispatch through the leaf receiver.
//--- inherited-post-super-chain.js
function post_super_chain() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
    }
    class Middle extends Base {
        constructor(n) { super(n); this.n = this.read() + 1; }
    }
    class Derived extends Middle {
        constructor(n) { super(n); this.n = this.read() + 10; }
        read() { return this.n * 2; }
    }
    return new Derived(3).n;
}
var a = post_super_chain();

//--- inherited-post-super-foreign.js
function post_super_foreign() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
    }
    class Derived extends Base {
        constructor(n, other) { super(n); this.n = other.read(); }
    }
    return new Derived(7, new Base(9)).n;
}
var a = post_super_foreign();

//--- inherited-post-super-before.js
function post_super_before() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
    }
    class Derived extends Base {
        constructor(n) { this.read(); super(n); }
    }
    return new Derived(7).n;
}
var a = post_super_before();

//--- inherited-post-super-dynamic.js
function post_super_dynamic() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
    }
    class Derived extends Base {
        constructor(n, key) { super(n); this.n = this[key](); }
    }
    return new Derived(7, "read").n;
}
var a = post_super_dynamic();

//--- inherited-post-super-replaced.js
function post_super_replaced() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
        other() { return 9; }
    }
    class Derived extends Base {
        constructor(n) { super(n); this.read = this.other; this.n = this.read(); }
    }
    return new Derived(7).n;
}
var a = post_super_replaced();
