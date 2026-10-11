// What the inheritance rewrite (ClosureLifting/ClassInheritance.cpp) leaves
// as it is, one program per reason. The reason is recorded on the class's
// constructor closure, and the class census refuses the class with it.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DYNAMIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/superwrite.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SUPERWRITE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/hidden.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=HIDDEN --implicit-check-not=ctnative.inheritance_reason
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/instanceof.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=INSTANCEOF
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/fields.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FIELDS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/newtarget.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NEWTARGET
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/superstatic.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SUPERSTATIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/spread.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SPREAD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/replaced.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=REPLACED

// DYNAMIC: ctnative.inheritance_reason = "its parent is not one class this module defines"
// SUPERWRITE: ctnative.inheritance_reason = "`n` uses `super` other than to call a method of the parent"
// A class chosen at run time is flattened like any other: native code has no
// carrier for a class value, so the closure rule refuses the choice itself.
// HIDDEN: ctnative.not_native = "a closure used as a value
// INSTANCEOF: ctnative.inheritance_reason = "`instanceof` tests a class of its chain"
// FIELDS: ctnative.inheritance_reason = "it declares class fields"
// NEWTARGET: ctnative.inheritance_reason = "its parent's constructor reads new.target"
// SUPERSTATIC: ctnative.inheritance_reason = "its static member `s` reads `super`"
// SPREAD: ctnative.inheritance_reason = "its constructor calls `super(...)` with a spread argument"
// REPLACED: ctnative.inheritance_reason = "its parent's constructor may return an object, which would replace `this`"

//--- dynamic.js
function run(flag) {
    class A { m() { return 1; } }
    class B { m() { return 2; } }
    class D extends (flag ? A : B) { n() { return this.m(); } }
    return new D().n();
}
var r = run(true);

//--- superwrite.js
function run() {
    class P { m() { return 1; } }
    class D extends P { n() { super.x = 2; return this.x; } }
    return new D().n();
}
var r = run();

//--- hidden.js
function run(flag) {
    class P { m() { return 1; } }
    class D extends P { m() { return 2; } }
    const K = flag ? P : D;
    return new K().m();
}
var r = run(true);

//--- instanceof.js
function run() {
    class P { m() { return 1; } }
    class D extends P { }
    const d = new D();
    return d instanceof P ? d.m() : 0;
}
var r = run();

//--- fields.js
function run() {
    class P { m() { return 1; } }
    class D extends P { x = 5; n() { return this.x + this.m(); } }
    return new D().n();
}
var r = run();

//--- newtarget.js
function run() {
    class P { constructor() { this.k = new.target === P ? 1 : 2; } }
    class D extends P { }
    return new D().k;
}
var r = run();

//--- superstatic.js
function run() {
    class P { static s() { return 1; } }
    class D extends P { static s() { return super.s() + 1; } }
    return D.s();
}
var r = run();

//--- spread.js
function run(...args) {
    class P { constructor(a, b) { this.v = a + b; } }
    class D extends P { constructor() { super(...args); this.w = 1; } }
    return new D().v;
}
var r = run(1, 2);

//--- replaced.js
function run() {
    class P { constructor(n) { this.n = n; return {n: 9}; } }
    class D extends P { constructor(n) { super(n); } }
    return new D(7).n;
}
var r = run();
