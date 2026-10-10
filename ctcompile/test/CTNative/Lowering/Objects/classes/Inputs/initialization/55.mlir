// A base constructed only through its subclasses' super() calls has no field list of
// its own: each subclass proves its fields after inlining super(), where the
// subclass's arguments decide the base constructor's branch.
//--- inherited-own-fields-iterate-deferred-arm.js
function inherited_own_fields_iterate_deferred_arm() {
    class Base {
        constructor(t) { this.a = 3; t && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor(t) { super(t); } }
    return new Leaf(null).count() * 3 + new Leaf().count() * 4;
}
var a = inherited_own_fields_iterate_deferred_arm();

// Bootstrap's dispose() shape: the subclass's copy of the base's clearing loop is folded.
//--- inherited-own-fields-iterate-deferred-loop.js
function inherited_own_fields_iterate_deferred_loop() {
    class Base {
        constructor(t) { this.a = 3; t && (this.b = t); }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return this.a === null ? 7 : 9;
        }
    }
    class Leaf extends Base { constructor(t) { super(t); } }
    return new Leaf(null).dispose();
}
var a = inherited_own_fields_iterate_deferred_loop();

// Truthy arguments select the other branch's fields.
//--- inherited-own-fields-iterate-deferred-truthy.js
function inherited_own_fields_iterate_deferred_truthy() {
    class Base {
        constructor(t) { this.a = 3; if (t) { this.b = t; } else { this.c = 1; } }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor(t) { super(t); } }
    return new Leaf(5).count() * 3 + new Leaf(2).count() * 4;
}
var a = inherited_own_fields_iterate_deferred_truthy();

// Sibling subclasses whose arguments decide the same fields share the folded body.
//--- inherited-own-fields-iterate-deferred-siblings.js
function inherited_own_fields_iterate_deferred_siblings() {
    class Base {
        constructor(t) { this.a = 3; t && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Left extends Base { constructor(t) { super(t); } }
    class Right extends Base { constructor(t) { super(t); } }
    return new Left(null).count() * 3 + new Right().count() * 4;
}
var a = inherited_own_fields_iterate_deferred_siblings();

// An intermediate base with no instances of its own is deferred to the leaf as well.
//--- inherited-own-fields-iterate-deferred-chain.js
function inherited_own_fields_iterate_deferred_chain() {
    class Base {
        constructor(t) { this.a = 3; t && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Middle extends Base { constructor(t) { super(t); } }
    class Leaf extends Middle { constructor(t) { super(t); } }
    return new Leaf(null).count() * 7;
}
var a = inherited_own_fields_iterate_deferred_chain();

// The first subclass to fold the shared body fixes its fields for every sibling.
//--- inherited-own-fields-iterate-deferred-siblings-differ.js
function inherited_own_fields_iterate_deferred_siblings_differ() {
    class Base {
        constructor(t) { this.a = 3; t && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Left extends Base { constructor(t) { super(t); } }
    class Right extends Base { constructor(t) { super(t); } }
    return new Left(null).count() * 3 + new Right(5).count() * 2;
}
var a = inherited_own_fields_iterate_deferred_siblings_differ();

// Subclass arguments that disagree leave the base's branch to be decided at run time.
//--- inherited-own-fields-iterate-deferred-mixed.js
function inherited_own_fields_iterate_deferred_mixed() {
    class Base {
        constructor(t) { this.a = 3; t && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor(t) { super(t); } }
    return new Leaf(null).count() * 3 + new Leaf(5).count() * 2;
}
var a = inherited_own_fields_iterate_deferred_mixed();

// The decided branch calls count() before the subclass's fields are complete.
//--- inherited-own-fields-iterate-deferred-early-snapshot.js
function inherited_own_fields_iterate_deferred_early_snapshot() {
    class Base {
        constructor(t) { this.a = 3; t && (this.b = t, this.count()); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor(t) { super(t); } }
    return new Leaf(5).count() * 3 + 1;
}
var a = inherited_own_fields_iterate_deferred_early_snapshot();

// With no base instance, a field only the subclass adds is simply part of its shape.
//--- inherited-own-fields-iterate-deferred-added.js
function inherited_own_fields_iterate_deferred_added() {
    class Base {
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor() { super(); this.a = 7; } }
    return new Leaf().count() * 7;
}
var a = inherited_own_fields_iterate_deferred_added();
