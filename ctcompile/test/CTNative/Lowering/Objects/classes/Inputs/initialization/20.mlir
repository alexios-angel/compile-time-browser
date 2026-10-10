// The complete global chain reads the same construction-time receiver.
//--- inherited-own-fields-iterate-forward-direct.js
function read_field(receiver, amount) { return receiver.first + amount; }
function forward_field(receiver, amount) { return read_field(receiver, amount); }
function inherited_own_fields_iterate_forward_direct() {
    class Shape {
        constructor(n) { this.first = n; this.second = forward_field(this, 2); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_forward_direct();

// Each immutable captured helper keeps its original body and read-only formal.
//--- inherited-own-fields-iterate-forward-captured.js
function inherited_own_fields_iterate_forward_captured() {
    const read = (receiver, amount) => receiver.first + amount;
    const middle = (receiver, amount) => read(receiver, amount);
    const forward = (receiver, amount) => middle(receiver, amount);
    class Shape {
        constructor(n) { this.first = n; this.second = forward(this, 3); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_forward_captured();

// A holder slot forwards its argument through a captured helper, not its receiver.
//--- inherited-own-fields-iterate-forward-holder.js
function inherited_own_fields_iterate_forward_holder() {
    const read = receiver => receiver.first + 2;
    const forward = receiver => read(receiver);
    const H = {read: receiver => forward(receiver)};
    class Shape {
        constructor(n) { this.first = n; this.second = H.read(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_forward_holder();

// Reordered and repeated receiver operands retain their own explicit parameter slots.
//--- inherited-own-fields-iterate-forward-arguments.js
function inherited_own_fields_iterate_forward_arguments() {
    const combine = (left, amount, right) => left.first * 100 + amount * 10 + right.first;
    const forward = (amount, first, second) => combine(second, amount, first);
    class Shape {
        constructor(n) { this.first = n; this.second = forward(2, this, this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(3).dispose();
}
var a = inherited_own_fields_iterate_forward_arguments();

// Base and derived calls preserve argument effects around the shared borrow chain.
//--- inherited-own-fields-iterate-forward-inherited.js
function inherited_own_fields_iterate_forward_inherited() {
    const combine = (left, receiver, right) => receiver.first * 100 + left * 10 + right;
    const forward = (left, receiver, right) => combine(left, receiver, right);
    class Base {
        constructor(n) {
            this.first = n;
            this.second = forward(n = n + 1, this, n = n + 1);
        }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base {
        constructor(n) { super(n); this.second = forward(n = n + 1, this, n = n + 1) + 1; }
    }
    return new Base(0).dispose() * 100000 + new Leaf(3).dispose();
}
var a = inherited_own_fields_iterate_forward_inherited();

// A deeper read still needs the field at every call, not merely in the final shape.
//--- inherited-own-fields-iterate-forward-missing.js
function inherited_own_fields_iterate_forward_missing() {
    const read = receiver => receiver.second === void 0 ? 1 : 2;
    const forward = receiver => read(receiver);
    class Complete {
        constructor(n) { this.first = n; this.second = 9; this.second = forward(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Early {
        constructor(n) { this.first = n; this.second = forward(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Complete(3).dispose() * 100000 + new Early(7).dispose();
}
var a = inherited_own_fields_iterate_forward_missing();

// A leaf store retains the receiver even if the forwarding helper only returns a number.
//--- inherited-own-fields-iterate-forward-store.js
var retained;
function retain_field(receiver) { retained = receiver; return receiver.first; }
function forward_field(receiver) { return retain_field(receiver); }
function inherited_own_fields_iterate_forward_store() {
    class Shape {
        constructor(n) { this.first = n; this.second = forward_field(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose() * 10 + (retained.first === null);
}
var a = inherited_own_fields_iterate_forward_store();

// Returning an alias through several helpers does not become a read-only borrow.
//--- inherited-own-fields-iterate-forward-return.js
function inherited_own_fields_iterate_forward_return() {
    const identity = receiver => receiver;
    const middle = receiver => identity(receiver);
    const forward = receiver => middle(receiver);
    class Shape {
        constructor(n) { this.first = n; this.second = forward(this).first; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_forward_return();

// Writes in the deepest helper remain outside the read-only receiver proof.
//--- inherited-own-fields-iterate-forward-write.js
function inherited_own_fields_iterate_forward_write() {
    const change = receiver => { receiver.first = receiver.first + 1; return receiver.first; };
    const forward = receiver => change(receiver);
    class Shape {
        constructor(n) { this.first = n; this.second = forward(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_forward_write();

// Actual String arguments cannot authorize an unchecked dynamic leaf key.
//--- inherited-own-fields-iterate-forward-dynamic.js
function inherited_own_fields_iterate_forward_dynamic() {
    const read = (receiver, key) => receiver[key];
    const forward = receiver => read(receiver, "first");
    class Shape {
        constructor(n) { this.first = n; this.second = forward(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_forward_dynamic();

// Finite runtime recursion cannot supply a provisional proof for its own formal.
//--- inherited-own-fields-iterate-forward-recursive.js
function recursive_field(receiver, count) {
    if (count > 0) return recursive_field(receiver, count - 1);
    return receiver.first;
}
function forward_field(receiver) { return recursive_field(receiver, 2); }
function inherited_own_fields_iterate_forward_recursive() {
    class Shape {
        constructor(n) { this.first = n; this.second = forward_field(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_forward_recursive();

// Forwarding does not omit ambient effects in an original unused holder slot.
//--- inherited-own-fields-iterate-forward-unused-effects.js
function inherited_own_fields_iterate_forward_unused_effects() {
    const read = receiver => receiver.first + 2;
    const H = {read: receiver => read(receiver), unused: receiver => unknown(receiver)};
    class Shape {
        constructor(n) { this.first = n; this.second = H.read(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_forward_unused_effects();
