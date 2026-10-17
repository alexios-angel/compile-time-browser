// Static members of a class lower to direct calls.
//
// `Shape` has a static getter, two static methods (one of which reaches the
// other through `this`), and an instance method that reads a static member
// both through the class binding it captures and through
// `this.constructor`. Every read names one function, so each becomes a
// ctjs.call_direct (ClosureLifting/ClassStatics.cpp), the definitions lower
// to nothing, and the class census then proves the class: it lowers to a C++
// class with no static data. The static method nothing reads (`unused`, which
// calls an undeclared global and relays its capture of `base` to an arrow
// through its own closure) is left uncalled, so the unreachable-refusal pass removes it and
// records the removal. The program
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
//
// `describe` reads `o.constructor` on a parameter, so `o` may be an instance
// of `Shape`, but it reads `name` there, which is not a static member of
// `Shape`: the read cannot reach one, so it does not stop the rewrite. The
// same program reading `o.constructor.SIDES` is refused
// (native-class-statics-refusals.mlir, constructor-static.js).
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/describe.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/describe.mlir
// RUN: FileCheck %s --check-prefix=DESCRIBE --implicit-check-not=ctnative.not_native --implicit-check-not=ctnative.class_reason < %t/describe.mlir
// RUN: ctjs-opt %t/describe.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/describe.cpp
// RUN: %gxx -O2 %t/describe.cpp -o %t/describe-gcc
// RUN: %t/describe-gcc | FileCheck %s --check-prefix=DESCRIBE-RUN
// RUN: %clangxx -O2 %t/describe.cpp -o %t/describe-clang
// RUN: %t/describe-clang | FileCheck %s --check-prefix=DESCRIBE-RUN
// RUN: %native_reference %t/describe.js | FileCheck %s --check-prefix=DESCRIBE-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/describe.js | FileCheck %s --check-prefix=DESCRIBE-RUN
//
// An arrow created in a method of `Tag` with that method's `this` reads
// `this.constructor.SIDES`: its `this` is an instance of `Tag`, so the read
// names `Tag` and resolves to Tag's getter, and it does not stop the rewrite
// of `Shape`, which has a static member of the same name.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrow.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/arrow.mlir
// RUN: FileCheck %s --check-prefix=ARROW --implicit-check-not=ctnative.not_native --implicit-check-not=ctnative.class_reason < %t/arrow.mlir
// RUN: ctjs-opt %t/arrow.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/arrow.cpp
// RUN: %gxx -O2 %t/arrow.cpp -o %t/arrow-gcc
// RUN: %t/arrow-gcc | FileCheck %s --check-prefix=ARROW-RUN
// RUN: %clangxx -O2 %t/arrow.cpp -o %t/arrow-clang
// RUN: %t/arrow-clang | FileCheck %s --check-prefix=ARROW-RUN
// RUN: %native_reference %t/arrow.js | FileCheck %s --check-prefix=ARROW-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/arrow.js | FileCheck %s --check-prefix=ARROW-RUN
//
// A helper that reads its capture `base` through its own closure is passed
// two classes: each call goes to a copy of the helper made for that class,
// called through a closure of the copy with the same captures, so `C.NAME`
// in each copy is a direct call of that class's getter. `base` is assigned
// after the helper's closure is made and before the calls; both copies see
// the new value, as the original does. The copies are recorded in
// ctnative.copies.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/helper.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/helper.mlir
// RUN: FileCheck %s --check-prefix=HELPER --implicit-check-not=ctnative.not_native --implicit-check-not=ctnative.class_reason < %t/helper.mlir
// RUN: ctjs-opt %t/helper.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/helper.cpp
// RUN: %gxx -O2 %t/helper.cpp -o %t/helper-gcc
// RUN: %t/helper-gcc | FileCheck %s --check-prefix=HELPER-RUN
// RUN: %clangxx -O2 %t/helper.cpp -o %t/helper-clang
// RUN: %t/helper-clang | FileCheck %s --check-prefix=HELPER-RUN
// RUN: %native_reference %t/helper.js | FileCheck %s --check-prefix=HELPER-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/helper.js | FileCheck %s --check-prefix=HELPER-RUN
//
// The same helper as an arrow held in a `const` (Bootstrap's
// defineJQueryPlugin and enableDismissTrigger): the calls are ctjs.call
// through the arrow's closure, made earlier in the same function, so each
// becomes a direct call of the arrow's copy for that class.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/viacall.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/viacall.mlir
// RUN: FileCheck %s --check-prefix=VIACALL --implicit-check-not=ctnative.not_native --implicit-check-not=ctnative.class_reason < %t/viacall.mlir
// RUN: ctjs-opt %t/viacall.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/viacall.cpp
// RUN: %gxx -O2 %t/viacall.cpp -o %t/viacall-gcc
// RUN: %t/viacall-gcc | FileCheck %s --check-prefix=HELPER-RUN
// RUN: %clangxx -O2 %t/viacall.cpp -o %t/viacall-clang
// RUN: %t/viacall-clang | FileCheck %s --check-prefix=HELPER-RUN
// RUN: %native_reference %t/viacall.js | FileCheck %s --check-prefix=HELPER-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/viacall.js | FileCheck %s --check-prefix=HELPER-RUN

// CHECK: ctnative.unreachable_refusals = [{{.*}}{function = "unused${{[0-9]+}}", reason = "removed: unreachable (no function that can run creates or calls it; it was refused: {{.*}}
// CHECK: emitc.class @Shape
// CHECK-NOT: ctjs.define_accessor
// CPP: class Shape {
// OBSERVATION: r=24
// DESCRIBE: ctnative.unreachable_refusals = [{{.*}}{function = "describe${{[0-9]+}}", reason = "removed: unreachable
// DESCRIBE: emitc.class @Shape
// DESCRIBE-RUN: r=7
// ARROW-DAG: emitc.class @Shape
// ARROW-DAG: emitc.class @Tag
// ARROW: emitc.func @fn_{{[0-9]+}}()
// ARROW-NEXT: value = 4.000000e+00
// ARROW: emitc.func @[[TAGSIDES:fn_[0-9]+]]()
// ARROW-NEXT: value = 1.000000e+01
// ARROW: emitc.func @size_{{[0-9]+}}(
// ARROW: call @[[TAGSIDES]]()
// ARROW-RUN: r=17
// HELPER: ctnative.copies = {{{.*}}q${{[0-9]+}} = "q${{[0-9]+}}", q${{[0-9]+}} = "q${{[0-9]+}}"
// HELPER-DAG: emitc.class @A
// HELPER-DAG: emitc.class @B
// VIACALL: ctnative.copies = {{{.*}}fn${{[0-9]+}} = "fn${{[0-9]+}}", fn${{[0-9]+}} = "fn${{[0-9]+}}"
// VIACALL-DAG: emitc.class @A
// VIACALL-DAG: emitc.class @B
// HELPER-RUN: r=122155

//--- shape.js
function run() {
    let base = 1;
    base += 1;
    class Shape {
        static get NAME() { return "shape"; }
        static twice(x) { return x * 2; }
        static quad(x) { return this.twice(this.twice(x)); }
        static unused() { const relay = () => base; return missing() + relay(); }
        constructor(side) { this.side = side; }
        area() { return Shape.twice(this.side) + this.constructor.NAME.length; }
    }
    const s = new Shape(5);
    return s.area() + Shape.NAME.length + Shape.quad(1);
}
var r = run();

//--- describe.js
function run() {
    class Shape {
        constructor(n) { this.n = n; }
        static get SIDES() { return 4; }
        static describe(o) { return o.constructor.name.length; }
    }
    return new Shape(3).n + Shape.SIDES;
}
var r = run();

//--- arrow.js
function run() {
    class Shape { constructor(n) { this.n = n; } static get SIDES() { return 4; } }
    class Tag {
        constructor(t) { this.t = t; }
        static get SIDES() { return 10; }
        size() { const f = () => this.constructor.SIDES; return f(); }
    }
    return new Shape(3).n + Shape.SIDES + new Tag(1).size();
}
var r = run();

//--- helper.js
function run() {
    let base = 1;
    base += 1;
    function q(C) { return C.NAME.length * 10 + base; }
    class A { constructor(n) { this.n = n; } static get NAME() { return "alpha"; } }
    class B { constructor(n) { this.n = n; } static get NAME() { return "be"; } }
    base += 100;
    return q(A) + q(B) * 1000 + new A(1).n + new B(2).n;
}
var r = run();

//--- viacall.js
function run() {
    let base = 1;
    base += 1;
    const q = (C) => C.NAME.length * 10 + base;
    class A { constructor(n) { this.n = n; } static get NAME() { return "alpha"; } }
    class B { constructor(n) { this.n = n; } static get NAME() { return "be"; } }
    base += 100;
    return q(A) + q(B) * 1000 + new A(1).n + new B(2).n;
}
var r = run();
