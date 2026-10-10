// Every construction of a class nothing extends passes an actual with the same
// truthiness, so the constructor arm on that formal runs the same way each time.
//--- own-fields-branch-decided-falsy.js
function own_fields_branch_decided_falsy() {
    class Shape {
        constructor(n) { this.a = 3; n && (this.b = n); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Shape(null).count() * 3 + new Shape().count() * 4;
}
var a = own_fields_branch_decided_falsy();

// Both actuals are truthy: the field is always present.
//--- own-fields-branch-decided-truthy.js
function own_fields_branch_decided_truthy() {
    class Shape {
        constructor(n) { this.a = 3; if (n) { this.b = n; } else { this.c = 1; } }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Shape(5).count() * 3 + new Shape(2).count() * 4;
}
var a = own_fields_branch_decided_truthy();

// Actuals that disagree leave the arm to run time.
//--- own-fields-branch-decided-mixed.js
function own_fields_branch_decided_mixed() {
    class Shape {
        constructor(n) { this.a = 3; n && (this.b = n); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    return new Shape(null).count() * 3 + new Shape(5).count() * 2;
}
var a = own_fields_branch_decided_mixed();

// A class that is extended has constructions through super as well.
//--- inherited-own-fields-branch-decided-extended.js
function inherited_own_fields_branch_decided_extended() {
    class Shape {
        constructor(n) { this.a = 3; n && (this.b = n); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Shape { constructor() { super(null); } }
    return new Shape(null).count() * 3 + new Leaf().count() * 4;
}
var a = inherited_own_fields_branch_decided_extended();
