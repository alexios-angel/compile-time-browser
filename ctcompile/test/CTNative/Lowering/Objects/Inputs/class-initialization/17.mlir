// A method reads an existing field before the second constructor field exists.
//--- inherited-own-fields-iterate-method-read.js
function inherited_own_fields_iterate_method_read() {
    class Shape {
        constructor(n) { this.first = n; this.second = this.read(2); }
        read(step) { return this.first + step; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_method_read();

// Updating a present field does not create a new constructor field.
//--- inherited-own-fields-iterate-method-update.js
function inherited_own_fields_iterate_method_update() {
    class Shape {
        constructor(n) { this.first = n; this.bump(2); this.second = this.read(); }
        bump(step) { this.first = this.first + step; }
        read() { return this.first + 1; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_method_update();

// A receiver-independent instance method can compute the first field's value.
//--- inherited-own-fields-iterate-method-primitive.js
function inherited_own_fields_iterate_method_primitive() {
    class Shape {
        constructor(n) { this.first = this.seed(n); this.second = n + 2; }
        seed(n) { return n + 1; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_method_primitive();

// Nested same-receiver methods retain the construction point's field set.
//--- inherited-own-fields-iterate-method-nested.js
function inherited_own_fields_iterate_method_nested() {
    class Shape {
        constructor(n) { this.first = n; this.second = this.outer(2); }
        outer(step) { return this.inner(step) + 1; }
        inner(step) { this.first = this.first + step; return this.first * 2; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(5).dispose();
}
var a = inherited_own_fields_iterate_method_nested();

// An expanded base constructor dispatches to the nearest receiver override.
//--- inherited-own-fields-iterate-method-nearest.js
function inherited_own_fields_iterate_method_nearest() {
    class Base {
        constructor(n) { this.first = n; this.second = this.read(2); }
        read(step) { return this.first + step; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Middle extends Base {
        constructor(n) { super(n); }
        read(step) { return this.first * 10 + step; }
    }
    class Leaf extends Middle { constructor(n) { super(n); } }
    return new Base(2).dispose() * 100000 + new Leaf(3).dispose();
}
var a = inherited_own_fields_iterate_method_nearest();

// Scalar argument assignments run left to right before the method body.
//--- inherited-own-fields-iterate-method-arguments.js
function inherited_own_fields_iterate_method_arguments() {
    class Shape {
        constructor(n) {
            this.first = n;
            this.second = this.combine(n = n + 1, n = n + 1);
        }
        combine(left, right) { return this.first * 100 + left * 10 + right; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(0).dispose() * 100000 + new Shape(3).dispose();
}
var a = inherited_own_fields_iterate_method_arguments();

// A final field is not present when an earlier constructor method reads it.
//--- inherited-own-fields-iterate-method-missing.js
function inherited_own_fields_iterate_method_missing() {
    class Shape {
        constructor() { this.first = 7; this.second = this.read(); this.later = 9; }
        read() { return this.later === undefined ? 4 : 5; }
        dispose() {
            const before = this.first * 100 + this.second * 10 + this.later;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 1000 + (this.first === null) * 100
                + (this.second === null) * 10 + (this.later === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_method_missing();

// Snapshotting before the second field exists must not use the eventual shape.
//--- inherited-own-fields-iterate-method-snapshot.js
function inherited_own_fields_iterate_method_snapshot() {
    class Shape {
        constructor() { this.first = 7; this.second = this.count(); }
        count() { return Object.getOwnPropertyNames(this).length; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_method_snapshot();

// A method-created field needs a separate proof of ordered field presence.
//--- inherited-own-fields-iterate-method-add-field.js
function inherited_own_fields_iterate_method_add_field() {
    class Shape {
        constructor() { this.first = 7; this.second = this.initialize(); }
        initialize() { this.extra = 3; return this.first + 1; }
        dispose() {
            const before = this.first * 100 + this.second * 10 + this.extra;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 1000 + (this.first === null) * 100
                + (this.second === null) * 10 + (this.extra === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_method_add_field();

// A local alias cannot hide the partial receiver crossing an ordinary helper call.
//--- inherited-own-fields-iterate-method-alias-escape.js
function inherited_own_fields_iterate_method_alias_escape() {
    const read = receiver => receiver.first;
    class Shape {
        constructor() { this.first = 7; this.second = this.read(); }
        read() { const alias = this; return read(alias); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_method_alias_escape();

// Runtime termination does not establish an acyclic construction-point proof.
//--- inherited-own-fields-iterate-method-recursive.js
function inherited_own_fields_iterate_method_recursive() {
    class Shape {
        constructor() { this.first = 7; this.second = this.read(1); }
        read(n) { if (n) return this.read(n - 1); return this.first; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_method_recursive();

// A construction-point receiver proof cannot erase a dead ambient method effect.
//--- inherited-own-fields-iterate-method-dead-ambient.js
function inherited_own_fields_iterate_method_dead_ambient() {
    class Shape {
        constructor() { this.first = 7; this.second = this.read(); }
        read() { if (false) unknown(); return this.first + 1; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_method_dead_ambient();

// A snapshot folded while preparing the base remains a snapshot in a leaf call.
//--- inherited-own-fields-iterate-method-inherited-snapshot.js
function inherited_own_fields_iterate_method_inherited_snapshot() {
    class Base {
        constructor() { this.first = 7; this.second = 9; }
        count() { return Object.getOwnPropertyNames(this).length; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base {
        constructor() { super(); this.second = this.count(); }
    }
    return new Base().count() * 10000 + new Leaf().dispose();
}
var a = inherited_own_fields_iterate_method_inherited_snapshot();

// The nearest override can read a field that is absent during the base call.
//--- inherited-own-fields-iterate-method-override-missing.js
function inherited_own_fields_iterate_method_override_missing() {
    class Base {
        constructor() { this.first = 7; this.second = this.read(); }
        read() { return this.first; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base {
        constructor() { super(); }
        read() { return this.second === undefined ? 4 : 5; }
    }
    return new Base().dispose() * 100000 + new Leaf().dispose();
}
var a = inherited_own_fields_iterate_method_override_missing();

// Super-method expansion must retain a folded ancestor snapshot's identity.
//--- inherited-own-fields-iterate-method-super-snapshot.js
function inherited_own_fields_iterate_method_super_snapshot() {
    class Base {
        constructor() { this.first = 7; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Middle extends Base {
        constructor() { super(); }
        wrapper() { return super.count(); }
    }
    class Leaf extends Middle {
        constructor() { super(); this.second = this.wrapper(); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Base().count() * 10000 + new Leaf().dispose();
}
var a = inherited_own_fields_iterate_method_super_snapshot();

// Dispatch on this does not authorize passing the partial receiver as an argument.
//--- inherited-own-fields-iterate-method-receiver-argument.js
function inherited_own_fields_iterate_method_receiver_argument() {
    class Shape {
        constructor() { this.first = 7; this.second = this.read(this); }
        read(other) { return other.first; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_method_receiver_argument();

// Equal final shapes must not conceal a snapshot before the second field exists.
//--- inherited-own-fields-iterate-method-inherited-early-snapshot.js
function inherited_own_fields_iterate_method_inherited_early_snapshot() {
    class Base {
        constructor() { this.first = 7; this.second = this.seed(); }
        seed() { return this.first; }
        count() { return Object.getOwnPropertyNames(this).length; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Middle extends Base {
        constructor() { super(); }
        wrapper() { return super.count(); }
    }
    class Leaf extends Middle {
        constructor() { super(); }
        seed() { return this.count(); }
    }
    return new Base().count() * 10000 + new Leaf().dispose();
}
var a = inherited_own_fields_iterate_method_inherited_early_snapshot();

// Equal final shapes must not conceal a snapshot before the second field exists.
//--- inherited-own-fields-iterate-method-super-early-snapshot.js
function inherited_own_fields_iterate_method_super_early_snapshot() {
    class Base {
        constructor() { this.first = 7; this.second = this.seed(); }
        seed() { return this.first; }
        count() { return Object.getOwnPropertyNames(this).length; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Middle extends Base {
        constructor() { super(); }
        wrapper() { return super.count(); }
    }
    class Leaf extends Middle {
        constructor() { super(); }
        seed() { return this.wrapper(); }
    }
    return new Base().count() * 10000 + new Leaf().dispose();
}
var a = inherited_own_fields_iterate_method_super_early_snapshot();

// A distinct helper name also exercises the alias escape in the VM oracle.
//--- inherited-own-fields-iterate-method-alias-escape-distinct.js
function inherited_own_fields_iterate_method_alias_escape_distinct() {
    const helper = receiver => receiver.first;
    class Shape {
        constructor() { this.first = 7; this.second = this.read(); }
        read() { const alias = this; return helper(alias); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_method_alias_escape_distinct();

