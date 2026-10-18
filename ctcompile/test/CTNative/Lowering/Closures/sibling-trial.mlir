// A function binding read from inside another function takes the data its
// function captures from that function's own captures (a sibling capture),
// which closureLifter::run tries on a clone of the module and adopts only if
// every binding it bound lifted with all its capture holders. One binding
// whose holder cannot lift used to send every binding of the module back to
// the stricter rule. Now the trial is repeated with that binding held to the
// stricter rule, and the bindings that do lift keep their sibling captures.
//
// In mixed.js, `good`'s `f` is read from inside `g`, made in the same frame,
// which takes `k` as a sibling capture; `bad`'s `h` is read from inside an
// arrow stored into an array, which never lifts (`bad` is a declaration
// nothing calls, so its refusal does not reach the script). The script,
// `good` and its two arrows lower, where all of them used to be refused;
// `bad`'s binding keeps the refusal of a capture nothing can pass. good.js
// alone checks what `good` computes, natively and on the interpreter.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/good.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/good.mlir
// RUN: FileCheck %s --check-prefix=GOOD --implicit-check-not=ctnative.not_native < %t/good.mlir
// RUN: ctjs-opt %t/good.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/good.cpp
// RUN: %gxx -O2 %t/good.cpp -o %t/good-gcc
// RUN: %t/good-gcc | FileCheck %s --check-prefix=GOODOUT
// RUN: %clangxx -O2 %t/good.cpp -o %t/good-clang
// RUN: %t/good-clang | FileCheck %s --check-prefix=GOODOUT
// RUN: %native_reference %t/good.js | FileCheck %s --check-prefix=GOODOUT

// The binding that lifts keeps its sibling capture; the other keeps the old
// refusal.
// MIXED-DAG: emitc.func @main()
// MIXED-DAG: emitc.func @good_{{[0-9]+}}()
// MIXED-DAG: emitc.func @fn_{{[0-9]+}}(
// MIXED-DAG: emitc.func @fn_{{[0-9]+}}(
// MIXED-DAG: ctjs.func private @bad${{[0-9]+}}({{.*}}ctnative.not_native
// MIXED-DAG: closure_reason = "it is the value of a local binding this tier cannot call directly: it is read from inside another function, and capture 0 of the function it holds is not a binding this step erases

// GOOD: emitc.func @good_{{[0-9]+}}()
// GOODOUT: r=31

//--- mixed.js
function good() {
    const k = 3;
    const f = () => k * 10;
    const g = () => f() + 1;
    return g();
}
function bad() {
    const m = 4;
    const h = () => m * 100;
    const keep = [() => h() + 2];
    return keep.length;
}
var r = good();

//--- good.js
function good() {
    const k = 3;
    const f = () => k * 10;
    const g = () => f() + 1;
    return g();
}
var r = good();
