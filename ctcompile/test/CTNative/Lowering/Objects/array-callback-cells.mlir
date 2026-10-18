// Array callbacks that capture a binding the callback or its frame writes.
// The runtime calls such a callback only during the call, so it takes the
// binding by the address of the frame's variable: EMIT checks a forEach that
// counts into it, a filter whose limit is written after the callback is made,
// a sort comparator that counts its calls, a String binding the callback
// sets to a Number (the callback's write reaches the binding's type), and a
// local function that hands the pointer it was given on to its own forEach
// callback. SHARED pins the
// one carried binding that stays refused, a binding a kept listener also
// holds. STORED pins that a callback whose closure is also stored or
// returned, or that hands its own closure on, keeps its refusal: the address
// it holds must not outlive the call.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EMIT --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shared.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SHARED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=STORED

// The callback takes a pointer to the frame's Number, and the call binds the
// address of the variable, or forwards the pointer the frame was given.
// EMIT-DAG: emitc.func @fn_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"ctnative::js_num">>, %arg1: !emitc.opaque<"ctnative::js_string">) -> !emitc.opaque<"ctnative::nullable_scalar">
// EMIT-DAG: %[[ADDRESS:[0-9]+]] = address_of %{{[0-9]+}} : !emitc.lvalue<!emitc.opaque<"ctnative::js_num">>
// EMIT-DAG: call_opaque "std::bind_front"(%{{[0-9]+}}, %[[ADDRESS]])
// EMIT-DAG: call_opaque "ctnative::Array.prototype.filter.call"
// EMIT-DAG: call_opaque "ctnative::Array.prototype.sort.call"
// EMIT-DAG: emitc.func @mixed_{{[0-9]+}}() -> !emitc.opaque<"ctnative::number_string">
// EMIT-DAG: emitc.func @fn_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"ctnative::number_string">>
// EMIT-DAG: emitc.func @add_{{[0-9]+}}(%arg0: !emitc.ptr<!emitc.opaque<"ctnative::js_num">>
// EMIT-DAG: call_opaque "std::bind_front"(%{{[0-9]+}}, %arg0)

// SHARED: @watch${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the callback of Array.prototype.forEach captures binding 0, which a callback the runtime keeps also holds - the callback takes its captures by value or by address, not as a shared handle"

// STORED-DAG: @storedAfter${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: it is stored to a global
// STORED-DAG: @storedBefore${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: it is passed as an argument
// STORED-DAG: @returned${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: returned closure
// STORED-DAG: @storesItself${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: its target's own closure escapes into `ctjs.store_global`
// STORED-DAG: @passesItself${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: its target's own closure escapes into `ctjs.call`

//--- emit.js
function counts() {
    var n = 0;
    ['a', 'b'].forEach(function (s) {
        n = n + 1;
    });
    return n;
}
function later() {
    var limit = 1;
    var kept = ['a', 'bb', 'ccc'].filter(function (s) {
        return s.length > limit;
    });
    limit = 4;
    return kept.length + limit;
}
function comparisons() {
    var calls = 0;
    ['b', 'a', 'c'].sort(function (a, b) {
        calls = calls + 1;
        return a.length - b.length;
    });
    return calls;
}
function mixed() {
    var seen = 'none';
    ['card', 'btn'].forEach(function (s) {
        seen = s.length;
    });
    return seen;
}
function handedOn() {
    var count = 0;
    function add(list) {
        list.forEach(function (s) {
            count = count + s.length;
        });
    }
    add(['ab', 'c']);
    add(['def']);
    return count;
}
var n1 = counts();
var n2 = later();
var n3 = comparisons();
var n4 = handedOn();
var n5 = mixed();

//--- shared.js
function watch() {
    var clicks = 0;
    document.documentElement.addEventListener('click', function () {
        clicks = clicks + 1;
    });
    ['a'].forEach(function (s) {
        clicks = clicks + s.length;
    });
    return clicks;
}
var watched = watch();

//--- stored.js
var saved = 0;
function storedAfter() {
    var n = 0;
    var f = function (s) {
        n = n + 1;
    };
    ['a'].forEach(f);
    saved = f;
    return n;
}
function storedBefore() {
    var n = 0;
    var f = function (s) {
        n = n + 1;
    };
    saved = f;
    ['a'].forEach(f);
    return n;
}
function returned() {
    var n = 0;
    var f = function (s) {
        n = n + 1;
    };
    ['a'].forEach(f);
    return f;
}
function storesItself() {
    var n = 0;
    ['a'].forEach(function self(s) {
        n = n + 1;
        saved = self;
    });
    return n;
}
function passesItself(keep) {
    var n = 0;
    ['a'].forEach(function self(s) {
        n = n + 1;
        keep(self);
    });
    return n;
}
var r1 = storedAfter();
var r2 = storedBefore();
var r3 = returned();
var r4 = storesItself();
var r5 = passesItself(function (f) {
    return f;
});
