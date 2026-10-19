// A class held in a static variable of a function that runs once, and
// constructed from other functions through that variable. RunOnce.cpp keeps
// the binding static when every read is a `new`; markStaticClassReads
// (ClosureLifting/Singletons.cpp) marks each read ctnative.static_class when
// the variable holds only the class's closure and every read runs after the
// last write (the class definition's), and the constructor lift
// (Constructors.cpp, Rewrite.cpp) then builds the instance at each `new`.
//
// RUN: split-file %s %t
//
// Helper is constructed in show and in measure. The whole program is native,
// and GCC, Clang, the interpreter and Node print the same value.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/two-sites.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/two-sites.mlir
// RUN: FileCheck %s --check-prefix=TWOSITES --implicit-check-not=ctnative.not_native < %t/two-sites.mlir
// RUN: ctjs-opt %t/two-sites.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/two-sites.cpp
// RUN: %gxx -O2 %t/two-sites.cpp -o %t/two-sites-gcc
// RUN: %t/two-sites-gcc | FileCheck %s --check-prefix=RUN-TWOSITES
// RUN: %clangxx -O2 %t/two-sites.cpp -o %t/two-sites-clang
// RUN: %t/two-sites-clang | FileCheck %s --check-prefix=RUN-TWOSITES
// RUN: %native_reference %t/two-sites.js | FileCheck %s --check-prefix=RUN-TWOSITES
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/two-sites.js | FileCheck %s --check-prefix=RUN-TWOSITES
//
// TWOSITES-DAG: emitc.class @Helper
// TWOSITES-DAG: emitc.func @show_{{[0-9]+}}
// TWOSITES-DAG: emitc.func @measure_{{[0-9]+}}
// RUN-TWOSITES: r=1515
//
// A read that can run before the class is written: early is called before
// the class definition (with `go` false, so the VM does not reach its `new`
// there), so the variable is not marked and says why.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=EARLY
// RUN: %native_reference %t/early.js | FileCheck %s --check-prefix=RUN-EARLY
// EARLY-NOT: ctnative.static_class =
// EARLY: ctnative.static_class_reason = "a read in `early${{[0-9]+}}` can run before the class is written"
// EARLY-NOT: ctnative.static_class =
// RUN-EARLY: r=3
//
// A binding the program assigns a second class: RunOnce.cpp keeps it a
// capture, so no read is marked.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reassigned.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=REASSIGNED
// REASSIGNED-NOT: ctnative.static_class

//--- two-sites.js
var r = (function () {
    class Helper {
        constructor(n) { this.width = n; }
        grow(k) { return this.width * k; }
    }
    function show(n) { return new Helper(n).grow(5); }
    function measure(n) { const h = new Helper(n + 1); return h.grow(2) + h.width; }
    return show(3) * 100 + measure(4);
})();

//--- early.js
var r = (function () {
    function early(go) { return go ? new Helper(1).width : 0; }
    const first = early(false);
    class Helper {
        constructor(n) { this.width = n; }
    }
    function late() { return new Helper(2).width; }
    return first + early(true) + late();
})();

//--- reassigned.js
var r = (function () {
    let Helper = class {
        constructor() { this.width = 3; }
    };
    function show() { return new Helper().width; }
    const before = show();
    Helper = class {
        constructor() { this.width = 4; }
    };
    return before * 10 + show();
})();
