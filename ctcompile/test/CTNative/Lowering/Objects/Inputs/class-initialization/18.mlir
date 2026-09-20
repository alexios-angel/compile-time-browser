// A constructor backedge is not an own field in the construction-point census.
//--- inherited-own-fields-iterate-getter-direct.js
function inherited_own_fields_iterate_getter_direct() {
    class Shape {
        constructor(n) { this.first = n; this.second = this.constructor.Default; }
        static get Default() { return 2; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_getter_direct();

// A construction method may read a proved getter and an already-present field.
//--- inherited-own-fields-iterate-getter-method.js
function inherited_own_fields_iterate_getter_method() {
    class Shape {
        constructor(n) { this.first = n; this.second = this.read(2); }
        static get Default() { return 3; }
        read(step) { return this.first + this.constructor.Default + step; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_getter_method();

// Nested methods retain the same receiver and original getter evaluation.
//--- inherited-own-fields-iterate-getter-nested.js
function inherited_own_fields_iterate_getter_nested() {
    class Shape {
        constructor(n) { this.first = n; this.second = this.outer(2); }
        static get Default() { return 3; }
        outer(step) { return this.inner(step) + 1; }
        inner(step) { return this.first + this.constructor.Default + step; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_getter_nested();

// Getter dependencies use their exact receiver just as outside construction.
//--- inherited-own-fields-iterate-getter-chain.js
function inherited_own_fields_iterate_getter_chain() {
    class Shape {
        constructor(n) { this.first = n; this.second = this.read(); }
        static get Default() { return 7; }
        static get Alias() { return this.Default + 1; }
        read() { return this.first + this.constructor.Alias; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_getter_chain();

// Argument assignments precede the method's getter evaluation, left to right.
//--- inherited-own-fields-iterate-getter-arguments.js
function inherited_own_fields_iterate_getter_arguments() {
    class Shape {
        constructor(n) {
            this.first = n;
            this.second = this.combine(n = n + 1, n = n + 1);
        }
        static get Default() { return 3; }
        combine(left, right) {
            return this.constructor.Default + this.first * 100 + left * 10 + right;
        }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(0).dispose() * 100000 + new Shape(3).dispose();
}
var a = inherited_own_fields_iterate_getter_arguments();

// Every getter call still creates its own object; construction cannot intern it.
//--- inherited-own-fields-iterate-getter-fresh.js
function inherited_own_fields_iterate_getter_fresh() {
    class Shape {
        constructor(n) { this.first = n; this.second = this.read(); }
        static get Default() { return { value: 3 }; }
        read() {
            const left = this.constructor.Default;
            const right = this.constructor.Default;
            return left.value + right.value + (left === right) * 100;
        }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_getter_fresh();

// Empty fresh objects exercise identity independently of getter field stores.
//--- inherited-own-fields-iterate-getter-fresh-empty.js
function inherited_own_fields_iterate_getter_fresh_empty() {
    class Shape {
        constructor(n) { this.first = n; this.second = this.read(); }
        static get Default() { return {}; }
        read() {
            const left = this.constructor.Default;
            const right = this.constructor.Default;
            return (left === right) * 100 + 6;
        }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_getter_fresh_empty();

// The shared inherited method resolves the same getter target on both classes.
//--- inherited-own-fields-iterate-getter-inherited.js
function inherited_own_fields_iterate_getter_inherited() {
    class Base {
        constructor(n) { this.first = n; this.second = this.read(); }
        static get Default() { return 3; }
        read() { return this.first + this.constructor.Default; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Base(2).dispose() * 100000 + new Leaf(7).dispose();
}
var a = inherited_own_fields_iterate_getter_inherited();

// A derived getter belongs to the actual receiver even inside the base method.
//--- inherited-own-fields-iterate-getter-leaf.js
function inherited_own_fields_iterate_getter_leaf() {
    class Base {
        constructor(n) { this.first = n; this.second = this.read(); }
        read() { return this.first + this.constructor.Default; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base {
        constructor(n) { super(n); }
        static get Default() { return 3; }
    }
    return new Leaf(7).dispose();
}
var a = inherited_own_fields_iterate_getter_leaf();

// A multi-level receiver selects the nearest getter, not the method's home.
//--- inherited-own-fields-iterate-getter-nearest.js
function inherited_own_fields_iterate_getter_nearest() {
    class Base {
        constructor(n) { this.first = n; this.second = this.read(); }
        static get Default() { return 3; }
        read() { return this.first + this.constructor.Default; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Middle extends Base {
        constructor(n) { super(n); }
        static get Default() { return 5; }
    }
    class Leaf extends Middle { constructor(n) { super(n); } }
    return new Leaf(7).dispose();
}
var a = inherited_own_fields_iterate_getter_nearest();

// One shared method must never inherit the first leaf's getter for all leaves.
//--- inherited-own-fields-iterate-getter-distinct.js
function inherited_own_fields_iterate_getter_distinct() {
    class Base {
        constructor(n) { this.first = n; this.second = this.read(); }
        static get Default() { return 1; }
        read() { return this.first + this.constructor.Default; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Left extends Base {
        constructor(n) { super(n); }
        static get Default() { return 3; }
    }
    class Right extends Base {
        constructor(n) { super(n); }
        static get Default() { return 5; }
    }
    return new Base(2).dispose() * 100000000
        + new Left(3).dispose() * 10000 + new Right(4).dispose();
}
var a = inherited_own_fields_iterate_getter_distinct();

// An own constructor field shadows the prototype's constructor backedge.
//--- inherited-own-fields-iterate-getter-shadow.js
function inherited_own_fields_iterate_getter_shadow() {
    class Shape {
        constructor(n) {
            this.first = n;
            this.constructor = { Default: 9 };
            this.second = this.read();
        }
        static get Default() { return 3; }
        read() { return this.constructor.Default; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 1000 + (this.first === null) * 100
                + (this.second === null) * 10 + (this.constructor === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_getter_shadow();

// Even an unused method that overwrites constructor invalidates its identity.
//--- inherited-own-fields-iterate-getter-overwrite.js
function inherited_own_fields_iterate_getter_overwrite() {
    class Shape {
        constructor(n) { this.first = n; this.second = this.read(); }
        static get Default() { return 3; }
        read() { return this.constructor.Default; }
        replace() { this.constructor = { Default: 9 }; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_getter_overwrite();

// A prototype mutation changes the constructor observed by the original method.
//--- inherited-own-fields-iterate-getter-prototype.js
function inherited_own_fields_iterate_getter_prototype() {
    class Shape {
        constructor(n) { this.first = n; this.second = this.read(); }
        static get Default() { return 3; }
        read() { return this.constructor.Default; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    Shape.prototype.constructor = { Default: 9 };
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_getter_prototype();

// The existing closed-getter proof cannot discard a captured write.
//--- inherited-own-fields-iterate-getter-effects.js
function inherited_own_fields_iterate_getter_effects() {
    let reads = 0;
    class Shape {
        constructor(n) { this.first = n; this.second = this.read(); }
        static get Default() { reads = reads + 1; return 3; }
        read() { return this.first + this.constructor.Default; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose() * 10 + reads;
}
var a = inherited_own_fields_iterate_getter_effects();

// Inherited getter bodies cannot replace the dynamic static receiver with Base.
//--- inherited-own-fields-iterate-getter-receiver.js
function inherited_own_fields_iterate_getter_receiver() {
    class Base {
        constructor(n) { this.first = n; this.second = this.read(); }
        static get Default() { return this === Leaf ? 9 : 3; }
        read() { return this.constructor.Default; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).dispose();
}
var a = inherited_own_fields_iterate_getter_receiver();

// Proving constructor identity does not make a later instance field present.
//--- inherited-own-fields-iterate-getter-missing.js
function inherited_own_fields_iterate_getter_missing() {
    class Shape {
        constructor() { this.first = 7; this.second = this.read(); this.later = 9; }
        static get Default() { return 3; }
        read() { return this.constructor.Default + (this.later === undefined ? 1 : 2); }
        dispose() {
            const before = this.first * 100 + this.second * 10 + this.later;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 1000 + (this.first === null) * 100
                + (this.second === null) * 10 + (this.later === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_getter_missing();

// A getter read cannot authorize a snapshot of the eventual constructor shape.
//--- inherited-own-fields-iterate-getter-snapshot.js
function inherited_own_fields_iterate_getter_snapshot() {
    class Shape {
        constructor() { this.first = 7; this.second = this.read(); }
        static get Default() { return 3; }
        read() { return this.constructor.Default + Object.getOwnPropertyNames(this).length; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_getter_snapshot();
