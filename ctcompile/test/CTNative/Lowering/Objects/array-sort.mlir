// Array.prototype.sort with a comparator closure lifting specialises for its
// one call. EMIT checks the runtime call, with the sorted array as the call's
// answer and as a statement, and that nothing is refused; each other prefix
// is one refusal with its reason: from closure lifting (the comparator's
// shape), from admission (what it returns), and from the element-array proof
// (an element's children, which have no sort).
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EMIT --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/refusals.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/children.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CHILDREN

// EMIT-COUNT-2: call_opaque "ctnative::Array.prototype.sort.call"

// CHECK-DAG: @extraArgument${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: Array.prototype.sort with an argument after the comparator"
// CHECK-DAG: @threeParameters${{[0-9]+}}{{.*}}ctnative.not_native = "a closure used as a value: the callback of Array.prototype.sort declares 3 parameters - it is passed 2, and needs at least one"
// CHECK-DAG: @textOrder${{[0-9]+}}{{.*}}ctnative.not_native = "the comparator of Array.prototype.sort returns {{.*}}, not a Number or a Boolean"

// CHILDREN: @ordered${{[0-9]+}}{{.*}}ctnative.not_native = "{{.*}}an element's children called with an Array method"

//--- emit.js
function shortestFirst() {
    return ['card', 'btn', 'nav'].sort(function (a, b) {
        return a.length - b.length;
    }).join(',');
}
function descending(least) {
    var words = ['a', 'bb'];
    words.sort(function (a, b) {
        return a.length < b.length + least;
    });
    return words.join(',');
}
var ascending = shortestFirst();
var down = descending(0);

//--- refusals.js
function extraArgument() {
    return ['b', 'a'].sort(function (a, b) {
        return a.length - b.length;
    }, 1).join(',');
}
function threeParameters() {
    return ['b', 'a'].sort(function (a, b, c) {
        return a.length - b.length;
    }).join(',');
}
function textOrder() {
    return ['b', 'a'].sort(function (a, b) {
        return a + b;
    }).join(',');
}
var r1 = extraArgument();
var r2 = threeParameters();
var r3 = textOrder();

//--- children.js
function ordered() {
    var kids = document.documentElement.children;
    kids.sort(function (a, b) {
        return a.tagName < b.tagName;
    });
    return kids.length;
}
var orderedCount = ordered();
