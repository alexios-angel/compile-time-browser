// An implicit derived constructor forwards its actuals to the base: with no
// arguments object on either side it is super(t) over the base's formals.
//--- inherited-own-fields-iterate-deferred-implicit.js
function inherited_own_fields_iterate_deferred_implicit() {
    class Base {
        constructor(t) { this.a = 3; t && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base {}
    return new Leaf(null).count() * 3 + new Leaf().count() * 4;
}
var a = inherited_own_fields_iterate_deferred_implicit();

// A base that reads its arguments object sees every forwarded actual.
//--- inherited-own-fields-iterate-deferred-implicit-arguments.js
function inherited_own_fields_iterate_deferred_implicit_arguments() {
    class Base {
        constructor(t) { this.a = arguments.length; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base {}
    return new Leaf(1, 2).count() * 7;
}
var a = inherited_own_fields_iterate_deferred_implicit_arguments();

