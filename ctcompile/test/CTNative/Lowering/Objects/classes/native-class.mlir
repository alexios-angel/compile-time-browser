// A JavaScript class lowers to a C++ class.
//
// `class Counter { constructor(start) {...} bump() {...} twice() {...} }` is
// spelled by the importer as a constructor closure, a prototype object, the
// method definitions and the class setup edges (ClosureLifting/Classes.cpp).
// The class census proves the setup lowers to nothing, the constructor lift
// makes `new Counter(5)` a frame-scope instance of `class Counter` (its every
// use is a field access or a method call, so it does not outlive the frame),
// and the methods become functions taking a pointer to it. `this.bump()`
// inside `twice` is a direct call. The program prints what Node and the
// interpreter print with both compilers.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/counter.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/counter.mlir
// RUN: FileCheck %s --implicit-check-not=ctnative.not_native < %t/counter.mlir
// RUN: ctjs-opt %t/counter.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/counter.cpp
// RUN: FileCheck %s --check-prefix=CPP < %t/counter.cpp
// RUN: %gxx -O2 %t/counter.cpp -o %t/counter-gcc
// RUN: %t/counter-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/counter.cpp -o %t/counter-clang
// RUN: %t/counter-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/counter.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/counter.js | FileCheck %s --check-prefix=OBSERVATION

// CHECK: emitc.class @Counter
// CHECK-NOT: ctnative.class_setup
// CPP: class Counter {
// OBSERVATION: r=9

//--- counter.js
function run() {
    class Counter {
        constructor(start) { this.count = start; this.step = 2; }
        bump() { this.count = this.count + this.step; return this.count; }
        twice() { this.bump(); return this.bump(); }
    }
    const c = new Counter(5);
    return c.twice();
}
var r = run();
