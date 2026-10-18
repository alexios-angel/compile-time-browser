// Array callback methods (forEach, map, filter, some, every, find and reduce)
// whose callback closure lifting specialises for its one call. EMIT checks
// the runtime calls with and without captures, and that nothing is refused;
// each other prefix is one refusal with its reason, from closure lifting (the
// callback's shape) or from admission (the types).
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true | FileCheck %s --check-prefix=EMIT --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/refusals.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true | FileCheck %s
//
// In settled.js a callback captures a binding its frame writes only before
// the callback is made (a defaulted parameter, an assignment under a test),
// so it takes that binding's value by copy; the native answers equal the
// interpreter's, and a changed answer is caught.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/settled.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true | FileCheck %s --check-prefix=SETTLED --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/settled.js 2>/dev/null | ctjs-opt --pass-pipeline="builtin.module(ctjs-resolve-globals,ctjs-lift-to-scf,ctnative-lower-to-emitc{observe-globals=true},emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))" -o %t/settled.mlir
// RUN: %compilation_unit --module %t/settled.mlir --js %t/settled.js --work %t/settled --name settled_captures
// RUN: not %compilation_unit --module %t/settled.mlir --js %t/settled.js --work %t/settled --name settled_captures_off --mutate p2 2>&1 | FileCheck %s --check-prefix=SETTLED-OFF

// SETTLED-DAG: call_opaque "ctnative::Array.prototype.filter.call"
// SETTLED-DAG: call_opaque "std::bind_front"
// SETTLED-OFF: global 'p2' differs

// EMIT-DAG: call_opaque "std::bind_front"
// EMIT-DAG: call_opaque "ctnative::Array.prototype.filter.call"
// EMIT-DAG: call_opaque "ctnative::Array.prototype.some.call"
// EMIT-DAG: call_opaque "ctnative::Array.prototype.reduce.call"

// CHECK-DAG: @reduceNoInit${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: Array.prototype.reduce without an initial value - it throws on an empty array"
// CHECK-DAG: @thisArg${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: Array.prototype.forEach called with a thisArg"
// CHECK-DAG: @readsThis${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the callback of Array.prototype.map reads `this`"
// CHECK-DAG: @fourParameters${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the callback of Array.prototype.filter declares 4 parameters - it is passed 3, and needs at least one"
// CHECK-DAG: @counts${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the callback of Array.prototype.forEach captures binding 0, which is written after it is captured - the callback takes its captures by value"
// A write after the callback is made, and one in a loop around it (the
// binding is made outside the loop), keep the copy from being exact.
// CHECK-DAG: @later${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the callback of Array.prototype.filter captures binding 0, which is written after it is captured - the callback takes its captures by value"
// CHECK-DAG: @rounds${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the callback of Array.prototype.filter captures binding 0, which is written after it is captured - the callback takes its captures by value"
// CHECK-DAG: @notAnArray${{[0-9]+}}{{.*}}ctnative.not_native = "the receiver of Array.prototype.filter is
// CHECK-DAG: @flags${{[0-9]+}}{{.*}}ctnative.not_native = "the result of Array.prototype.map is
// CHECK-DAG: @mixedSum${{[0-9]+}}{{.*}}ctnative.not_native = "reduce's accumulator changes type: the initial value is

//--- emit.js
function longCount(least) {
    return ['ab', 'abcd'].filter(function (s) {
        return s.length > least;
    }).length;
}
function hasB() {
    return ['a', 'b'].some(function (s) {
        return s === 'b';
    });
}
function joined() {
    return ['a', 'b'].reduce(function (text, s) {
        return text + s;
    }, '');
}
var count = longCount(2);
var b = hasB() ? 1 : 0;
var text = joined();

//--- refusals.js
function reduceNoInit() {
    return ['a', 'b'].reduce(function (text, s) {
        return text + s;
    });
}
function thisArg() {
    ['a'].forEach(function (s) {
        return s;
    }, 1);
    return 0;
}
function readsThis() {
    return ['a'].map(function (s) {
        return this === undefined ? s : s;
    }).length;
}
function fourParameters() {
    return ['a'].filter(function (s, i, all, extra) {
        return s;
    }).length;
}
function counts() {
    var n = 0;
    ['a', 'b'].forEach(function (s) {
        n = n + 1;
    });
    return n;
}
function later() {
    var limit = 1;
    var keep = function (s) {
        return s.length >= limit;
    };
    limit = 2;
    return ['a', 'bb'].filter(keep).length;
}
function rounds() {
    var total = 0;
    var limit = 0;
    for (var i = 1; i < 3; i++) {
        limit = i;
        total = total + ['a', 'bb'].filter(function (s) {
            return s.length >= limit;
        }).length;
    }
    return total;
}
function notAnArray(list) {
    return list.filter(function (s) {
        return s;
    }).length;
}
function flags() {
    return ['a', 'bb'].map(function (s) {
        return s === 'a';
    }).length;
}
function mixedSum() {
    return ['a', 'b'].reduce(function (sum, s) {
        return sum + s;
    }, 0);
}
var r1 = reduceNoInit();
var r2 = thisArg();
var r3 = readsThis();
var r4 = fourParameters();
var r5 = counts();
var r6 = notAnArray(7);
var r7 = flags();
var r8 = mixedSum();
var r9 = later();
var r10 = rounds();

//--- settled.js
function longerThan(least = 2) {
    return ['ab', 'abcd'].filter(function (s) {
        return s.length > least;
    }).length;
}
function pick(flag) {
    var limit = 1;
    if (flag) {
        limit = 3;
    }
    return ['a', 'bb', 'ccc'].filter(function (s) {
        return s.length >= limit;
    }).length;
}
var d1 = longerThan();
var d2 = longerThan(1);
var p1 = pick(true);
var p2 = pick(false);
