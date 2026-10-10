// Page reads of an ordinary script that stay refused, each with its reason:
// documentElement inside a try (the runtime throws a C++ exception for a
// document with no root element, which native code cannot catch), clientWidth
// of an element other than the root, an Array method on an element's
// children, an element member that is not native yet, an element read at an
// index not proved in bounds, a rect field that is not native yet, and
// getBoundingClientRect read as a value rather than tested or called.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/try.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=TRY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/client.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CLIENT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/push.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PUSH
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/member.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MEMBER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/field.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=FIELD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/method.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=METHOD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/index.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=INDEX

// TRY: ctjs.func private @guarded$1
// TRY-SAME: ctnative.not_native = "native try/catch cannot prove a nonthrowing operation: `ctjs.load_global`"

// CLIENT: ctjs.func private @widths$1
// CLIENT-SAME: ctnative.not_native = "`clientWidth` of an element other than document.documentElement - only the root's client size is native"

// PUSH: ctjs.func private @grow$1
// PUSH-SAME: ctnative.not_native = "an element's children: an element's children called with an Array method"

// MEMBER: ctjs.func private @markup$1
// MEMBER-SAME: ctnative.not_native = "an element read through `innerHTML` - {{.*}}"

// INDEX: ctjs.func private @fifth$1
// INDEX-SAME: ctnative.not_native = "an element of an element's children read at an index not proved in bounds"

// FIELD: ctjs.func private @corner$1
// FIELD-SAME: ctnative.not_native = "a rect read through `x` - only left, top, width and height are native"

// METHOD: ctjs.func private @method$1
// METHOD-SAME: ctnative.not_native = "an element's getBoundingClientRect read as a value - only testing it and calling it are native"

//--- try.js
function guarded(stop) {
    var tag = 'none';
    try {
        if (stop) {
            throw 'stopped';
        }
        tag = document.documentElement.tagName;
    } catch (e) {
        tag = e;
    }
    return tag;
}
var guardedTag = guarded(false);

//--- client.js
function widths() {
    var kids = document.documentElement.children;
    var total = 0;
    for (var i = 0; i < kids.length; i++) {
        total += kids[i].clientWidth;
    }
    return total;
}
var widthTotal = widths();

//--- push.js
function grow() {
    var kids = document.documentElement.children;
    kids.push(document.documentElement);
    return kids.length;
}
var grown = grow();

//--- member.js
function markup() {
    return document.documentElement.innerHTML;
}
var markupText = markup();

//--- index.js
function fifth() {
    var kids = document.documentElement.children;
    return kids[5].tagName;
}
var fifthTag = fifth();

//--- field.js
function corner() {
    return document.documentElement.getBoundingClientRect().x;
}
var cornerX = corner();

//--- method.js
function method() {
    var read = document.documentElement.getBoundingClientRect;
    return read === undefined;
}
var methodMissing = method();
