// Arrow functions that read their lexical `this`.
//
// The interpreter fixes an arrow's `this` when the closure is made
// (make_closure's captured_this), so closureLifter::captureLexicalThis turns
// it into a constant capture of the creating frame: a ctjs.create_cell of
// `$enclosing_this` captured in a new last slot, read with ctjs.load_upvalue
// where the arrow read its receiver. No program here may still be refused as
// "an arrow function that reads its lexical `this`"; each is decided by the
// ordinary capture and receiver rules instead, and the checks pin which.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/direct.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/direct.mlir
// RUN: FileCheck %s --check-prefix=DIRECT --implicit-check-not=ctnative.not_native < %t/direct.mlir
// RUN: ctjs-opt %t/direct.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/direct.cpp
// RUN: FileCheck %s --check-prefix=DIRECTCPP < %t/direct.cpp
// RUN: %gxx -O2 %t/direct.cpp -o %t/direct-gcc
// RUN: %t/direct-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/direct.cpp -o %t/direct-clang
// RUN: %t/direct-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/direct.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/direct.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nested.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NESTED --implicit-check-not="reads its lexical"
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STORED --implicit-check-not="reads its lexical"
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unproved.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNPROVED --implicit-check-not="reads its lexical"
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/borrowed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BORROWED --implicit-check-not="reads its lexical"

// A method makes `() => this._x` and calls it twice in its own frame. The
// arrow lifts with the instance as an object-pointer capture, the method
// passes its own receiver pointer on, the cell that boxed `this` is gone, and
// the program is native: it prints what Node and the interpreter print with
// both compilers.
// DIRECT: emitc.func @twice_3
// DIRECT: call @fn_4(
// DIRECT: call @fn_4(
// DIRECT: emitc.func @fn_4
// DIRECTCPP: ctnative::js_num twice_3(Box* const [[SELF:v[0-9]+]]) {
// DIRECTCPP: fn_4([[SELF]])
// DIRECTCPP: fn_4([[SELF]])
// DIRECTCPP: fn_4(Box* const
// OBSERVATION: r=8

// An arrow inside an arrow: the inner one's `$enclosing_this` is the outer
// one's receiver, which the outer rewrite replaces with its own capture, so
// both lift and read the method's `this` through one capture each. The inner
// capture is relayed from the outer arrow rather than taken from a receiver,
// so neither is an object pointer yet and the method still refuses.
// NESTED: ctjs.func @total$3({{.*}}ctnative.not_native = "uses `this` - a method field: it passes `this` as an argument to another function
// NESTED: ctjs.create_closure %arg2[4] this %{{.*}} captures %{{.*}} {ctnative.lifted
// NESTED: ctjs.func private @fn$4(
// NESTED: ctjs.create_closure %arg2[5] this %{{.*}} captures %{{.*}} {ctnative.lifted

// Negative: the arrow is stored on the instance and called later, so it is
// a closure value with no call site to move its capture to. The constructor
// is refused too, because its receiver now flows into the capture's cell.
// STORED: ctjs.create_closure %arg2[2] this %{{.*}} {ctnative.closure_reason = "it reaches `ctjs.create_cell`, which slice 1 does not carry a receiver through"
// STORED: ctjs.create_closure %arg2[3] this %{{.*}} captures %{{.*}} {ctnative.closure_reason = "it is stored into something that is not an object literal made here - Phase 59 slice 2"

// Negative: a plain function's `this`, which `read.call(o)` supplies and no
// rule proves. The arrow lifts with an object-pointer capture, but `read`
// keeps the receiver rule's refusal, and so does the arrow, which only `read`
// calls.
// UNPROVED: ctjs.func @read$1({{.*}}ctnative.not_native = "uses `this`"
// UNPROVED: ctjs.create_closure %arg2[2] this %{{.*}} captures %{{.*}} {ctnative.lifted
// UNPROVED: ctjs.func private @fn$2({{.*}}ctnative.not_native = "called by `read$1`, which is not native"

// The arrow passed to `apply`, whose parameter is only called: it becomes a
// borrowed callback bound to the instance, its one capture. A bound callable
// holding an object pointer is not typed or grouped with the receiver yet, so
// the capture stays boxed and the method refuses.
// BORROWED: ctjs.func @get$4({{.*}}ctnative.not_native = "a closure used as a value - Phase 59"
// BORROWED: ctjs.create_closure %{{.*}}[5] this %{{.*}} captures %arg0 {ctnative.borrowed_callback = @fn$5
// BORROWED: ctjs.func @fn$5({{.*}}ctnative.borrowed_callback, ctnative.captures = 1 : i32, {{.*}}ctnative.not_native = "capture 0 is !ctnative.boxed - no caller proves it

//--- direct.js
function run() {
    class Box {
        constructor(x) { this._x = x; }
        twice() { const f = () => this._x; return f() + f(); }
    }
    const b = new Box(4);
    return b.twice();
}
var r = run();

//--- nested.js
function run() {
    class Box {
        constructor(x) { this._x = x; }
        total() { const outer = () => { const inner = () => this._x; return inner() + 1; }; return outer(); }
    }
    return new Box(4).total();
}
var r = run();

//--- stored.js
function run() {
    class Box {
        constructor(x) { this._x = x; this.get = () => this._x; }
    }
    const b = new Box(4);
    return b.get() + 1;
}
var r = run();

//--- unproved.js
function read() { const g = () => this.x; return g() + 1; }
var o = { x: 3 };
var r = read.call(o);

//--- borrowed.js
function apply(f) { return f() + 1; }
function run() {
    class Box {
        constructor(x) { this._x = x; }
        get() { return apply(() => this._x); }
    }
    const b = new Box(4);
    return b.get();
}
var r = run();
