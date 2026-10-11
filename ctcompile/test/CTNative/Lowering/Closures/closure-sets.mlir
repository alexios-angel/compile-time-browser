// A call through a value that is one of a proved set of closures.
//
// `const f = c ? (x => x + k) : (x => x * 3); f(5)` calls one of two
// closures, and no call site names either one. ClosureLifting/ClosureSets.cpp
// lifts both as members of a set: each stays a value bound to copies of its
// captures, type inference gives the merged value the lattice's
// `closure_set` type, and the emitted C++ is a std::variant of
// ctnative::bound members that ctnative::invoke_callable dispatches with
// std::visit. The positive prints what Node and the interpreter print with
// both compilers; each negative pins the reason a set is refused.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/set.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/set.mlir
// RUN: FileCheck %s --check-prefix=SET --implicit-check-not=ctnative.not_native < %t/set.mlir
// RUN: ctjs-opt %t/set.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/set.cpp
// RUN: FileCheck %s --check-prefix=SETCPP < %t/set.cpp
// RUN: %gxx -O2 %t/set.cpp -o %t/set-gcc
// RUN: %t/set-gcc | FileCheck %s --check-prefix=SETOUT
// RUN: %clangxx -O2 %t/set.cpp -o %t/set-clang
// RUN: %t/set-clang | FileCheck %s --check-prefix=SETOUT
// RUN: %native_reference %t/set.js | FileCheck %s --check-prefix=SETOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/set.js | FileCheck %s --check-prefix=SETOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arity.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARITY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/result.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RESULT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unproved.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNPROVED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escapes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ESCAPES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/written.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=WRITTEN

// Both closures are members bound to their captures, and both calls go
// through the set.
// SET: emitc.func @run_1
// SET: call_opaque "ctnative::bind<fn_2>"
// SET: call_opaque "ctnative::bind<fn_3>"
// SET: call_opaque "ctnative::invoke_callable"
// SET: call_opaque "ctnative::invoke_callable"
// SETCPP: auto {{v[0-9]+}} = ctnative::bind<fn_2>(
// SETCPP: std::variant<ctnative::bound<fn_2, ctnative::js_num>, ctnative::bound<fn_3>> {{v[0-9]+}} =
// SETCPP: auto {{v[0-9]+}} = ctnative::bind<fn_3>();
// SETCPP: ctnative::invoke_callable(
// SETCPP: ctnative::invoke_callable(
// SETOUT: r=28

// Negative: one member declares a parameter the call does not pass. One C++
// signature serves the whole set, so both are refused.
// ARITY: closure_reason = "a closure called through a set of closures declares 2 parameters and a call through the set passes 1 - one C++ signature needs the same count"

// Negative: the members return a number and a string. Admission requires one
// result type for every member.
// RESULT: ctnative.not_native = "a borrowed callback's result is !ctnative.variant<!ctnative.num<f64>, !ctnative.str<utf8>>, not a Number, a Boolean or a String"

// Negative: the other arm is a function the caller passes, not a closure made
// here, so the set is not closed and the closure keeps its own refusal.
// UNPROVED: closure_reason = "it reaches `scf.yield`, which slice 1 does not lower"

// Negative: the merged value is returned rather than called, so it leaves
// the frame and is no set.
// ESCAPES: closure_reason = "it reaches `scf.yield`, which slice 1 does not lower"

// Negative: `k` is assigned after the closure captures it, and a member holds
// a copy of its captures.
// WRITTEN: closure_reason = "a closure called through a set of closures captures binding 0, which is written after it is captured - a member takes its captures by value"

//--- set.js
function run(c) {
    const k = c ? 2 : 4;
    const a = x => x + k;
    const b = x => x * 3;
    const f = c ? a : b;
    return f(5) + f(1);
}
var r = run(true) + run(false);

//--- arity.js
function run(c) {
    const a = x => x + 1;
    const b = (x, y) => x * 3;
    const f = c ? a : b;
    return f(5);
}
var r = run(true) + run(false);

//--- result.js
function run(c) {
    const a = x => x + 1;
    const b = x => "n" + x;
    const f = c ? a : b;
    return f(5);
}
var r = run(true);

//--- unproved.js
function run(c, g) {
    const a = x => x + 1;
    const f = c ? a : g;
    return f(5);
}
var r = run(true, function (x) { return x; });

//--- escapes.js
function pick(c) {
    const a = x => x + 1;
    const b = x => x * 3;
    return c ? a : b;
}
var r = pick(true)(5);

//--- written.js
function run(c) {
    let k = 1;
    const a = x => x + k;
    const b = x => x * 3;
    k = 10;
    const f = c ? a : b;
    return f(5);
}
var r = run(true);
