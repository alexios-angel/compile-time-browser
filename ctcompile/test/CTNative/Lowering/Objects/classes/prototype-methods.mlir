// A class's prototype methods stay direct calls when the program writes
// other objects through computed keys.
//
// The method census (ClosureLifting/Methods.cpp) binds `this.m()` and
// `x.m()` on an instance to the class's own method only when nothing can
// replace it. A write of the method's name to anything but its definition
// still blocks the binding: `this`, a parameter or any other value may name
// an instance, an instance's struct admits a constant-key write, and an own
// field would shadow the method. A write through a computed key does not
// block it: an instance lowers to a struct that admission gives only
// constant-key accesses and no delete, the prototype has no use but its
// definitions, and code that admission refuses never runs natively.
//
// prototype.js writes a table through a computed key in another function,
// the shape of the writes Bootstrap makes, which used to leave every
// prototype method unbound. Shape and its subclass Square lower to C++
// classes, `this.area()` in the copy of `twice` made for Square calls
// Square's own `area`, `super.area()` calls the copy of Shape's, and the
// program prints what Node and the interpreter print with both compilers.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prototype.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/prototype.mlir
// RUN: FileCheck %s --check-prefix=PROTOTYPE --implicit-check-not=ctnative.not_native < %t/prototype.mlir
// RUN: ctjs-opt %t/prototype.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/prototype.cpp
// RUN: FileCheck %s --check-prefix=CPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/prototype.cpp
// RUN: %gxx -O2 %t/prototype.cpp -o %t/prototype-gcc
// RUN: %t/prototype-gcc | FileCheck %s --check-prefix=PROTOTYPE-RUN
// RUN: %clangxx -O2 %t/prototype.cpp -o %t/prototype-clang
// RUN: %t/prototype-clang | FileCheck %s --check-prefix=PROTOTYPE-RUN
// RUN: %native_reference %t/prototype.js | FileCheck %s --check-prefix=PROTOTYPE-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/prototype.js | FileCheck %s --check-prefix=PROTOTYPE-RUN
//
// Each program below replaces a method, or makes the prototype reachable, in a
// way that must keep the class refused now that a computed-key write
// elsewhere no longer refuses it: the instance written through a computed key
// (KEYED), `this` written through one (THISKEY), the instance handed to a
// helper that writes it through one (HELPER), the prototype written or
// deleted through one (PROTOKEY, DELETED), the prototype reached through
// Object.getPrototypeOf (REACHED), and Bootstrap's dispose, which nulls every
// own property (DISPOSE). OWNFIELD shadows the method with an own field
// through its constant name, which still blocks the binding itself.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/keyed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=KEYED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-key.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=THISKEY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/helper.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=HELPER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prototype-key.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=PROTOKEY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/deleted.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DELETED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reached.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=REACHED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dispose.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DISPOSE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/own-field.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OWNFIELD
//
// A method's receiver is an instance of its class, which only the constructor
// lift makes, so a method lifts with its class or not at all. In
// class-refused.js the constructor passes `this` to a helper, so Child does
// not lift; its methods keep dispatching through the prototype, and the
// empty constructor it inherits from Base (Base_Child, called through the
// prototype) stays native instead of being lifted alone and then refused
// with its refused caller.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/class-refused.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CLASSREFUSED
//
// The constructor rule (ClosureLifting/Constructors.cpp) also refuses a class
// whose method is read as a value. It counts only reads that may observe the
// method in native code: a constant-key read of the method's name on a value
// that may name an instance (a parameter, a capture, a call result, a `new`
// of the class or a value `behind` names). A computed-key read cannot, for
// the same reason as a computed-key write, and the prototype is never a value
// there. table.js reads and writes a table through a computed key, and
// observed.js reads `constructor` and the method's name off a record of its
// own; both used to leave Shape refused and now run natively.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/table.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/table.mlir
// RUN: FileCheck %s --check-prefix=TABLE --implicit-check-not=ctnative.not_native < %t/table.mlir
// RUN: ctjs-opt %t/table.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/table.cpp
// RUN: FileCheck %s --check-prefix=TABLE-CPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/table.cpp
// RUN: %gxx -O2 %t/table.cpp -o %t/table-gcc
// RUN: %t/table-gcc | FileCheck %s --check-prefix=TABLE-RUN
// RUN: %clangxx -O2 %t/table.cpp -o %t/table-clang
// RUN: %t/table-clang | FileCheck %s --check-prefix=TABLE-RUN
// RUN: %native_reference %t/table.js | FileCheck %s --check-prefix=TABLE-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/table.js | FileCheck %s --check-prefix=TABLE-RUN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/observed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/observed.mlir
// RUN: FileCheck %s --check-prefix=OBSERVED --implicit-check-not=ctnative.not_native < %t/observed.mlir
// RUN: ctjs-opt %t/observed.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/observed.cpp
// RUN: FileCheck %s --check-prefix=OBSERVED-CPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/observed.cpp
// RUN: %gxx -O2 %t/observed.cpp -o %t/observed-gcc
// RUN: %t/observed-gcc | FileCheck %s --check-prefix=OBSERVED-RUN
// RUN: %clangxx -O2 %t/observed.cpp -o %t/observed-clang
// RUN: %t/observed-clang | FileCheck %s --check-prefix=OBSERVED-RUN
// RUN: %native_reference %t/observed.js | FileCheck %s --check-prefix=OBSERVED-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/observed.js | FileCheck %s --check-prefix=OBSERVED-RUN
//
// Reads that observe the method keep the class refused: an arrow that reads
// it through its captured `this` (ARROW), the instance or `this` read through
// a computed key (READKEY, THISREAD), and the prototype reached through
// `constructor` or `__proto__`, which admission refuses on a struct
// (CONSTRUCTOR, DUNDER).
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrow.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARROW
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/read-key.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=READKEY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-read.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=THISREAD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/constructor.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CONSTRUCTOR
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dunder.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DUNDER

// PROTOTYPE-DAG: emitc.class @Shape
// PROTOTYPE-DAG: emitc.class @Square
// CPP-DAG: class Shape {
// CPP-DAG: class Square {
// CPP: put_{{[0-9]+}}(std::shared_ptr<ctnative::dictionary<ctnative::js_num>> const
// CPP: ctnative::js_num area_Square_[[INHERITED:[0-9]+]](Square* const v1) {
// CPP: ctnative::js_num twice_{{[0-9]+}}(Shape* const v1) {
// CPP-NEXT: = area_{{[0-9]+}}(v1);
// CPP: ctnative::js_num twice_Square_{{[0-9]+}}(Square* const v1) {
// CPP-NEXT: = area_[[OWN:[0-9]+]](v1);
// CPP: ctnative::js_num area_[[OWN]](Square* const v1) {
// CPP: = area_Square_[[INHERITED]](v1);
// PROTOTYPE-RUN: r=82007

// KEYED: ctnative.closure_reason = "the instance `new` makes does not have a closed shape
// THISKEY: ctnative.closure_reason = "it writes `this` through a dynamic key"
// HELPER: ctnative.closure_reason = "the instance `new` makes does not have a closed shape
// PROTOKEY: ctnative.closure_reason = "its constructor's `prototype` is read or written
// DELETED: ctnative.closure_reason = "its constructor's `prototype` is read or written
// REACHED: ctnative.closure_reason = "the instance `new` makes does not have a closed shape
// DISPOSE: ctnative.closure_reason = "it writes `this` through a dynamic key"
// OWNFIELD: ctnative.closure_reason = "its prototype method is not an immutable direct-call binding"
// CLASSREFUSED-DAG: ctnative.closure_reason = "its class does not lift: it passes `this` to a call this rewrite cannot make direct"
// CLASSREFUSED-DAG: emitc.func @Base_Child_{{[0-9]+}}(

// TABLE: emitc.class @Shape
// TABLE-CPP: bump_{{[0-9]+}}(std::shared_ptr<ctnative::dictionary<ctnative::js_num>> const
// TABLE-CPP: ctnative::js_num twice_{{[0-9]+}}(Shape* const v1) {
// TABLE-CPP-NEXT: = area_{{[0-9]+}}(v1);
// TABLE-RUN: r=83
// OBSERVED: emitc.class @Shape
// OBSERVED-CPP: ctnative::js_num twice_{{[0-9]+}}(Shape* const v1) {
// OBSERVED-CPP-NEXT: = area_{{[0-9]+}}(v1);
// OBSERVED-RUN: r=8136

// ARROW: ctnative.closure_reason = "its prototype method is also observed as a value"
// READKEY: ctnative.closure_reason = "the instance `new` makes does not have a closed shape
// THISREAD: ctnative.closure_reason = "it reads `this` through a dynamic key"
// CONSTRUCTOR: ctnative.not_native = "field `constructor` is read but never written, and Object.prototype answers that name
// DUNDER: ctnative.not_native = "an object literal reached through the inherited Object.prototype.__proto__ accessor"

//--- prototype.js
function put(counts, key) { counts[key] = 7; }
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
        twice() { return this.area() + this.area(); }
    }
    class Square extends Shape {
        constructor(w) { super(w); this.h = 1; }
        area() { return super.area() + this.h; }
    }
    const counts = {a: 1, b: 2};
    put(counts, "b");
    const s = new Shape(2);
    const q = new Square(3);
    return s.twice() * 10000 + q.twice() * 100 + counts.b;
}
var r = run();

//--- keyed.js
function run(key) {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
    }
    const s = new Shape(2);
    s[key] = function () { return 7; };
    return s.area();
}
var r = run(["ar", "ea"].join(""));

//--- this-key.js
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
        swap(key) { this[key] = function () { return 7; }; return 0; }
    }
    const s = new Shape(2);
    s.swap("area");
    return s.area();
}
var r = run();

//--- helper.js
function put(o, key, v) { o[key] = v; }
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
    }
    const s = new Shape(2);
    put(s, "area", function () { return 7; });
    return s.area();
}
var r = run();

//--- prototype-key.js
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
    }
    const key = "area";
    Shape.prototype[key] = function () { return 7; };
    return new Shape(2).area();
}
var r = run();

//--- deleted.js
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
        side() { return this.w; }
    }
    const s = new Shape(2);
    const before = s.area();
    const key = "area";
    delete Shape.prototype[key];
    return before * 10 + s.side();
}
var r = run();

//--- reached.js
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
    }
    const s = new Shape(2);
    const key = "area";
    Object.getPrototypeOf(s)[key] = function () { return 7; };
    return s.area();
}
var r = run();

//--- dispose.js
function run() {
    class Base {
        constructor(el) { this._element = el; }
        size() { return this._element * 2; }
        dispose() { for (const t of Object.getOwnPropertyNames(this)) this[t] = null; }
    }
    const b = new Base(3);
    const n = b.size();
    b.dispose();
    return n;
}
var r = run();

//--- own-field.js
function run() {
    class Shape {
        constructor(w) { this.w = w; this.area = function () { return 7; }; }
        area() { return this.w * this.w; }
    }
    return new Shape(2).area();
}
var r = run();

//--- class-refused.js
var seen = 0;
function register(o) { seen = seen + 1; }
function run() {
    class Base {}
    class Child extends Base {
        constructor(n) { super(); this.n = n; register(this); }
        size() { return this.n; }
    }
    return new Child(4).size() + seen;
}
var r = run();

//--- table.js
function bump(counts, key) { counts[key] = counts[key] + 1; }
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
        twice() { return this.area() + this.area(); }
    }
    const counts = {a: 1, b: 2};
    bump(counts, "b");
    return new Shape(2).twice() * 10 + counts.b;
}
var r = run();

//--- observed.js
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
        twice() { return this.area() + this.area(); }
    }
    const rec = {constructor: 1, area: 30};
    const marks = [1, 2, 3];
    let total = 0;
    for (let i = 0; i < marks.length; i++) { total += marks[i]; }
    return new Shape(2).twice() * 1000 + rec.constructor * 100 + rec.area + total;
}
var r = run();

//--- arrow.js
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
        kind() { const f = () => this.area; return f() === undefined ? 1 : 2; }
    }
    const s = new Shape(2);
    return s.area() * 10 + s.kind();
}
var r = run();

//--- read-key.js
function run(key) {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
    }
    const s = new Shape(2);
    const f = s[key];
    return s.area() * 10 + (f === undefined ? 1 : 2);
}
var r = run(["ar", "ea"].join(""));

//--- this-read.js
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
        has(key) { return this[key] === undefined ? 1 : 2; }
    }
    const s = new Shape(2);
    return s.area() * 10 + s.has("area");
}
var r = run();

//--- constructor.js
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
    }
    const s = new Shape(2);
    const f = s.constructor.prototype.area;
    return s.area() * 10 + (f === undefined ? 1 : 2);
}
var r = run();

//--- dunder.js
function run() {
    class Shape {
        constructor(w) { this.w = w; }
        area() { return this.w * this.w; }
    }
    const s = new Shape(2);
    const f = s.__proto__.area;
    return s.area() * 10 + (f === undefined ? 1 : 2);
}
var r = run();
