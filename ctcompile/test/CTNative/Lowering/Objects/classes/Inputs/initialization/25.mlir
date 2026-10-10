// `this.constructor.Ready` on a Leaf instance resolves to Base's static getter.
//--- inherited-getter-constant.js
function inherited_getter_constant() {
    class Base {
        constructor(n) { this.n = n; }
        static get Ready() { return 3; }
        read() { return this.n + this.constructor.Ready; }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).read();
}
var a = inherited_getter_constant();

// Getters that read other getters through `this` all resolve the same way from Leaf.
//--- inherited-getter-dependencies.js
function inherited_getter_dependencies() {
    class Base {
        constructor(n) { this.n = n; }
        static get Unit() { return 2; }
        static get Factor() { return this.Unit + 3; }
        static get Ready() { return this.Factor * 2; }
        read() { return this.n + this.constructor.Ready; }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).read();
}
var a = inherited_getter_dependencies();

// The getter resolves the same through two levels of `extends` with changed arguments.
//--- inherited-getter-three-levels.js
function inherited_getter_three_levels() {
    class Base {
        constructor(n) { this.n = n * 2; }
        static get Ready() { return 3; }
        read() { return this.n + this.constructor.Ready; }
    }
    class Middle extends Base { constructor(n) { super(n + 1); } }
    class Leaf extends Middle { constructor(n) { super(n); } }
    return new Leaf(4).read();
}
var a = inherited_getter_three_levels();

// The inherited constructor reads the getter before it writes `this.n`.
//--- inherited-getter-constructor.js
function inherited_getter_constructor() {
    class Base {
        constructor(n) { this.n = n + this.constructor.Ready; }
        static get Ready() { return 3; }
        read() { return this.n; }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).read();
}
var a = inherited_getter_constructor();

// One shared method resolves the getter correctly for Base and two distinct subclasses.
//--- inherited-getter-shared-receivers.js
function inherited_getter_shared_receivers() {
    class Base {
        constructor(n) { this.n = n; }
        static get Ready() { return 7; }
        read() { return this.n + this.constructor.Ready; }
    }
    class Left extends Base { constructor(n) { super(n); } }
    class Right extends Base { constructor(n) { super(n); } }
    const base = new Base(2), left = new Left(3), right = new Right(4);
    return base.read() * 10000 + left.read() * 100 + right.read();
}
var a = inherited_getter_shared_receivers();

// Inlining a getter that returns `{}` twice must still give two distinct objects.
//--- inherited-getter-fresh-empty.js
function inherited_getter_fresh_empty() {
    class Base {
        constructor(n) { this.n = n; }
        static get Default() { return {}; }
        read() {
            const left = this.constructor.Default;
            const right = this.constructor.Default;
            return (left === right) * 100 + this.n;
        }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).read();
}
var a = inherited_getter_fresh_empty();

// Leaf's own `Ready` getter overrides Base's; the method must not reuse Base's getter.
//--- inherited-getter-override.js
function inherited_getter_override() {
    class Base {
        constructor(n) { this.n = n; }
        static get Ready() { return 3; }
        read() { return this.n + this.constructor.Ready; }
    }
    class Leaf extends Base {
        constructor(n) { super(n); }
        static get Ready() { return 9; }
    }
    return new Base(2).read() * 100 + new Leaf(7).read();
}
var a = inherited_getter_override();

// Leaf overrides only `Unit`, which `Ready` reads indirectly through `Factor`.
//--- inherited-getter-override-dependency.js
function inherited_getter_override_dependency() {
    class Base {
        constructor(n) { this.n = n; }
        static get Unit() { return 2; }
        static get Factor() { return this.Unit + 1; }
        static get Ready() { return this.Factor * 2; }
        read() { return this.n + this.constructor.Ready; }
    }
    class Leaf extends Base {
        constructor(n) { super(n); }
        static get Unit() { return 9; }
    }
    return new Base(2).read() * 100 + new Leaf(7).read();
}
var a = inherited_getter_override_dependency();

// Refused: an unused getter calling an unknown global counts; every getter is checked.
//--- inherited-getter-unused-ambient.js
function inherited_getter_unused_ambient() {
    class Base {
        constructor(n) { this.n = n; }
        static get Ready() { return 3; }
        static get Unused() { return ambient(); }
        read() { return this.n + this.constructor.Ready; }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).read();
}
var a = inherited_getter_unused_ambient();

// Refused: the constructor sets an own `constructor` field that hides the class.
//--- inherited-getter-constructor-shadow.js
function inherited_getter_constructor_shadow() {
    class Base {
        constructor(n) { this.n = n; this.constructor = {Ready: 9}; }
        static get Ready() { return 3; }
        read() { return this.n + this.constructor.Ready; }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).read();
}
var a = inherited_getter_constructor_shadow();

// Refused: Leaf.prototype.constructor is replaced, so it no longer names the class.
//--- inherited-getter-prototype-replaced.js
function inherited_getter_prototype_replaced() {
    class Base {
        constructor(n) { this.n = n; }
        static get Ready() { return 3; }
        read() { return this.n + this.constructor.Ready; }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    Leaf.prototype.constructor = {Ready: 9};
    return new Leaf(7).read();
}
var a = inherited_getter_prototype_replaced();

// Refused: Leaf's static method `Ready` hides Base's static getter of the same name.
//--- inherited-getter-static-method-collision.js
function inherited_getter_static_method_collision() {
    class Base {
        constructor(n) { this.n = n; }
        static get Ready() { return 3; }
        read() { return this.constructor.Ready === 3 ? this.n : 9; }
    }
    class Leaf extends Base {
        constructor(n) { super(n); }
        static Ready() { return 11; }
    }
    return new Leaf(7).read();
}
var a = inherited_getter_static_method_collision();

// Direct `Leaf.Ready` and a static method's `this.Ready` resolve to the same getter.
//--- inherited-getter-static-reads.js
function inherited_getter_static_reads() {
    class Base {
        constructor(n) { this.n = n; }
        static get Ready() { return 3; }
    }
    class Leaf extends Base {
        constructor(n) { super(n); }
        static read() { return this.Ready; }
    }
    const leaf = new Leaf(7);
    return leaf.n * 100 + Leaf.read() * 10 + Leaf.Ready;
}
var a = inherited_getter_static_reads();
