// An exact global helper reads only the field present at its constructor call.
//--- inherited-own-fields-iterate-borrow-direct.js
function borrowed_field(receiver, step) { return receiver.first + step; }
function inherited_own_fields_iterate_borrow_direct() {
    class Shape {
        constructor(n) { this.first = n; this.second = borrowed_field(this, 2); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_borrow_direct();

// Captured immutable helpers keep their receiver formal as a read-only borrow.
//--- inherited-own-fields-iterate-borrow-captured.js
function inherited_own_fields_iterate_borrow_captured() {
    const plus = (receiver, step) => receiver.first + step;
    class Shape {
        constructor(n) { this.first = n; this.second = plus(this, 2); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_borrow_captured();

// A fixed holder's helper borrows its argument, independently of the holder.
//--- inherited-own-fields-iterate-borrow-holder.js
function inherited_own_fields_iterate_borrow_holder() {
    const H = {read: receiver => receiver.first + 2};
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
var a = inherited_own_fields_iterate_borrow_holder();

// Every receiver argument must reach a separately checked read-only formal.
//--- inherited-own-fields-iterate-borrow-two-arguments.js
function inherited_own_fields_iterate_borrow_two_arguments() {
    const combine = (left, right) => left.first * 10 + right.first;
    class Shape {
        constructor(n) { this.first = n; this.second = combine(this, this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_borrow_two_arguments();

// Base and leaf construction keep the same helper and their actual receiver.
//--- inherited-own-fields-iterate-borrow-inherited.js
function inherited_own_fields_iterate_borrow_inherited() {
    const plus = receiver => receiver.first + 2;
    class Base {
        constructor(n) { this.first = n; this.second = plus(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base {
        constructor(n) { super(n); this.second = plus(this) + 1; }
    }
    return new Base(2).dispose() * 100000 + new Leaf(7).dispose();
}
var a = inherited_own_fields_iterate_borrow_inherited();

// Scalar argument effects surround the borrow in their original source order.
//--- inherited-own-fields-iterate-borrow-arguments.js
function inherited_own_fields_iterate_borrow_arguments() {
    const combine = (left, receiver, right) => receiver.first * 100 + left * 10 + right;
    class Shape {
        constructor(n) {
            this.first = n;
            this.second = combine(n = n + 1, this, n = n + 1);
        }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(0).dispose() * 100000 + new Shape(3).dispose();
}
var a = inherited_own_fields_iterate_borrow_arguments();

// Identical final shapes cannot share a proof for fields absent at an early call.
//--- inherited-own-fields-iterate-borrow-missing.js
function inherited_own_fields_iterate_borrow_missing() {
    const inspect = receiver => receiver.second === undefined ? 1 : 2;
    class Complete {
        constructor(n) { this.first = n; this.second = 9; this.second = inspect(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Early {
        constructor(n) { this.first = n; this.second = inspect(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Complete(3).dispose() * 100000 + new Early(7).dispose();
}
var a = inherited_own_fields_iterate_borrow_missing();

// Reading a field does not authorize storing the receiver in another object.
//--- inherited-own-fields-iterate-borrow-store.js
function inherited_own_fields_iterate_borrow_store() {
    const retained = {value: 0};
    const keep = receiver => { retained.value = receiver; return receiver.first; };
    class Shape {
        constructor(n) { this.first = n; this.second = keep(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose() * 10 + (retained.value.first === null);
}
var a = inherited_own_fields_iterate_borrow_store();

// Returning the receiver exports an alias even when the caller only reads it.
//--- inherited-own-fields-iterate-borrow-return.js
function inherited_own_fields_iterate_borrow_return() {
    const identity = receiver => receiver;
    class Shape {
        constructor(n) { this.first = n; this.second = identity(this).first; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_borrow_return();

// A write through a formal is outside the read-only helper contract.
//--- inherited-own-fields-iterate-borrow-write.js
function inherited_own_fields_iterate_borrow_write() {
    const change = receiver => { receiver.first = receiver.first + 1; return receiver.first; };
    class Shape {
        constructor(n) { this.first = n; this.second = change(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_borrow_write();

// An immutable nested helper can still forward the receiver to an escaping use.
//--- inherited-own-fields-iterate-borrow-forward.js
function inherited_own_fields_iterate_borrow_forward() {
    const identity = receiver => receiver;
    const forward = receiver => identity(receiver);
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
var a = inherited_own_fields_iterate_borrow_forward();

// A helper shared by incompatible receiver shapes needs a proof at every call.
//--- inherited-own-fields-iterate-borrow-incompatible.js
function inherited_own_fields_iterate_borrow_incompatible() {
    const inspect = receiver => receiver.first;
    class Complete {
        constructor() { this.first = 7; this.second = inspect(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    class Missing {
        constructor() { this.second = inspect(this); }
        dispose() {
            const before = this.second === undefined ? 1 : 2;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 10 + (this.second === null);
        }
    }
    return new Complete().dispose() * 100 + new Missing().dispose();
}
var a = inherited_own_fields_iterate_borrow_incompatible();

// Replacing a helper slot invalidates its supposedly immutable read-only target.
//--- inherited-own-fields-iterate-borrow-slot-replaced.js
function inherited_own_fields_iterate_borrow_slot_replaced() {
    const H = {read: receiver => receiver.first + 2};
    class Shape {
        constructor(n) { this.first = n; this.second = H.read(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    H.read = receiver => receiver.first + 3;
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_borrow_slot_replaced();

// The complete holder census still sees ambient effects in an unused helper.
//--- inherited-own-fields-iterate-borrow-unused-effects.js
function inherited_own_fields_iterate_borrow_unused_effects() {
    const H = {read: receiver => receiver.first + 2, unused: receiver => unknown(receiver)};
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
var a = inherited_own_fields_iterate_borrow_unused_effects();

// A constructor/getter chain is not an ordinary existing-field read on a formal.
//--- inherited-own-fields-iterate-borrow-constructor.js
function inherited_own_fields_iterate_borrow_constructor() {
    const inspect = receiver => receiver.first + receiver.constructor.Default;
    class Shape {
        constructor(n) { this.first = n; this.second = inspect(this); }
        static get Default() { return 3; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_borrow_constructor();

// A helper formal cannot dispatch a method using the constructor's self proof.
//--- inherited-own-fields-iterate-borrow-method.js
function inherited_own_fields_iterate_borrow_method() {
    const inspect = receiver => receiver.read();
    class Shape {
        constructor(n) { this.first = n; this.second = inspect(this); }
        read() { return this.first + 2; }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose();
}
var a = inherited_own_fields_iterate_borrow_method();

// One constructor borrow cannot turn the same helper's primitive call into a pointer.
//--- inherited-own-fields-iterate-borrow-mixed-primitive.js
function borrowed_field(receiver) { return receiver.first; }
function inherited_own_fields_iterate_borrow_mixed_primitive() {
    class Shape {
        constructor(n) { this.first = n; this.second = borrowed_field(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose() * 10 + (borrowed_field(3) === undefined);
}
var a = inherited_own_fields_iterate_borrow_mixed_primitive();

// A missing ordinary argument remains undefined and throws on the original read.
//--- inherited-own-fields-iterate-borrow-missing-argument.js
function borrowed_field(receiver) { return receiver.first; }
function inherited_own_fields_iterate_borrow_missing_argument() {
    class Shape {
        constructor(n) { this.first = n; this.second = borrowed_field(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose() + borrowed_field();
}
var a = inherited_own_fields_iterate_borrow_missing_argument();

// Literal undefined isolates mixed call origins from the ambient binding control.
//--- inherited-own-fields-iterate-borrow-mixed-literal.js
function borrowed_field(receiver) { return receiver.first; }
function inherited_own_fields_iterate_borrow_mixed_literal() {
    class Shape {
        constructor(n) { this.first = n; this.second = borrowed_field(this); }
        dispose() {
            const before = this.first * 10 + this.second;
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return before * 100 + (this.first === null) * 10 + (this.second === null);
        }
    }
    return new Shape(7).dispose() * 10 + (borrowed_field(3) === void 0);
}
var a = inherited_own_fields_iterate_borrow_mixed_literal();
