// for-of over arrays. EMIT checks that a for-of over split's array and over
// a map result lowers with nothing refused and no iterator helper call left
// in the lowered functions.
// STRING iterates a String, which the fold indexes only as an array, so the
// function is refused with the fold's reason and gets its body back.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EMIT --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/string.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=STRING

// EMIT: emitc.func @total
// EMIT: emitc.func @longest
// EMIT-NOT: __ctbrowser_iter
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
