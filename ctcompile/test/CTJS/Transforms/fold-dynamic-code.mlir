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
// A name splices only when resolution on a throwaway clone proves there is
// no rebind, including aliases, parameters, computed writes and object
// mutators. The actual module retains no speculative resolver rewrites.

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
// RUN: for name in alias parameter reflect define-alias injected skipped declarations lexical-declarations timer-declarations timer-lexical dynamic-timer mixed-delayed nested-eval direct-eval; do \
// RUN:   ctjs-translate --ctbrowser-js-to-ctjs %t/$name.js -o %t/$name.mlir 2>/dev/null; \
// RUN:   ctjs-opt %t/$name.mlir -o %t/$name.plain.mlir; \
// RUN:   ctjs-opt %t/$name.mlir --ctjs-fold-dynamic-code -o %t/$name.folded.mlir; \
// RUN:   cmp %t/$name.plain.mlir %t/$name.folded.mlir || exit 1; done
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/phaser.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=PHASER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nested.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=NESTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/interval.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=INTERVAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/callable-delayed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code | FileCheck %s --check-prefix=CALLABLE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/anonymous.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-fold-dynamic-code --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ANONYMOUS

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
// KEPT: ctjs.load_global "Function"
// KEPT: ctjs.construct
function dynamic(source) {
    return new Function(source);
}
var d = dynamic('return 1');

//--- rebound.js
// A name the program stores is not spliced, even at a constant site: the
// store could rebind it before the call runs.
var Function = 1;
function rebound() {
    return new Function('return 1');
}
var rb = rebound();

//--- computed.js
// A computed-key write onto the global object could rebind ANY of the four
// by a name known only at run time, so no site splices, however constant its
// own string is.
function computed(key) {
    globalThis[key] = 1;
    return new Function('return 1');
}
var c = computed('x');

//--- defprop.js
// `Object.defineProperty(globalThis, name, desc)` with a non-constant name
// can rebind any of the four, so the site is left alone.
function defprop(name) {
    Object.defineProperty(globalThis, name, { value: 1 });
    return new Function('return 1');
}
var dp = defprop('x');

//--- assign.js
// `Object.assign(globalThis, source)` copies every key of source onto the
// global object; with source not a closed literal the keys are unknowable,
// so the site is left alone.
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
// SYNTAX: ctjs.call
// SYNTAX-LABEL: ctjs.func @anonymous$
// SYNTAX: ctjs.load_global "SyntaxError"
// SYNTAX: ctjs.construct
// SYNTAX: ctjs.throw
function broken_call() {
    return new Function('return (');
}
var b = broken_call();

// Rebind and declaration refusals leave the entire module byte-identical,
// including its original call and attributes. Mutation checks separately
// disable the rebind proof for alias/parameter/reflect/define-alias.

//--- alias.js
function changed() { return 7; }
function alias() {
    var w = window;
    w.eval = changed;
    var e = eval;
    return e('21 + 21');
}
var r = alias();

//--- parameter.js
function changed() { return 7; }
function install(w) { w.eval = changed; }
install(window);
var r = (0, eval)('21 + 21');

//--- reflect.js
function changed() { return 7; }
Reflect.set(window, 'eval', changed);
var r = (0, eval)('21 + 21');

//--- define-alias.js
function changed() { return 7; }
var w = self;
Object.defineProperty(w, 'eval', { value: changed });
var r = (0, eval)('21 + 21');

//--- injected.js
var f = Function('eval = 1; return 1');
var r = (0, eval)('21 + 21');

//--- skipped.js
function* changed() { eval = 1; yield 1; }
var r = (0, eval)('21 + 21');

//--- declarations.js
var r = (0, eval)('var x = 1; 21 + 21');

//--- lexical-declarations.js
var r = (0, eval)('let x = 1; x + 1');

//--- timer-declarations.js
setTimeout('var x = 1; 21 + 21', 0);

//--- timer-lexical.js
setTimeout('let x = 1; x + 1', 0);

//--- dynamic-timer.js
function later(source) { setTimeout(source, 0); }
var r = (0, eval)('21 + 21');

//--- nested-eval.js
var f = Function('source', 'return eval(source)');
var r = (0, eval)('21 + 21');

//--- direct-eval.js
function direct(source) { return eval(source); }
var r = (0, eval)('21 + 21');

//--- phaser.js
// Phaser's global-return probe stays refused while its receiver escapes.
// Node substitutes the global object for this bare sloppy call; the VM
// observes undefined. This pass must not choose either behaviour.
// PHASER-LABEL: ctjs.func @probe$
// PHASER: ctjs.load_global "Function"
// PHASER: ctjs.construct
// PHASER: ctjs.call
function probe() { return new Function('return this')(); }
var r = probe();

//--- nested.js
// Parameter-default closures are emitted before the outer function. Find the
// wrapper's returned closure rather than guessing its index is one, and shift
// every nested closure index consistently.
// NESTED: ctjs.create_closure
// NESTED-LABEL: ctjs.func @anonymous$
// NESTED: ctjs.create_closure
function nested() { return Function('a = () => 4', 'return () => a()'); }
var r = nested();

//--- interval.js
// INTERVAL: ctjs.load_global "setInterval"
// INTERVAL: ctjs.create_closure
// INTERVAL: ctjs.call
setInterval('tick()', 100, 7);

//--- mixed-delayed.js
// A may-set containing a closure does not exclude a String alternative. The
// first task can rebind eval before the final callback, so nothing splices.
(function () {
    function later(code) { setTimeout(code, 0); }
    later(function () {});
    later('eval = function () { return 7; }');
    setTimeout(function () {
        var result = (0, eval)('21 + 21');
    }, 0);
})();

//--- callable-delayed.js
// A directly known closure callback cannot compile source. Its constant
// indirect eval remains eligible, unlike the mixed helper parameter.
// CALLABLE-NOT: ctjs.load_global "eval"
// CALLABLE: ctjs.func @_script_${{[0-9]+}}
// CALLABLE-NOT: ctjs.load_global "eval"
(function () {
    setTimeout(function () {}, 0);
    setTimeout(function () {
        var result = (0, eval)('21 + 21');
    }, 0);
})();

//--- anonymous.js
// The VM's named wrapper makes `anonymous` a self-binding, unlike Node's
// Function constructor. Keep native own-closure refusal until that inherited
// VM discrepancy is repaired; never admit the wrong native observation.
// ANONYMOUS: ctnative.not_native = "{{.*}}own closure{{.*}}"
var anonymous = 5;
var result = Function('return typeof anonymous')();
