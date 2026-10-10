// A shared inherited disposer clears every fixed field on both receiver kinds.
//--- inherited-own-fields-iterate-shared.js
function inherited_own_fields_iterate_shared() {
    class Base {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base { constructor() { super(); this.second = 8; } }
    return new Base().dispose() * 100 + new Leaf().dispose();
}
var a = inherited_own_fields_iterate_shared();

// Both constructor arms establish the same ordered fields before iteration.
//--- inherited-own-fields-iterate-branches.js
function inherited_own_fields_iterate_branches() {
    class Base {
        constructor(n) {
            if (n) { this.z = 1; this.a = 2; }
            else { this.z = 3; this.a = 4; }
        }
        dispose() {
            const before = this.z * 10 + this.a;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.z === null) * 10 + (this.a === null);
        }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Base(1).dispose() * 10000 + new Leaf(0).dispose();
}
var a = inherited_own_fields_iterate_branches();

// The body does not run for an empty fixed field set.
//--- inherited-own-fields-iterate-empty.js
function inherited_own_fields_iterate_empty() {
    class Base {
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return 7;
        }
    }
    class Leaf extends Base { constructor() { super(); } }
    return new Leaf().dispose();
}
var a = inherited_own_fields_iterate_empty();

// Effects surrounding disposal retain their order and the pre-clear scalar value.
//--- inherited-own-fields-iterate-effects.js
function inherited_own_fields_iterate_effects() {
    class Base {
        constructor() { this.n = 1; this.first = 7; this.second = 9; }
        dispose() {
            this.n = this.n + 3;
            const before = this.n;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            this.n = before + (this.first === null) * 10 + (this.second === null);
            return this.n;
        }
    }
    class Leaf extends Base { constructor() { super(); } }
    return new Leaf().dispose();
}
var a = inherited_own_fields_iterate_effects();

// Each fresh snapshot clears the values present at its own point in the method.
//--- inherited-own-fields-iterate-repeated.js
function inherited_own_fields_iterate_repeated() {
    class Base {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            const first = (this.first === null) * 10 + (this.second === null);
            this.first = 3; this.second = 4;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return first * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base { constructor() { super(); } }
    return new Leaf().dispose();
}
var a = inherited_own_fields_iterate_repeated();

// Replacing the iterator changes which keys are visited, even when later restored.
//--- inherited-own-fields-iterate-array-replaced.js
function inherited_own_fields_iterate_array_replaced() {
    const original = Array.prototype[Symbol.iterator];
    Array.prototype[Symbol.iterator] = function() {
        return { next() { return { done: true }; } };
    };
    class Shape {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return (this.first === null) * 10 + (this.second === null);
        }
    }
    const result = new Shape().dispose();
    Array.prototype[Symbol.iterator] = original;
    return result;
}
var a = inherited_own_fields_iterate_array_replaced();

// Returning the names exposes the snapshot identity beyond the clearing loop.
//--- inherited-own-fields-iterate-snapshot-escape.js
function inherited_own_fields_iterate_snapshot_escape() {
    class Shape {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            const names = Object.getOwnPropertyNames(this);
            for (const t of names) this[t] = null
            return names;
        }
    }
    return new Shape().dispose().length;
}
var a = inherited_own_fields_iterate_snapshot_escape();

// Abrupt completion after one key cannot be replaced by clearing both fields.
//--- inherited-own-fields-iterate-break.js
function inherited_own_fields_iterate_break() {
    class Shape {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) { this[t] = null; break; }
            return (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_break();

// A method return inside the loop retains its exit and leaves later fields alone.
//--- inherited-own-fields-iterate-return.js
function inherited_own_fields_iterate_return() {
    class Shape {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) { this[t] = null; return 3; }
            return 9;
        }
    }
    const instance = new Shape();
    const result = instance.dispose();
    return result * 100 + (instance.first === null) * 10 + (instance.second === null);
}
var a = inherited_own_fields_iterate_return();

// Throwing after the first store must preserve the caller's exception path.
//--- inherited-own-fields-iterate-throw.js
function inherited_own_fields_iterate_throw() {
    class Shape {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) { this[t] = null; throw 3; }
        }
    }
    const instance = new Shape();
    try { instance.dispose(); }
    catch (value) {
        return value * 100 + (instance.first === null) * 10 + (instance.second === null);
    }
    return 9;
}
var a = inherited_own_fields_iterate_throw();

// Even an empty snapshot cannot hide an unproved body from the source census.
//--- inherited-own-fields-iterate-unused-ambient.js
function inherited_own_fields_iterate_unused_ambient() {
    class Shape {
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) { unknown(); this[t] = null; }
            return 7;
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_unused_ambient();
