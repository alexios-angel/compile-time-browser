// --ctjs-fold-dynamic-code compiles the constant strings of `new Function`,
// `Function(...)`, an indirect `eval` and a string timer ahead of time, and
// splices the site: a construct or Function call becomes a ctjs.create_closure
// of the anonymous function, an eval call becomes a call of the compiled
// script's entry closure, and a string timer's first argument becomes the
// entry closure itself.
//
// The pass runs before --ctjs-resolve-globals, whose clause 5 resolves no
// global while a load of `Function` or `eval` remains. Splicing the constant
// cases removes those loads; whatever is left (a non-constant string, a name
// the program rebinds) stays for clause 5 to refuse, and these tests pin
// both halves.
//
// A name splices only when the module never stores it and never writes or
// deletes it as a constant property key, and only while no computed-key
// write or `Object.defineProperty`/`Object.assign`-style call onto the
// global object could rebind any of the four names by a key known only at
// run time.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/newfunction.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=NEWFN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/functioncall.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=FNCALL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/eval.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=EVAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/timer.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=TIMER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/rebound.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/computed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/defprop.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/assign.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/syntaxerror.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=SYNTAX

//--- newfunction.js
// A constant body splices: the construct site becomes a ctjs.create_closure
// of the anonymous function the wrap's body defines (function index 1 of the
// spliced program), and no load of `Function` remains at the site.
// NEWFN-LABEL: ctjs.func {{.*}}@make$
// NEWFN-NOT: ctjs.load_global "Function"
// NEWFN: ctjs.create_closure
// NEWFN-NOT: ctjs.construct
function make() {
    return new Function('a', 'b', 'return a + b');
}
var f = make();

//--- functioncall.js
// `Function(...)` without `new` is the same wrap: the call site becomes a
// ctjs.create_closure, with no load of `Function` left.
// FNCALL-LABEL: ctjs.func {{.*}}@plain$
// FNCALL-NOT: ctjs.load_global "Function"
// FNCALL: ctjs.create_closure
function plain() {
    return Function('return 1');
}
var g = plain();

//--- eval.js
// An indirect eval of a constant string: the call site becomes a call of the
// compiled script's entry closure (its top level), and no load of `eval`
// remains at the site.
// EVAL-LABEL: ctjs.func {{.*}}@use_eval$
// EVAL-NOT: ctjs.load_global "eval"
// EVAL: ctjs.create_closure
// EVAL: ctjs.call
function use_eval() {
    var indirect = eval;
    return indirect('1 + 2');
}
var r = use_eval();

//--- timer.js
// A string timer's first argument becomes the entry closure; the host calls
// it as the task. The call to setTimeout stays, with the closure in place of
// the string, so the load of `setTimeout` is still here for the resolver.
// TIMER-LABEL: ctjs.func {{.*}}@schedule$
// TIMER-NOT: ctjs.constant #ctjs.string<"tick()">
// TIMER: ctjs.create_closure
function schedule() {
    return setTimeout('tick()', 100);
}
var t = schedule();

//--- dynamic.js
// A non-constant body is left alone for clause 5: the load of `Function` and
// the construct both stay.
// KEPT-LABEL: ctjs.func {{.*}}@dynamic$
// KEPT: ctjs.load_global "Function"
// KEPT: ctjs.construct
function dynamic(source) {
    return new Function(source);
}
var d = dynamic('return 1');

//--- rebound.js
// A name the program stores is not spliced, even at a constant site: the
// store could rebind it before the call runs.
// KEPT-LABEL: ctjs.func {{.*}}@rebound$
// KEPT: ctjs.store_global "Function"
// KEPT: ctjs.load_global "Function"
var Function = 1;
function rebound() {
    return new Function('return 1');
}
var rb = rebound();

//--- computed.js
// A computed-key write onto the global object could rebind ANY of the four
// by a name known only at run time, so no site splices, however constant its
// own string is.
// KEPT-LABEL: ctjs.func {{.*}}@computed$
// KEPT: ctjs.load_global "Function"
// KEPT: ctjs.construct
function computed(key) {
    globalThis[key] = 1;
    return new Function('return 1');
}
var c = computed('x');

//--- defprop.js
// `Object.defineProperty(globalThis, name, desc)` with a non-constant name
// can rebind any of the four, so the site is left alone.
// KEPT-LABEL: ctjs.func {{.*}}@defprop$
// KEPT: ctjs.load_global "Function"
// KEPT: ctjs.construct
function defprop(name) {
    Object.defineProperty(globalThis, name, { value: 1 });
    return new Function('return 1');
}
var dp = defprop('x');

//--- assign.js
// `Object.assign(globalThis, source)` copies every key of source onto the
// global object; with source not a closed literal the keys are unknowable,
// so the site is left alone.
// KEPT-LABEL: ctjs.func {{.*}}@assign$
// KEPT: ctjs.load_global "Function"
// KEPT: ctjs.construct
function assign(source) {
    Object.assign(globalThis, source);
    return new Function('return 1');
}
var a = assign({});

//--- syntaxerror.js
// A body that does not parse splices to a closure whose body throws the
// SyntaxError when it runs (never a build failure). The thrown message
// names the parse error.
// SYNTAX-LABEL: ctjs.func {{.*}}@broken_call$
// SYNTAX-NOT: ctjs.load_global "Function"
// SYNTAX: ctjs.create_closure
function broken_call() {
    return new Function('return (');
}
var b = broken_call();
