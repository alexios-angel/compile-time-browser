// What the static-member rewrite refuses, one program per reason.
//
// ClosureLifting/ClassStatics.cpp turns a read of a static member into a
// direct call only when nothing else could reach that member. Each program
// here breaks one of its rules, so the class keeps its static members and the
// class census refuses it with the rewrite's reason.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escape.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ESCAPE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EARLY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/other-field.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OTHERFIELD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/home.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=HOME
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/new-this.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/new-this.mlir
// RUN: FileCheck %s --check-prefix=NEWTHIS --implicit-check-not=ctnative.not_native < %t/new-this.mlir
// RUN: ctjs-opt %t/new-this.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/new-this.cpp
// RUN: %gxx -O2 %t/new-this.cpp -o %t/new-this-gcc
// RUN: %t/new-this-gcc | FileCheck %s --check-prefix=NEWTHIS-RUN
// RUN: %clangxx -O2 %t/new-this.cpp -o %t/new-this-clang
// RUN: %t/new-this-clang | FileCheck %s --check-prefix=NEWTHIS-RUN
// RUN: %native_reference %t/new-this.js | FileCheck %s --check-prefix=NEWTHIS-RUN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/relay.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RELAY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/constructor-read.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CONSTRUCTORREAD --implicit-check-not=unreachable_refusals
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/constructor-static.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CONSTRUCTORSTATIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/choice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CHOICE
// RUN: %native_reference %t/choice.js | FileCheck %s --check-prefix=CHOICE-RUN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/init-fields.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=INITFIELDS --implicit-check-not=unreachable_refusals
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/get-or-create.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=GETORCREATE
// RUN: %native_reference %t/get-or-create.js | FileCheck %s --check-prefix=GETORCREATE-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/get-or-create.js | FileCheck %s --check-prefix=GETORCREATE-RUN

// The class is passed to a function that returns it, so it leaves as a value.
// ESCAPE: ctnative.class_reason = "it has static getter `size`; `Box` is used as a value by `ctjs.return` in `show${{[0-9]+}}`"
//
// `early` reads a static member and is called before the class statement
// runs, when the binding is still uninitialized (JavaScript throws there).
// EARLY: ctnative.class_reason = "it has static method `make`; a read of a static member in `early${{[0-9]+}}` can run before the class is defined"
//
// `Box.name` is the constructor's own property, not a static member.
// OTHERFIELD: ctnative.class_reason = "it has static method `make`; it reads `Box.name`, which is not a static member in `make${{[0-9]+}}`"
//
// The VM answers `Box.__home` with the engine's home-object edge, not the
// getter (Node runs the getter), so a static member of that name is refused.
// HOME: ctnative.class_reason = "it has static getter `__home`; its static member `__home` is the engine's home-object key"
//
// `new this(v)` in a static member constructs Box (new-this.mlir has the
// cases): the class is proved, and make returns the instance by value as Box
// (Analysis/Inference/Records.cpp), so the program is native and prints the
// interpreter's value. instances-by-value.mlir's twice.js keeps a returned
// instance that is passed on refused.
// NEWTHIS-NOT: ctnative.class_reason
// NEWTHIS: emitc.func @make_{{[0-9]+}}
// NEWTHIS-RUN: r=3
//
// A static member that is called and relays its capture of `base` to a
// nested function through its own closure: the direct call passes no closure.
// RELAY: ctnative.class_reason = "it has static method `make`; its static member `make` reads new.target or its own closure"
//
// A static getter read through an instance's `constructor` outside the class
// (`read(item)`): the rewrite cannot see that read, so it keeps every static
// member, and no getter is removed as unreachable while it can still run.
// CONSTRUCTORREAD: ctnative.class_reason = "it has static getter `VALUE`; `x.constructor` is read in `read${{[0-9]+}}`, where `x` may be an instance of the class"
//
// The same read inside the class, of a name that is one of its static
// members: `o` may be an instance of the class. (Reading `name` there instead
// does not refuse; native-class-statics.mlir, describe.js.)
// CONSTRUCTORSTATIC: ctnative.class_reason = "it has static method `describe`; `x.constructor` is read in `describe${{[0-9]+}}`, where `x` may be an instance of the class"
//
// A helper is passed one of two classes, chosen at run time: the helper
// cannot be copied for one class, so both keep their static members.
// CHOICE-DAG: ctnative.class_reason = "it has static getter `NAME`; `A` is used as a value by `scf.yield` in `run${{[0-9]+}}`"
// CHOICE-DAG: ctnative.class_reason = "it has static getter `NAME`; `B` is used as a value by `scf.yield` in `run${{[0-9]+}}`"
// CHOICE-RUN: r=80
//
// A field initializer: the constructor passes its own closure to the VM's
// __ctbrowser_init_fields, which reads the initializer off it, so the
// constructor's closure is a use of the class the rewrite cannot resolve.
// INITFIELDS: ctnative.class_reason = "it has static getter `SIDES`; `Shape` is used as a value by `ctjs.call` in `Shape${{[0-9]+}}`"
//
// Bootstrap's BaseComponent.getOrCreateInstance on two subclasses that
// override a static getter: `new this(e)` constructs the subclass that the
// call names. Flattening (ClassInheritance.cpp) gives X and Y their own
// constructor and their own copy of getOrCreateInstance, and erases the
// original subclass constructors and Base, which nothing reaches any more;
// each copy is called, and it calls `get`, which it reaches through its own
// closure (a capture of `run`), so the direct call that would replace it has
// no closure to pass.
// GETORCREATE: ctnative.inheritance_erased = ["X${{[0-9]+}}", "Y${{[0-9]+}}", {{.*}}"Base${{[0-9]+}}"]
// GETORCREATE-COUNT-2: ctnative.class_reason = "it has static method `getOrCreateInstance`; its static member `getOrCreateInstance` reads new.target or its own closure"
// GETORCREATE-RUN: r=34

//--- escape.js
function show(k) { return k; }
function run() {
    class Box { constructor(v) { this.v = v; } static get size() { return 4; } }
    return new Box(2).v + show(Box).size;
}
var r = run();

//--- early.js
function run() {
    function early() { return Box.make(); }
    const a = early();
    class Box { constructor(v) { this.v = v; } static make() { return 1; } }
    return new Box(2).v + early() + a;
}
var r = run();

//--- other-field.js
function run() {
    class Box { constructor(v) { this.v = v; } static make() { return this.name.length; } }
    return new Box(2).v + Box.make();
}
var r = run();

//--- home.js
function run() {
    class Box { constructor(v) { this.v = v; } static get __home() { return 7; } }
    return new Box(2).v + (Box.__home === 7) * 1;
}
var r = run();

//--- new-this.js
function run() {
    class Box { constructor(v) { this.v = v; } static make(v) { return new this(v); } }
    return Box.make(3).v;
}
var r = run();

//--- relay.js
function run() {
    let base = 5;
    base += 1;
    class Box { constructor(v) { this.v = v; } static make(v) { function f() { return base + v; } return [f][0](); } }
    return new Box(2).v + Box.make(1);
}
var r = run();

//--- get-or-create.js
function run() {
    const store = new Map();
    function get(e) { return store.get(e); }
    class Base {
        constructor(e) { this.e = e; store.set(e, this); }
        static get NAME() { return "base"; }
        static getOrCreateInstance(e) { return get(e) || new this(e); }
    }
    class X extends Base { static get NAME() { return "x"; } size() { return 10; } }
    class Y extends Base { static get NAME() { return "yy"; } size() { return 20; } }
    const a = X.getOrCreateInstance(1), b = Y.getOrCreateInstance(2);
    return a.size() + b.size() + X.NAME.length + Y.NAME.length + (X.getOrCreateInstance(1) === a ? 1 : 0);
}
var r = run();

//--- constructor-read.js
function read(saved) { return saved.constructor.VALUE; }
function run() {
    class Item { constructor(n) { this.n = n; } static get VALUE() { return 7; } }
    const item = new Item(3);
    return item.n + Item.VALUE + read(item);
}
var r = run();

//--- constructor-static.js
function run() {
    class Shape {
        constructor(n) { this.n = n; }
        static get SIDES() { return 4; }
        static describe(o) { return o.constructor.SIDES; }
    }
    return new Shape(3).n + Shape.SIDES;
}
var r = run();

//--- choice.js
function run(flag) {
    let base = 1;
    base += 1;
    function q(C) { return C.NAME.length * 10 + base; }
    class A { constructor(n) { this.n = n; } static get NAME() { return "alpha"; } }
    class B { constructor(n) { this.n = n; } static get NAME() { return "be"; } }
    return q(flag ? A : B) + new A(1).n + new B(2).n;
}
var r = run(true) + run(false);

//--- init-fields.js
function run() {
    class Shape { n = 7; static get SIDES() { return 4; } }
    return new Shape().n + Shape.SIDES;
}
var r = run();
