// Arrays of Strings that stay refused, each with its reason: an array made in
// a loop body and read after the loop (so its variable is carried out of the
// loop with its hoisted undefined), an array whose elements are not all
// Strings, split by a RegExp literal other than /\s+/, and a parameter array
// returned from its function. An array used only inside the loop that made it
// is not carried at all: --ctjs-lift-to-scf drops the unread loop argument.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/loop.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=LOOP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/regexp.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REGEXP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/paramwrite.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PARAMWRITE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/spaces.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=SPACES --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func

// LOOP: ctjs.func private @lastRow$1
// LOOP-SAME: ctnative.not_native = "an array of Strings: an array
// MIXED: ctjs.func private @mixed$1
// MIXED-SAME: ctnative.not_native = "an array whose elements are {{.*}}, not Strings"
// REGEXP: ctjs.func private @commas$1
// REGEXP-SAME: ctnative.not_native = "a regular expression literal - only `text.split(/\\s+/)` is native"

// split(/\s+/) in a helper the top level calls. The RegExp literal's factory
// load lowers to nothing; the global census once counted it as a global
// read but never stored and refused the top level after the call graph was
// closed, which left the helper lowered with a caller still naming it (an
// LLVM ERROR, not a refusal). Both functions are native now.
// SPACES-DAG: emitc.func @words_1(
// SPACES-DAG: emitc.func @main(

// A parameter is a read-only borrow of the caller's array: a callee that
// pushes to it is refused with the proof's reason, and its caller with it.
// PARAMWRITE: ctjs.func private @addTo$1
// PARAMWRITE-SAME: ctnative.not_native = "an element array: a parameter array returned from the function"

//--- paramwrite.js
function addTo(list) {
    list.push('x');
    return list;
}
function caller() {
    var words = ['q'];
    return addTo(words).length;
}
var count = caller();

//--- spaces.js
function words(text) {
    return text.split(/\s+/).join('.');
}
var joined = words('a  b');

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
