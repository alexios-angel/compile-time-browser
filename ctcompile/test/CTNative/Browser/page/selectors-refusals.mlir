// Element calls and reads of an ordinary script that stay refused, each with
// its reason: matches given something other than a String, a member of an
// element or null that is not native yet, and SelectorEngine's default root
// `e === undefined` when a caller passes null, which the element-or-null
// carrier cannot tell from an omitted argument.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/argument.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ARGUMENT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/null.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NULL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/member.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MEMBER

// ARGUMENT: ctjs.func private @pick$1
// ARGUMENT-SAME: ctnative.not_native = "an element's matches given !ctnative.num{{.*}}, not a String"

// MEMBER: ctjs.func private @sibling$1
// MEMBER-SAME: ctnative.not_native = "an element or null read through `innerHTML` - it reads only tagName, id, className and the element links"

// NULL: ctnative.not_native = "`=== undefined` on an element or null: a caller passes {{.*}}, which may be null, and null and undefined are one empty optional"

//--- argument.js
function pick() {
    const root = document.documentElement;
    return root.matches(1);
}
var picked = pick();

//--- member.js
function sibling() {
    const next = document.documentElement.nextElementSibling;
    return next ? next.innerHTML : '';
}
var siblingText = sibling();

//--- null.js
var foundNull = (function () {
    const R = {
        find: (t, e = document.documentElement) => [].concat(...Element.prototype.querySelectorAll.call(e, t))
    };
    function run() { return R.find('p', null).length + R.find('p', document.documentElement).length; }
    return run();
})();
