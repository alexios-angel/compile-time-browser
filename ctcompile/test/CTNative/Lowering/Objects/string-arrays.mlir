// Arrays of Strings that stay refused, each with its reason: an array made in
// a loop body and read after the loop (so its variable is carried out of the
// loop with its hoisted undefined), an array whose elements are not all
// Strings, and split by a RegExp literal other than /\s+/. An array used only
// inside the loop that made it is not carried at all: --ctjs-lift-to-scf drops
// the unread loop argument.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/loop.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=LOOP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/regexp.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REGEXP

// LOOP: ctjs.func private @lastRow$1
// LOOP-SAME: ctnative.not_native = "an array of Strings: an array
// MIXED: ctjs.func private @mixed$1
// MIXED-SAME: ctnative.not_native = "an array whose elements are {{.*}}, not Strings"
// REGEXP: ctjs.func private @commas$1
// REGEXP-SAME: ctnative.not_native = "a regular expression literal - only `text.split(/\\s+/)` is native"

//--- loop.js
function lastRow() {
    var row;
    for (var i = 0; i < 2; i++) {
        row = ['a'];
        row.push('b');
    }
    return row.join(',');
}
var last = lastRow();

//--- mixed.js
function mixed() {
    var words = ['a', 1];
    return words.join(',');
}
var text = mixed();

//--- regexp.js
function commas(text) {
    return text.split(/,/).join('|');
}
var parts = commas('a,b');
