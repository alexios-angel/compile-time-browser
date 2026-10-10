// Array callback methods (forEach, map, filter, some, every, find and reduce)
// whose callback closure lifting specialises for its one call. EMIT checks the
// spelling of a call with and without captures; each other prefix is one
// refusal with its reason, from closure lifting (the callback's shape) or
// from admission (the types).
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | ctjs-translate --mlir-to-cpp | FileCheck %s --check-prefix=EMIT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/refusals.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s

// EMIT-DAG: ctnative::Array.prototype.filter.call({{.*}}std::bind_front(
// EMIT-DAG: ctnative::Array.prototype.some.call(
// EMIT-DAG: ctnative::Array.prototype.reduce.call(

// CHECK-DAG: @reduceNoInit${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: Array.prototype.reduce without an initial value - it throws on an empty array"
// CHECK-DAG: @thisArg${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: Array.prototype.forEach called with a thisArg"
// CHECK-DAG: @readsThis${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the callback of Array.prototype.map reads `this`"
// CHECK-DAG: @fourParameters${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the callback of Array.prototype.filter declares 4 parameters - it is passed 3, and needs at least one"
// CHECK-DAG: @counts${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the callback of Array.prototype.forEach captures binding 0, which is written after it is captured - the callback takes its captures by value"
// CHECK-DAG: @notAnArray${{[0-9]+}}{{.*}}ctnative.not_native = "the receiver of Array.prototype.filter is
// CHECK-DAG: @lengths${{[0-9]+}}{{.*}}ctnative.not_native = "the result of Array.prototype.map is
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
function notAnArray(list) {
    return list.filter(function (s) {
        return s;
    }).length;
}
function lengths() {
    return ['a', 'bb'].map(function (s) {
        return s.length;
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
var r7 = lengths();
var r8 = mixedSum();
