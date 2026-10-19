// A class chain with static members goes through both class rewrites:
// flattening (ClosureLifting/ClassInheritance.cpp) copies each parent's
// static getters and methods onto each concrete class, then the static-member
// rewrite (ClassStatics.cpp) turns `this.constructor.X` in an inherited method
// and `this.NAME` in a static member into direct calls for that one class.
//
// Config, BaseComponent and two components mirror Bootstrap: Alert overrides
// NAME and Default, Toast overrides NAME and writes a constructor, and the
// inherited static method describe() is called on each subclass. Config and
// BaseComponent are never constructed, so their definitions go with their
// static members. The program prints what Node and the interpreter print
// with both compilers.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/components.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/components.mlir
// RUN: FileCheck %s --implicit-check-not=ctnative.not_native < %t/components.mlir
// RUN: ctjs-opt %t/components.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/components.cpp
// RUN: %gxx -O2 %t/components.cpp -o %t/components-gcc
// RUN: %t/components-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/components.cpp -o %t/components-clang
// RUN: %t/components-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/components.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/components.js | FileCheck %s --check-prefix=OBSERVATION

// CHECK: ctnative.inheritance_erased = [{{.*}}"Config$2"{{.*}}"BaseComponent${{[0-9]+}}"
// CHECK-DAG: emitc.class @Alert
// CHECK-DAG: emitc.class @Toast
// CHECK-NOT: ctnative.class_setup
// OBSERVATION: r=231625

//--- components.js
function run() {
    class Config {
        static get Default() { return 1; }
        static get NAME() { return "config"; }
        _getConfig(n) { return n + this.constructor.Default; }
    }
    class BaseComponent extends Config {
        constructor(el) { super(); this._element = el; this._config = this._getConfig(el); }
        static get DATA_KEY() { return "bs." + this.NAME; }
        static describe(n) { return this.NAME.length * n; }
        key() { return this.constructor.DATA_KEY; }
    }
    class Alert extends BaseComponent {
        static get NAME() { return "alert"; }
        static get Default() { return 10; }
        close() { return this._config + this.key().length; }
    }
    class Toast extends BaseComponent {
        static get NAME() { return "toast"; }
        constructor(el) { super(el * 2); this.shown = 1; }
        show() { return this._config + this.shown + this.key().length; }
    }
    const a = new Alert(5);
    const t = new Toast(3);
    return a.close() * 10000 + t.show() * 100 + Alert.describe(2) + Toast.describe(3);
}
var r = run();
