// for-of over arrays. EMIT checks that a for-of over split's array and over
// a map result lowers with nothing refused and no iterator helper call left
// in the lowered functions.
// STRING iterates a String, which the fold indexes only as an array, so the
// function is refused with the fold's reason and gets its body back. PAIRS
// destructures pair-vector rows in the loop head; REST, DEFAULT and NESTED
// keep the iterator helpers and are refused with their pattern named, and
// TEXT destructures a String, which is not a tuple or an array. REDEFINED
// replaces Array.prototype[Symbol.iterator], so neither fold runs: both
// functions (over a String, so the open is the first operation admission
// meets) keep the iterator helpers and are refused with the proof's reason.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EMIT --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/pairs.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PAIRS --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/patterns.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PATTERNS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/redefined.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REDEFINED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=STRING

// EMIT: emitc.func @total
// EMIT: emitc.func @longest
// EMIT-NOT: __ctbrowser_iter
// PAIRS: emitc.func @rows
// PAIRS-NOT: __ctbrowser_iter
// PATTERNS-DAG: @rest${{[0-9]+}}{{.*}}ctnative.not_native = "array destructuring with a rest element, which steps the iterator in a loop"
// PATTERNS-DAG: @withDefault${{[0-9]+}}{{.*}}ctnative.not_native = "array destructuring with a default value"
// PATTERNS-DAG: @nested${{[0-9]+}}{{.*}}ctnative.not_native = "a nested array destructuring pattern"
// PATTERNS-DAG: @text${{[0-9]+}}{{.*}}ctnative.not_native = "array destructuring of {{.*}}, not a tuple or an array this tier holds"
// REDEFINED-DAG: @count${{[0-9]+}}{{.*}}ctnative.not_native = "array iteration needs the realm's own Array iterator: the program uses Array.prototype other than to read a method"
// REDEFINED-DAG: @pair${{[0-9]+}}{{.*}}ctnative.not_native = "array iteration needs the realm's own Array iterator: the program uses Array.prototype other than to read a method"
// REDEFINED-DAG: "__ctbrowser_for_of_open"
// REDEFINED-DAG: "__ctbrowser_iter_open"
// STRING: @letters${{[0-9]+}}{{.*}}ctnative.not_native = "a for-of over {{.*}}, not an array this tier holds"
// STRING: "__ctbrowser_for_of_open"

//--- emit.js
function total(text) {
    var sum = 0;
    for (const p of text.split(',')) {
        sum = sum + Number.parseInt(p, 10);
    }
    return sum;
}
function longest(text) {
    var best = 0;
    for (const n of text.split(',').map(function (t) {
        return t.length;
    })) {
        if (n > best) {
            best = n;
        }
    }
    return best;
}
var a = total('1,2') + longest('a,bb');

//--- string.js
function letters(text) {
    var n = 0;
    for (const c of text) {
        n = n + c.length;
    }
    return n;
}
var r = letters('abc');

//--- pairs.js
function rows() {
    var lines = [];
    var pairs = [];
    pairs.push(['@x', 1.5]);
    pairs.push(['color', 'red']);
    for (const [name, value] of pairs) {
        lines.push(name + '=' + value);
    }
    return lines.join(' ');
}
var r = rows();

//--- patterns.js
function rest(value) {
    const [first, ...others] = value;
    return 1;
}
function withDefault(value) {
    const [first = 'none', second] = value;
    return 2;
}
function nested(value) {
    const [[a, b], c] = value;
    return 3;
}
function text(value) {
    const [a, b] = value;
    return a.length + b.length;
}
var r = rest('ab') + withDefault('cd') + nested('ef') + text('xy');

//--- redefined.js
Array.prototype[Symbol.iterator] = function () {
    return {
        next: function () {
            return { done: true };
        }
    };
};
function count(text) {
    var n = 0;
    for (const c of text) {
        n = n + 1;
    }
    return n;
}
function pair(text) {
    const [a, b] = text;
    return a;
}
var r = count('a,b') + pair('c,d');
