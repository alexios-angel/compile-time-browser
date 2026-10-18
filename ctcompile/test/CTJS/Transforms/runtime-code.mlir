// Code a module creates at run time is refused, with a reason that names the
// API and why its code is not known at build time (WholePage.h).
//
// A native program cannot run code it did not compile, and a whole page
// (every script it will ever run, in one module) stays whole only while no
// code is added later. --ctjs-resolve-globals marks each such operation
// `ctjs.runtime_code` and counts it as a dynamic global write, as eval would
// be: the new code could bind any global, so no global resolves.
// --ctnative-lower-to-emitc refuses the function holding it with that reason.
//
// One witness per shape: a script element, a timer given a String, a
// javascript: URL, markup with an on* attribute, and an on* attribute set by
// name. safe.js holds the same calls with nothing to compile (a function for
// the timer, a div, markup and an attribute name without a handler, a URL that
// is not javascript:), and none is marked.
//
// RUN: split-file %s %t
// RUN: for name in script timer url markup handler; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --mlir-print-debuginfo --whole-page \
// RUN:     2>/dev/null | ctjs-opt --ctjs-resolve-globals > %t/$name.mlir; done
// RUN: FileCheck %s --check-prefix=SCRIPT < %t/script.mlir
// RUN: FileCheck %s --check-prefix=TIMER < %t/timer.mlir
// RUN: FileCheck %s --check-prefix=URL < %t/url.mlir
// RUN: FileCheck %s --check-prefix=MARKUP < %t/markup.mlir
// RUN: FileCheck %s --check-prefix=HANDLER < %t/handler.mlir
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/safe.js --mlir-print-debuginfo --whole-page \
// RUN:   2>/dev/null | ctjs-opt --ctjs-resolve-globals \
// RUN:   | FileCheck %s --check-prefix=SAFE --implicit-check-not=ctjs.runtime_code
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/timer.js --mlir-print-debuginfo --whole-page \
// RUN:   2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NATIVE

// SCRIPT: ctjs.global_escapes = ["the module may create code at run time: `createElement` makes a `script` element, whose code a native program cannot run (ctjs.call at {{.*}}script.js:1)"]
// SCRIPT: ctjs.call {{.*}}ctjs.runtime_code = "`createElement` makes a `script` element

// TIMER: ctjs.global_escapes = ["the module may create code at run time: `setTimeout` compiles a String first argument as code at run time, and this one is not a function made here, so it is not known at build time (ctjs.call at {{.*}}timer.js:1)"]

// URL: ctjs.global_escapes = ["the module may create code at run time: `href` set to the javascript: URL `javascript:go()`, which runs code when followed (ctjs.set_property at {{.*}}url.js:1)"]

// MARKUP: ctjs.global_escapes = ["the module may create code at run time: `innerHTML` with markup holding an on* attribute adds an event handler, whose code a native program cannot compile (ctjs.set_property at {{.*}}markup.js:1)"]

// HANDLER: ctjs.global_escapes = ["the module may create code at run time: `setAttribute` with the name `onclick` adds an on* event handler, whose code a native program cannot compile (ctjs.call at {{.*}}handler.js:1)"]

// SAFE-NOT: ctjs.global_escapes
// SAFE: {name = "later", reason = "closed: 1 call(s) rewritten"

// The function that passes the timer its String is refused with the reason;
// the top level that calls it with a closure is not marked.
// NATIVE: ctjs.func {{.*}}@later$1({{.*}}ctnative.not_native = "`setTimeout` compiles a String first argument as code at run time, and this one is not a function made here, so it is not known at build time (ctjs.call at {{.*}}timer.js:1)"

//--- script.js
function load() { var s = document.createElement("script"); s.text = "x = 1"; document.body.appendChild(s); }
load();

//--- timer.js
function later(code) { setTimeout(code, 0); }
later("x = 1");

//--- url.js
function link(a) { a.href = "javascript:go()"; }
link(document.createElement("a"));

//--- markup.js
function show(el) { el.innerHTML = '<img src="x" onerror="go()">'; }
show(document.body);

//--- handler.js
function arm(el) { el.setAttribute("onclick", "go()"); }
arm(document.body);

//--- safe.js
function later(el, key) {
    setTimeout(function () { el.innerHTML = "<b>done</b>"; }, 0);
    el.appendChild(document.createElement("div"));
    el.setAttribute("data-" + key, "1");
    el.href = "https://example.com/";
}
later(document.body, "x");
