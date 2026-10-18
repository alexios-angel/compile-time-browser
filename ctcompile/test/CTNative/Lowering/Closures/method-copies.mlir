// A closure handed to a method, as Bootstrap's components hand theirs to
// `this._queueCallback(cb, element, animated)`, which hands it on to
// executeAfterTransition.
//
// A method call takes part in ClosureLifting/CallSiteCopies.cpp's copies like
// a direct call when the method census resolves it to one function that reads
// neither its receiver nor its own closure, on a receiver whose every object
// is known. Each `this.queue(n, h)` call in fader.js then calls its own copy
// of `queue` and of `add`, and the closure is made in the copy of `add`, which
// stores it in a record and calls it. Every call of `queue` now calls a copy,
// so the method lifts with no call and its field lowers to nothing; the
// original `queue` and `add` are removed after admission, and the class, its
// methods and the closures lower. The program prints what the interpreter
// prints with both compilers.
//
// The negatives keep the old refusal of the closures, "it is passed as an
// argument": in receiver.js `queue` reads `this`, which a copy called with no
// receiver would not have, and in unknown.js `run` is called through a
// parameter, so the census knows no object its receiver names and resolves
// none of its calls.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/fader.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/fader.mlir
// RUN: FileCheck %s --check-prefix=COPIES --implicit-check-not=ctnative.not_native < %t/fader.mlir
// RUN: ctjs-opt %t/fader.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/fader.cpp
// RUN: FileCheck %s --check-prefix=COPIESCPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/fader.cpp
// RUN: %gxx -O2 %t/fader.cpp -o %t/fader-gcc
// RUN: %t/fader-gcc | FileCheck %s --check-prefix=COPIESOUT
// RUN: %clangxx -O2 %t/fader.cpp -o %t/fader-clang
// RUN: %t/fader-clang | FileCheck %s --check-prefix=COPIESOUT
// RUN: %native_reference %t/fader.js | FileCheck %s --check-prefix=COPIESOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/receiver.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RECEIVER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unknown.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNKNOWN

// COPIES: function = "add${{[0-9]+}}", reason = "removed: unreachable
// COPIES-SAME: function = "queue${{[0-9]+}}", reason = "removed: unreachable
// COPIES-DAG: emitc.func @queue_{{[0-9]+}}(
// COPIES-DAG: emitc.func @queue_{{[0-9]+}}(
// COPIES-DAG: emitc.func @add_{{[0-9]+}}(
// COPIES-DAG: emitc.func @add_{{[0-9]+}}(
// COPIES-DAG: emitc.func @run_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"Fader">>
// COPIESCPP: Fader
// COPIESOUT: r=1630

// RECEIVER: ctjs.func @run${{[0-9]+}}(
// RECEIVER: closure_reason = "it is passed as an argument - lifting has no call site to move the captures to

// UNKNOWN: ctjs.func @run${{[0-9]+}}(
// UNKNOWN: closure_reason = "it is passed as an argument - lifting has no call site to move the captures to

//--- fader.js
var r = (function () {
    function add(n, h) { const entry = { n: n, h: h }; return entry.h(entry.n); }
    class Fader {
        constructor(n) { this.n = n; }
        queue(n, h) { return add(n, h); }
        run(c) {
            const k = c ? 2 : 4;
            return this.queue(this.n, x => x + k) * 100 + this.queue(this.n, x => x * k);
        }
    }
    return new Fader(5).run(true) + new Fader(5).run(false);
})();

//--- receiver.js
var r = (function () {
    function add(n, h) { const entry = { n: n, h: h }; return entry.h(entry.n); }
    class Fader {
        constructor(n) { this.n = n; }
        queue(n, h) { return add(this.n + n, h); }
        run(c) {
            const k = c ? 2 : 4;
            return this.queue(1, x => x + k) * 100 + this.queue(1, x => x * k);
        }
    }
    return new Fader(5).run(true) + new Fader(5).run(false);
})();

//--- unknown.js
var r = (function () {
    function add(n, h) { const entry = { n: n, h: h }; return entry.h(entry.n); }
    class Fader {
        constructor(n) { this.n = n; }
        queue(n, h) { return add(n, h); }
        run(c) {
            const k = c ? 2 : 4;
            return this.queue(this.n, x => x + k) * 100 + this.queue(this.n, x => x * k);
        }
    }
    function go(o, c) { return o.run(c); }
    return go(new Fader(5), true) + go(new Fader(5), false);
})();
