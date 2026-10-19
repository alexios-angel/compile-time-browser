// A test of a closure is answered before the closure census
// (ClosureLifting/ConstantTests.cpp): `typeof` of a closure is "function" and
// a closure is truthy. What those answers meet folds in turn through the
// script runtime's operators, and an scf.if they decide becomes the branch it
// takes, so a closure it used to yield is called directly and lifts.
//
// In known.js, `a`, `b` and `c` are each `h` behind a test the closure
// answers: `typeof h === "string"`, `h ||`, and `typeof h != "function"`
// (`!=` is the negation of `==`). Without the fold `h` reaches an scf.yield
// and `known` is refused.
//
// In kept.js the test is answered but the branch it drops calls `zero`, and
// erasing a call would leave the lifter's indexes naming it, so the branch
// stays and `h` keeps the old refusal. Its condition stays a test too
// (ctjs.truthy, not a constant): a constant one would let the type solve
// treat `zero`, called only from the dead arm, as unreachable. `zero` is
// refused with its refused caller, as any function a refused caller calls
// directly is (Admission/refusal/call-graph.mlir LEAF). kept-static.js is
// the same program with a static member `Z.zero()` beside the branch: the
// refused caller calls it through a closure, so it stays native (STATIC
// there). In runtime.js the test reads a number known only at run time, so
// the branch stays.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/known.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/known.mlir
// RUN: FileCheck %s --check-prefix=KNOWN --implicit-check-not=ctnative.not_native < %t/known.mlir
// RUN: ctjs-opt %t/known.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/known.cpp
// RUN: %gxx -O2 %t/known.cpp -o %t/known-gcc
// RUN: %t/known-gcc | FileCheck %s --check-prefix=OUT
// RUN: %clangxx -O2 %t/known.cpp -o %t/known-clang
// RUN: %t/known-clang | FileCheck %s --check-prefix=OUT
// RUN: %native_reference %t/known.js | FileCheck %s --check-prefix=OUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/kept.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/kept-static.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=KEPTSTATIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/runtime.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=RUNTIME

// KNOWN: emitc.func @known_{{[0-9]+}}(%arg0: !emitc.opaque<"ctnative::js_num">)
// KNOWN: emitc.func @fn_{{[0-9]+}}(%arg0: !emitc.opaque<"ctnative::js_num">, %arg1: !emitc.opaque<"ctnative::js_num">)
// OUT: r=102

// KEPT: ctjs.func private @zero${{[0-9]+}}({{.*}}ctnative.not_native = "called by `kept${{[0-9]+}}`, which is not native"
// KEPT: ctjs.func private @kept${{[0-9]+}}({{.*}}ctnative.not_native = "a closure used as a value: it reaches `scf.yield`, which slice 1 does not lower"
// KEPT: [[TEST:%[0-9]+]] = ctjs.truthy
// KEPT-NEXT: scf.if [[TEST]]

// KEPTSTATIC-DAG: ctjs.func private @kept${{[0-9]+}}({{.*}}ctnative.not_native = "a closure used as a value: it reaches `scf.yield`, which slice 1 does not lower"
// KEPTSTATIC-DAG: emitc.func @zero_{{[0-9]+}}()

// RUNTIME: ctjs.func private @runtime${{[0-9]+}}({{.*}}ctnative.not_native = "a closure used as a value: it reaches `scf.yield`, which slice 1 does not lower"

//--- known.js
function known(k) {
    const h = (x) => x + k;
    const a = typeof h === "string" ? null : h;
    const b = h || null;
    const c = typeof h != "function" ? null : h;
    return a(1) + b(2) + c(3);
}
var r = known(32);

//--- kept.js
function zero() { return null; }
function kept(k) {
    const h = (x) => x + k;
    const g = typeof h === "function" ? h : zero();
    return g(1);
}
var r = kept(3);

//--- kept-static.js
function none() { return null; }
function kept(k) {
    class Z { static zero() { return 0; } }
    const h = (x) => x + k;
    const g = typeof h === "function" ? h : none();
    return g(Z.zero());
}
var r = kept(3);

//--- runtime.js
function runtime(c) {
    const h = (x) => x + 1;
    const g = c > 15 ? h : null;
    return g ? g(c) : 0;
}
var r = runtime(20);
