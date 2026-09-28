// A base constructed only through its leaves' super calls has no own-key shape
// of its own: the leaf proves the snapshot after super normalization, where its
// actuals decide the base's arm.
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

// Bootstrap's dispose shape: the leaf folds the base's clearing loop.
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

// Truthy actuals select the other arm's fields.
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

// Siblings that decide the same fields share the folded body.
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

// An intermediate base with no instances defers as well.
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

// The first leaf to fold the shared body fixes its fields for every sibling.
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

// Leaf actuals that disagree leave the base arm to run time.
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

// The decided arm calls the snapshot before the leaf's fields are complete.
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

// With no base instance, a field only the leaf adds is simply part of its shape.
//--- inherited-own-fields-iterate-deferred-added.js
function inherited_own_fields_iterate_deferred_added() {
    class Base {
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor() { super(); this.a = 7; } }
    return new Leaf().count() * 7;
}
var a = inherited_own_fields_iterate_deferred_added();
