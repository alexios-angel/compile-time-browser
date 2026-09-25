// A snapshot contains initialized own fields, never the class methods.
//--- own-fields-length.js
function own_fields_length() {
    class Shape {
        constructor() { this.first = 7; this.second = 9; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    var instance = new Shape();
    return instance.count();
}
var a = own_fields_length();

// Replacing a value preserves its property's original insertion position.
//--- own-fields-order.js
function own_fields_order() {
    class Shape {
        constructor() { this.z = 1; this.a = 2; this.z = 3; }
        read() {
            const names = Object.getOwnPropertyNames(this);
            return (names[0] === "z") * 100 + (names[1] === "a") * 10 + this.z;
        }
    }
    var instance = new Shape();
    return instance.read();
}
var a = own_fields_order();

// Exact snapshot keys become ordinary field writes, including null clearing.
//--- own-fields-clear.js
function own_fields_clear() {
    class Shape {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            const names = Object.getOwnPropertyNames(this);
            this[names[0]] = null;
            this[names[1]] = null;
            return (this.first === null) * 10 + (this.second === null);
        }
    }
    var instance = new Shape();
    return instance.dispose();
}
var a = own_fields_clear();

// Folding snapshots preserves the ordinary method effects between them.
//--- own-fields-effects.js
function own_fields_effects() {
    class Shape {
        constructor() { this.first = 1; this.second = 2; }
        read() {
            const first = Object.getOwnPropertyNames(this);
            this.first = this.first + 3;
            const second = Object.getOwnPropertyNames(this);
            this.second = this.first + second.length;
            return first.length * 100 + this.first * 10 + this.second;
        }
    }
    var instance = new Shape();
    return instance.read();
}
var a = own_fields_effects();

// A nested shadowed key makes the importer's capture census box the outer key.
//--- own-fields-boxed-key.js
function own_fields_boxed_key() {
    const helper = key => key;
    class Shape {
        constructor() { this.a = 7; }
        read() { return helper(1); }
    }
    var instance = new Shape();
    var key = Object.getOwnPropertyNames(instance)[0];
    return (key === "a") * 10 + instance.read();
}
var a = own_fields_boxed_key();

// Preserve B.dispose's original loop: iterator consumption needs another proof.
//--- own-fields-loop.js
function own_fields_loop() {
    class Shape {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return (this.first === null) * 10 + (this.second === null);
        }
    }
    var instance = new Shape();
    return instance.dispose();
}
var a = own_fields_loop();

// The observed constructor argument cannot establish an unconditional shape.
//--- own-fields-conditional.js
function own_fields_conditional() {
    class Shape {
        constructor(n) { if (n) this.first = 7; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    var instance = new Shape(1);
    return instance.count();
}
var a = own_fields_conditional();

//--- own-fields-dynamic.js
function own_fields_dynamic() {
    class Shape {
        constructor(key) { this[key] = 7; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    var instance = new Shape("first");
    return instance.count();
}
var a = own_fields_dynamic();

// Adding a later field invalidates the complete constructor field set.
//--- own-fields-new-field.js
function own_fields_new_field() {
    class Shape {
        constructor() { this.first = 7; }
        count() { this.second = 9; return Object.getOwnPropertyNames(this).length; }
    }
    var instance = new Shape();
    return instance.count();
}
var a = own_fields_new_field();

// Index-like property names use a different enumeration order.
//--- own-fields-numeric-key.js
function own_fields_numeric_key() {
    class Shape {
        constructor() { this.a = 1; this["2"] = 2; }
        read() { return (Object.getOwnPropertyNames(this)[0] === "2") * 7; }
    }
    var instance = new Shape();
    return instance.read();
}
var a = own_fields_numeric_key();

// Passing the receiver to another helper is not an own-key snapshot proof.
//--- own-fields-receiver-escape.js
function own_fields_receiver_escape() {
    const names = receiver => Object.getOwnPropertyNames(receiver).length;
    class Shape {
        constructor() { this.first = 7; }
        count() { return names(this); }
    }
    var instance = new Shape();
    return instance.count();
}
var a = own_fields_receiver_escape();

//--- own-fields-snapshot-escape.js
function own_fields_snapshot_escape() {
    class Shape {
        constructor() { this.first = 7; }
        names() { return Object.getOwnPropertyNames(this); }
    }
    var instance = new Shape();
    instance.names();
    return 7;
}
var a = own_fields_snapshot_escape();

// Callback invocation must not disappear when the snapshot is inspected.
//--- own-fields-callback.js
function own_fields_callback() {
    class Shape {
        constructor() { this.first = 7; }
        dispose() {
            Object.getOwnPropertyNames(this).forEach(t => { this[t] = null; });
            return (this.first === null) * 7;
        }
    }
    var instance = new Shape();
    return instance.dispose();
}
var a = own_fields_callback();

// Restoring the global afterwards does not undo a changed snapshot identity.
//--- own-fields-object-replaced.js
function own_fields_object_replaced() {
    const original = Object;
    Object = { getOwnPropertyNames: () => ["changed"] };
    class Shape {
        constructor() { this.first = 7; this.second = 9; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    var instance = new Shape();
    var result = instance.count();
    Object = original;
    return result;
}
var a = own_fields_object_replaced();

//--- own-fields-helper-replaced.js
function own_fields_helper_replaced() {
    Object.getOwnPropertyNames = () => ["changed"];
    class Shape {
        constructor() { this.first = 7; this.second = 9; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    var instance = new Shape();
    return instance.count();
}
var a = own_fields_helper_replaced();

// A base field can shadow a method inherited by the leaf.
//--- inherited-own-fields-collision.js
function inherited_own_fields_collision() {
    class Base {
        constructor() { this.count = 7; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor() { super(); } }
    var instance = new Leaf();
    return instance.count;
}
var a = inherited_own_fields_collision();

// The complete census still sees an uncalled ambient body.
//--- own-fields-unused-ambient.js
function own_fields_unused_ambient() {
    class Shape {
        constructor() { this.first = 7; }
        count(n) { if (n) unknown(); return Object.getOwnPropertyNames(this).length; }
    }
    var instance = new Shape();
    return 7;
}
var a = own_fields_unused_ambient();

// A replacement object is not the constructed receiver, even without methods.
//--- own-fields-replacement-return.js
function own_fields_replacement_return() {
    class Shape { constructor(value) { this.a = 1; return value; } }
    var instance = new Shape({ b: 2 });
    return (Object.getOwnPropertyNames(instance)[0] === "b") * 7;
}
var a = own_fields_replacement_return();
