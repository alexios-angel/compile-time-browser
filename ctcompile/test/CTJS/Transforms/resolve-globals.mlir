// THE CLOSED WORLD - Phase 62½-A.
//
// A whole-program compile knows every call site, and the importer's IR does
// not say so: `function add` is a create_closure stored into the global "add",
// and every `add(x, x)` is a load_global followed by a call through the
// dispatcher. --ctjs-resolve-globals proves, per global name across the whole
// module, that the name is bound exactly once, in the top level's hoisting
// prologue, to one closure, and that nothing writes the globals table
// dynamically - and then every call through it becomes a ctjs.call_direct
// naming the function, which is made private once every caller is one.
//
// FROM JAVASCRIPT, ON PURPOSE. The pass exists to match what the importer
// actually emits - the `%arg2[1]` closure, the `$1` symbol suffix, the hoisted
// closure/store prologue - and hand-written IR would prove only that the pass
// matches the IR its author imagined. Four programs live in this one file
// because ctjs-translate reads a whole file as one script; split-file cuts
// them apart and drops this preamble.
//
// THE THREE NEGATIVE CASES ARE THE POINT. Each removes one clause of the proof
// and asserts that NOTHING resolves: a global stored twice, a call passing
// more arguments than the callee declares, and a write through globalThis.
// Blinding the "exactly one store" check made the first of them go green with
// `f = 1` resolved to @f$1 - which is what a negative test is for.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/closed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CLOSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/rebound.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=REBOUND
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/surplus.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SURPLUS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/global-object.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=DYNAMIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/skipped.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SKIPPED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/constructor.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CTOR
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/constructor-compared.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=COMPARED

// --- THE POSITIVE CASE: the MVP program ---------------------------------------
//
// THE VERDICT IS IN THE IR. Every global the program names gets a row on the
// module: `add` and `twice` resolve and are closed - one call and two calls
// rewritten - while `r`, bound to a call's result, does not.
//
// CLOSED: module attributes {ctjs.globals = [
// CLOSED-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// CLOSED-SAME: {name = "twice", reason = "closed: 2 call(s) rewritten", resolved = @twice$2, stores = 1 : i32}
// CLOSED-SAME: {name = "r", reason = "bound to something other than a closure", resolved = "none", stores = 1 : i32}
//
// THE TOP LEVEL STAYS PUBLIC - the host calls it - and both its calls to
// `twice` become direct. The load_global is KEPT as the callee-value operand,
// because the boxed tier dispatches on it; new.target is a fresh undefined -
// the constant the resolver puts IMMEDIATELY before the call, after the
// receiver's own undefined the importer emitted - which is what op::call
// pushes for a plain call.
//
// CLOSED-LABEL: ctjs.func @_script_$0(
// CLOSED-NOT: ctjs.call %
// CLOSED: %[[F1:.*]] = ctjs.load_global "twice"
// CLOSED: %[[RCV1:.*]] = ctjs.constant #ctjs.undefined
// CLOSED-NEXT: %[[NT1:.*]] = ctjs.constant #ctjs.undefined
// CLOSED-NEXT: ctjs.call_direct @twice$2(%[[RCV1]], %[[NT1]], %[[F1]], %{{.*}})
// CLOSED-NOT: ctjs.call %
// CLOSED: %[[F2:.*]] = ctjs.load_global "twice"
// CLOSED: %[[RCV2:.*]] = ctjs.constant #ctjs.undefined
// CLOSED-NEXT: %[[NT2:.*]] = ctjs.constant #ctjs.undefined
// CLOSED-NEXT: ctjs.call_direct @twice$2(%[[RCV2]], %[[NT2]], %[[F2]], %{{.*}})
// CLOSED-NOT: ctjs.call %
//
// BOTH FUNCTIONS ARE PRIVATE: every caller of each is a ctjs.call_direct in
// this module, which is the closed-world statement. `twice`'s call to `add`
// passes the callee's entry block in order - receiver, new.target, the
// callee value, then `x` twice, which is %arg3 twice.
//
// CLOSED-LABEL: ctjs.func private @add$1(
// CLOSED-NOT: ctjs.call
// CLOSED-LABEL: ctjs.func private @twice$2(
// CLOSED-NOT: ctjs.call %
// CLOSED: %[[F3:.*]] = ctjs.load_global "add"
// CLOSED: %[[RCV3:.*]] = ctjs.constant #ctjs.undefined
// CLOSED-NEXT: %[[NT3:.*]] = ctjs.constant #ctjs.undefined
// CLOSED-NEXT: ctjs.call_direct @add$1(%[[RCV3]], %[[NT3]], %[[F3]], %arg3, %arg3)
// CLOSED-NOT: ctjs.call %

// --- NEGATIVE: a global stored twice -------------------------------------------
//
// `f = 1` after the declaration is a second store, so no call site can know
// which value it reaches. Nothing resolves, the function stays public, and the
// call stays a ctjs.call - the `callee unresolved` diagnostic of part 24.
//
// REBOUND-NOT: call_direct
// REBOUND: ctjs.globals = [{name = "f", reason = "stored 2 times", resolved = "none", stores = 2 : i32}
// REBOUND-LABEL: ctjs.func @_script_$0(
// REBOUND: ctjs.load_global "f"
// REBOUND-NOT: call_direct
// REBOUND: ctjs.call %
// REBOUND-LABEL: ctjs.func @f$1(
// REBOUND-NOT: call_direct

// --- NEGATIVE: a surplus argument -----------------------------------------------
//
// `add(1, 2, 3)` passes three arguments to two parameters. The third lands in
// the callee's raw register window - where `arguments` and a rest parameter
// read it - and ctjs.call_direct has no operand for a value the callee's
// block does not name. That site keeps the dispatcher; `add(1)` is short and
// is padded with undefined exactly as op::call fills a missing parameter; and
// because one caller is not a call_direct, `add` is resolved but OPEN - not
// private.
//
// SURPLUS: ctjs.globals = [{name = "add", reason = "open: a call passes 3 argument(s) to 2 parameter(s) - the surplus has frame semantics (ctjs.call at {{[^)]*}}); 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// SURPLUS-LABEL: ctjs.func @_script_$0(
// SURPLUS: ctjs.load_global "add"
// SURPLUS: ctjs.call %{{.*}}(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}})
// SURPLUS: %[[F:.*]] = ctjs.load_global "add"
// SURPLUS: %[[RCV:.*]] = ctjs.constant #ctjs.undefined
// SURPLUS-NEXT: %[[NT:.*]] = ctjs.constant #ctjs.undefined
// SURPLUS-NEXT: ctjs.call_direct @add$1(%[[RCV]], %[[NT]], %[[F]], %{{.*}}, %[[NT]])
// SURPLUS-LABEL: ctjs.func @add$1(

// --- NEGATIVE: a write through the global object --------------------------------
//
// `globalThis.x = 1` is a ctjs.set_property on a value loaded from
// "globalThis". In a spec-conformant engine that is a write to the global
// bindings, so nothing in the module may be resolved - every row carries the
// same reason, and `add`'s call stays a ctjs.call.
//
// DYNAMIC-NOT: call_direct
// DYNAMIC: ctjs.globals = [
// DYNAMIC-SAME: {name = "add", reason = "the global object is written through (ctjs.set_property at {{[^)]*}})", resolved = "none", stores = 1 : i32}
// DYNAMIC-SAME: {name = "globalThis", reason = "the global object is written through (ctjs.set_property at {{[^)]*}})", resolved = "none", stores = 0 : i32}
// DYNAMIC-LABEL: ctjs.func @_script_$0(
// DYNAMIC: ctjs.set_property
// DYNAMIC-NOT: call_direct
// DYNAMIC: ctjs.call %
// DYNAMIC-LABEL: ctjs.func @add$1(
// DYNAMIC-NOT: call_direct

// --- NEGATIVE: a function the IMPORTER refused ---------------------------------
//
// A body this pass cannot read may contain a `ctjs.store_global`, and the
// census counts only the stores that are IN the module. With a refused
// function present, a name stored once visibly and once inside that body looks
// singly bound, and every call to it would be compiled to the wrong function
// rather than diagnosed. So a non-empty `ctjs.skipped` unresolves the whole
// module, exactly as a dynamic write does.
//
// SKIPPED-NOT: call_direct
// SKIPPED: ctjs.globals = [
// SKIPPED-SAME: reason = "the importer refused {{[0-9]+}} function(s) (ctjs.skipped), and a body this pass cannot read may store any global"
// SKIPPED: ctjs.call %

// --- NEGATIVE: the compiler reached through `.constructor` ----------------------
//
// `Function` is not only a global name: it is on every function's prototype as
// `.constructor`, so this reaches the run-time compiler without ever loading
// the global. The value is followed rather than the key refused, and here it
// is CALLED, which is the escape that answers.
//
// CTOR-NOT: call_direct
// CTOR: ctjs.globals = [
// CTOR-SAME: reason = "a value that may be the run-time compiler escapes into ctjs.call{{[^"]*}}"
// CTOR: ctjs.call %

// --- POSITIVE: `.constructor` that only gets COMPARED still resolves ------------
//
// The point of following the value instead of refusing the key: a comparison
// answers a primitive, so it stops the walk, and the module stays closed. If
// this went red the fix would have cost every program that writes
// `o.constructor === C`.
//
// COMPARED: ctjs.globals = [
// COMPARED-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// COMPARED: ctjs.call_direct @add$1

//--- closed.js
function add(a, b) { return a + b; }
function twice(x) { return add(x, x); }
var r = twice(21);
var s = 0;
for (var i = 0; i < 10; i++) { s = s + twice(i); }

//--- rebound.js
function f() { return 1; }
f = 1;
var t = f();

//--- surplus.js
function add(a, b) { return a + b; }
var u = add(1, 2, 3);
var v = add(1);

//--- global-object.js
function add(a, b) { return a + b; }
globalThis.x = 1;
var w = add(1, 2);

//--- skipped.js
function add(a, b) { return a + b; }
// A generator is a suspension point, which the importer refuses to lower, so
// its body - and any store_global in it - never reaches this pass.
function* gen() { yield 1; }
var y = add(1, 2);

//--- constructor.js
function add(a, b) { return a + b; }
function seed() { return 1; }
// Called where it is read, so the CALL is the escape. Storing it into a global
// first would also answer - as a store_global escape - but then this test
// would be pinning the store rather than the thing it is about.
seed.constructor("return 1");
var z = add(1, 2);

//--- constructor-compared.js
function add(a, b) { return a + b; }
function seed() { return 1; }
var same = seed.constructor === seed.constructor;
var z = add(1, 2);
