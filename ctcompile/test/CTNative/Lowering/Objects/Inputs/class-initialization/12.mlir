// Runtime branches may choose field values while preserving the same own-key order.
//--- own-fields-branch-same-order.js
function own_fields_branch_same_order() {
    class Shape {
        constructor(n) {
            if (n) { this.z = 1; this.a = 2; }
            else { this.z = 3; this.a = 4; }
        }
        read() {
            const names = Object.getOwnPropertyNames(this);
            return names.length * 1000 + (names[0] === "z") * 100 + (names[1] === "a") * 10 + this.z + this.a;
        }
    }
    return new Shape(1).read() * 10000 + new Shape(0).read();
}
var a = own_fields_branch_same_order();

// Overwriting existing fields in different orders must retain their insertion order.
//--- own-fields-branch-overwrite.js
function own_fields_branch_overwrite() {
    class Shape {
        constructor(n) {
            this.z = 1; this.a = 2;
            if (n > 0) { this.a = 3; this.z = 4; }
            else { this.z = 5; this.a = 6; }
        }
        read() {
            const names = Object.getOwnPropertyNames(this);
            return names.length * 1000 + (names[0] === "z") * 100 + (names[1] === "a") * 10 + this.z + this.a;
        }
    }
    return new Shape(1).read() * 10000 + new Shape(0).read();
}
var a = own_fields_branch_overwrite();

// Every nested arm contributes the same final ordered field set.
//--- own-fields-branch-nested.js
function own_fields_branch_nested() {
    class Shape {
        constructor(n) {
            if (n) {
                if (n > 1) { this.z = 1; this.a = 2; }
                else { this.z = 3; this.a = 4; }
            } else { this.z = 5; this.a = 6; }
        }
        read() { return Object.getOwnPropertyNames(this).length * 100 + this.z * 10 + this.a; }
    }
    return new Shape(2).read() * 1000000 + new Shape(1).read() * 1000 + new Shape(0).read();
}
var a = own_fields_branch_nested();

// A shared inherited snapshot sees the same fields from either base constructor arm.
//--- inherited-own-fields-branch-base.js
function inherited_own_fields_branch_base() {
    class Base {
        constructor(n) {
            if (n) { this.z = 1; this.a = 2; }
            else { this.z = 3; this.a = 4; }
        }
        read() { return Object.getOwnPropertyNames(this).length * 100 + this.z * 10 + this.a; }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Base(1).read() * 1000 + new Leaf(0).read();
}
var a = inherited_own_fields_branch_base();

// Runtime control after completed super initialization preserves both leaf arms.
//--- inherited-own-fields-branch-leaf.js
function inherited_own_fields_branch_leaf() {
    class Base { constructor() { this.z = 1; } }
    class Leaf extends Base {
        constructor(n) {
            super();
            if (n) { this.a = 2; }
            else { this.a = 4; }
        }
        read() { return Object.getOwnPropertyNames(this).length * 100 + this.z * 10 + this.a; }
    }
    return new Leaf(1).read() * 1000 + new Leaf(0).read();
}
var a = inherited_own_fields_branch_leaf();

// Keep argument evaluation order, nested branch values and prior field order together.
//--- inherited-own-fields-branch-arguments.js
function inherited_own_fields_branch_arguments() {
    class Base {
        constructor(left, right) { this.z = left; this.a = right; }
        read() { return Object.getOwnPropertyNames(this).length * 100 + this.z * 10 + this.a; }
    }
    class Leaf extends Base {
        constructor(n, left, right) {
            super(left, right);
            if (n) {
                if (n > 1) { this.a = right + 1; this.z = left + 1; }
                else { this.z = left + 2; this.a = right + 2; }
            } else { this.a = right + 3; this.z = left + 3; }
        }
    }
    var value = 0;
    var first = new Leaf(2, value = value + 1, value = value + 1);
    var second = new Leaf(1, value = value + 1, value = value + 1);
    var third = new Leaf(0, value = value + 1, value = value + 1);
    return first.read() * 10000000 + second.read() * 10000 + third.read() * 10 + value;
}
var a = inherited_own_fields_branch_arguments();

// Equal field sets are insufficient when their insertion order differs.
//--- own-fields-branch-order.js
function own_fields_branch_order() {
    class Shape {
        constructor(n) {
            if (n) { this.z = 1; this.a = 2; }
            else { this.a = 3; this.z = 4; }
        }
        read() { return Object.getOwnPropertyNames(this)[0] === "z" ? 1 : 2; }
    }
    return new Shape(1).read() * 10 + new Shape(0).read();
}
var a = own_fields_branch_order();

// One observed invocation cannot establish a field omitted by another runtime arm.
//--- inherited-own-fields-branch-missing.js
function inherited_own_fields_branch_missing() {
    class Base { constructor() { this.z = 1; } }
    class Leaf extends Base {
        constructor(n) { super(); if (n) this.a = 2; }
        read() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Leaf(1).read() * 10 + new Leaf(0).read();
}
var a = inherited_own_fields_branch_missing();

// Early completion still requires a separate proof of conditional return transport.
//--- inherited-own-fields-branch-early.js
function inherited_own_fields_branch_early() {
    class Base { constructor() { this.z = 1; } }
    class Leaf extends Base {
        constructor(n) { super(); this.a = 2; if (n) return; this.a = 3; }
        read() { return Object.getOwnPropertyNames(this).length * 10 + this.a; }
    }
    return new Leaf(1).read() * 100 + new Leaf(0).read();
}
var a = inherited_own_fields_branch_early();

// A constructor observation sees the partially initialized receiver in that branch.
//--- own-fields-branch-observed.js
function own_fields_branch_observed() {
    class Shape {
        constructor(n) {
            this.z = 1;
            if (n) this.a = this.count();
            else this.a = 9;
        }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    var instance = new Shape(1);
    return instance.a * 100 + instance.count();
}
var a = own_fields_branch_observed();

// A loop is not a pair of proved fixed-shape constructor arms.
//--- own-fields-branch-loop.js
function own_fields_branch_loop() {
    class Shape {
        constructor(n) {
            this.z = 1;
            while (n > 0) { this.a = 2; n = n - 1; }
        }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Shape(1).count() * 10 + new Shape(0).count();
}
var a = own_fields_branch_loop();

// Unknown control cannot select which super initialization establishes the receiver.
//--- inherited-own-fields-branch-before-super.js
function inherited_own_fields_branch_before_super() {
    class Base {
        constructor(n) { this.z = n; }
        read() { return Object.getOwnPropertyNames(this).length * 10 + this.z; }
    }
    class Leaf extends Base {
        constructor(n) { if (n) super(7); else super(9); }
    }
    return new Leaf(1).read() * 100 + new Leaf(0).read();
}
var a = inherited_own_fields_branch_before_super();

// Cloned base branches and post-super leaf branches retain distinct nested captures.
//--- inherited-branch-helper.js
function inherited_branch_helper() {
    const plus = n => n + 1;
    const scale = n => n * 10;
    const base = n => plus(n);
    const leaf = n => scale(n);
    class Base {
        constructor(n) {
            if (n) this.n = base(n);
            else this.n = base(3);
        }
    }
    class Leaf extends Base {
        constructor(n) {
            super(n);
            if (n) this.n = leaf(this.n);
            else this.n = leaf(this.n + 1);
        }
    }
    return new Leaf(7).n * 1000 + new Leaf(0).n;
}
var a = inherited_branch_helper();

// A source-constant condition cannot discard the unused arm before its effect census.
//--- inherited-own-fields-branch-unused-ambient.js
function inherited_own_fields_branch_unused_ambient() {
    class Base { constructor() { this.a = 1; } }
    class Leaf extends Base {
        constructor() {
            super();
            if (true) this.a = 2;
            else { unknown(); this.a = 3; }
        }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Leaf().count();
}
var a = inherited_own_fields_branch_unused_ambient();
