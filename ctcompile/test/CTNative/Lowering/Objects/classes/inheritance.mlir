// A class that extends another class of the module is flattened per concrete
// class (ClosureLifting/ClassInheritance.cpp).
//
// The chain mirrors Bootstrap's Config, BaseComponent and components: Config
// holds the configuration methods, Base calls `super()` and then a method
// Alert overrides, Alert inherits Base's constructor without writing one, and
// Toast calls `super(el, 10)` and `super.dispose()`. Base's constructor
// branches, so its implicit `return this` reaches the return through the
// branch's results. Each concrete class gets
// its own copy of every function that runs on its instances, named after the
// class, so `this._base()` inside Alert's copy of `_getConfig` calls Alert's
// `_base` with no dispatch. Config is never constructed and its copies cover
// every use, so its definition goes. The program prints what Node and the
// interpreter print with both compilers.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/chain.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/chain.mlir
// RUN: FileCheck %s --implicit-check-not=ctnative.not_native < %t/chain.mlir
// RUN: ctjs-opt %t/chain.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/chain.cpp
// RUN: FileCheck %s --check-prefix=CPP < %t/chain.cpp
// RUN: %gxx -O2 %t/chain.cpp -o %t/chain-gcc
// RUN: %t/chain-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/chain.cpp -o %t/chain-clang
// RUN: %t/chain-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/chain.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/chain.js | FileCheck %s --check-prefix=OBSERVATION
//
// The static members of a parent are copied onto each child too, so the
// static-member rule sees an ordinary class. That rule is a separate slice;
// here only the copies are checked.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/statics.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STATICS
//
// An arrow made after `super(...)` reads the importer's flag for `this` as
// set, so the class is flattened; the arrow keeps no check of the flag (its
// lexical `this` is the arrow rule's to lift).
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrow.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARROW --implicit-check-not=ctnative.inheritance_reason

// The module lists every copy with the source function it stands for (the
// forwarding constructor Alert never wrote is erased and its copy stands for
// it), and every source function erased.
// CHECK: ctnative.copies = {
// CHECK-SAME: Alert${{[0-9]+}} = "Alert$8"
// CHECK-SAME: _getConfig_Alert${{[0-9]+}} = "_getConfig$3"
// CHECK-SAME: ctnative.inheritance_erased = [{{.*}}"Alert$8"{{.*}}"Config$2"]
// CHECK-SAME: ctnative.inheritance_summary = {copies = {{[1-9][0-9]*}} : i64, erased = {{[1-9][0-9]*}} : i64}
// CHECK-DAG: emitc.class @Alert
// CHECK-DAG: emitc.class @Toast
// CHECK-DAG: emitc.class @Base
// CHECK-NOT: ctnative.class_setup
// CPP-DAG: class Alert {
// CPP-DAG: ctnative::js_num _getConfig_Alert_{{[0-9]+}}(
// CPP-DAG: dispose_Toast_{{[0-9]+}}(Toast* const
// CPP-DAG: Base_Toast_{{[0-9]+}}(Toast* const
// OBSERVATION: r=109036004

// ARROW: ctjs.func @fn$4(
// ARROW-NOT: ctjs.load_upvalue
// ARROW-NOT: ReferenceError
// ARROW: ctjs.return
// STATICS-NOT: ctnative.inheritance_reason
// STATICS-DAG: ctjs.func @make_D$
// STATICS-DAG: ctjs.func @name_D$

//--- chain.js
function run() {
    class Config {
        _getConfig(n) { return n + this._base(); }
        _base() { return 1; }
        dispose() { return 0; }
    }
    class Base extends Config {
        constructor(el, n) {
            super();
            if (el > 0) { this._element = el; } else { this._element = 0; }
            this._config = this._getConfig(n);
        }
        dispose() { return super.dispose() + this._element + this._config; }
    }
    class Alert extends Base {
        close() { return this.dispose() + 1; }
        _base() { return 100; }
    }
    class Toast extends Base {
        constructor(el) { super(el, 10); this.shown = 2; }
        dispose() { return super.dispose() * this.shown; }
    }
    const a = new Alert(5, 3);
    const t = new Toast(7);
    const b = new Base(1, 2);
    return a.close() * 1000000 + t.dispose() * 1000 + b.dispose();
}
var r = run();

//--- statics.js
function run() {
    class P { static make() { return new this(); } static get NAME() { return "p"; } name() { return this.constructor.NAME; } }
    class D extends P { static get NAME() { return "d"; } }
    return D.make().name() + P.make().name();
}
var r = run();

//--- arrow.js
function run() {
    class P { constructor(n) { this.n = n; } }
    class D extends P {
        constructor(n) { super(n); const f = () => this.n + 1; this.m = f(); }
    }
    return new D(4).m;
}
var r = run();
