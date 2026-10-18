// Constructor lift refusals: one program for each reason `new` cannot build a
// struct in the caller's frame. The GUARD numbers match the comments in
// lib/CTNative/Lowering/ClosureLifting/Constructors.cpp.
//
// A refusal spreads in both directions: the call-graph fixpoint refuses every
// caller and every callee of a refused function, and the top level calls them
// all. A test that pins a refusal and an acceptance in one program pins
// neither, so split-file keeps each program about its own clause.
//
// The programs are JavaScript, as in CTJS/Transforms/resolve-globals.mlir,
// because the rule must match what the importer emits for `new`; hand-written
// IR would only show that it matches the IR its author imagined.
//
// The accepted constructors are tested in ../../Fixtures/Objects/constructor.js,
// whose native output is run and compared with the interpreter: a `new` that
// builds the wrong object still verifies and compiles cleanly.

// RUN: split-file %s %t
// RUN: python3 %S/constructor_refusals.py --translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --fixtures %t --work %t.controls
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/returns-object.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=RETURNS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-escapes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=ESCAPES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/opaque-callee.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=OPAQUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unwritten-key.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=UNWRITTEN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/open-instance.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=OPENINSTANCE

// --- GUARD 3: the constructor returns an object ----------------------------
//
// The VM then replaces the instance. `context::construct` ends
// `return produced.is_object_like() ? produced : self`
// (ctbrowser/lib/Script/vm/call/construct.cpp, and the same line for a native),
// and `is_object_like()` is `is_object() || is_array() || is_callable() ||
// is_kind(proxy)` (include/ctbrowser/script/value/base.hpp). So `new Sneaky()`
// here evaluates to `{v: 9}`, not to the struct the rewrite builds: a module
// that verifies, compiles cleanly under -Werror, and prints 1 where the
// interpreter prints 9.
//
// A string is not object-like, which is why the clause asks about the returned
// value and not about whether there is a `return` at all.
//
// RETURNS: ctnative.not_native = "a closure used as a value: its constructor returns a value this pass cannot prove is not an object, and a constructor that returns one REPLACES the instance (context::construct: `produced.is_object_like() ? produced : self`) - so `new` would not evaluate to the struct this rewrite builds"

// --- GUARD 2: `this` escapes the constructor -------------------------------
//
// The receiver lives in the caller's frame, so returning it or storing it into
// something that outlives the frame would hand out a pointer to a local. This
// is the same rule the method lift applies to a receiver (CONDITION 3,
// `whyThisLeaks`, in lib/CTNative/Lowering/ClosureLifting/Methods.cpp).
//
// ESCAPES: ctnative.not_native = "a closure used as a value: it stores `this` into another object - that needs an owner, and this slice introduces none"

// --- GUARD 1: the callee is not one function this pass can name -------------
//
// `Maker` is a global with two stores, so global resolution cannot name it and
// `ctjs.construct`'s callee stays a `ctjs.load_global`. There is no closure to
// prove, so there is no constructor to lift.
//
// Most real code has this shape: a top-level `function Point() {}` is stored
// to a global and `new Point(...)` loads it back, so even a declaration bound
// once reaches `new` through a global load and is refused here.





// OPAQUE: ctjs.func {{.*}}@opaque$
// OPAQUE-SAME: ctnative.not_native
// OPAQUE-NOT: emitc.class

// prototype-written.js has no FileCheck block: prototype-scalars.mlir checks
// that it lowers. constructor_refusals.py runs it and every input from
// inherited-call.js on in Node and the interpreter, and checks that native
// lowering accepts inherited-call.js and refuses the rest.

// --- The one prototype read that does diverge ------------------------------
//
// An instance inherits `constructor`, and it is a function. `ensure_prototype`
// gives every instance a table whose `constructor` is the function that owns it
// (vm/call/construct.cpp), so `p.constructor` is truthy in the interpreter and
// would be undefined on a struct with no chain: 3 where the interpreter says 4.
//
// This is caught by the general object-literal admission rule (a field read
// but never written whose name Object.prototype answers), applied here to an
// instance. A broader rule refusing every unwritten key is not used: other
// unwritten keys read undefined in the VM too, so it would refuse correct
// programs without preventing any divergence.



// UNWRITTEN: ctnative.not_native = "field `constructor` is read but never written, and Object.prototype answers that name - the interpreter finds a function where this would find undefined"

// --- GUARD 4: the instance's own uses do not close its shape ----------------
//
// The shape rule an object literal must meet, applied to the value the rewrite
// is about to turn into a literal: a dynamic key means no member name, so there
// is no struct to build.
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

//--- inherited-call.js
function inherited_call() {
    var Shape = function () {};
    Shape.prototype = {
        kind: 7,
        read: function () { return this.kind; }
    };
    var t = new Shape();
    return t.read();
}
var a = inherited_call();

//--- default-prototype.js
function default_prototype() {
    var Shape = function () {};
    Shape.prototype.kind = 7;
    var t = new Shape();
    return t.kind;
}
var a = default_prototype();

//--- late-mutation.js
function late_mutation() {
    var Shape = function () {};
    Shape.prototype = {kind: 7};
    var t = new Shape();
    Shape.prototype.kind = 9;
    return t.kind;
}
var a = late_mutation();

//--- alias-mutation.js
function alias_mutation() {
    var Shape = function () {};
    Shape.prototype = {kind: 7};
    var alias = Shape.prototype;
    var t = new Shape();
    alias.kind = 11;
    return t.kind;
}
var a = alias_mutation();

//--- helper-mutation.js
function mutate(prototype) { prototype.kind = 13; }
function helper_mutation() {
    var Shape = function () {};
    Shape.prototype = {kind: 7};
    var alias = Shape.prototype;
    var t = new Shape();
    mutate(alias);
    return t.kind;
}
var a = helper_mutation();

//--- prototype-replacement.js
function prototype_replacement() {
    var Shape = function () {};
    Shape.prototype = {kind: 7};
    var old = new Shape();
    Shape.prototype = {kind: 9};
    var fresh = new Shape();
    return old.kind * 10 + fresh.kind;
}
var a = prototype_replacement();

//--- mutable-class-helper.js
// The class helper is engine-internal in the VM, so these writes make an
// ordinary global and never run: a=0 in the VM, as in Node. In the IR the
// helper keeps the name `__ctbrowser_class_defined`, so native lowering refuses
// the script's own use of that name rather than confuse the two.
__ctbrowser_class_defined = function first(value) {};
function mutable_class_helper() {
    var calls = 0;
    __ctbrowser_class_defined = function replacement(value) {
        calls = calls + 1;
    };
    class Shape {}
    return calls;
}
var a = mutable_class_helper();

//--- new-target.js
function new_target() {
    var Shape = function () { this.kind = new.target ? 7 : 2; };
    var instance = new Shape();
    return instance.kind;
}
var a = new_target();

//--- constructor-argument.js
function constructor_argument() {
    var First = function () { this.kind = 1; };
    var Second = function (callee) { this.kind = typeof callee === "function" ? 7 : 0; };
    var first = new First();
    var second = new Second(First);
    return first.kind * second.kind;
}
var a = constructor_argument();

//--- arrow-constructor.js
// Adding a prototype property does not make an arrow constructible. Its body
// never reads lexical `this`, so the receiver check for ordinary closures
// cannot stand in for the constructability check.
// Node and the interpreter both throw TypeError; native lowering must refuse
// under both optimization settings.
function arrow_constructor() {
    var Shape = () => 3;
    Shape.prototype = {kind: 7};
    var instance = new Shape();
    return instance.kind;
}
var a = arrow_constructor();
