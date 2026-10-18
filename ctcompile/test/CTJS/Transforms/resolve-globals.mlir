// Tests for --ctjs-resolve-globals: closed-world resolution of global calls.
//
// A whole-program compile knows every call site, and the importer's IR does
// not say so: `function add` is a create_closure stored into the global "add",
// and every `add(x, x)` is a load_global followed by a call through the
// dispatcher. --ctjs-resolve-globals proves, per global name across the whole
// module, that the name is bound exactly once, in the top level's hoisting
// prologue, to one closure, and that nothing writes the globals table
// dynamically; then every call through it becomes a ctjs.call_direct
// naming the function, which is made private once every caller is one.
//
// The inputs are JavaScript on purpose. The pass exists to match what the
// importer actually emits (the `%arg2[1]` closure, the `$1` symbol suffix, the
// hoisted closure/store prologue), and hand-written IR would prove only that
// the pass matches the IR its author imagined. Many programs live in this one
// file because ctjs-translate reads a whole file as one script; split-file cuts
// them apart and drops this preamble.
//
// The negative cases are the point. Each removes one clause of the proof and
// asserts that the affected name does not resolve: a global stored twice, or a
// call passing more arguments than the callee declares. Disabling the
// "exactly one store" check makes the first of them fail with `f = 1`
// resolved to @f$1, which is what a negative test is for.
//
// Several clauses refuse per name rather than for the whole module: a
// function the importer refused refuses only the names its body stores, and a
// `.constructor` read is followed as a value rather than assumed to be
// `Function`. The second half of this file pairs each with a positive case
// beside its negative one (a refused body that stores nothing, an object
// literal, `super(...)`), so widening either one again makes a passing test
// fail.
//
// `skipped.js` asserts that a refused function (a generator) that stores no
// global refuses nothing, and `skipped-stores.js` asserts that one that does
// store a global refuses exactly that name and no other. Together they pin
// the per-name rule from both sides: a module-wide refusal fails the first,
// and ignoring refused bodies fails the second.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/closed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CLOSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/rebound.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=REBOUND
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/surplus.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SURPLUS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/global-object.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=DYNAMIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/skipped.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SKIPPED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/constructor.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CTOR
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/constructor-compared.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=COMPARED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/skipped-stores.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SKIPSTORE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/skipped-opaque.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SKIPOPAQUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-literal.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=LITERAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-literal-chain.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CHAIN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-super.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SUPER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-extends-function.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=EXTENDSFN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-prototype-replaced.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PROTOSET
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-member.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=MEMBER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-member-prototype.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=MEMBERPROTO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-static-call.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=STATICCALL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-static-polluted.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=STATICPOLLUTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-static-realm.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=STATICREALM
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-static-written.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=STATICWRITTEN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window-lacks.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINLACKS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window-lacks-bound.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINLACKSBOUND
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window-lacks-polluted.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINLACKSPOLLUTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window-read.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINREAD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window-alias.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINALIAS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-read.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERREAD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-write.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERWRITE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-computed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERCOMPUTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-unknown.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERUNKNOWN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-cycle.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERCYCLE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-inert.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERINERT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-rebound.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERREBOUND
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-method.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERNAMED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-binding.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERNAMED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-method-write.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERMETHODWRITE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-stored.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERSTORED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-cell.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERNAMED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-return.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERNAMED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-cell-overwritten.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERCELLLEAK
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-return-stored.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERRETURNSTORED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-return-value.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERRETURNVALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-arguments.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERARGS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enter-surplus.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERSURPLUS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/self-write.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SELFWRITE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/closed.js 2>/dev/null | ctjs-opt '--ctjs-resolve-globals=report=true' 2>&1 >/dev/null | FileCheck %s --check-prefix=REPORT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/closure-callee.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=IIFE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/closure-callee.js 2>/dev/null | ctjs-opt '--ctjs-resolve-globals=report=true' 2>&1 >/dev/null | FileCheck %s --check-prefix=IIFEREPORT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/closure-surplus.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CLOSURESURPLUS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/closure-arguments.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CLOSUREARGS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/name-arguments.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=NAMEARGS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-top.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISTOP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-called.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISCALLED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-arrow.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISARROW
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-iife.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISIIFE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-returned.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISRETURNED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-field.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISFIELD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-refused.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISREFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-handed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISHANDED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this-inherited.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISINHERITED

// --- Positive: a closed program --------------------------------------------
//
// The verdict is in the IR. Every global the program names gets a row on the
// module: `add` and `twice` resolve and are closed (one call and two calls
// rewritten), while `r`, bound to a call's result, does not.
//
// CLOSED: module attributes {ctjs.globals = [
// CLOSED-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// CLOSED-SAME: {name = "twice", reason = "closed: 2 call(s) rewritten", resolved = @twice$2, stores = 1 : i32}
// CLOSED-SAME: {name = "r", reason = "bound to something other than a closure", resolved = "none", stores = 1 : i32}
//
// The top level stays public (the host calls it), and both its calls to
// `twice` become direct. The load_global is kept as the callee-value operand,
// because the boxed backend (generated code that keeps every JS value boxed)
// dispatches on it; new.target is a fresh undefined (the constant the resolver
// puts immediately before the call, after the receiver's own undefined the
// importer emitted), which is what op::call pushes for a plain call.
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
// Both functions are private: every caller of each is a ctjs.call_direct in
// this module, which is the closed-world statement. `twice`'s call to `add`
// passes the callee's entry block in order: receiver, new.target, the
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

// --- Negative: a global stored twice -------------------------------------------
//
// `f = 1` after the declaration is a second store, so no call site can know
// which value it reaches. Nothing resolves, the function stays public, and the
// call stays a ctjs.call and gets the `callee unresolved` diagnostic.
//
// REBOUND-NOT: call_direct
// REBOUND: ctjs.globals = [{name = "f", reason = "stored 2 times", resolved = "none", stores = 2 : i32}
// REBOUND-LABEL: ctjs.func @_script_$0(
// REBOUND: ctjs.load_global "f"
// REBOUND-NOT: call_direct
// REBOUND: ctjs.call %
// REBOUND-LABEL: ctjs.func @f$1(
// REBOUND-NOT: call_direct

// --- Negative: a surplus argument -----------------------------------------------
//
// `add(1, 2, 3)` passes three arguments to two parameters. The third lands in
// the callee's raw register window (where `arguments` and a rest parameter
// read it), and ctjs.call_direct has no operand for a value the callee's
// block does not name. That site keeps the dispatcher; `add(1)` is short and
// is padded with undefined exactly as op::call fills a missing parameter; and
// because one caller is not a call_direct, `add` is resolved but open, not
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

// --- Negative: a write through the global object --------------------------------
//
// `globalThis.x = 1` is a ctjs.set_property on a value loaded from
// "globalThis". In a spec-conformant engine that is a write to the global
// bindings, but it rebinds only the name written: the window proxy's `set`
// handler (ctbrowser/lib/Shell/bindings/window/window.cpp) either stores an
// own property of the window target or calls define_global, and both touch
// exactly that name. So `x` gets a refusal row of its own and `add` still
// resolves.
//
// A computed key (`globalThis[k] = 1`) names no binding, so it still refuses
// the whole module; enter-computed.js below covers that through a callee.
// The `globalThis` row itself only says that the name is a host binding.
//
// Deleting the constantKey() test in ResolveGlobals.cpp's SetPropertyOp
// clause makes `x` resolve as an ordinary unstored name, and this test fails.
// makes `x` resolve as an ordinary unstored name and this goes red on it.
//
// DYNAMIC: ctjs.globals = [
// DYNAMIC-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// DYNAMIC-SAME: {host_binding, name = "globalThis", reason = "never stored in this program - a host binding", resolved = "none", stores = 0 : i32}
// DYNAMIC-SAME: {name = "x", reason = "written through the global object (ctjs.set_property at {{[^)]*}}), which binds this name", resolved = "none", stores = 0 : i32}
// DYNAMIC-LABEL: ctjs.func @_script_$0(
// DYNAMIC: ctjs.set_property
// DYNAMIC: ctjs.call_direct @add$1

// --- Positive: a function the importer refused that stores nothing --------------
//
// A body this pass cannot read may contain a `ctjs.store_global`, and the
// census counts only the stores that are in the module. With a refused
// function present, a name stored once visibly and once inside that body looks
// singly bound, and every call to it would be compiled to the wrong function
// rather than diagnosed.
//
// The importer still has the dropped function's bytecode when it refuses the
// function, and op::set_global names its target by a static index into the
// function's own name pool. So the importer records exactly which names the
// body stores on the ctjs.skipped row, and this pass refuses only those
// names. A body that stores nothing therefore refuses nothing, and `add`
// resolves with a generator in the program. This matters for real bundles,
// which routinely contain functions the importer refuses; refusing the
// whole module for one of them would leave nothing resolved.
//
// SKIPPED: ctjs.globals = [
// SKIPPED-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// SKIPPED: ctjs.call_direct @add$1

// --- Negative: and one that does store, which refuses that name only ------------
//
// This half must stay. `gen` assigns to `add`, which is an
// op::set_global in a body that emits no ctjs.func - so the store the census
// can see is not the only one, and `add` is refused exactly as a second
// visible store would refuse it.
//
// And `twice` is not refused: one program pins a refusal and an acceptance at
// once, which a module-wide clause makes impossible and a per-name one is
// defined by. Deleting the `refused.contains(name)` test in ResolveGlobals.cpp
// resolves `add` to @add$1, and this test fails on the first line.
//
// SKIPSTORE: ctjs.globals = [
// SKIPSTORE-SAME: {name = "add", reason = "a function the importer refused (ctjs.skipped) stores this name, so the binding this pass can see is not the only one", resolved = "none", stores = 1 : i32}
// SKIPSTORE-SAME: {name = "twice", reason = "closed: 1 call(s) rewritten", resolved = @twice$2, stores = 1 : i32}
// SKIPSTORE: ctjs.call_direct @twice$2

// --- Negative: a refused body whose summary bounds nothing ----------------------
//
// `Function` in a refused body's name pool. This pass cannot follow a value
// through bytecode it never imported, so the summary says so in `opaque` and
// the whole module is refused, but only for bodies like this one, not for
// every refused body.
//
// SKIPOPAQUE-NOT: call_direct
// SKIPOPAQUE: ctjs.globals = [
// SKIPOPAQUE-SAME: reason = "a refused body reads `Function`, and the program it compiles can store any global"
// SKIPOPAQUE: ctjs.call %

// --- Negative: the compiler reached through `.constructor` ----------------------
//
// `Function` is not only a global name: it is on every function's prototype as
// `.constructor`, so this reaches the run-time compiler without ever loading
// the global. The value is followed rather than the key refused, and here it
// is called, which is the escape that refuses the module.
//
// CTOR-NOT: call_direct
// CTOR: ctjs.globals = [
// CTOR-SAME: reason = "a value that may be the run-time compiler escapes into ctjs.call{{[^"]*}}"
// CTOR: ctjs.call %

// --- Positive: `.constructor` that is only compared still resolves ------------
//
// This is why the value is followed instead of the key being refused: a
// comparison answers a primitive, so it stops the walk, and the module stays
// closed. Refusing the key would cost every program that writes
// `o.constructor === C`.
//
// COMPARED: ctjs.globals = [
// COMPARED-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// COMPARED: ctjs.call_direct @add$1

// --- Positive: `.constructor` on an object literal --------------------------------
//
// `({}).constructor` is `Object`, not `Function`; the receiver decides, not
// the key. `constructor` is an own property of every `X.prototype` table,
// holding the function that owns that table, and only `Function.prototype`'s
// copy holds `Function`. The value escapes into a ctjs.store_global here, so
// a rule that looked only at the key would refuse the module and this test
// would fail.
//
// LITERAL: ctjs.globals = [
// LITERAL-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// LITERAL: ctjs.call_direct @add$1

// --- Negative: and the second read off it is the compiler -------------------------
//
// `Object` is itself a function, so `({}).constructor.constructor` is
// `Function`. The second read's receiver is a ctjs.get_property, which
// may_be_function() does not clear; listing a `.constructor` result as a
// non-function would open exactly this hole.
//
// CHAIN-NOT: call_direct
// CHAIN: ctjs.globals = [
// CHAIN-SAME: reason = "a value that may be the run-time compiler escapes into ctjs.store_global{{[^"]*}}"

// --- Positive: `super(...)`, which is a `.constructor` read ------------------------
//
// The ctbrowser bytecode compiler (lib/Script/compile/expression_calls.cpp)
// compiles `super(x)` to `load_home; get_proto; get_prop "constructor"` and
// then calls the result. Treating that call as an escape would refuse every
// global in a program at its first derived constructor, which class-based
// libraries such as Bootstrap reach almost immediately.
//
// SUPER: ctjs.globals = [
// SUPER-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// SUPER: ctjs.call_direct @add$1

// --- Negative: unless the class extends `Function` ---------------------------------
//
// The premise of the super clause, negated. `super()` reaches `Function` only
// when the parent's `prototype` table is `Function.prototype`, which means
// naming `Function`, and a ctjs.load_global of that name refuses the module on
// its own. That is why the relaxation is sound, and this is the test of it.
//
// EXTENDSFN-NOT: call_direct
// EXTENDSFN: ctjs.globals = [
// EXTENDSFN-SAME: reason = "a run-time compiler is reachable (ctjs.load_global{{[^)]*}}) and the program it builds can store any global"

// --- Negative: or unless a `prototype` table was replaced --------------------------
//
// THE PREMISE THE SSA WALK CANNOT SEE, CHECKED RATHER THAN ASSUMED. The super
// clause reasons that the link `extends` installs points at a `prototype`
// table, whose `constructor` is the class that owns it. `Base.prototype =
// make()` points it at something this pass cannot rule out being a function,
// whose `.constructor` is `Function`, so prototype_replaced() retires the
// clause for the whole module and `super()` is tainted again.
//
// PROTOSET-NOT: call_direct
// PROTOSET: ctjs.globals = [
// PROTOSET-SAME: reason = "a value that may be the run-time compiler escapes into ctjs.call{{[^"]*}}"

// --- Positive: a named read off a value that may be the compiler -------------------
//
// `o.constructor.name`. The receiver of the first read is a block argument, so
// that read is tainted, but the named read off it is not, because no property
// of `Function` called `name` is `Function`. Real code reads many such
// members off `this.constructor` (`.NAME`, `.Default`, `.DATA_KEY`,
// `.eventName(...)`, `.getOrCreateInstance(...)` in Bootstrap), and each
// ends in a call that would otherwise refuse the module.
//
// MEMBER: ctjs.globals = [
// MEMBER-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// MEMBER-SAME: {name = "label", reason = "closed: 1 call(s) rewritten", resolved = @label$2, stores = 1 : i32}
// MEMBER: ctjs.call_direct @label$2

// --- Negative: except for the names that hand it back ------------------------------
//
// `prototype` is one of them (hands_back_the_compiler() lists the rest):
// `o.constructor.prototype.constructor` is `Function` for any function `o`, so
// the taint follows this read and the returned value escapes. Dropping
// "prototype" from that list makes `add` resolve, and this test fails.
//
// MEMBERPROTO-NOT: call_direct
// MEMBERPROTO: ctjs.globals = [
// MEMBERPROTO-SAME: reason = "a value that may be the run-time compiler escapes into ctjs.return{{[^"]*}}"

// --- Positive: a method called off a value that may be the compiler ---------------
//
// `this.constructor.eventName("show")`, Bootstrap's idiom, passes the tainted
// value as the receiver of a callee nobody can name. If the value is
// `Function`, `Function.eventName` is undefined, since neither the realm nor
// the program gives `Function`, Function.prototype or Object.prototype that
// key (compiler_lacks), so the call throws before it runs any code; if it is
// not `Function`, it is not the compiler.
//
// STATICCALL: ctjs.globals = [
// STATICCALL-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// STATICCALL: ctjs.call_direct @add$1

// --- Negative: unless the program puts the key on Object.prototype -----------------
//
// Here `Function.eventName` is the program's function, and `grab` calls it
// with `this` = `Function`, which compiles source: a real escape. The key
// itself is only ever written onto a literal made here; Object.prototype
// handed to Object.assign is what retires the proof.
//
// STATICPOLLUTED-NOT: call_direct
// STATICPOLLUTED: ctjs.globals = [
// STATICPOLLUTED-SAME: reason = "a value that may be the run-time compiler escapes into a call whose callee this module does not name{{[^"]*}}"

// --- Negative: or the key is one the realm gives functions -------------------------
//
// `Function.valueOf()` hands back `Function` itself.
//
// STATICREALM-NOT: call_direct
// STATICREALM: ctjs.globals = [
// STATICREALM-SAME: reason = "a value that may be the run-time compiler escapes into a call whose callee this module does not name{{[^"]*}}"

// --- Negative: or the key is written onto an object not made here ------------------
//
// `t` could be any object, Function.prototype included, so a write of
// `eventName` through it retires the proof for that key.
//
// STATICWRITTEN-NOT: call_direct
// STATICWRITTEN: ctjs.globals = [
// STATICWRITTEN-SAME: reason = "a value that may be the run-time compiler escapes into a call whose callee this module does not name{{[^"]*}}"

// --- Positive: a method the window does not have, called on a value that may be it -
//
// Popper calls getBoundingClientRect, contains and the like on a node that
// may be the window. On the window the read answers nothing callable (no
// such property, or a named element), so the call throws and runs no code;
// on anything else the receiver is not the window. Object.prototype's
// toString, which answers "[object Window]", runs no code either.
//
// WINLACKS: ctjs.globals = [
// WINLACKS-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// WINLACKS: ctjs.call_direct @add$1

// --- Negative: unless the program binds a global of that name ----------------------
//
// Then `window.getRootNode` is this function, run with `this` = the window,
// and its write binds a global.
//
// WINLACKSBOUND-NOT: call_direct @add
// WINLACKSBOUND: ctjs.globals = [
// WINLACKSBOUND-SAME: reason = "the global object is the receiver of `getRootNode`, which this program rebinds{{[^"]*}}"

// --- Negative: or puts it on a prototype the window inherits -----------------------
//
// `window.contains` is then this function, through Object.prototype.
//
// WINLACKSPOLLUTED-NOT: call_direct @add
// WINLACKSPOLLUTED: ctjs.globals = [
// WINLACKSPOLLUTED-SAME: reason = "the global object escapes into a call whose callee this module does not name{{[^"]*}}"

// --- Positive: a named read off the global object ----------------------------------
//
// The same precision, applied to the global object. A read off `window`
// propagates the taint only when the key is one of the host's self-aliases
// (`window.window` is the window); `window.innerWidth` is a number, so storing
// it is not "the global object escapes into ctjs.store_global".
// hands_back_the_global_object() in Globals/Effects.cpp lists the aliases.
//
// The alias set is the host's and it is closed, because putting the window in
// any other global needs a ctjs.store_global of the marked value, which is an
// escape and already the answer.
//
// This does not cover `window.foo(...)`: that is a method call, so the window
// is the call's receiver and escapes anyway. That is a real refusal, because
// the window proxy's `set` handler calls define_global. A UMD header that
// passes `globalThis` or `self` into its factory is handled separately (see
// enter-read.js below).
//
// WINREAD: ctjs.globals = [
// WINREAD-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// WINREAD: ctjs.call_direct @add$1

// --- Negative: and `window.self` is the window -------------------------------------
//
// One of the names ctbrowser/lib/Shell/bindings/window/window.cpp binds to the
// same proxy. The taint follows it and the write through it is refused, and
// rightly: the proxy's `set` handler calls define_global, so this really does
// rebind a global.
//
// It is still the window under the named-write clause, which is what this
// case has to keep proving. `injected` gets a row only because the walk
// followed `window` through `.self` and recognised the write as a global
// binding; dropping "self" from hands_back_the_global_object() leaves the
// value unmarked, no row is written for `injected` at all, and the first line
// below fails while `add` resolves either way.
//
// WINALIAS: ctjs.globals = [
// WINALIAS-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// WINALIAS-SAME: {name = "injected", reason = "written through the global object (ctjs.set_property at {{[^)]*}}), which binds this name", resolved = "none", stores = 0 : i32}
// WINALIAS: ctjs.call_direct @add$1

// --- Positive: the global object followed into a known callee ----------------------
//
// `(function (g) { ... })(globalThis)`, the shape of every UMD header, calls a
// closure defined two tokens earlier. The callee is a ctjs.create_closure
// result naming its own function index, so nothing leaves this pass's sight:
// instead of treating the call as an escape that refuses all four names here,
// the walk follows the object into `g` and continues at that block argument.
// `g.innerWidth` is a named read that is not one of the host's self-aliases,
// so it stops there.
//
// Deleting the CallOp clause in dynamic_global_writes() restores the escape, and
// this test fails on the first line.
//
// ENTERREAD: ctjs.globals = [
// ENTERREAD-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// ENTERREAD: ctjs.call_direct @add$1

// --- Positive and negative at once: a named write inside that callee ---------------
//
// The write rules do not change when the walk crosses a call: `g.injected = 1`
// two frames from the load is the same proxy `set` trap as `globalThis.injected
// = 1` at the top level, so it binds `injected` and says nothing about `add` or
// `twice`. One program pins the refusal and both acceptances, which is only
// possible because the clause is per name.
//
// ENTERWRITE: ctjs.globals = [
// ENTERWRITE-SAME: {name = "add", reason = "closed: 2 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// ENTERWRITE-SAME: {name = "twice", reason = "closed: 1 call(s) rewritten", resolved = @twice$2, stores = 1 : i32}
// ENTERWRITE-SAME: {name = "injected", reason = "written through the global object (ctjs.set_property at {{[^)]*}}), which binds this name", resolved = "none", stores = 0 : i32}
// ENTERWRITE: ctjs.call_direct @add$1

// --- Negative: a computed key inside it refuses the module, unchanged --------------
//
// This half must not move. `o[k] = 1` with `k` a parameter names no binding,
// so nothing bounds which binding it rebinds and the whole census is void.
// That the write is two frames from the ctjs.load_global changes nothing:
// following the value is what makes this reachable at all, and precision is
// not permission.
//
// ENTERCOMPUTED-NOT: call_direct
// ENTERCOMPUTED: ctjs.globals = [
// ENTERCOMPUTED-SAME: reason = "the global object is written through (ctjs.set_property{{[^"]*}}"

// --- Negative: an unknown callee still bails ----------------------------------------
//
// `someHost(window)` passes the window to a call whose callee is a name the
// program never binds, a native this module does not hold, and no amount of
// following makes a body that is not here visible. Every refusal is also
// listed in `ctjs.global_escapes`.
//
// ENTERUNKNOWN-NOT: call_direct
// ENTERUNKNOWN: ctjs.global_escapes = ["the global object escapes into a call whose callee this module does not name (ctjs.call{{[^"]*}}"]
// ENTERUNKNOWN-SAME: ctjs.globals = [
// ENTERUNKNOWN-SAME: reason = "the global object escapes into a call whose callee this module does not name (ctjs.call{{[^"]*}}"

// --- Positive: an inert host method on the window ---------------------------------
//
// `window.scrollTo(0, 0)` and `window.getComputedStyle(el)` were the first
// escapes in bootstrap.bundle.js. The callee is read off the receiver itself,
// so it is the host's method whenever the receiver is the window, and a method
// NativeRealm.h lists as inert stores no global.
//
// ENTERINERT-NOT: ctjs.global_escapes
// ENTERINERT: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}

// --- Negative: the same method after the program rebinds it ---------------------
//
// ENTERREBOUND: ctjs.global_escapes = ["the global object is the receiver of `scrollTo`, which this program rebinds (ctjs.call{{[^"]*}}"]

// --- Positive: a function this module names, by binding or by frozen literal ------
//
// The window goes to a method of an object literal nothing writes after it is
// built (`tools.see(window)`), and to a local function reached through the
// binding a nested closure captured (`see(window)` inside `run`). Both bodies
// are followed into, and only read a property of it.
//
// ENTERNAMED-NOT: ctjs.global_escapes
// ENTERNAMED: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}

// --- Negatives: what a named body does with it still counts ----------------------
//
// ENTERMETHODWRITE: ctjs.global_escapes = ["the global object is written through (ctjs.set_property{{[^"]*}}"]
// ENTERSTORED: ctjs.global_escapes = ["the global object escapes into ctjs.set_property{{[^"]*}}"]

// --- Bindings and returns ---------------------------------------------------------
//
// A binding that holds the window is followed to every read of it, here the
// one inside the closure that captured it, and a return of the window to the
// result of every call of a function whose callers are all in sight
// (ENTERNAMED above, enter-cell.js and enter-return.js). Writing another value
// into the binding keeps its reads watched, so one passed to an unknown callee
// is still refused; a returned window stored into an object is refused; and a
// return is refused outright when the function is also used as a value,
// because a caller out of sight would receive the window.
//
// ENTERCELLLEAK: ctjs.global_escapes = ["the global object escapes into a call whose callee this module does not name (ctjs.call{{[^"]*}}"]
// ENTERRETURNSTORED: ctjs.global_escapes = ["the global object escapes into ctjs.set_property{{[^"]*}}"]
// ENTERRETURNVALUE: ctjs.global_escapes = ["the global object escapes into ctjs.return{{[^"]*}} - `host${{[0-9]+}}` is also used as a value by ctjs.call{{[^"]*}}"]

// --- Positive: a cycle terminates -------------------------------------------------
//
// `ping` and `pong` hand the global object to each other. Entering a callee
// marks a block argument and mark() enqueues a value only the first time it is
// seen, so each parameter is marked once and the fixpoint closes. A walk
// without that property does not fail this test, it hangs, which is why the
// case is here rather than in a comment.
//
// ENTERCYCLE: ctjs.globals = [
// ENTERCYCLE-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// ENTERCYCLE-SAME: {name = "ping", reason = "closed: 2 call(s) rewritten", resolved = @ping$2, stores = 1 : i32}
// ENTERCYCLE-SAME: {name = "pong", reason = "closed: 1 call(s) rewritten", resolved = @pong$3, stores = 1 : i32}
// ENTERCYCLE: ctjs.call_direct

// --- Negative: a callee that reads its raw argument window -------------------------
//
// `arguments` copies the frame's registers (every value the site passed) and
// is read back under an index this walk cannot see, so a parameter is not where
// the value stops. Deleting the reads_arguments() guard makes this resolve
// `add` and hides a real hole: `arguments[0]` is the global object here.
//
// ENTERARGS-NOT: call_direct
// ENTERARGS: ctjs.globals = [
// ENTERARGS-SAME: reason = "the global object is passed to loose$2, which reads its raw argument window (ctjs.call{{[^"]*}}"

// --- NEGATIVE: more arguments than parameters --------------------------------------
//
// The surplus lands in the callee's raw register window with no block argument
// to name it (the same frame semantics the rewrite refuses a call site for),
// so there is nowhere to continue the walk.
//
// ENTERSURPLUS-NOT: call_direct
// ENTERSURPLUS: ctjs.globals = [
// ENTERSURPLUS-SAME: reason = "the global object is passed to one$2 beyond its 1 parameter(s), where the surplus keeps frame semantics (ctjs.call{{[^"]*}}"

// --- Negative: `self` is a name of the global object -------------------------
//
// install_window (ctbrowser/lib/Shell/bindings/window/window.cpp) binds the
// window proxy as a global under three names (`window`, `self`, and
// `globalThis` through set_global_this); names_global_object() must list all
// three, or `self.injected = 1` (define_global("injected", 1) through the
// `set` handler) goes unseen, no row is written for `injected`, and `add`
// resolves for the wrong reason.
//
// SELFWRITE: ctjs.globals = [
// SELFWRITE-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// SELFWRITE-SAME: {name = "injected", reason = "written through the global object (ctjs.set_property at {{[^)]*}}), which binds this name", resolved = "none", stores = 0 : i32}

// --- The counters, asserted rather than read ---------------------------------------
//
// The LLVM build used here compiles pass statistics out, so
// --mlir-pass-statistics prints an empty report and a check reading one would
// check nothing. The `report` option emits the three counters as a remark
// instead (the same approach --ctjs-lift-to-scf uses), and
// tools/check/native-claims.py reads exactly this line to put a floor under
// the closed world on the real corpora.
//
// REPORT: resolved 2 global(s), rewrote 3 call(s), closed 2 function(s) over 5 name(s)
// The closure-call counter is separate, and this program earns none of it:
// every call in it goes through a global name.
// REPORT-SAME: named 0 closure call(s)

// --- Closure calls: a call whose callee is the closure ---------------------
//
// No global name is involved. The per-name clauses need a name bound once in
// the hoisting prologue, and a vendor bundle binds no globals at all: it hands
// a factory's result to one property of the window. A ctjs.create_closure
// result names its function index outright, so calling it enters that
// function whatever the globals table does, and the call can be direct.
// This is the main source of direct calls in real bundles, which typically
// wrap everything in such immediately invoked functions. The report counts
// these as `named N closure call(s)`.
//
// The operands are the entry block in order: receiver, new.target, the closure
// value (kept, since the boxed backend dispatches on it), then the two arguments.
//
// IIFE-LABEL: ctjs.func @_script_$0(
// IIFE: %[[FN:.*]] = ctjs.create_closure
// IIFE-NOT: ctjs.call %
// IIFE: ctjs.call_direct @fn$1(%{{.*}}, %{{.*}}, %[[FN]], %{{.*}}, %{{.*}})
// IIFE-NOT: ctjs.call %
// IIFEREPORT: named 1 closure call(s)

// A surplus is refused, and it has to be refused here rather than left to fail:
// CallDirectOp::verifySymbolUses holds the operand count equal to the entry
// block's, so emitting one would be a hard verifier error and not a wrong
// answer. Three arguments, two parameters: the call stays a ctjs.call.
//
// CLOSURESURPLUS-LABEL: ctjs.func @_script_$0(
// CLOSURESURPLUS: ctjs.call %
// CLOSURESURPLUS-NOT: ctjs.call_direct

// And a short call into a body that reads its raw argument window. The pad this
// rewrite adds is what VM_CASE(call) does to the callee's registers, not
// what op::call does to `arguments`: make_arguments_object copies the raw
// window, whose length is argc. `(function (a, b) { return arguments.length;
// })(1)` is 1 in the VM, and the boxed backend parks getArgs() as that window,
// so padding this site would make it 2.
//
// CLOSUREARGS-LABEL: ctjs.func @_script_$0(
// CLOSUREARGS: ctjs.call %
// CLOSUREARGS-NOT: ctjs.call_direct

// The same hole on the per-name path. `two` is bound once in the prologue to
// one closure and passes every other clause in ResolveGlobals.cpp, but the
// call is short and the body reads `arguments`, so it must stay a ctjs.call:
// padding it would make `arguments.length` 2 where the VM says 1.
//
// NAMEARGS: {name = "two", reason = "open: a call passes 1 argument(s) to 2 parameter(s) and the callee reads its raw argument window, which the pad would lengthen ({{[^)]*}}); 0 call(s) rewritten"
// NAMEARGS-LABEL: ctjs.func @_script_$0(
// NAMEARGS: ctjs.call %
// NAMEARGS-NOT: ctjs.call_direct

// --- The global object as `this` ------------------------------------------------
//
// No name is needed to hold it. A classic script's top level runs with the
// global object as its receiver, so `this.f = ...` there is the same write as
// `globalThis.f = ...`; if the receiver were not watched, `f` would resolve
// and its call_direct would answer 1 where the VM answers 2.
//
// THISTOP: ctjs.globals = [
// THISTOP-SAME: {name = "f", reason = "written through the global object (ctjs.set_property at {{[^)]*}}), which binds this name", resolved = "none", stores = 1 : i32}
// THISTOP-NOT: call_direct

// And one call deeper. A plain call hands its callee an undefined receiver,
// which OrdinaryCallBindThis turns into the global object in a sloppy body,
// so the `this` of every body a call this pass can name enters is watched.
//
// THISCALLED: ctjs.globals = [
// THISCALLED-SAME: {name = "f", reason = "written through a receiver that may be the global object (ctjs.set_property at {{[^)]*}}), which binds this name", resolved = "none", stores = 1 : i32}
// THISCALLED-SAME: {name = "install", reason = "closed: 1 call(s) rewritten", resolved = @install${{[0-9]+}}, stores = 1 : i32}
// THISCALLED-NOT: call_direct @f$

// An arrow keeps the `this` it was written with, which at the top level is the
// global object itself.
//
// THISARROW: ctjs.globals = [
// THISARROW-SAME: {name = "f", reason = "written through the global object (ctjs.set_property at {{[^)]*}}), which binds this name", resolved = "none", stores = 1 : i32}
// THISARROW-NOT: call_direct @f$

// A closure called where it is written is a call this pass names too.
//
// THISIIFE: ctjs.globals = [
// THISIIFE-SAME: {name = "f", reason = "written through a receiver that may be the global object (ctjs.set_property at {{[^)]*}}), which binds this name", resolved = "none", stores = 1 : i32}
// THISIIFE-NOT: call_direct @f$

// And returned, it is out of sight: `(function () { return this; })()` is the
// global object obtained without naming it, and whoever receives it may write
// anything through it.
//
// THISRETURNED: ctjs.globals = [
// THISRETURNED-SAME: {name = "add", reason = "a receiver that may be the global object escapes into ctjs.return{{[^"]*}}", resolved = "none", stores = 1 : i32}
// THISRETURNED-NOT: call_direct

// But a receiver is almost always an instance. `this.count = 0` binds the
// global `count` only in the world where `this` is the global object, and no
// load or store in the program names `count`, so it gets no row, and `add`
// resolves.
//
// THISFIELD: ctjs.globals = [
// THISFIELD-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// THISFIELD-NOT: name = "count"
// THISFIELD: ctjs.call_direct @add$1

// A refused body's `this` may be the global object too, and its summary reads
// the name written through it off the bytecode, as it does for `globalThis`.
//
// THISREFUSED: ctjs.globals = [
// THISREFUSED-SAME: {name = "f", reason = "a function the importer refused (ctjs.skipped) stores this name, so the binding this pass can see is not the only one", resolved = "none", stores = 1 : i32}
// THISREFUSED-SAME: ctjs.skipped
// THISREFUSED-SAME: stores = ["f"]
// THISREFUSED-NOT: call_direct @f$

// And handed on from there, where no IR marks the parameter it arrives at:
// `helper(this)` in a generator writes `f` through `o`. The summary follows
// the object through the body's registers and refuses the body opaque.
//
// THISHANDED: ctjs.globals = [
// THISHANDED-SAME: {name = "f", reason = "a refused body holds the global object and hands it on (call), where this summary cannot follow it", resolved = "none", stores = 1 : i32}
// THISHANDED-NOT: call_direct

// An arrow a refused body creates inherits that body's `this`. No
// create_closure in the module builds it, so its receiver is watched.
//
// THISINHERITED: ctjs.globals = [
// THISINHERITED-SAME: {name = "f", reason = "written through a receiver that may be the global object (ctjs.set_property at {{[^)]*}}), which binds this name", resolved = "none", stores = 1 : i32}
// THISINHERITED-NOT: call_direct @f$

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
// its body (and any store_global in it) never reaches this pass. This one
// has none, and the importer says so on the skipped row.
function* gen() { yield 1; }
var y = add(1, 2);

//--- skipped-stores.js
function add(a, b) { return a + b; }
function twice(x) { return x + x; }
// The same refusal, and this body does store a global: `add = 1` is an
// op::set_global that emits no IR. `add` is refused for it; `twice` is not.
function* gen() { add = 1; yield 1; }
var y = add(1, 2);
var z = twice(3);

//--- skipped-opaque.js
function add(a, b) { return a + b; }
// And a refused body that names the run-time compiler. Its set_globals do not
// bound it, because the program `Function` builds can store anything.
function* gen() { var f = Function; yield f; }
var y = add(1, 2);

//--- constructor.js
function add(a, b) { return a + b; }
function seed() { return 1; }
// Called where it is read, so the call is the escape. Storing it into a global
// first would also be refused (as a store_global escape), but then this test
// would be pinning the store rather than the thing it is about.
seed.constructor("return 1");
var z = add(1, 2);

//--- constructor-compared.js
function add(a, b) { return a + b; }
function seed() { return 1; }
var same = seed.constructor === seed.constructor;
var z = add(1, 2);

//--- ctor-literal.js
function add(a, b) { return a + b; }
// The receiver is an object literal, so this is `Object`, not `Function`, and
// it escapes into a store_global, which a key-only rule would refuse.
var ctor = ({}).constructor;
var z = add(1, 2);

//--- ctor-literal-chain.js
function add(a, b) { return a + b; }
// But `Object` is a function, so the second read is the compiler.
var ctor = ({}).constructor.constructor;
var z = add(1, 2);

//--- ctor-super.js
function add(a, b) { return a + b; }
// `super(x)` desugars to load_home, get_proto, get_prop "constructor", and
// then calls it.
class Base { constructor(x) { this.x = x; } }
class Derived extends Base { constructor(x) { super(x); } }
var d = new Derived(1);
var z = add(1, 2);

//--- ctor-extends-function.js
function add(a, b) { return a + b; }
// The one class for which that value is the compiler, and naming `Function`
// is what refuses the module.
class Weird extends Function { constructor() { super("return 1"); } }
var w = new Weird();
var z = add(1, 2);

//--- ctor-prototype-replaced.js
function add(a, b) { return a + b; }
function Base() {}
// A `prototype` table replaced with a value this pass cannot rule out being a
// function. The link `extends` installs may now point at one.
Base.prototype = make();
class Derived extends Base { constructor() { super(); } }
var d = new Derived();
var z = add(1, 2);

//--- ctor-member.js
function add(a, b) { return a + b; }
// `o.constructor` is tainted (`o` is a parameter) and `.name` off it is not.
function label(o) { return o.constructor.name; }
var t = label(1);
var z = add(1, 2);

//--- window-read.js
function add(a, b) { return a + b; }
// A named read off the window that is not one of the host's self-aliases, and
// the value then stored, which would be an escape if the read were tainted.
var w = window.innerWidth;
var z = add(1, 2);

//--- window-alias.js
function add(a, b) { return a + b; }
// And one that is. `window.self` is the same proxy, so the write through it
// binds a global.
window.self.injected = 1;
var z = add(1, 2);

//--- ctor-member-prototype.js
function add(a, b) { return a + b; }
// `.prototype` off it is, because `Function.prototype.constructor` is
// `Function`.
function grab(o) { return o.constructor.prototype; }
var t = grab(1);
var z = add(1, 2);

//--- ctor-static-call.js
function add(a, b) { return a + b; }
// A static method called through `this.constructor` from an instance method.
class Base {
    static eventName(t) { return t + ".base"; }
    fire() { return this.constructor.eventName("show"); }
}
var e = new Base().fire();
var z = add(1, 2);

//--- ctor-static-polluted.js
function add(a, b) { return a + b; }
// The key on Object.prototype, which `Function` inherits from.
Object.assign(Object.prototype, { eventName: function () { return this("return 1"); } });
function grab(o) { return o.constructor.eventName("show"); }
var g = grab(function () {});
var z = add(1, 2);

//--- ctor-static-realm.js
function add(a, b) { return a + b; }
// `valueOf` is a realm method every function inherits.
function grab(o) { return o.constructor.valueOf(); }
var g = grab(function () {});
var z = add(1, 2);

//--- ctor-static-written.js
function add(a, b) { return a + b; }
// The key written through a parameter, which may be any object.
function put(t, f) { t.eventName = f; }
put({}, function () { return 1; });
function grab(o) { return o.constructor.eventName("show"); }
var g = grab(function () {});
var z = add(1, 2);

//--- window-lacks.js
function add(a, b) { return a + b; }
// Each call throws on the window, which is the point: it runs no code there.
(function (g) { return g.getBoundingClientRect(); })(globalThis);
(function (g) { return g.toString(); })(globalThis);
var z = add(1, 2);

//--- window-lacks-bound.js
function add(a, b) { return a + b; }
function getRootNode() { this.injected = 1; }
(function (g) { return g.getRootNode(); })(globalThis);
var z = add(1, 2);

//--- window-lacks-polluted.js
function add(a, b) { return a + b; }
Object.assign(Object.prototype, { contains: function () { this.injected = 1; return true; } });
(function (g) { return g.contains(1); })(globalThis);
var z = add(1, 2);

//--- enter-read.js
function add(a, b) { return a + b; }
// The global object handed to a closure defined two tokens earlier (a UMD
// header, minus the module/define arms). `g` is a parameter and the walk
// continues there; `g.innerWidth` is a number.
(function (g) { return g.innerWidth; })(globalThis);
var z = add(1, 2);

//--- enter-write.js
function add(a, b) { return a + b; }
function twice(x) { return add(x, x); }
// A named write two frames from the load: the proxy's `set` handler, so it binds
// `injected` and nothing else.
(function (g) { g.injected = 1; })(globalThis);
var z = add(1, 2);
var y = twice(3);

//--- enter-computed.js
function add(a, b) { return a + b; }
// And a computed key, which names no binding. `k` is a block argument, so
// constantKey() is empty, exactly as it is for `globalThis[k] = 1`.
function bind(o, k) { o[k] = 1; }
bind(globalThis, "x");
var z = add(1, 2);

//--- enter-unknown.js
function add(a, b) { return a + b; }
// The window passed to a callee the program never binds.
someHost(window);
var z = add(1, 2);

//--- enter-inert.js
function add(a, b) { return a + b; }
window.scrollTo(0, 0);
var style = window.getComputedStyle(document.body);
var z = add(1, 2);

//--- enter-rebound.js
function add(a, b) { return a + b; }
window.scrollTo = function () { return 0; };
window.scrollTo(0, 0);
var z = add(1, 2);

//--- enter-method.js
function add(a, b) { return a + b; }
var width = (function () {
    var tools = { see: function (w) { return w.innerWidth; } };
    return tools.see(window);
})();
var z = add(1, 2);

//--- enter-binding.js
function add(a, b) { return a + b; }
var width = (function () {
    function see(w) { return w.innerWidth; }
    function run() { return see(window); }
    return run();
})();
var z = add(1, 2);

//--- enter-method-write.js
function add(a, b) { return a + b; }
(function () {
    var tools = { put: function (w, k) { w[k] = 1; } };
    tools.put(window, "x");
})();
var z = add(1, 2);

//--- enter-stored.js
function add(a, b) { return a + b; }
var box = {};
box.held = window;
var z = add(1, 2);

//--- enter-cell.js
function add(a, b) { return a + b; }
var width = (function () {
    var w = window;
    function see() { return w.innerWidth; }
    return see();
})();
var z = add(1, 2);

//--- enter-return.js
function add(a, b) { return a + b; }
var width = (function () {
    function host() { return window; }
    function use() { return host().innerWidth; }
    return use();
})();
var z = add(1, 2);

//--- enter-cell-overwritten.js
function add(a, b) { return a + b; }
(function () {
    var w = window;
    function leak() { someHost(w); }
    w = {};
    leak();
})();
var z = add(1, 2);

//--- enter-return-stored.js
function add(a, b) { return a + b; }
(function () {
    function host() { return window; }
    function use() { var box = {}; box.held = host(); return box; }
    use();
})();
var z = add(1, 2);

//--- enter-return-value.js
function add(a, b) { return a + b; }
var keep = [];
var width = (function () {
    function host() { return window; }
    keep.push(host);
    return host().innerWidth;
})();
var z = add(1, 2);

//--- enter-cycle.js
function add(a, b) { return a + b; }
// Each hands the object to the other. The walk must mark each parameter once.
function ping(g, n) { if (n > 0) { pong(g, n - 1); } return n; }
function pong(g, n) { ping(g, n - 1); return n; }
ping(globalThis, 4);
var z = add(1, 2);

//--- enter-arguments.js
function add(a, b) { return a + b; }
// `arguments[0]` is the global object, under an index this walk cannot read.
function loose(o) { return arguments.length + (o ? 1 : 0); }
loose(globalThis);
var z = add(1, 2);

//--- enter-surplus.js
function add(a, b) { return a + b; }
// One parameter, two arguments: the surplus has frame semantics.
function one(o) { return o ? 1 : 0; }
one(globalThis, globalThis);
var z = add(1, 2);

//--- self-write.js
function add(a, b) { return a + b; }
// The third name the Shell binds the window proxy to. testharness.js is
// `(function (global_scope) { ... }(self))`.
self.injected = 1;
var z = add(1, 2);

//--- closure-callee.js
// Closure calls, the only shape a vendor bundle has. No global name is
// involved at all: the callee is the closure two tokens back, which names its
// function index outright. This is what a UMD header is.
var out = (function (a, b) { return a + b; })(1, 2);

//--- closure-surplus.js
// Two parameters, three arguments. CallDirectOp::verifySymbolUses holds the
// operand count equal to the entry block's, so the surplus cannot be dropped
// and cannot be passed, so it is refused here rather than failing the verifier.
var out = (function (a, b) { return a + b; })(1, 2, 3);

//--- closure-arguments.js
// A short call into a body that reads its raw argument window. The pad the
// rewrite would add is invisible to registers and visible to `arguments`:
// op::call fills registers with undefined but make_arguments_object copies the
// raw window, whose length is argc. Padding this would make `arguments.length`
// 2 where the VM says 1.
var out = (function (a, b) { return arguments.length; })(1);

//--- name-arguments.js
// The same hole on the per-name path: `two(1)`
// is one argument to two parameters, and `two` reads its raw window.
function two(a, b) { return arguments.length; }
var out = two(1);

//--- this-top.js
function f() { return 1; }
this.f = function () { return 2; };
var r = f();

//--- this-called.js
function f() { return 1; }
function install() { this.f = function () { return 2; }; }
install();
var r = f();

//--- this-arrow.js
function f() { return 1; }
var install = () => { this.f = function () { return 2; }; };
install();
var r = f();

//--- this-iife.js
function f() { return 1; }
(function () { this.f = function () { return 2; }; })();
var r = f();

//--- this-returned.js
function add(a, b) { return a + b; }
function grab() { return this; }
var g = grab();
var z = add(1, 2);

//--- this-field.js
function add(a, b) { return a + b; }
function init() { this.count = 0; }
init();
var z = add(1, 2);

//--- this-refused.js
function f() { return 1; }
function* g() { this.f = function () { return 2; }; yield 0; }
g().next();
var r = f();

//--- this-handed.js
function f() { return 1; }
function helper(o) { o.f = function () { return 2; }; }
function* g() { helper(this); yield 0; }
g().next();
var r = f();

//--- this-inherited.js
function f() { return 1; }
function* g() { var set = () => { this.f = function () { return 2; }; }; set(); yield 0; }
g().next();
var r = f();
