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
// RUN: %t/direct-gcc | FileCheck %s --check-prefix=DIRECTOUT
// RUN: %clangxx -O2 %t/direct.cpp -o %t/direct-clang
// RUN: %t/direct-clang | FileCheck %s --check-prefix=DIRECTOUT
// RUN: %native_reference %t/direct.js | FileCheck %s --check-prefix=DIRECTOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/direct.js | FileCheck %s --check-prefix=DIRECTOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nested.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/nested.mlir
// RUN: FileCheck %s --check-prefix=NESTED --implicit-check-not=ctnative.not_native < %t/nested.mlir
// RUN: ctjs-opt %t/nested.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/nested.cpp
// RUN: FileCheck %s --check-prefix=NESTEDCPP < %t/nested.cpp
// RUN: %gxx -O2 %t/nested.cpp -o %t/nested-gcc
// RUN: %t/nested-gcc | FileCheck %s --check-prefix=NESTEDOUT
// RUN: %clangxx -O2 %t/nested.cpp -o %t/nested-clang
// RUN: %t/nested-clang | FileCheck %s --check-prefix=NESTEDOUT
// RUN: %native_reference %t/nested.js | FileCheck %s --check-prefix=NESTEDOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/nested.js | FileCheck %s --check-prefix=NESTEDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/borrowed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/borrowed.mlir
// RUN: FileCheck %s --check-prefix=BORROWED --implicit-check-not=ctnative.not_native < %t/borrowed.mlir
// RUN: ctjs-opt %t/borrowed.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/borrowed.cpp
// RUN: FileCheck %s --check-prefix=BORROWEDCPP < %t/borrowed.cpp
// RUN: %gxx -O2 %t/borrowed.cpp -o %t/borrowed-gcc
// RUN: %t/borrowed-gcc | FileCheck %s --check-prefix=BORROWEDOUT
// RUN: %clangxx -O2 %t/borrowed.cpp -o %t/borrowed-clang
// RUN: %t/borrowed-clang | FileCheck %s --check-prefix=BORROWEDOUT
// RUN: %native_reference %t/borrowed.js | FileCheck %s --check-prefix=BORROWEDOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/borrowed.js | FileCheck %s --check-prefix=BORROWEDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STORED --implicit-check-not="reads its lexical"
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nestedstored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NESTEDSTORED --implicit-check-not="reads its lexical"
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unproved.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNPROVED --implicit-check-not="reads its lexical"
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/calls.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/calls.mlir
// RUN: FileCheck %s --check-prefix=CALLS --implicit-check-not=ctnative.not_native < %t/calls.mlir
// RUN: ctjs-opt %t/calls.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/calls.cpp
// RUN: FileCheck %s --check-prefix=CALLSCPP < %t/calls.cpp
// RUN: %gxx -O2 %t/calls.cpp -o %t/calls-gcc
// RUN: %t/calls-gcc | FileCheck %s --check-prefix=CALLSOUT
// RUN: %clangxx -O2 %t/calls.cpp -o %t/calls-clang
// RUN: %t/calls-clang | FileCheck %s --check-prefix=CALLSOUT
// RUN: %native_reference %t/calls.js | FileCheck %s --check-prefix=CALLSOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/calls.js | FileCheck %s --check-prefix=CALLSOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/callsstored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CALLSSTORED

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
// DIRECTOUT: r=8

// An arrow inside an arrow: the inner one's `$enclosing_this` is the outer
// one's receiver, which the outer rewrite replaces with its own capture. The
// outer arrow lifts first and takes the method's `this` as a pointer; the
// inner one then takes the outer one's pointer parameter, so both are native.
// NESTED: emitc.func @total_3
// NESTEDCPP: total_3(Box* const [[SELF:v[0-9]+]]) {
// NESTEDCPP: fn_4([[SELF]])
// NESTEDCPP: fn_4(Box* const [[OUTER:v[0-9]+]]) {
// NESTEDCPP: fn_5([[OUTER]])
// NESTEDCPP: fn_5(Box* const
// NESTEDOUT: r=5

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
// borrowed callback bound to the instance pointer. `apply` takes it as a
// ctnative::function_ref, which it can only call during the call, so the
// pointer the bound callable holds never outlives `get`'s frame.
// BORROWED: emitc.func @get_4
// BORROWEDCPP: apply_1(ctnative::function_ref<ctnative::nullable_scalar()>
// BORROWEDCPP: ctnative::nullable_scalar fn_5(Box* const
// BORROWEDOUT: r=5

// Negative: an inner arrow stored on the instance. It is not lifted, so the
// cell holding the outer arrow's `this` stays, and the outer arrow, the
// method and the program are refused.
// NESTEDSTORED: ctjs.func private @fn$4({{.*}}ctnative.not_native = "a closure used as a value: it is stored into something that is not an object literal made here - Phase 59 slice 2"
// NESTEDSTORED: ctjs.func @fn$5({{.*}}ctnative.not_native = "uses its own closure"

// An arrow that calls a method through the `this` it captures, directly and
// from an arrow inside it. The method only calls them, so the call resolves
// as the method's own `this.bump()` would (Methods.cpp methodCensus), the
// arrows lift with the instance pointer, each passes it to `bump` as the
// receiver, and the program is native.
// CALLS: emitc.func @total_{{[0-9]+}}
// CALLSCPP: ctnative::js_num fn_{{[0-9]+}}(Box* const [[SELF:v[0-9]+]]) {
// CALLSCPP: bump_{{[0-9]+}}([[SELF]],
// CALLSOUT: r=654

// Negative: an arrow stored on the instance calls `this.bump(1)`. It can run
// after `keep` returns, so its `this` is not the method's receiver to the
// method census: the call stays dispatched through the closure, and `bump`,
// which nothing else calls, is not lifted.
// CALLSSTORED: ctjs.func @bump${{[0-9]+}}({{.*}}ctnative.not_native = "uses `this` - a method field: nothing calls it"
// CALLSSTORED: ctjs.func @fn${{[0-9]+}}({{.*}}ctnative.lexical_this
// CALLSSTORED-NOT: call_direct @bump
// CALLSSTORED: ctjs.call %{{[0-9]+}}(%{{[0-9]+}}, %{{[0-9]+}})

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

//--- nestedstored.js
function run() {
    class Box {
        constructor(x) { this._x = x; }
        total() { const outer = () => { const inner = () => this._x; this.keep = inner; return this._x; }; return outer(); }
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

//--- calls.js
function run() {
    class Box {
        constructor(x) { this._x = x; }
        bump(n) { return this._x + n; }
        total() {
            const f = () => this.bump(1) * 10 + this._x;
            const outer = () => { const inner = () => this.bump(2); return inner() * 100; };
            return f() + outer();
        }
    }
    return new Box(4).total();
}
var r = run();

//--- callsstored.js
function run() {
    class Box {
        constructor(x) { this._x = x; this.get = null; }
        bump(n) { return this._x + n; }
        keep() { this.get = () => this.bump(1); return 0; }
    }
    const b = new Box(4);
    b.keep();
    return b.get();
}
var r = run();
