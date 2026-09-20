// An inherited receiver selects the base getter without changing its target.
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

// Every getter in the dependency chain keeps the same target on the leaf.
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

// Getter identity survives two heritage edges and constructor argument changes.
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

// The inherited constructor sees the same getter before its field is written.
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

// One shared method is checked against the base and both distinct leaf classes.
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

// Expanding the same getter twice must preserve two fresh object identities.
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

// A leaf override cannot reuse the getter target selected for the base method.
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

// The selected outer getter is unchanged, but its transitive dependency differs.
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

// Unused inherited getters still belong to the complete body census.
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

// An own constructor field changes lookup before the inherited method executes.
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

// A replaced prototype backedge cannot retain class-constructor authority.
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

// A derived static method also shadows an inherited getter with the same name.
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

// Direct and static-method reads use the same inherited getter environment.
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
