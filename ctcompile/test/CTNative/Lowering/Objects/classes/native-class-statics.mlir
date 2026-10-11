// Static members of a class lower to direct calls.
//
// `Shape` has a static getter, two static methods (one of which reaches the
// other through `this`), and an instance method that reads a static member
// both through the class binding it captures and through
// `this.constructor`. Every read names one function, so each becomes a
// ctjs.call_direct (ClosureLifting/ClassStatics.cpp), the definitions lower
// to nothing, and the class census then proves the class: it lowers to a C++
// class with no static data. The static method nothing reads (`unused`, which
// calls an undeclared global and is refused) is left uncalled, so the
// unreachable-refusal pass removes it and records the removal. The program
// prints what Node and the interpreter print with both compilers.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shape.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/shape.mlir
// RUN: FileCheck %s --implicit-check-not=ctnative.not_native --implicit-check-not=ctnative.class_reason < %t/shape.mlir
// RUN: ctjs-opt %t/shape.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/shape.cpp
// RUN: FileCheck %s --check-prefix=CPP < %t/shape.cpp
// RUN: %gxx -O2 %t/shape.cpp -o %t/shape-gcc
// RUN: %t/shape-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/shape.cpp -o %t/shape-clang
// RUN: %t/shape-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/shape.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/shape.js | FileCheck %s --check-prefix=OBSERVATION

// CHECK: ctnative.unreachable_refusals = [{function = "unused${{[0-9]+}}", reason = "removed: unreachable (no function that can run creates or calls it; it was refused: {{.*}})"}]
// CHECK: emitc.class @Shape
// CHECK-NOT: ctjs.define_accessor
// CPP: class Shape {
// OBSERVATION: r=24

//--- shape.js
function run() {
    class Shape {
        static get NAME() { return "shape"; }
        static twice(x) { return x * 2; }
        static quad(x) { return this.twice(this.twice(x)); }
        static unused() { return missing(); }
        constructor(side) { this.side = side; }
        area() { return Shape.twice(this.side) + this.constructor.NAME.length; }
    }
    const s = new Shape(5);
    return s.area() + Shape.NAME.length + Shape.quad(1);
}
var r = run();
