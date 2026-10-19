// Code a module creates at run time is refused, with a reason that names the
// API and why its code is not known at build time (WholePage.h).
//
// A native program cannot run code it did not compile, and a whole page
// (every script it will ever run, in one module) stays whole only while no
// code is added later. --ctnative-lower-to-emitc marks each such operation
// `ctjs.runtime_code` and refuses the function holding it with that reason,
// so a program that may create code has no native build and the new code
// never runs natively. Nothing is marked on a module that is not its page's
// only program: the census's closure query assumes the whole page.
//
// One witness per shape: a script element, a timer given a String, a
// javascript: URL, markup with an on* attribute, and an on* attribute set by
// name. safe.js holds the same calls with nothing to compile (a function for
// the timer, a div, markup and an attribute name without a handler — one with
// a fixed `data-bs-` prefix — and a URL that is not javascript:), and none is
// marked. open.js holds the timer's String in a module that is not its
// page's whole program, and nothing is marked.
//
// RUN: split-file %s %t
// RUN: for name in script timer url markup handler safe name-o name-oncap name-onconst name-param name-mixed; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --mlir-print-debuginfo --whole-page \
// RUN:     2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:     > %t/$name.mlir; done
// RUN: for name in url-tab url-lf url-cr url-location url-assign url-attribute url-attribute-ns; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:     | FileCheck %s --check-prefix=NORMALIZED; done
// RUN: for name in url-https url-space url-percent; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:     | FileCheck %s --check-prefix=CLEAN --implicit-check-not=ctjs.runtime_code; done
// RUN: for name in timer-local-mixed timer-return-mixed timer-array-mixed timer-cell-mixed timer-interval-mixed timer-omitted timer-short-array; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:     | FileCheck %s --check-prefix=MIXED; done
// RUN: for name in timer-local-function timer-return-function timer-array-function; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:     | FileCheck %s --check-prefix=CLEAN --implicit-check-not=ctjs.runtime_code; done
// RUN: FileCheck %s --check-prefix=SCRIPT < %t/script.mlir
// RUN: FileCheck %s --check-prefix=TIMER < %t/timer.mlir
// RUN: FileCheck %s --check-prefix=URL < %t/url.mlir
// RUN: FileCheck %s --check-prefix=MARKUP < %t/markup.mlir
// RUN: FileCheck %s --check-prefix=HANDLER < %t/handler.mlir
// RUN: FileCheck %s --check-prefix=SAFE --implicit-check-not=ctjs.runtime_code < %t/safe.mlir
// RUN: FileCheck %s --check-prefix=NAME < %t/name-o.mlir
// RUN: FileCheck %s --check-prefix=NAME < %t/name-oncap.mlir
// RUN: FileCheck %s --check-prefix=NAMEON < %t/name-onconst.mlir
// RUN: FileCheck %s --check-prefix=NAME < %t/name-param.mlir
// RUN: FileCheck %s --check-prefix=NAME < %t/name-mixed.mlir
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/timer.js --mlir-print-debuginfo \
// RUN:   2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --implicit-check-not=ctjs.runtime_code

// The op is marked, and the function holding it is refused with the reason.
// SCRIPT: ctjs.func {{.*}}@load$1({{.*}}ctnative.not_native = "`createElement` makes a `script` element
// SCRIPT: ctjs.call {{.*}}ctjs.runtime_code = "`createElement` makes a `script` element, whose code a native program cannot run (ctjs.call at {{.*}}script.js:1)"

// TIMER: ctjs.func {{.*}}@later$1({{.*}}ctnative.not_native = "`setTimeout` compiles a String first argument as code at run time, and this one may be a String, so it is not known at build time (ctjs.call at {{.*}}timer.js:1)"

// URL: ctjs.func {{.*}}@link$1({{.*}}ctnative.not_native = "`href` set to the javascript: URL `javascript:go()`, which runs code when followed (ctjs.set_property at {{.*}}url.js:1)"

// MARKUP: ctjs.func {{.*}}@show$1({{.*}}ctnative.not_native = "`innerHTML` with markup holding an on* attribute adds an event handler, whose code a native program cannot compile (ctjs.set_property at {{.*}}markup.js:1)"

// HANDLER: ctjs.func {{.*}}@arm$1({{.*}}ctnative.not_native = "`setAttribute` with the name `onclick` adds an on* event handler, whose code a native program cannot compile (ctjs.call at {{.*}}handler.js:1)"

// Nothing is marked, and the timer taking a closure lowers natively.
// SAFE-NOT: ctnative.not_native
// SAFE: {name = "later", reason = "closed: 1 call(s) rewritten"

// Each name-* witness is refused with the setAttribute name rule's reason.
// NAME: ctnative.not_native = "`setAttribute` with a name not known at build time may add an on* event handler, whose code a native program cannot compile
// NAMEON: ctnative.not_native = "`setAttribute` with the name `On` adds an on* event handler, whose code a native program cannot compile

// Every URL setter uses the URL parser's scheme normalization, not a raw
// prefix check. An internal space and percent-encoded TAB are not removed.
// NORMALIZED: ctnative.not_native = "`{{.*}}` set to the javascript: URL
// NORMALIZED: ctjs.runtime_code = "`{{.*}}` set to the javascript: URL
// CLEAN: module

// A mixed parameter, return, captured cell, or destructured element keeps
// its scalar alternative. The reason itself pins the runtime-code clause.
// MIXED: ctnative.not_native = "`{{setTimeout|setInterval}}` compiles a String first argument as code at run time
// MIXED: ctjs.runtime_code = "`{{setTimeout|setInterval}}` compiles a String first argument as code at run time

// Without --whole-page, the String timer marks nothing.

//--- script.js
function load() { var s = document.createElement("script"); s.text = "x = 1"; document.body.appendChild(s); }
load();

//--- timer.js
function later(code) { setTimeout(code, 0); }
later("x = 1");

//--- timer-local-mixed.js
(function () {
    function later(code) { setTimeout(code, 0); }
    later(function () {});
    later("x = 1");
})();

//--- timer-return-mixed.js
(function () {
    function choose(flag) { if (flag) return function () {}; return "x = 1"; }
    setTimeout(choose(false), 0);
})();

//--- timer-array-mixed.js
(function () {
    function choose(flag) { if (flag) return [function () {}]; return ["x = 1"]; }
    var [code] = choose(false);
    setTimeout(code, 0);
})();

//--- timer-cell-mixed.js
(function () {
    var code = function () {};
    function change() { code = "x = 1"; }
    change();
    setTimeout(code, 0);
})();

//--- timer-interval-mixed.js
(function () {
    function later(code) { setInterval(code, 0); }
    later(function () {});
    later("x = 1");
})();

//--- timer-omitted.js
(function () {
    function later(code) { setTimeout(code, 0); }
    later(function () {});
    later();
})();

//--- timer-short-array.js
(function () {
    function choose(flag) { if (flag) return [function () {}]; return []; }
    var [code] = choose(false);
    setTimeout(code, 0);
})();

//--- timer-local-function.js
(function () {
    function later(code) { setTimeout(code, 0); }
    later(function () {});
})();

//--- timer-return-function.js
(function () {
    function choose() { return function () {}; }
    setTimeout(choose(), 0);
})();

//--- timer-array-function.js
(function () {
    function choose() { return [function () {}]; }
    var [code] = choose();
    setTimeout(code, 0);
})();

//--- url.js
function link(a) { a.href = "javascript:go()"; }
link(document.createElement("a"));

//--- url-tab.js
document.body.href = "java\tscript:go()";

//--- url-lf.js
document.body.href = "java\nscript:go()";

//--- url-cr.js
document.body.href = "java\rscript:go()";

//--- url-location.js
location = "  JaVa\tScRiPt:go()";

//--- url-assign.js
location.assign("java\nscript:go()");

//--- url-attribute.js
document.body.setAttribute("href", "java\rscript:go()");

//--- url-attribute-ns.js
document.body.setAttributeNS(null, "href", "java\tscript:go()");

//--- url-https.js
document.body.href = "https://exa\tmple.com/";

//--- url-space.js
document.body.href = "java script:go()";

//--- url-percent.js
document.body.href = "java%09script:go()";

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
    el.setAttribute(`data-bs-${key}`, "1");
    el.href = "https://example.com/";
}
later(document.body, "x");

//--- name-o.js
function arm(el, x) { el.setAttribute("o" + x, "go()"); }
arm(document.body, "nclick");

//--- name-oncap.js
function arm(el, x) { el.setAttribute("ON" + x, "go()"); }
arm(document.body, "click");

//--- name-onconst.js
function arm(el) { el.setAttribute("On", "go()"); }
arm(document.body);

//--- name-param.js
function arm(el, name) { el.setAttribute(name, "go()"); }
arm(document.body, "onclick");

//--- name-mixed.js
function arm(el, which) {
    var name = "data-x";
    if (which) { name = el.getAttribute("data-name"); }
    el.setAttribute(name, "1");
}
arm(document.body, 1);
