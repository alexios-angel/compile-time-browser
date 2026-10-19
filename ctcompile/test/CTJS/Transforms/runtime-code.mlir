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

// RUN: for name in sink-srcdoc-property sink-srcdoc-attribute sink-srcdoc-attribute-ns sink-sethtml-script sink-sethtml-runscripts sink-parsehtml-script sink-fragment-script sink-write-script sink-writeln-script sink-outer-handler sink-adjacent-handler sink-srcdoc-nested sink-markup-frame sink-iframe-url sink-object-url sink-embed-url sink-anchor-url sink-form-url sink-location-url sink-location-href-url sink-location-assign-url sink-location-replace-url sink-window-open-url sink-global-open-url sink-attribute-src-url sink-attribute-data-url sink-attribute-href-url sink-attribute-action-url sink-script-create sink-script-create-ns sink-script-insert sink-script-before sink-script-replace sink-script-append sink-script-text sink-script-src sink-script-text-content sink-script-src-attribute sink-safe-img sink-safe-anchor-text sink-safe-div-text sink-safe-append-div sink-safe-srcdoc sink-safe-sethtml sink-safe-parsehtml; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page --mlir-print-debuginfo 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc > %t/$name.mlir; done
// RUN: FileCheck %s --check-prefix=SINKSRCDOCPROPERTY < %t/sink-srcdoc-property.mlir
// RUN: FileCheck %s --check-prefix=SINKSRCDOCATTRIBUTE < %t/sink-srcdoc-attribute.mlir
// RUN: FileCheck %s --check-prefix=SINKSRCDOCATTRIBUTENS < %t/sink-srcdoc-attribute-ns.mlir
// RUN: FileCheck %s --check-prefix=SINKSETHTMLSCRIPT < %t/sink-sethtml-script.mlir
// RUN: FileCheck %s --check-prefix=SINKSETHTMLRUNSCRIPTS < %t/sink-sethtml-runscripts.mlir
// RUN: FileCheck %s --check-prefix=SINKPARSEHTMLSCRIPT < %t/sink-parsehtml-script.mlir
// RUN: FileCheck %s --check-prefix=SINKFRAGMENTSCRIPT < %t/sink-fragment-script.mlir
// RUN: FileCheck %s --check-prefix=SINKWRITESCRIPT < %t/sink-write-script.mlir
// RUN: FileCheck %s --check-prefix=SINKWRITELNSCRIPT < %t/sink-writeln-script.mlir
// RUN: FileCheck %s --check-prefix=SINKOUTERHANDLER < %t/sink-outer-handler.mlir
// RUN: FileCheck %s --check-prefix=SINKADJACENTHANDLER < %t/sink-adjacent-handler.mlir
// RUN: FileCheck %s --check-prefix=SINKSRCDOCNESTED < %t/sink-srcdoc-nested.mlir
// RUN: FileCheck %s --check-prefix=SINKMARKUPFRAME < %t/sink-markup-frame.mlir
// RUN: FileCheck %s --check-prefix=SINKIFRAMEURL < %t/sink-iframe-url.mlir
// RUN: FileCheck %s --check-prefix=SINKOBJECTURL < %t/sink-object-url.mlir
// RUN: FileCheck %s --check-prefix=SINKEMBEDURL < %t/sink-embed-url.mlir
// RUN: FileCheck %s --check-prefix=SINKANCHORURL < %t/sink-anchor-url.mlir
// RUN: FileCheck %s --check-prefix=SINKFORMURL < %t/sink-form-url.mlir
// RUN: FileCheck %s --check-prefix=SINKLOCATIONURL < %t/sink-location-url.mlir
// RUN: FileCheck %s --check-prefix=SINKLOCATIONHREFURL < %t/sink-location-href-url.mlir
// RUN: FileCheck %s --check-prefix=SINKLOCATIONASSIGNURL < %t/sink-location-assign-url.mlir
// RUN: FileCheck %s --check-prefix=SINKLOCATIONREPLACEURL < %t/sink-location-replace-url.mlir
// RUN: FileCheck %s --check-prefix=SINKWINDOWOPENURL < %t/sink-window-open-url.mlir
// RUN: FileCheck %s --check-prefix=SINKGLOBALOPENURL < %t/sink-global-open-url.mlir
// RUN: FileCheck %s --check-prefix=SINKATTRIBUTESRCURL < %t/sink-attribute-src-url.mlir
// RUN: FileCheck %s --check-prefix=SINKATTRIBUTEDATAURL < %t/sink-attribute-data-url.mlir
// RUN: FileCheck %s --check-prefix=SINKATTRIBUTEHREFURL < %t/sink-attribute-href-url.mlir
// RUN: FileCheck %s --check-prefix=SINKATTRIBUTEACTIONURL < %t/sink-attribute-action-url.mlir
// RUN: FileCheck %s --check-prefix=SINKSCRIPTCREATE < %t/sink-script-create.mlir
// RUN: FileCheck %s --check-prefix=SINKSCRIPTCREATENS < %t/sink-script-create-ns.mlir
// RUN: FileCheck %s --check-prefix=SINKSCRIPTINSERT < %t/sink-script-insert.mlir
// RUN: FileCheck %s --check-prefix=SINKSCRIPTBEFORE < %t/sink-script-before.mlir
// RUN: FileCheck %s --check-prefix=SINKSCRIPTREPLACE < %t/sink-script-replace.mlir
// RUN: FileCheck %s --check-prefix=SINKSCRIPTAPPEND < %t/sink-script-append.mlir
// RUN: FileCheck %s --check-prefix=SINKSCRIPTTEXT < %t/sink-script-text.mlir
// RUN: FileCheck %s --check-prefix=SINKSCRIPTSRC < %t/sink-script-src.mlir
// RUN: FileCheck %s --check-prefix=SINKSCRIPTTEXTCONTENT < %t/sink-script-text-content.mlir
// RUN: FileCheck %s --check-prefix=SINKSCRIPTSRCATTRIBUTE < %t/sink-script-src-attribute.mlir
// RUN: FileCheck %s --check-prefix=SINKSAFEIMG --implicit-check-not=ctjs.runtime_code < %t/sink-safe-img.mlir
// RUN: FileCheck %s --check-prefix=SINKSAFEANCHORTEXT --implicit-check-not=ctjs.runtime_code < %t/sink-safe-anchor-text.mlir
// RUN: FileCheck %s --check-prefix=SINKSAFEDIVTEXT --implicit-check-not=ctjs.runtime_code < %t/sink-safe-div-text.mlir
// RUN: FileCheck %s --check-prefix=SINKSAFEAPPENDDIV --implicit-check-not=ctjs.runtime_code < %t/sink-safe-append-div.mlir
// RUN: FileCheck %s --check-prefix=SINKSAFESRCDOC --implicit-check-not=ctjs.runtime_code < %t/sink-safe-srcdoc.mlir
// RUN: FileCheck %s --check-prefix=SINKSAFESETHTML --implicit-check-not=ctjs.runtime_code < %t/sink-safe-sethtml.mlir
// RUN: FileCheck %s --check-prefix=SINKSAFEPARSEHTML --implicit-check-not=ctjs.runtime_code < %t/sink-safe-parsehtml.mlir

// RUN: for name in computed-document-method reflected-document-method; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:     | FileCheck %s --check-prefix=UNTRUSTED-SRC; done
// RUN: for name in object-define reflect-define set-prototype; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js --whole-page 2>/dev/null \
// RUN:     | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=PROTOTYPE; done

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

// An overwritten document method cannot prove a non-script receiver, and the
// shared prototype census already rejects reflected writes to realm prototypes.
// UNTRUSTED-SRC: ctnative.not_native = "`src` writes a possibly-script element
// UNTRUSTED-SRC: ctjs.runtime_code = "`src` writes a possibly-script element
// PROTOTYPE: ctjs.test_closures_unknown = "an array destructuring, and the program may change how arrays iterate: a prototype table is used other than to read it

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

// Every sink pins its census mark and the same API-specific admission reason.
// SINKSRCDOCPROPERTY: ctnative.not_native = "`srcdoc` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKSRCDOCPROPERTY: ctjs.runtime_code = "`srcdoc` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKSRCDOCATTRIBUTE: ctnative.not_native = "`setAttribute(\22srcdoc\22)` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKSRCDOCATTRIBUTE: ctjs.runtime_code = "`setAttribute(\22srcdoc\22)` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKSRCDOCATTRIBUTENS: ctnative.not_native = "`setAttributeNS(\22srcdoc\22)` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKSRCDOCATTRIBUTENS: ctjs.runtime_code = "`setAttributeNS(\22srcdoc\22)` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKSETHTMLSCRIPT: ctnative.not_native = "`setHTMLUnsafe` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKSETHTMLSCRIPT: ctjs.runtime_code = "`setHTMLUnsafe` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKSETHTMLRUNSCRIPTS: ctnative.not_native = "`setHTMLUnsafe` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKSETHTMLRUNSCRIPTS: ctjs.runtime_code = "`setHTMLUnsafe` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKPARSEHTMLSCRIPT: ctnative.not_native = "`parseHTMLUnsafe` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKPARSEHTMLSCRIPT: ctjs.runtime_code = "`parseHTMLUnsafe` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKFRAGMENTSCRIPT: ctnative.not_native = "`createContextualFragment` can add a script element, whose code a native program cannot run
// SINKFRAGMENTSCRIPT: ctjs.runtime_code = "`createContextualFragment` can add a script element, whose code a native program cannot run
// SINKWRITESCRIPT: ctnative.not_native = "`write` can add a script element, whose code a native program cannot run
// SINKWRITESCRIPT: ctjs.runtime_code = "`write` can add a script element, whose code a native program cannot run
// SINKWRITELNSCRIPT: ctnative.not_native = "`writeln` can add a script element, whose code a native program cannot run
// SINKWRITELNSCRIPT: ctjs.runtime_code = "`writeln` can add a script element, whose code a native program cannot run
// SINKOUTERHANDLER: ctnative.not_native = "`outerHTML` with markup holding an on* attribute adds an event handler, whose code a native program cannot compile
// SINKOUTERHANDLER: ctjs.runtime_code = "`outerHTML` with markup holding an on* attribute adds an event handler, whose code a native program cannot compile
// SINKADJACENTHANDLER: ctnative.not_native = "`insertAdjacentHTML` with markup holding an on* attribute adds an event handler, whose code a native program cannot compile
// SINKADJACENTHANDLER: ctjs.runtime_code = "`insertAdjacentHTML` with markup holding an on* attribute adds an event handler, whose code a native program cannot compile
// SINKSRCDOCNESTED: ctnative.not_native = "`srcdoc` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKSRCDOCNESTED: ctjs.runtime_code = "`srcdoc` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKMARKUPFRAME: ctnative.not_native = "`innerHTML` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKMARKUPFRAME: ctjs.runtime_code = "`innerHTML` with executable markup may add a script or browsing context, whose code a native program cannot compile
// SINKIFRAMEURL: ctnative.not_native = "`src` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKIFRAMEURL: ctjs.runtime_code = "`src` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKOBJECTURL: ctnative.not_native = "`data` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKOBJECTURL: ctjs.runtime_code = "`data` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKEMBEDURL: ctnative.not_native = "`src` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKEMBEDURL: ctjs.runtime_code = "`src` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKANCHORURL: ctnative.not_native = "`href` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKANCHORURL: ctjs.runtime_code = "`href` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKFORMURL: ctnative.not_native = "`action` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKFORMURL: ctjs.runtime_code = "`action` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKLOCATIONURL: ctnative.not_native = "`location` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKLOCATIONURL: ctjs.runtime_code = "`location` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKLOCATIONHREFURL: ctnative.not_native = "`href` set to the javascript: URL `java\09script:x=1`, which runs code when followed
// SINKLOCATIONHREFURL: ctjs.runtime_code = "`href` set to the javascript: URL `java\09script:x=1`, which runs code when followed
// SINKLOCATIONASSIGNURL: ctnative.not_native = "`assign` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKLOCATIONASSIGNURL: ctjs.runtime_code = "`assign` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKLOCATIONREPLACEURL: ctnative.not_native = "`replace` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKLOCATIONREPLACEURL: ctjs.runtime_code = "`replace` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKWINDOWOPENURL: ctnative.not_native = "`open` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKWINDOWOPENURL: ctjs.runtime_code = "`open` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKGLOBALOPENURL: ctnative.not_native = "`open` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKGLOBALOPENURL: ctjs.runtime_code = "`open` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKATTRIBUTESRCURL: ctnative.not_native = "`setAttribute(\22src\22)` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKATTRIBUTESRCURL: ctjs.runtime_code = "`setAttribute(\22src\22)` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKATTRIBUTEDATAURL: ctnative.not_native = "`setAttribute(\22data\22)` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKATTRIBUTEDATAURL: ctjs.runtime_code = "`setAttribute(\22data\22)` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKATTRIBUTEHREFURL: ctnative.not_native = "`setAttribute(\22href\22)` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKATTRIBUTEHREFURL: ctjs.runtime_code = "`setAttribute(\22href\22)` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKATTRIBUTEACTIONURL: ctnative.not_native = "`setAttribute(\22action\22)` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKATTRIBUTEACTIONURL: ctjs.runtime_code = "`setAttribute(\22action\22)` set to the javascript: URL `javascript:x=1`, which runs code when followed
// SINKSCRIPTCREATE: ctnative.not_native = "`createElement` makes a `script` element, whose code a native program cannot run
// SINKSCRIPTCREATE: ctjs.runtime_code = "`createElement` makes a `script` element, whose code a native program cannot run
// SINKSCRIPTCREATENS: ctnative.not_native = "`createElementNS` makes a `script` element, whose code a native program cannot run
// SINKSCRIPTCREATENS: ctjs.runtime_code = "`createElementNS` makes a `script` element, whose code a native program cannot run
// SINKSCRIPTINSERT: ctnative.not_native = "`appendChild` inserts a possibly-script node, whose code a native program cannot compile
// SINKSCRIPTINSERT: ctjs.runtime_code = "`appendChild` inserts a possibly-script node, whose code a native program cannot compile
// SINKSCRIPTBEFORE: ctnative.not_native = "`insertBefore` inserts a possibly-script node, whose code a native program cannot compile
// SINKSCRIPTBEFORE: ctjs.runtime_code = "`insertBefore` inserts a possibly-script node, whose code a native program cannot compile
// SINKSCRIPTREPLACE: ctnative.not_native = "`replaceChild` inserts a possibly-script node, whose code a native program cannot compile
// SINKSCRIPTREPLACE: ctjs.runtime_code = "`replaceChild` inserts a possibly-script node, whose code a native program cannot compile
// SINKSCRIPTAPPEND: ctnative.not_native = "`append` inserts a possibly-script node, whose code a native program cannot compile
// SINKSCRIPTAPPEND: ctjs.runtime_code = "`append` inserts a possibly-script node, whose code a native program cannot compile
// SINKSCRIPTTEXT: ctnative.not_native = "`text` writes a possibly-script element, whose new code a native program cannot compile
// SINKSCRIPTTEXT: ctjs.runtime_code = "`text` writes a possibly-script element, whose new code a native program cannot compile
// SINKSCRIPTSRC: ctnative.not_native = "`src` writes a possibly-script element, whose new code a native program cannot compile
// SINKSCRIPTSRC: ctjs.runtime_code = "`src` writes a possibly-script element, whose new code a native program cannot compile
// SINKSCRIPTTEXTCONTENT: ctnative.not_native = "`textContent` writes a possibly-script element, whose new code a native program cannot compile
// SINKSCRIPTTEXTCONTENT: ctjs.runtime_code = "`textContent` writes a possibly-script element, whose new code a native program cannot compile
// SINKSCRIPTSRCATTRIBUTE: ctnative.not_native = "`setAttribute(\22src\22)` writes a possibly-script element, whose new code a native program cannot compile
// SINKSCRIPTSRCATTRIBUTE: ctjs.runtime_code = "`setAttribute(\22src\22)` writes a possibly-script element, whose new code a native program cannot compile
// SINKSAFEIMG: module
// SINKSAFEANCHORTEXT: module
// SINKSAFEDIVTEXT: module
// SINKSAFEAPPENDDIV: module
// SINKSAFESRCDOC: module
// SINKSAFESETHTML: module
// SINKSAFEPARSEHTML: module

//--- sink-srcdoc-property.js
document.querySelector("iframe").srcdoc = "<script>x=1</script>";

//--- sink-srcdoc-attribute.js
document.querySelector("iframe").setAttribute("srcdoc", "<script>x=1</script>");

//--- sink-srcdoc-attribute-ns.js
document.querySelector("iframe").setAttributeNS(null,"srcdoc", "<script>x=1</script>");

//--- sink-sethtml-script.js
document.body.setHTMLUnsafe("<script>x=1</script>");

//--- sink-sethtml-runscripts.js
document.body.setHTMLUnsafe("<script>x=1</script>", {runScripts:true});

//--- sink-parsehtml-script.js
Document.parseHTMLUnsafe("<script>x=1</script>");

//--- sink-fragment-script.js
document.createRange().createContextualFragment("<script>x=1</script>");

//--- sink-write-script.js
document.write("<script>x=1</script>");

//--- sink-writeln-script.js
document.writeln("<script>x=1</script>");

//--- sink-outer-handler.js
document.body.outerHTML="<div onclick=go()></div>";

//--- sink-adjacent-handler.js
document.body.insertAdjacentHTML("beforeend","<img onerror=go()>");

//--- sink-srcdoc-nested.js
document.querySelector("iframe").srcdoc='<iframe srcdoc="&lt;script&gt;x=1&lt;/script&gt;"></iframe>';

//--- sink-markup-frame.js
document.body.innerHTML='<iframe srcdoc="&lt;script&gt;x=1&lt;/script&gt;"></iframe>';

//--- sink-iframe-url.js
document.querySelector("iframe").src="javascript:x=1";

//--- sink-object-url.js
document.querySelector("object").data="javascript:x=1";

//--- sink-embed-url.js
document.querySelector("embed").src="javascript:x=1";

//--- sink-anchor-url.js
document.querySelector("a").href="javascript:x=1";

//--- sink-form-url.js
document.querySelector("form").action="javascript:x=1";

//--- sink-location-url.js
location="javascript:x=1";

//--- sink-location-href-url.js
location.href="java\tscript:x=1";

//--- sink-location-assign-url.js
location.assign("javascript:x=1");

//--- sink-location-replace-url.js
location.replace("javascript:x=1");

//--- sink-window-open-url.js
window.open("javascript:x=1");

//--- sink-global-open-url.js
open("javascript:x=1");

//--- sink-attribute-src-url.js
document.querySelector("iframe").setAttribute("src","javascript:x=1");

//--- sink-attribute-data-url.js
document.querySelector("object").setAttribute("data","javascript:x=1");

//--- sink-attribute-href-url.js
document.querySelector("a").setAttribute("href","javascript:x=1");

//--- sink-attribute-action-url.js
document.querySelector("form").setAttribute("action","javascript:x=1");

//--- sink-script-create.js
document.createElement("script");

//--- sink-script-create-ns.js
document.createElementNS("http://www.w3.org/2000/svg","script");

//--- sink-script-insert.js
document.body.appendChild(document.querySelector("script"));

//--- sink-script-before.js
document.body.insertBefore(document.querySelector("script"),null);

//--- sink-script-replace.js
document.body.replaceChild(document.querySelector("script"),document.body.firstChild);

//--- sink-script-append.js
document.body.append(document.querySelector("script"));

//--- sink-script-text.js
document.querySelector("script").text="x=1";

//--- sink-script-src.js
document.querySelector("script").src="https://example.com/code.js";

//--- sink-script-text-content.js
document.querySelector("script").textContent="x=1";

//--- sink-script-src-attribute.js
document.querySelector("script").setAttribute("src","https://example.com/code.js");

//--- sink-safe-img.js
document.createElement("img").src="https://example.com/x.png";

//--- sink-safe-anchor-text.js
document.createElement("a").text="safe";

//--- sink-safe-div-text.js
document.createElement("div").textContent="safe";

//--- sink-safe-append-div.js
document.body.appendChild(document.createElement("div"));

//--- sink-safe-srcdoc.js
document.querySelector("iframe").srcdoc="<b>safe</b>";

//--- sink-safe-sethtml.js
document.body.setHTMLUnsafe("<b>safe</b>");

//--- sink-safe-parsehtml.js
Document.parseHTMLUnsafe("<b>safe</b>");

//--- computed-document-method.js
document[location.hash] = function () { return document.querySelector("script"); };
document.createElement("img").src = "https://example.com/x.js";

//--- reflected-document-method.js
Object.defineProperty(Document.prototype, "createElement", {
    value: function () { return document.querySelector("script"); }
});
document.createElement("img").src = "https://example.com/x.js";

//--- object-define.js
Object.defineProperty(Document.prototype, location.hash, {value:function(){}});
(function () { const [cb] = [function () {}]; cb(); })();

//--- reflect-define.js
Reflect.defineProperty(Document.prototype, location.hash, {value:function(){}});
(function () { const [cb] = [function () {}]; cb(); })();

//--- set-prototype.js
Object.setPrototypeOf(Document.prototype, {});
(function () { const [cb] = [function () {}]; cb(); })();
