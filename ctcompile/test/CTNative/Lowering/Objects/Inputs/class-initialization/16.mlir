// Captured computations determine values without changing the fixed field order.
//--- inherited-own-fields-iterate-helper-captured.js
function inherited_own_fields_iterate_helper_captured() {
    const plus = n => n + 1;
    class Shape {
        constructor(n) { this.first = plus(n); this.second = plus(n + 1); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_helper_captured();

// Fixed holder calls remain in the constructor before its own-key clearing method.
//--- inherited-own-fields-iterate-helper-holder.js
function inherited_own_fields_iterate_helper_holder() {
    const H = {read: n => n + 1, scale: n => n * 2};
    class Shape {
        constructor(n) { n = H.read(n); this.first = n; this.second = H.scale(n); }
        dispose() {
            const before = this.first * 100 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(3).dispose();
}
var a = inherited_own_fields_iterate_helper_holder();

// Nested helper captures retain their original identities through the field census.
//--- inherited-own-fields-iterate-helper-nested.js
function inherited_own_fields_iterate_helper_nested() {
    const plus = n => n + 1;
    const scale = n => n * 10;
    const nested = n => scale(plus(n));
    class Shape {
        constructor(n) { this.first = nested(n); this.second = plus(n); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(2).dispose();
}
var a = inherited_own_fields_iterate_helper_nested();

// Capture slot zero names different holders in the base and leaf constructors.
//--- inherited-own-fields-iterate-helper-distinct.js
function inherited_own_fields_iterate_helper_distinct() {
    const base = {read: n => n + 1};
    const leaf = {read: n => n * 10};
    class Base {
        constructor(n) { this.first = base.read(n); this.second = base.read(n + 1); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base {
        constructor(n) { super(n); this.second = leaf.read(n); }
    }
    return new Base(2).dispose() * 100000 + new Leaf(7).dispose();
}
var a = inherited_own_fields_iterate_helper_distinct();

// Super and ordinary helper arguments preserve their left-to-right scalar effects.
//--- inherited-own-fields-iterate-helper-arguments.js
function inherited_own_fields_iterate_helper_arguments() {
    const H = {combine: (left, right) => left * 10 + right};
    class Base {
        constructor(n) { this.first = n; this.second = n; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base {
        constructor(n) {
            super(n = n + 1);
            this.first = H.combine(n = n + 1, n = n + 1);
            this.second = n;
        }
    }
    return new Leaf(0).dispose() * 100000 + new Leaf(3).dispose();
}
var a = inherited_own_fields_iterate_helper_arguments();

// Passing the partially initialized receiver to a helper cannot use its final shape.
//--- inherited-own-fields-iterate-helper-receiver-escape.js
function inherited_own_fields_iterate_helper_receiver_escape() {
    const count = receiver => Object.getOwnPropertyNames(receiver).length;
    class Shape {
        constructor() { this.first = 7; this.second = count(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_helper_receiver_escape();

// An instance method can observe zero fields before the first constructor store.
//--- inherited-own-fields-iterate-helper-method-before-fields.js
function inherited_own_fields_iterate_helper_method_before_fields() {
    class Shape {
        constructor() { this.first = this.count(); this.second = 9; }
        count() { return Object.getOwnPropertyNames(this).length; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_helper_method_before_fields();

// Retaining a known holder does not discard ambient effects in its unused slots.
//--- inherited-own-fields-iterate-helper-unused-ambient.js
function inherited_own_fields_iterate_helper_unused_ambient() {
    const H = {read: n => n + 1, unused: n => unknown(n)};
    class Shape {
        constructor() { this.first = H.read(7); this.second = 9; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_helper_unused_ambient();

// A source-constant branch cannot erase an ambient call from a captured helper.
//--- inherited-own-fields-iterate-helper-dead-ambient.js
function inherited_own_fields_iterate_helper_dead_ambient() {
    const plus = n => { if (false) unknown(); return n + 1; };
    class Shape {
        constructor() { this.first = plus(7); this.second = 9; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_helper_dead_ambient();

// The complete holder census must retain a replacement made before construction.
//--- inherited-own-fields-iterate-helper-slot-replaced.js
function inherited_own_fields_iterate_helper_slot_replaced() {
    const H = {read: n => n + 1};
    class Shape {
        constructor() { this.first = H.read(7); this.second = 9; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    H.read = n => n + 2;
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_helper_slot_replaced();

// Proved helper values do not establish fields absent from a constructor arm.
//--- inherited-own-fields-iterate-helper-branch-fields.js
function inherited_own_fields_iterate_helper_branch_fields() {
    const plus = n => n + 1;
    class Shape {
        constructor(n) { this.first = plus(n); if (n) this.second = 2; }
        dispose() {
            const before = Object.getOwnPropertyNames(this).length;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 10 + (this.first === null);
        }
    }
    return new Shape(1).dispose() * 100 + new Shape(0).dispose();
}
var a = inherited_own_fields_iterate_helper_branch_fields();

// A holder method using its own receiver still needs the separate callable proof.
//--- inherited-own-fields-iterate-helper-holder-receiver.js
function inherited_own_fields_iterate_helper_holder_receiver() {
    const H = {read(n) { return this ? n + 1 : 0; }};
    class Shape {
        constructor() { this.first = H.read(7); this.second = 9; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape().dispose();
}
var a = inherited_own_fields_iterate_helper_holder_receiver();

// Structured selection cannot hide a receiver passed to another function.
//--- inherited-own-fields-iterate-helper-selected-receiver.js
function inherited_own_fields_iterate_helper_selected_receiver() {
    const read = receiver => receiver.first;
    class Shape {
        constructor(n) {
            this.first = 7;
            const selected = n ? this : 0;
            this.second = read(selected);
        }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(1).dispose();
}
var a = inherited_own_fields_iterate_helper_selected_receiver();
