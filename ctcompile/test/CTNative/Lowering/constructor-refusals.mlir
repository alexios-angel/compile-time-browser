// `new` BUILDS A STRUCT IN THE FRAME - the refusals, one program apiece.
//
// A REFUSAL IS CONTAGIOUS IN BOTH DIRECTIONS, so a lit that pins a refusal and
// an acceptance in one program pins neither: the call-graph fixpoint marks
// every caller of a refused function and every callee of one, and the top level
// calls all of them. split-file is what keeps each of these a statement about
// its own clause.
//
// FROM JAVASCRIPT, ON PURPOSE, exactly as resolve-globals.mlir is: the rule
// exists to match what the importer emits for `new`, and hand-written IR would
// prove only that the rule matches the IR its author imagined.
//
// THE ACCEPTING CASES ARE NOT HERE. They are native-constructor-fixture.js,
// which goes through the compilation-unit gate and is COMPARED AGAINST THE
// INTERPRETER - the only thing that can catch a `new` that evaluates to the
// wrong object, because such a module verifies and compiles clean.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/returns-object.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RETURNS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-escapes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ESCAPES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/opaque-callee.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OPAQUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prototype-written.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=PROTOTYPE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unwritten-key.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNWRITTEN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/open-instance.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OPENINSTANCE

// --- GUARD 3: THE CONSTRUCTOR RETURNS AN OBJECT ------------------------------
//
// AND THE VM REPLACES THE INSTANCE WHEN IT DOES. `context::construct` ends
// `return produced.is_object_like() ? produced : self` (Script/vm/call.cpp,
// and the same line for a native), and `is_object_like()` is
// `is_object() || is_array() || is_callable() || is_kind(proxy)`
// (Script/value.hpp). So `new Sneaky()` here evaluates to `{v: 9}` and NOT to
// the struct this rewrite would build - the one failure mode that produces a
// module which verifies, compiles clean under -Werror, and prints 1 where the
// interpreter prints 9.
//
// A STRING IS NOT OBJECT-LIKE, which is why the clause asks about the VALUE and
// not about whether there is a `return` at all.
//
// RETURNS: ctnative.not_native = "a closure used as a value: its constructor returns a value this pass cannot prove is not an object, and a constructor that returns one REPLACES the instance (context::construct: `produced.is_object_like() ? produced : self`) - so `new` would not evaluate to the struct this rewrite builds"

// --- GUARD 2: `this` ESCAPES THE CONSTRUCTOR ---------------------------------
//
// The receiver is the CALLER'S FRAME, so returning it - or storing it into
// something that outlives the frame - would hand out a pointer to a local. This
// is the receiver carrier's condition 3 word for word, asked of a constructor,
// because it IS that condition.
//
// ESCAPES: ctnative.not_native = "a closure used as a value: it stores `this` into another object - that needs an owner, and this slice introduces none"

// --- GUARD 1: THE CALLEE IS NOT ONE FUNCTION THIS PASS CAN NAME --------------
//
// `Maker` is a global with two stores, so the closed world refuses the name and
// `ctjs.construct`'s callee stays a `ctjs.load_global`. There is no closure to
// prove, so there is no constructor to lift.
//
// THIS IS ALSO THE SHAPE ALL REAL CODE IS IN, AND THE MEASUREMENT SAYS SO: a
// top-level `function Point() {}` is stored to a global and `new Point(...)`
// loads it back, so even singly-bound declarations arrive here. The census over
// bootstrap, p5 and phaser finds `construct.argument.callee-known` at ZERO and
// `construct.argument.callee-opaque` at 344 of the 994 open literals' sole
// blockers - so this slice moves no corpus number, which is a measurement and
// not a disappointment.
//
// OPAQUE: ctjs.func {{.*}}@opaque$
// OPAQUE-SAME: ctnative.not_native
// OPAQUE-NOT: emitc.class

// --- GUARD 5: THE CONSTRUCTOR'S `prototype` IS WRITTEN -----------------------
//
// REFUSED BY NAME, and deferred to the plan's Stage 60A immutability proof
// rather than compiled. The VM gives every instance a prototype
// (`make_instance` -> `ensure_prototype`), so a program that puts a method on
// `Shape.prototype` and calls it would compile, here, to a struct with no such
// member at all. Naming the clause is what stops that being discovered by a
// wrong answer.
//
// PROTOTYPE: ctnative.not_native = "a closure used as a value: its constructor's `prototype` is read or written, which is a prototype chain this slice does not build - Stage 60A's immutability proof owns it"

// --- AND THE ONE PROTOTYPE READ THAT REALLY DOES DIVERGE ---------------------
//
// AN INSTANCE INHERITS `constructor`, AND IT IS A FUNCTION. `ensure_prototype`
// gives every instance a table whose `constructor` is the function that owns it
// (vm/call.cpp), so `p.constructor` is truthy in the interpreter and would be
// undefined on a struct with no chain - `3` where the interpreter says `4`.
//
// THE CLAUSE THAT CATCHES IT IS ADMISSION'S OWN AND PREDATES THIS SLICE, which
// is the finding: a guard of this rule's own ("the instance's `k` is read but
// never assigned") was written, measured, and DELETED. It refused every
// unwritten key, and every unwritten key other than the ones Object.prototype
// answers is undefined in the VM too - exactly as it is for a literal. So it
// narrowed the tier and prevented no divergence; this case pins the clause that
// does, asked of an instance rather than a literal.
//
// UNWRITTEN: ctnative.not_native = "field `constructor` is read but never written, and Object.prototype answers that name - the interpreter finds a function where this would find undefined"

// --- GUARD 4: THE INSTANCE'S OWN USES DO NOT CLOSE ITS SHAPE ----------------
//
// The same rule a literal's shape closes by, asked of the value the rewrite is
// about to make a literal: a dynamic key means no member name, so there is no
// struct to build.
//
// OPENINSTANCE: ctnative.not_native = "a closure used as a value: the instance `new` makes does not have a closed shape - every use of it has to be a constant-key read or write, or a receiver this lift carries"

//--- returns-object.js
function returns_object() {
    var Sneaky = function () {
        this.v = 1;
        return { v: 9 };
    };
    var s = new Sneaky();
    return s.v;
}
var a = returns_object();

//--- this-escapes.js
function this_escapes() {
    var box = { held: 0 };
    var Leaky = function (n) {
        this.n = n;
        box.held = this;
    };
    var l = new Leaky(4);
    return l.n;
}
var a = this_escapes();

//--- opaque-callee.js
function Maker(n) {
    this.n = n;
}
Maker = function (n) {
    this.n = n + 1;
};
function opaque() {
    var m = new Maker(4);
    return m.n;
}
var a = opaque();

//--- prototype-written.js
function prototype_written() {
    var Shape = function (s) {
        this.s = s;
    };
    Shape.prototype = { kind: 1 };
    var t = new Shape(2);
    return t.s;
}
var a = prototype_written();

//--- unwritten-key.js
function unwritten_key() {
    var Partial = function (n) {
        this.n = n;
    };
    var p = new Partial(3);
    return p.n + (p.constructor ? 1 : 0);
}
var a = unwritten_key();

//--- open-instance.js
function open_instance(k) {
    var Bag = function (n) {
        this.n = n;
    };
    var b = new Bag(7);
    return b[String(k)] + b.n;
}
var a = open_instance(1);
