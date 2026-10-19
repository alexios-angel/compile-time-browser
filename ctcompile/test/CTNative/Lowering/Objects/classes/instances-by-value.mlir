// Class instances held by value. A function that returns the instance a
// `new` in it makes, when every caller keeps the result as its only
// reference (constant-key reads and writes, method calls), returns the class
// by value (ClosureLifting/Methods.cpp returnsAnInstance,
// Analysis/Inference/Records.cpp). An instance stored into exactly one field
// of `this` in the class's constructor, never shared, aliased or published, is
// a ctnative::owned member of the owner's class (ClosureLifting/Methods.cpp
// ownsByValue, kNativeOwnedInstance, Runtime/Owned.hpp): the field is
// undefined before its store and null after a store of null in a method, and
// a read through it then throws the interpreter's TypeError. The field may
// also be read through the `this` of an arrow the class's code makes and only
// calls. A refused store or return is named in the constructor's
// closure_reason.
//
// RUN: split-file %s %t
//
// Bootstrap's shapes: Modal owns a ScrollBarHelper in a field and calls its
// methods through the field, and a helper returns a Backdrop. The whole
// program is native, and GCC, Clang, the interpreter and Node print the same
// value: hide() and reset() write through the member, and the reads after
// them see the writes.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/modal.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/modal.mlir
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
// MODAL-CPP: ctnative::owned<ScrollBarHelper> _scrollBar;
// MODAL-CPP-DAG: Backdrop makeBackdrop_{{[0-9]+}}(
// RUN-MODAL: r=253356
//
// A class defined in the function that returns its instance: its `new` is
// not a class construct through a static binding, so the constructor lifts
// only because returnsAnInstance accepts the return. Native, and the same
// value as the interpreter and Node.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inner.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/inner.mlir
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
// A method called on `this` before the store: native, since a read there
// would find the field undefined and throw as JavaScript does (early-read.js).
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/early.mlir
// RUN: FileCheck %s --check-prefix=OWNED --implicit-check-not=ctnative.not_native < %t/early.mlir
// RUN: ctjs-opt %t/early.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/early.cpp
// RUN: %gxx -O2 %t/early.cpp -o %t/early-gcc
// RUN: %t/early-gcc | FileCheck %s --check-prefix=RUN-EARLY
// RUN: %clangxx -O2 %t/early.cpp -o %t/early-clang
// RUN: %t/early-clang | FileCheck %s --check-prefix=RUN-EARLY
// RUN: %native_reference %t/early.js | FileCheck %s --check-prefix=RUN-EARLY
// OWNED: emitc.func @main
// RUN-EARLY: r=3
//
// A store under a condition and a dispose method that stores null: native,
// and the same value as the interpreter and Node.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/disposed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/disposed.mlir
// RUN: FileCheck %s --check-prefix=OWNED --implicit-check-not=ctnative.not_native < %t/disposed.mlir
// RUN: ctjs-opt %t/disposed.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/disposed.cpp
// RUN: FileCheck %s --check-prefix=DISPOSED-CPP < %t/disposed.cpp
// RUN: %gxx -O2 %t/disposed.cpp -o %t/disposed-gcc
// RUN: %t/disposed-gcc | FileCheck %s --check-prefix=RUN-DISPOSED
// RUN: %clangxx -O2 %t/disposed.cpp -o %t/disposed-clang
// RUN: %t/disposed-clang | FileCheck %s --check-prefix=RUN-DISPOSED
// RUN: %native_reference %t/disposed.js | FileCheck %s --check-prefix=RUN-DISPOSED
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/disposed.js | FileCheck %s --check-prefix=RUN-DISPOSED
// DISPOSED-CPP: ctnative::disown(
// RUN-DISPOSED: r=25
//
// Bootstrap's Carousel shape (swipe.js): the constructor stores null, a
// method it calls stores the Swipe helper, and other methods test the field
// for truth before using it, and dispose() stores null again. A method's
// store can replace an instance whose method is still running, so the field
// is a ctnative::replaceable, which keeps every instance it held alive
// (replace.js stores twice). Both are native and print the interpreter's
// value:
//   swipe.js: with touch, slide() is (1 + 2) then (2 + 2), dispose() 7, and
//   slide() 0 after it: 347; without touch everything is 0: r = 347000.
//   replace.js: get() -1 before any store, reset(1) grows to 2, reset(5) to
//   6, get() 6, two resets: -1000 + 200 + 60 + 6 + 20000 = 19266.
// RUN: for name in swipe replace; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/$name.mlir; \
// RUN:   ctjs-opt %t/$name.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/$name.cpp; done
// RUN: FileCheck %s --check-prefix=OWNED --implicit-check-not=ctnative.not_native < %t/swipe.mlir
// RUN: FileCheck %s --check-prefix=OWNED --implicit-check-not=ctnative.not_native < %t/replace.mlir
// RUN: FileCheck %s --check-prefix=SWIPE-CPP < %t/swipe.cpp
// RUN: %gxx -O2 %t/swipe.cpp -o %t/swipe-gcc
// RUN: %t/swipe-gcc | FileCheck %s --check-prefix=RUN-SWIPE
// RUN: %clangxx -O2 %t/swipe.cpp -o %t/swipe-clang
// RUN: %t/swipe-clang | FileCheck %s --check-prefix=RUN-SWIPE
// RUN: %native_reference %t/swipe.js | FileCheck %s --check-prefix=RUN-SWIPE
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/swipe.js | FileCheck %s --check-prefix=RUN-SWIPE
// RUN: %gxx -O2 %t/replace.cpp -o %t/replace-gcc
// RUN: %t/replace-gcc | FileCheck %s --check-prefix=RUN-REPLACE
// RUN: %clangxx -O2 %t/replace.cpp -o %t/replace-clang
// RUN: %t/replace-clang | FileCheck %s --check-prefix=RUN-REPLACE
// RUN: %native_reference %t/replace.js | FileCheck %s --check-prefix=RUN-REPLACE
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/replace.js | FileCheck %s --check-prefix=RUN-REPLACE
// SWIPE-CPP-DAG: ctnative::replaceable<Swipe> _swipeHelper;
// SWIPE-CPP-DAG: ctnative::held(
// RUN-SWIPE: r=347000
// RUN-REPLACE: r=19266
//
// A read before the store (early-read.js) and after a store of null
// (disposed-read.js) throw the interpreter's TypeError: the native programs
// print the same stdout, exit with the same status and report the same first
// line on stderr as the interpreter.
// RUN: for name in early-read disposed-read; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/$name.mlir; \
// RUN:   ctjs-opt %t/$name.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/$name.cpp; \
// RUN:   %native_reference %t/$name.js > %t/$name-vm.out 2> %t/$name-vm.err; echo "status=$?" >> %t/$name-vm.out; \
// RUN:   head -n 1 %t/$name-vm.err | sed 's/^.* threw: /threw: /' > %t/$name-vm.first; done
// RUN: FileCheck %s --check-prefix=OWNED --implicit-check-not=ctnative.not_native < %t/early-read.mlir
// RUN: FileCheck %s --check-prefix=OWNED --implicit-check-not=ctnative.not_native < %t/disposed-read.mlir
// RUN: FileCheck %s --check-prefix=EARLY-READ < %t/early-read-vm.first
// RUN: FileCheck %s --check-prefix=DISPOSED-READ < %t/disposed-read-vm.first
// RUN: FileCheck %s --check-prefix=STATUS < %t/early-read-vm.out
// RUN: FileCheck %s --check-prefix=STATUS < %t/disposed-read-vm.out
// RUN: for name in early-read disposed-read; do \
// RUN:   %gxx -O2 %t/$name.cpp -o %t/$name-gcc; \
// RUN:   %t/$name-gcc > %t/$name-gcc.out 2> %t/$name-gcc.err; echo "status=$?" >> %t/$name-gcc.out; \
// RUN:   head -n 1 %t/$name-gcc.err | sed 's/^.* threw: /threw: /' > %t/$name-gcc.first; \
// RUN:   %clangxx -O2 %t/$name.cpp -o %t/$name-clang; \
// RUN:   %t/$name-clang > %t/$name-clang.out 2> %t/$name-clang.err; echo "status=$?" >> %t/$name-clang.out; \
// RUN:   head -n 1 %t/$name-clang.err | sed 's/^.* threw: /threw: /' > %t/$name-clang.first; done
// RUN: diff %t/early-read-vm.out %t/early-read-gcc.out
// RUN: diff %t/early-read-vm.first %t/early-read-gcc.first
// RUN: diff %t/early-read-vm.out %t/early-read-clang.out
// RUN: diff %t/early-read-vm.first %t/early-read-clang.first
// RUN: diff %t/disposed-read-vm.out %t/disposed-read-gcc.out
// RUN: diff %t/disposed-read-vm.first %t/disposed-read-gcc.first
// RUN: diff %t/disposed-read-vm.out %t/disposed-read-clang.out
// RUN: diff %t/disposed-read-vm.first %t/disposed-read-clang.first
// EARLY-READ: threw: uncaught TypeError: Cannot read properties of undefined (reading 'width')
// DISPOSED-READ: threw: uncaught TypeError: Cannot read properties of null (reading 'hide')
// STATUS: status=1
//
// A read of the field through an object other than `this` in the class's
// own code: refused, since that object need not be a Modal.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/foreign.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=FOREIGN --implicit-check-not=ctnative.owned_instance
// FOREIGN: ctnative.closure_reason = "the instance `new` makes does not have a closed shape - it is stored into a field that does not own it by value - field `_scrollBar` is read in `peek${{[0-9]+}}` through something other than the `this` of the class's constructor or methods"
//
// An arrow a method makes reads the field through the `this` it captures,
// which is the method's (ClosureLifting/ArrowThis.cpp lexicalThisMaker).
// When the method only calls it, directly or from an arrow it only calls,
// the arrow runs while the method's call does, so the instance outlives it:
// native, and GCC, Clang, the interpreter and Node print the same value.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrow.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/arrow.mlir
// RUN: FileCheck %s --check-prefix=OWNED --implicit-check-not=ctnative.not_native < %t/arrow.mlir
// RUN: ctjs-opt %t/arrow.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/arrow.cpp
// RUN: FileCheck %s --check-prefix=ARROW-CPP < %t/arrow.cpp
// RUN: %gxx -O2 %t/arrow.cpp -o %t/arrow-gcc
// RUN: %t/arrow-gcc | FileCheck %s --check-prefix=RUN-ARROW
// RUN: %clangxx -O2 %t/arrow.cpp -o %t/arrow-clang
// RUN: %t/arrow-clang | FileCheck %s --check-prefix=RUN-ARROW
// RUN: %native_reference %t/arrow.js | FileCheck %s --check-prefix=RUN-ARROW
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/arrow.js | FileCheck %s --check-prefix=RUN-ARROW
// ARROW-CPP: ctnative::owned<ScrollBarHelper> _scrollBar;
// ARROW-CPP: ctnative::js_num close_{{[0-9]+}}(Modal* const {{v[0-9]+}}) {
// ARROW-CPP: ctnative::js_num fn_{{[0-9]+}}(Modal* const {{v[0-9]+}}) {
// ARROW-CPP: = &{{v[0-9]+}}->_scrollBar;
// ARROW-CPP-NEXT: ScrollBarHelper* const {{v[0-9]+}} = ctnative::reading({{v[0-9]+}}, "reset");
// RUN-ARROW: r=805109052
//
// An arrow that is stored (arrow-stored.js) or passed to a call
// (arrow-passed.js) can run after the call that made it returns, when
// nothing holds the instance: refused, naming the arrow and that use. In
// arrow-nested-stored.js the reading arrow is only called, but the arrow
// that makes it is stored, and that one is named.
// RUN: for name in arrow-stored arrow-passed arrow-nested-stored; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/$name.mlir; done
// RUN: FileCheck %s --check-prefix=ARROW-STORED --implicit-check-not=ctnative.owned_instance < %t/arrow-stored.mlir
// RUN: FileCheck %s --check-prefix=ARROW-PASSED --implicit-check-not=ctnative.owned_instance < %t/arrow-passed.mlir
// RUN: FileCheck %s --check-prefix=ARROW-NESTED --implicit-check-not=ctnative.owned_instance < %t/arrow-nested-stored.mlir
// ARROW-STORED: ctnative.closure_reason = "the instance `new` makes does not have a closed shape - it is stored into a field that does not own it by value - field `_scrollBar` is read in `[[FN:fn\$[0-9]+]]` through the `this` that arrow `[[FN]]` captures, and that arrow is stored into an object, so it can run after the call that made it returns, when nothing holds the instance"
// ARROW-PASSED: ctnative.closure_reason = "the instance `new` makes does not have a closed shape - it is stored into a field that does not own it by value - field `_scrollBar` is read in `[[FN:fn\$[0-9]+]]` through the `this` that arrow `[[FN]]` captures, and that arrow is passed to a call, so it can run after the call that made it returns, when nothing holds the instance"
// ARROW-NESTED: ctnative.closure_reason = "the instance `new` makes does not have a closed shape - it is stored into a field that does not own it by value - field `_scrollBar` is read in `fn$[[READS:[0-9]+]]` through the `this` that arrow `[[ESCAPES:fn\$[0-9]+]]` captures, and that arrow is stored into an object, so it can run after the call that made it returns, when nothing holds the instance"
// ARROW-NESTED: ctjs.func {{(private )?}}@[[ESCAPES]](
// ARROW-NESTED: ctjs.create_closure %arg2{{\[}}[[READS]]{{\]}}
//
// One instance stored into two fields would be shared by them: refused.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/aliased.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=ALIASED --implicit-check-not=ctnative.owned_instance
// ALIASED: ctnative.closure_reason = "the instance `new` makes does not have a closed shape - it is stored into a field that does not own it by value - the instance has another use, so field `{{[ab]}}` would share it"
//
// A returned instance its caller returns again stays refused: make does not
// return a record, and it is not native. Built through the run-once
// function's static binding, the class does not reach this rule's refusal;
// inner-twice.js builds the class in make, where the constructor's
// closure_reason names the use.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/twice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=TWICE --implicit-check-not=ctnative.record_return
// TWICE: ctjs.func {{(private )?}}@make${{[0-9]+}}({{.*}}ctnative.not_native =
// static-twice.js pins the clause itself: a static member's `new this`
// lifts as before, its caller returns the instance again, so make is no
// record return and admission refuses it with the escape reason.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/static-twice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=STATIC-TWICE --implicit-check-not=ctnative.record_return
// STATIC-TWICE: ctjs.func {{(private )?}}@make${{[0-9]+}}({{.*}}ctnative.not_native = "an object literal that escapes - it is returned"
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inner-twice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
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

//--- disposed.js
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
        hide() { this.width = 0; return 1; }
    }
    class Modal {
        constructor(on) { this.k = 1; if (on) { this._scrollBar = new ScrollBarHelper(); } }
        open() { return this._scrollBar.hide() + this._scrollBar.width + this.k; }
        dispose() { this._scrollBar = null; this.k = 0; return 5; }
    }
    function show() {
        const m = new Modal(true);
        const a = m.open();
        return a * 10 + m.dispose();
    }
    return show();
})();

//--- early-read.js
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
    }
    class Modal {
        constructor() { this.n = this.peek(); this._scrollBar = new ScrollBarHelper(); }
        peek() { return this._scrollBar.width; }
    }
    return new Modal().n;
})();

//--- disposed-read.js
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
        hide() { this.width = 0; return 1; }
    }
    class Modal {
        constructor() { this.k = 1; this._scrollBar = new ScrollBarHelper(); }
        open() { return this._scrollBar.hide() + this.k; }
        dispose() { this._scrollBar = null; return 5; }
    }
    function show() {
        const m = new Modal();
        const a = m.open() + m.dispose();
        return a + m.open();
    }
    return show();
})();

//--- swipe.js
var r = (function () {
    class Swipe {
        constructor(n) { this.n = n; this.moves = 0; }
        move() { this.moves = this.moves + 1; return this.moves; }
        dispose() { this.n = 0; return 7; }
    }
    class Carousel {
        constructor(touch) {
            this._swipeHelper = null;
            this.k = 2;
            this._addEventListeners(touch);
        }
        _addEventListeners(touch) { if (touch) { this._addTouchEventListeners(); } }
        _addTouchEventListeners() { this._swipeHelper = new Swipe(this.k); }
        slide() { return this._swipeHelper ? this._swipeHelper.move() + this._swipeHelper.n : 0; }
        dispose() {
            const d = this._swipeHelper ? this._swipeHelper.dispose() : 0;
            this._swipeHelper = null;
            return d;
        }
    }
    function show(touch) {
        const c = new Carousel(touch);
        const a = c.slide();
        const b = c.slide();
        return a * 100 + b * 10 + c.dispose() + c.slide();
    }
    return show(true) * 1000 + show(false);
})();

//--- replace.js
var r = (function () {
    class Helper {
        constructor(n) { this.n = n; }
        grow() { this.n = this.n + 1; return this.n; }
    }
    class Owner {
        constructor() { this.h = null; this.count = 0; }
        reset(n) { this.h = new Helper(n); this.count = this.count + 1; return this.h.grow(); }
        get() { return this.h ? this.h.n : -1; }
    }
    function show() {
        const o = new Owner();
        const before = o.get();
        const a = o.reset(1);
        const b = o.reset(5);
        return before * 1000 + a * 100 + b * 10 + o.get() + o.count * 10000;
    }
    return show();
})();

//--- static-twice.js
function run() {
    class Box { constructor(v) { this.v = v; } static make(v) { return new this(v); } }
    function pass(v) { return Box.make(v); }
    return pass(3).v;
}
var r = run();

//--- arrow.js
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
        reset() { this.width = 5; return 2; }
    }
    class Modal {
        constructor(k) { this._scrollBar = new ScrollBarHelper(); this.k = k; }
        close() {
            const done = () => this._scrollBar.reset() + this._scrollBar.width + this.k;
            return done();
        }
        nested() {
            const outer = () => {
                const inner = () => this._scrollBar.width * 10;
                return inner() + this.k;
            };
            return outer();
        }
    }
    function show(k) { const m = new Modal(k); return m.close() * 1000 + m.nested(); }
    return show(1) * 100000 + show(2);
})();

//--- arrow-stored.js
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
        reset() { this.width = 5; return 2; }
    }
    class Modal {
        constructor() { this._scrollBar = new ScrollBarHelper(); this.later = null; }
        close() { this.later = () => this._scrollBar.reset(); return 1; }
    }
    function show() { const m = new Modal(); const a = m.close(); return a + m.later(); }
    return show();
})();

//--- arrow-passed.js
var kept = [];
function keep(f) { kept.push(f); return 0; }
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
        reset() { this.width = 5; return 2; }
    }
    class Modal {
        constructor() { this._scrollBar = new ScrollBarHelper(); }
        close() { return keep(() => this._scrollBar.reset()); }
    }
    function show() { const m = new Modal(); return m.close() + kept[0](); }
    return show();
})();

//--- arrow-nested-stored.js
var r = (function () {
    class ScrollBarHelper {
        constructor() { this.width = 3; }
    }
    class Modal {
        constructor() { this._scrollBar = new ScrollBarHelper(); this.later = null; }
        close() {
            this.later = () => { const inner = () => this._scrollBar.width; return inner(); };
            return 1;
        }
    }
    function show() { const m = new Modal(); return m.close() + m.later(); }
    return show();
})();
