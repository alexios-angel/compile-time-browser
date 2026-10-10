// A captured helper applied to the constructor parameter decides its branch when it
// returns the same truthiness for the argument of every `new`.
//--- own-fields-branch-helper-falsy.js
function own_fields_branch_helper_falsy() {
    const pick = t => typeof t == "string" ? t : null;
    class Shape {
        constructor(t) { this.a = 3; (t = pick(t)) && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Shape(null).count() * 3 + new Shape().count() * 4;
}
var a = own_fields_branch_helper_falsy();

// Bootstrap's element/isElement shape: a helper that calls a nested helper.
//--- own-fields-branch-helper-nested.js
function own_fields_branch_helper_nested() {
    const isElement = t => !(!t || "object" != typeof t) && void 0 !== t.nodeType;
    const element = t => isElement(t) ? t : "string" == typeof t && t.length > 0 ? t : null;
    class Shape {
        constructor(t) { this.a = 3; (t = element(t)) && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Shape(null).count() * 3 + new Shape().count() * 4;
}
var a = own_fields_branch_helper_nested();

// A truthy helper result keeps the branch's field.
//--- own-fields-branch-helper-truthy.js
function own_fields_branch_helper_truthy() {
    const pick = t => typeof t == "number" ? t : null;
    class Shape {
        constructor(t) { this.a = 3; (t = pick(t)) && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Shape(5).count() * 3 + new Shape(2).count() * 4;
}
var a = own_fields_branch_helper_truthy();

// Helper results that disagree leave the branch to be decided at run time.
//--- own-fields-branch-helper-mixed.js
function own_fields_branch_helper_mixed() {
    const pick = t => typeof t == "string" ? t : null;
    class Shape {
        constructor(t) { this.a = 3; (t = pick(t)) && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Shape(null).count() * 3 + new Shape("x").count() * 2;
}
var a = own_fields_branch_helper_mixed();

// A fresh object is not a primitive result.
//--- own-fields-branch-helper-object.js
function own_fields_branch_helper_object() {
    const pick = t => t ? t : {};
    class Shape {
        constructor(t) { this.a = 3; (t = pick(t)) && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Shape(null).count() * 3 + 1;
}
var a = own_fields_branch_helper_object();

// A helper whose taken path reads a property (t.length) is not evaluated.
//--- own-fields-branch-helper-property.js
function own_fields_branch_helper_property() {
    const pick = t => t.length > 0 ? t : null;
    class Shape {
        constructor(t) { this.a = 3; (t = pick(t)) && (this.b = t); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Shape("xy").count() * 3 + 1;
}
var a = own_fields_branch_helper_property();

// Bootstrap's BaseComponent shape: an implicit subclass constructor, a base branch on
// the element() helper and the base's clearing loop, all proved at the subclass.
//--- inherited-own-fields-iterate-deferred-helper.js
function inherited_own_fields_iterate_deferred_helper() {
    const isElement = t => !(!t || "object" != typeof t) && void 0 !== t.nodeType;
    const element = t => isElement(t) ? t : "string" == typeof t && t.length > 0 ? t : null;
    class Base {
        constructor(t) { this.a = 3; (t = element(t)) && (this.b = t); }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return this.a === null ? 7 : 9;
        }
    }
    class Leaf extends Base {}
    return new Leaf(null).dispose();
}
var a = inherited_own_fields_iterate_deferred_helper();
