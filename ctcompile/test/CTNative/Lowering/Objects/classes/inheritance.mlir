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
// here only the copies are checked. name_D lowers natively; make_D returns
// the instance it builds, which is refused as an escape, so this program
// cannot run natively. new-this.mlir's names.js runs the same name_D
// natively with GCC and Clang against the interpreter and Node; here the
// interpreter and Node pin the value the copies must keep.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/statics.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STATICS
// RUN: %native_reference %t/statics.js | FileCheck %s --check-prefix=STATICS-RUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/statics.js | FileCheck %s --check-prefix=STATICS-RUN
//
// An arrow made after `super(...)` reads the importer's flag for `this` as
// set, so the class is flattened; the arrow keeps no check of the flag, and
// no capture of it either, so the arrow rule lifts it with its lexical `this`
// as an object argument and the arrow reads no upvalue. The flag's slot is
// gone, so `this` is the arrow's only capture.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrow.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARROW --implicit-check-not=ctnative.inheritance_reason
//
// Classes of a factory function are stored in bindings, and a child's
// `extends` reads its parent's binding. Once flattening drops the heritage
// call, that read is unused, and the parents (never constructed) are erased
// with their members like any unreachable parent. Base's own copies of
// Config's members go with Base, so ctnative.copies no longer lists them.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/factory.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FACTORY

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

// FACTORY: ctnative.copies = {
// FACTORY-NOT: _Base$
// FACTORY: ctnative.inheritance_erased = [{{.*}}"Config$3"{{.*}}"Base$5"]
// ARROW: ctjs.call_direct @fn$4({{.*}}ctnative.object_args
// ARROW: ctjs.func private @fn$4({{.*}}ctnative.captures = 1 : i32, ctnative.lexical_this = 0 : i32
// ARROW-NOT: ctjs.load_upvalue
// ARROW-NOT: ReferenceError
// ARROW: ctjs.return
// STATICS-NOT: ctnative.inheritance_reason
// STATICS-DAG: ctjs.func private @make_D$
// STATICS-DAG: emitc.func @name_D_
// STATICS-RUN: r={{"?dp"?$}}

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

//--- factory.js
var r = (function () {
    class Config { base() { return 2; } }
    class Base extends Config { constructor(n) { super(); this.n = n; } get2() { return this.n * this.base(); } }
    class Alert extends Base { close() { return this.get2() + 1; } }
    function make(n) { return new Alert(n).close(); }
    return make(5);
})();
