// Surplus actuals are evaluated before an explicit constructor even when its
// declared parameter window does not include them. Raw arguments and implicit
// forwarding constructors keep their complete window and remain refused.
//
// RUN: split-file %s %t
// RUN: python3 %S/constructor_extra_arguments.py --translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --fixtures %t --work %t.controls
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/effects.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false observe-globals=true" -o %t/effects.mlir
// RUN: FileCheck %s --check-prefix=EFFECTS --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func < %t/effects.mlir
// RUN: ctjs-opt %t/effects.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/effects.cpp
// RUN: %gxx -O2 %t/effects.cpp -o %t/effects-gcc
// RUN: %t/effects-gcc | FileCheck %s --check-prefix=EFFECTSOUT --match-full-lines
// RUN: %clangxx -O2 %t/effects.cpp -o %t/effects-clang
// RUN: %t/effects-clang | FileCheck %s --check-prefix=EFFECTSOUT --match-full-lines
// RUN: %native_reference %t/effects.js | FileCheck %s --check-prefix=EFFECTSOUT --match-full-lines
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("a=" + scope.a);' %t/effects.js | FileCheck %s --check-prefix=EFFECTSOUT --match-full-lines
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/throwing.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false observe-globals=true" -o %t/throwing.mlir
// RUN: FileCheck %s --check-prefix=THROW --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func < %t/throwing.mlir
// RUN: ctjs-opt %t/throwing.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/throwing.cpp
// RUN: %gxx -O2 %t/throwing.cpp -o %t/throwing-gcc
// RUN: %t/throwing-gcc > %t/throwing-gcc.out 2> %t/throwing-gcc.err; echo "status=$?" >> %t/throwing-gcc.out
// RUN: FileCheck %s --check-prefix=THROWOUT --match-full-lines --implicit-check-not=constructor < %t/throwing-gcc.out
// RUN: %clangxx -O2 %t/throwing.cpp -o %t/throwing-clang
// RUN: %t/throwing-clang > %t/throwing-clang.out 2> %t/throwing-clang.err; echo "status=$?" >> %t/throwing-clang.out
// RUN: %native_reference --page %t/empty.html --script %t/throwing.js > %t/throwing-vm.out 2> %t/throwing-vm.err; echo "status=$?" >> %t/throwing-vm.out
// RUN: %node %t/throwing.js > %t/throwing-node.out 2> %t/throwing-node.err; echo "status=$?" >> %t/throwing-node.out
// RUN: diff %t/throwing-gcc.out %t/throwing-clang.out
// RUN: diff %t/throwing-gcc.out %t/throwing-vm.out
// RUN: diff %t/throwing-gcc.out %t/throwing-node.out
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/length.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=RAW
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/index.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=RAW
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/rest.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=RAW
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/derived.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=FORWARD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/spread.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=OBSERVER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=OBSERVER
//
// EFFECTS: emitc.func @probe_{{[0-9]+}}
// EFFECTSOUT: a=12347
// THROW: emitc.func @probe_{{[0-9]+}}
// THROWOUT: first
// THROWOUT-NEXT: extra
// THROWOUT-NEXT: status=1
// RAW: ctjs.create_closure {{.*}}ctnative.closure_reason = "surplus constructor arguments are observed by arguments or a rest parameter"
// FORWARD: ctjs.create_closure {{.*}}ctnative.closure_reason = "surplus constructor arguments require an explicit, uncopied constructor with declared parameters"
// OBSERVER: ctjs.create_closure {{.*}}ctnative.closure_reason = "surplus constructor arguments may reach whole-window forwarding or run-time code creation"

//--- effects.js
function probe() {
    let trace = 0;
    function stamp(n) { trace = trace * 10 + n; return n; }
    function Point(n) { trace = trace * 10 + 4; this.x = n; }
    const point = new Point(stamp(1), stamp(2), stamp(3));
    return trace * 10 + point.x + 6;
}
var a = probe();

//--- empty.html
<!doctype html><title>Constructor Arguments</title>

//--- throwing.js
function first() { console.log("first"); return 7; }
function stop(n) { console.log("extra"); if (n) { throw new TypeError("surplus"); } return 0; }
function probe() {
    function Point(n) { console.log("constructor"); this.x = n; }
    const point = new Point(first(), stop(1));
    return point.x;
}
probe();

//--- length.js
function probe() {
    function Point(n) { this.x = arguments.length; }
    const point = new Point(1, 2);
    return point.x;
}
var a = probe();

//--- index.js
function probe() {
    function Point(n) { this.x = arguments[1]; }
    const point = new Point(1, 2);
    return point.x;
}
var a = probe();

//--- rest.js
function probe() {
    function Point(n, ...rest) { this.x = rest[0]; }
    const point = new Point(1, 2, 3);
    return point.x;
}
var a = probe();

//--- derived.js
function probe() {
    class Base { constructor(n) { this.x = n; } }
    class Leaf extends Base {}
    const point = new Leaf(1, 2);
    return point.x;
}
var a = probe();

//--- spread.js
function probe() {
    function Point(n) { this.x = n; const values = [n]; Number(...values); }
    const point = new Point(1, 2);
    return point.x;
}
var a = probe();

//--- dynamic.js
function probe() {
    function Point(n) { this.x = n; new Function("return 0"); }
    const point = new Point(1, 2);
    return point.x;
}
var a = probe();
