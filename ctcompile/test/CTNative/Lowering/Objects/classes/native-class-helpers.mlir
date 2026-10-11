// A class passed to a helper function that only reads its static members.
//
// `label` takes a class and reads its static getter and calls its static
// method, also from an arrow it creates. Each call passes a different class,
// so ClosureLifting/ClassStatics.cpp clones `label` (with the arrow) once per
// class, the clone's parameter names that class, and its static reads become
// direct calls as for the class's own name. The program prints what Node and
// the interpreter print with both compilers.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/helper.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/helper.mlir
// RUN: FileCheck %s --implicit-check-not=ctnative.not_native --implicit-check-not=ctnative.class_reason < %t/helper.mlir
// RUN: ctjs-opt %t/helper.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/helper.cpp
// RUN: FileCheck %s --check-prefix=CPP < %t/helper.cpp
// RUN: %gxx -O2 %t/helper.cpp -o %t/helper-gcc
// RUN: %t/helper-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/helper.cpp -o %t/helper-clang
// RUN: %t/helper-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/helper.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/helper.js | FileCheck %s --check-prefix=OBSERVATION
//
// A helper that reads one static getter of the class passed to it: the input
// that native-class-statics-refusals.mlir's ESCAPE case used to refuse, with
// `show` declared inside `run`. Declared at the top level instead (global.js),
// the class still specialises and is proved, but `run` reads `show` through a
// global binding that still holds the original function, so it is refused
// for that binding, not for the class.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/show.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/show.mlir
// RUN: FileCheck %s --check-prefix=SHOW --implicit-check-not=ctnative.not_native --implicit-check-not=ctnative.class_reason < %t/show.mlir
// RUN: ctjs-opt %t/show.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/show.cpp
// RUN: %gxx -O2 %t/show.cpp -o %t/show-gcc
// RUN: %t/show-gcc | FileCheck %s --check-prefix=SHOW-RUN
// RUN: %clangxx -O2 %t/show.cpp -o %t/show-clang
// RUN: %t/show-clang | FileCheck %s --check-prefix=SHOW-RUN
// RUN: %native_reference %t/show.js | FileCheck %s --check-prefix=SHOW-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/show.js | FileCheck %s --check-prefix=SHOW-RUN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/global.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=GLOBAL --implicit-check-not=ctnative.class_reason
//
// What the specialisation refuses: a helper that stores the class or compares
// it, and one that reaches its captures through its own closure (a direct
// call of the clone has no closure to pass).
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stores.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STORES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compares.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=COMPARES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/captures.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CAPTURES

// CHECK: emitc.class @A
// CHECK: emitc.class @B
// CPP-COUNT-2: function label,
// OBSERVATION: r=23
// SHOW: emitc.func @main
// SHOW-RUN: r=6
// GLOBAL: ctnative.not_native = "global `show` is !ctnative.boxed, not a number"
// STORES: ctnative.class_reason = "it has static getter `NAME`; `A` is used as a value by `ctjs.set_property` in `keep${{[0-9]+}}`"
// COMPARES: ctnative.class_reason = "it has static getter `NAME`; `A` is used as a value by `ctjs.compare` in `same${{[0-9]+}}`"
// CAPTURES: ctnative.class_reason = "it has static getter `NAME`; `A` is passed to `keep${{[0-9]+}}`, which reads new.target or its own closure in `run${{[0-9]+}}`"

//--- helper.js
function run() {
    function label(k, suffix) { const tag = () => k.NAME + suffix; return tag().length + k.twice(3); }
    class A { constructor(v) { this.v = v; } static get NAME() { return "a"; } static twice(x) { return 2 * x; } }
    class B { constructor(v) { this.v = v; } static get NAME() { return "bb"; } static twice(x) { return 3 * x; } }
    return label(A, "!") + label(B, "?") + new A(1).v + new B(2).v;
}
var r = run();

//--- stores.js
function run() {
    function keep(k, into) { into.k = k; return k.NAME; }
    class A { constructor(v) { this.v = v; } static get NAME() { return "a"; } }
    return keep(A, {}).length + new A(1).v;
}
var r = run();

//--- compares.js
function run() {
    function same(k, j) { return k === j ? 1 : 0; }
    class A { constructor(v) { this.v = v; } static get NAME() { return "a"; } }
    return same(A, A) + A.NAME.length + new A(1).v;
}
var r = run();

//--- captures.js
function run() {
    let seen = 0;
    seen += 1;
    function keep(k) { return k.NAME.length + seen; }
    class A { constructor(v) { this.v = v; } static get NAME() { return "a"; } }
    return keep(A) + new A(1).v;
}
var r = run();

//--- show.js
function run() {
    function show(k) { return k.size; }
    class Box { constructor(v) { this.v = v; } static get size() { return 4; } }
    return new Box(2).v + show(Box);
}
var r = run();

//--- global.js
function show(k) { return k.size; }
function run() {
    class Box { constructor(v) { this.v = v; } static get size() { return 4; } }
    return new Box(2).v + show(Box);
}
var r = run();
