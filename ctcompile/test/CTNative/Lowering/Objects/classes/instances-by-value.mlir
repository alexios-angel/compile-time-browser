// Class instances held by value. A function that returns the instance a
// `new` in it makes, when every caller keeps the result as its only
// reference (constant-key reads and writes, method calls), returns the class
// by value (ClosureLifting/Methods.cpp returnsAnInstance,
// Analysis/Inference/Records.cpp). An instance stored into exactly one field
// of `this`, never shared, aliased or published, is a member of the owner's
// class (ClosureLifting/Methods.cpp ownsByValue, kNativeOwnedInstance). A
// refused store or return is named in the constructor's closure_reason.
//
// RUN: split-file %s %t
//
// Bootstrap's shapes: Modal owns a ScrollBarHelper in a field and calls its
// methods through the field, and a helper returns a Backdrop. The whole
// program is native, and GCC, Clang, the interpreter and Node print the same
// value: hide() and reset() write through the member, and the reads after
// them see the writes.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/modal.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/modal.mlir
// RUN: FileCheck %s --check-prefix=MODAL --implicit-check-not=ctnative.not_native < %t/modal.mlir
// RUN: ctjs-opt %t/modal.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/modal.cpp
// RUN: FileCheck %s --check-prefix=MODAL-CPP < %t/modal.cpp
// RUN: %gxx -O2 %t/modal.cpp -o %t/modal-gcc
// RUN: %t/modal-gcc | FileCheck %s --check-prefix=RUN-MODAL
// RUN: %clangxx -O2 %t/modal.cpp -o %t/modal-clang
// RUN: %t/modal-clang | FileCheck %s --check-prefix=RUN-MODAL
// RUN: %native_reference %t/modal.js | FileCheck %s --check-prefix=RUN-MODAL
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/modal.js | FileCheck %s --check-prefix=RUN-MODAL
//
// MODAL-DAG: emitc.class @ScrollBarHelper
// MODAL-DAG: emitc.class @Modal
// MODAL-DAG: emitc.class @Backdrop
// MODAL-CPP: class ScrollBarHelper {
// MODAL-CPP: class Modal {
// MODAL-CPP: ScrollBarHelper _scrollBar;
// MODAL-CPP-DAG: Backdrop makeBackdrop_{{[0-9]+}}(
// RUN-MODAL: r=253356
//
// A class defined in the function that returns its instance: its `new` is
// not a class construct through a static binding, so the constructor lifts
// only because returnsAnInstance accepts the return. Native, and the same
// value as the interpreter and Node.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inner.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/inner.mlir
// RUN: FileCheck %s --check-prefix=INNER --implicit-check-not=ctnative.not_native < %t/inner.mlir
// RUN: ctjs-opt %t/inner.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/inner.cpp
// RUN: FileCheck %s --check-prefix=INNER-CPP < %t/inner.cpp
// RUN: %gxx -O2 %t/inner.cpp -o %t/inner-gcc
// RUN: %t/inner-gcc | FileCheck %s --check-prefix=RUN-INNER
// RUN: %clangxx -O2 %t/inner.cpp -o %t/inner-clang
// RUN: %t/inner-clang | FileCheck %s --check-prefix=RUN-INNER
// RUN: %native_reference %t/inner.js | FileCheck %s --check-prefix=RUN-INNER
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/inner.js | FileCheck %s --check-prefix=RUN-INNER
// INNER: emitc.func @make_{{[0-9]+}}
// INNER-CPP: Backdrop make_{{[0-9]+}}(
// RUN-INNER: r=1215
//
// A field the constructor stores after `this` has already been passed to a
// method could be read before the store, when JavaScript finds no field:
// refused, although open() here never runs that early.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EARLY --implicit-check-not=ctnative.owned_instance
// EARLY: ctnative.closure_reason = "the instance `new` makes does not have a closed shape - it is stored into a field that does not own it by value - `this` reaches `{{ctjs.call(_direct)?}}` before the store to `_scrollBar`, so code could read the field before it exists"
//
// A read of the field through an object other than `this` in the class's
// own code: refused, since that object need not be a Modal.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/foreign.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FOREIGN --implicit-check-not=ctnative.owned_instance
// FOREIGN: ctnative.closure_reason = "the instance `new` makes does not have a closed shape - it is stored into a field that does not own it by value - field `_scrollBar` is read in `peek${{[0-9]+}}` through something other than the `this` of the class's constructor or methods"
//
// One instance stored into two fields would be shared by them: refused.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/aliased.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ALIASED --implicit-check-not=ctnative.owned_instance
// ALIASED: ctnative.closure_reason = "the instance `new` makes does not have a closed shape - it is stored into a field that does not own it by value - the instance has another use, so field `{{[ab]}}` would share it"
//
// A returned instance its caller returns again stays refused: make does not
// return a record, and it is not native. Built through the run-once
// function's static binding, the class does not reach this rule's refusal;
// inner-twice.js builds the class in make, where the constructor's
// closure_reason names the use.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/twice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=TWICE --implicit-check-not=ctnative.record_return
// TWICE: ctjs.func {{(private )?}}@make${{[0-9]+}}({{.*}}ctnative.not_native =
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inner-twice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=INNER-TWICE --implicit-check-not=ctnative.record_return
// INNER-TWICE: ctnative.closure_reason = "the instance `new` makes does not have a closed shape - it is returned, and `pass${{[0-9]+}}` keeps the instance `make${{[0-9]+}}` returns other than through its fields and methods - it is returned"

//--- modal.js
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
        hide() { this.width = 0; return 1; }
        reset() { this.width = 3; return 2; }
    }
    class Backdrop {
        constructor(n) { this.n = n; }
        show() { return this.n * 2; }
    }
    function makeBackdrop(n) { return new Backdrop(n); }
    class Modal {
        constructor(k) { this._scrollBar = new ScrollBarHelper(); this.k = k; }
        open() { return this._scrollBar.hide() + this._scrollBar.width + this.k; }
        close() { return this._scrollBar.reset() + this._scrollBar.width; }
    }
    function show(k) {
        const m = new Modal(k);
        const b = makeBackdrop(k);
        return m.open() * 100 + m.close() * 10 + b.show() + b.n;
    }
    return show(1) * 1000 + show(2);
})();

//--- aliased.js
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
        hide() { this.width = 0; return 1; }
    }
    class Modal {
        constructor() { const s = new ScrollBarHelper(); this.a = s; this.b = s; }
        open() { return this.a.hide() + this.b.width; }
    }
    function show() { return new Modal().open(); }
    return show();
})();

//--- twice.js
var r = (function () {
    class Backdrop {
        constructor(n) { this.n = n; }
        show() { return this.n * 2; }
    }
    function make(n) { return new Backdrop(n); }
    function pass(n) { return make(n); }
    function use(k) { const b = pass(k); return b.show() + b.n; }
    return use(4) * 100 + use(5);
})();

//--- early.js
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
        hide() { this.width = 0; return 1; }
    }
    class Modal {
        constructor() { this.n = this.count(); this._scrollBar = new ScrollBarHelper(); }
        count() { return 2; }
        open() { return this._scrollBar.hide() + this.n; }
    }
    function show() { return new Modal().open(); }
    return show();
})();

//--- foreign.js
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
        hide() { this.width = 0; return 1; }
    }
    class Modal {
        constructor() { this._scrollBar = new ScrollBarHelper(); }
        open() { return this._scrollBar.hide(); }
    }
    function peek(m) { return m._scrollBar.width; }
    function show() { const m = new Modal(); return m.open() + peek(m); }
    return show();
})();

//--- inner.js
var r = (function () {
    function make(n) {
        class Backdrop {
            constructor(n) { this.n = n; }
            show() { return this.n * 2; }
        }
        return new Backdrop(n);
    }
    function use(k) { const b = make(k); return b.show() + b.n; }
    return use(4) * 100 + use(5);
})();

//--- inner-twice.js
var r = (function () {
    function make(n) {
        class Backdrop {
            constructor(n) { this.n = n; }
            show() { return this.n * 2; }
        }
        return new Backdrop(n);
    }
    function pass(n) { return make(n); }
    function use(k) { const b = pass(k); return b.show() + b.n; }
    return use(4) * 100 + use(5);
})();
