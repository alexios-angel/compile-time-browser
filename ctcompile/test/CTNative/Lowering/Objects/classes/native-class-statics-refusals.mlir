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
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NEWTHIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/relay.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RELAY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/get-or-create.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=GETORCREATE
// RUN: %native_reference %t/get-or-create.js | FileCheck %s --check-prefix=GETORCREATE-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/get-or-create.js | FileCheck %s --check-prefix=GETORCREATE-RUN

// The class is passed to a function, which could read any static member.
// ESCAPE: ctnative.class_reason = "it has static getter `size`; `Box` is used as a value by `ctjs.call{{(_direct)?}}` in `run${{[0-9]+}}`"
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
// `new this(v)` in a static member: the direct call passes no `this`.
// NEWTHIS: ctnative.class_reason = "it has static method `make`; it constructs `new this` or `new this.constructor` in `make${{[0-9]+}}`"
//
// A static member that is called and relays its capture of `base` to a
// nested function through its own closure: the direct call passes no closure.
// RELAY: ctnative.class_reason = "it has static method `make`; its static member `make` reads new.target or its own closure"
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
function show(k) { return k.size; }
function run() {
    class Box { constructor(v) { this.v = v; } static get size() { return 4; } }
    return new Box(2).v + show(Box);
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
