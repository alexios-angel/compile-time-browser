// Computed writes onto objects the program did not create must invalidate all
// three property-absence proofs. The location strings stand for outside data:
// "#contains" for argument.js, and "#__proto__" / "?getRootNode" or
// "?eventName" for window.js / compiler.js. None of those key strings is a
// constant operand of the write.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/argument.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ARGUMENT --implicit-check-not='call_direct @add'
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINDOW --implicit-check-not='call_direct @add'
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compiler.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=COMPILER --implicit-check-not='call_direct @add'
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/local.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=LOCAL

// The overwritten DOM method gets the global object as an argument. Assuming
// contains is still the realm's method hides its write to the add binding.
// ARGUMENT: {name = "add", reason = "the global object escapes into a call whose callee this module does not name{{[^"]*}}", resolved = "none", stores = 1 : i32}
// ARGUMENT: ctjs.load_global "add"
// ARGUMENT: ctjs.call %

// A computed prototype read avoids the separate constant-prototype guard. The
// computed write can give the window an inherited method with a global write.
// WINDOW: {name = "add", reason = "the global object escapes into a call whose callee this module does not name{{[^"]*}}", resolved = "none", stores = 1 : i32}
// WINDOW: ctjs.load_global "add"
// WINDOW: ctjs.call %

// The same inherited property on Function receives the compiler as its this.
// Treating eventName as absent would hide the program's call into that compiler.
// COMPILER: {name = "add", reason = "a value that may be the run-time compiler escapes into a call whose callee this module does not name{{[^"]*}}", resolved = "none", stores = 1 : i32}
// COMPILER: ctjs.load_global "add"
// COMPILER: ctjs.call %

// Fresh objects, arrays and closures cannot be realm prototype tables. Their
// computed writes do not disable the three otherwise valid absence proofs.
// LOCAL: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// LOCAL: ctjs.call_direct @add$1

//--- argument.js
function add(a, b) { return a + b; }
var el = document.body;
el[location.hash.slice(1)] = function (w) {
    w.add = function () { return 9; };
};
el.contains(window);
var z = add(1, 2);

//--- window.js
function add(a, b) { return a + b; }
var table = ({})[location.hash.slice(1)];
table[location.search.slice(1)] = function () {
    this.add = function () { return 9; };
};
window.getRootNode();
var z = add(1, 2);

//--- compiler.js
function add(a, b) { return a + b; }
var table = ({})[location.hash.slice(1)];
table[location.search.slice(1)] = function () {
    return this("add = function () { return 9; };")();
};
function grab(o) { return o.constructor.eventName(); }
var compiled = grab(function () {});
var z = add(1, 2);

//--- local.js
function add(a, b) { return a + b; }
(function () {
    var object = {};
    var array = [];
    var closure = function () {};
    if (location.hash) { object[location.hash.slice(1)] = 1; }
    array[location.hash.slice(1)] = 2;
    closure[location.hash.slice(1)] = 3;
})();
document.body.contains(window);
window.getRootNode();
function grab(o) { return o.constructor.eventName(); }
var compiled = grab(function () {});
var z = add(1, 2);
