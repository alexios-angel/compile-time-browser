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
// THE NEGATIVE CASES ARE THE POINT. Each removes one clause of the proof and
// asserts that NOTHING resolves: a global stored twice, a call passing more
// arguments than the callee declares, and a write through globalThis.
// Blinding the "exactly one store" check made the first of them go green with
// `f = 1` resolved to @f$1 - which is what a negative test is for.
//
// AND TWO OF THE CLAUSES ARE PER NAME RATHER THAN PER MODULE, which is what
// the second half of this file is about. A refused function used to refuse
// every global in the program and a `.constructor` read used to be assumed to
// be `Function`; between them they resolved NOTHING on bootstrap, p5 or
// phaser. Both are still here, and each now has a positive case beside its
// negative one - a refused body that stores nothing, an object literal, a
// `super(...)` - so that widening either again has to make a green test red.
//
// ONE EXPECTATION IN THIS FILE WAS DELIBERATELY FLIPPED. `skipped.js` used to
// assert that a generator ANYWHERE refuses the whole module; it now asserts
// that a generator storing NO global refuses nothing, and `skipped-stores.js`
// is the case that keeps the tooth. That is the change, stated where somebody
// reading the diff will see it.

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
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window-read.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINREAD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/window-alias.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINALIAS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/closed.js 2>/dev/null | ctjs-opt '--ctjs-resolve-globals=report=true' 2>&1 >/dev/null | FileCheck %s --check-prefix=REPORT

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

// --- POSITIVE: a function the importer refused that stores NOTHING --------------
//
// A body this pass cannot read may contain a `ctjs.store_global`, and the
// census counts only the stores that are IN the module. With a refused
// function present, a name stored once visibly and once inside that body looks
// singly bound, and every call to it would be compiled to the wrong function
// rather than diagnosed.
//
// THIS FILE USED TO ASSERT THAT SUCH A FUNCTION REFUSES THE WHOLE MODULE, and
// that is the expectation that changed. The reason it gave - "nothing says
// which names the missing bodies touch" - was not true of the BYTECODE, which
// the importer still has when it drops the function: op::set_global names its
// target with a static index into the function's own name pool. So the
// importer writes the exact set on the ctjs.skipped row, and a body that
// stores nothing refuses nothing. `add` resolves with a generator in the
// program, which is what 51 refused functions cost p5 and two cost phaser.
//
// SKIPPED: ctjs.globals = [
// SKIPPED-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// SKIPPED: ctjs.call_direct @add$1

// --- NEGATIVE: and one that DOES store, which refuses that name ONLY ------------
//
// THE TOOTH, AND THE HALF THAT MUST STAY. `gen` assigns to `add`, which is an
// op::set_global in a body that emits no ctjs.func - so the store the census
// can see is not the only one, and `add` is refused exactly as a second
// visible store would refuse it.
//
// AND `twice` IS NOT. That is the change: one program pins a refusal and an
// acceptance at once, which a module-wide clause makes impossible and a
// per-name one is defined by. Deleting the `refused.contains(name)` test
// resolves `add` to @add$1 and this goes red on the first line.
//
// SKIPSTORE: ctjs.globals = [
// SKIPSTORE-SAME: {name = "add", reason = "a function the importer refused (ctjs.skipped) stores this name, so the binding this pass can see is not the only one", resolved = "none", stores = 1 : i32}
// SKIPSTORE-SAME: {name = "twice", reason = "closed: 1 call(s) rewritten", resolved = @twice$2, stores = 1 : i32}
// SKIPSTORE: ctjs.call_direct @twice$2

// --- NEGATIVE: a refused body whose summary bounds nothing ----------------------
//
// `Function` in a refused body's name pool. This pass cannot follow a value
// through bytecode it never imported, so the summary says so in `opaque` and
// the module is refused exactly as it was before - the old behaviour, kept for
// the bodies that earn it rather than for all of them.
//
// SKIPOPAQUE-NOT: call_direct
// SKIPOPAQUE: ctjs.globals = [
// SKIPOPAQUE-SAME: reason = "a refused body reads `Function`, and the program it compiles can store any global"
// SKIPOPAQUE: ctjs.call %

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

// --- POSITIVE: `.constructor` on an object literal --------------------------------
//
// `({}).constructor` is `Object`. It is not `Function`, and the clause used to
// say it might be because it looked only at the KEY. The receiver decides:
// `constructor` is an own property of every `X.prototype` table holding the
// function that owns that table, and only `Function.prototype`'s copy holds
// `Function`. The value escapes into a ctjs.store_global here, which is what
// the old rule answered on.
//
// LITERAL: ctjs.globals = [
// LITERAL-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// LITERAL: ctjs.call_direct @add$1

// --- NEGATIVE: and the second read off it IS the compiler -------------------------
//
// `Object` is itself a function, so `({}).constructor.constructor` IS
// `Function`. The second read's receiver is a ctjs.get_property, which
// may_be_function() does not clear - listing a `.constructor` RESULT as a
// non-function would have opened exactly this hole.
//
// CHAIN-NOT: call_direct
// CHAIN: ctjs.globals = [
// CHAIN-SAME: reason = "a value that may be the run-time compiler escapes into ctjs.store_global{{[^"]*}}"

// --- POSITIVE: `super(...)`, which IS a `.constructor` read ------------------------
//
// AND IT IS THE WHOLE OF BOOTSTRAP'S LOSS. compile/expressions.cpp:640
// compiles `super(x)` to `load_home; get_proto; get_prop "constructor"` and
// then CALLS it, so the taint answered "escapes into ctjs.call" at
// bootstrap.bundle.js's first derived constructor and refused all 37 of its
// globals. 17 of bootstrap's 38 constructor reads are this shape.
//
// SUPER: ctjs.globals = [
// SUPER-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// SUPER: ctjs.call_direct @add$1

// --- NEGATIVE: unless the class extends `Function` ---------------------------------
//
// THE PREMISE OF THE SUPER CLAUSE, NEGATED. `super()` reaches `Function` only
// when the parent's `prototype` table IS `Function.prototype` - which means
// naming `Function`, and a ctjs.load_global of that name refuses the module on
// its own. That is why the relaxation is sound, and this is the test of it.
//
// EXTENDSFN-NOT: call_direct
// EXTENDSFN: ctjs.globals = [
// EXTENDSFN-SAME: reason = "a run-time compiler is reachable (ctjs.load_global{{[^)]*}}) and the program it builds can store any global"

// --- NEGATIVE: or unless a `prototype` table was replaced --------------------------
//
// THE PREMISE THE SSA WALK CANNOT SEE, CHECKED RATHER THAN ASSUMED. The super
// clause reasons that the link `extends` installs points at a `prototype`
// table, whose `constructor` is the class that owns it. `Base.prototype =
// make()` points it at something this pass cannot rule out being a function,
// whose `.constructor` IS `Function` - so prototype_replaced() retires the
// clause for the whole module and `super()` is tainted again.
//
// PROTOSET-NOT: call_direct
// PROTOSET: ctjs.globals = [
// PROTOSET-SAME: reason = "a value that may be the run-time compiler escapes into ctjs.call{{[^"]*}}"

// --- POSITIVE: a NAMED read off a value that may be the compiler -------------------
//
// `o.constructor.name`. The receiver of the first read is a block argument, so
// that read IS tainted - and the named read off it is not, because no property
// of `Function` called `name` is `Function`. This is bootstrap's other 21
// constructor reads (`this.constructor.NAME`, `.Default`, `.DATA_KEY`,
// `.eventName(...)`, `.getOrCreateInstance(...)`), every one of which used to
// end in a call and answer.
//
// MEMBER: ctjs.globals = [
// MEMBER-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// MEMBER-SAME: {name = "label", reason = "closed: 1 call(s) rewritten", resolved = @label$2, stores = 1 : i32}
// MEMBER: ctjs.call_direct @label$2

// --- NEGATIVE: except for the four names that hand it back -------------------------
//
// `prototype` is one of them: `o.constructor.prototype.constructor` is
// `Function` for any function `o`, so the taint follows this read and the
// returned value escapes. Dropping "prototype" from hands_back_the_compiler()
// makes this go green.
//
// MEMBERPROTO-NOT: call_direct
// MEMBERPROTO: ctjs.globals = [
// MEMBERPROTO-SAME: reason = "a value that may be the run-time compiler escapes into ctjs.return{{[^"]*}}"

// --- POSITIVE: a NAMED read off the GLOBAL OBJECT ----------------------------------
//
// THE SAME PRECISION, ASKED OF THE OTHER WATCHED KIND. The walk propagated
// through every read off `window` on the grounds that `window.window` is the
// window - so `window.innerWidth` was a value that "may be the global object"
// and storing it answered "the global object escapes into ctjs.store_global".
// It is a number.
//
// THE ALIAS SET IS THE HOST'S AND IT IS CLOSED, because putting the window in
// any OTHER global needs a ctjs.store_global of the marked value, which is an
// escape and already the answer.
//
// WHAT THIS DOES NOT BUY, and the measurement says so: `window.foo(...)` is a
// METHOD call, so the window is the call's RECEIVER and escapes anyway. That
// is why bootstrap still resolves nothing - its UMD header passes
// `globalThis`/`self` into the factory - and it is a real refusal, because
// the window proxy's `set` trap calls define_global.
//
// WINREAD: ctjs.globals = [
// WINREAD-SAME: {name = "add", reason = "closed: 1 call(s) rewritten", resolved = @add$1, stores = 1 : i32}
// WINREAD: ctjs.call_direct @add$1

// --- NEGATIVE: and `window.self` IS the window -------------------------------------
//
// One of the five names lib/Shell/bindings/window.cpp binds to the same proxy.
// The taint follows it and the write through it answers - which is not
// politeness: the proxy's `set` trap calls define_global, so this really does
// rebind a global.
//
// WINALIAS-NOT: call_direct
// WINALIAS: ctjs.globals = [
// WINALIAS-SAME: reason = "the global object is written through (ctjs.set_property{{[^"]*}}"

// --- THE COUNTERS, ASSERTED RATHER THAN READ ---------------------------------------
//
// A STATISTIC IS NOT A TEST IN THIS RELEASE. The LLVM package this builds
// against compiles pass statistics out, so --mlir-pass-statistics prints an
// empty report and a check reading one checks nothing. The `report` option
// emits the three counters as a remark instead - the same escape hatch
// --ctjs-lift-to-scf ships - and tools/check/native-claims.py reads exactly
// this line to put a floor under the closed world on the real corpora.
//
// REPORT: resolved 2 global(s), rewrote 3 call(s), closed 2 function(s) over 5 name(s)

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
// its body - and any store_global in it - never reaches this pass. This one
// has none, and the importer says so on the skipped row.
function* gen() { yield 1; }
var y = add(1, 2);

//--- skipped-stores.js
function add(a, b) { return a + b; }
function twice(x) { return x + x; }
// The same refusal, and this body DOES store a global: `add = 1` is an
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

//--- ctor-literal.js
function add(a, b) { return a + b; }
// The receiver is an object literal, so this is `Object`, not `Function` - and
// it escapes into a store_global, which is what the old rule answered on.
var ctor = ({}).constructor;
var z = add(1, 2);

//--- ctor-literal-chain.js
function add(a, b) { return a + b; }
// But `Object` IS a function, so the second read is the compiler.
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
// The one class for which that value IS the compiler - and naming `Function`
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
// `o.constructor` is tainted - `o` is a parameter - and `.name` off it is not.
function label(o) { return o.constructor.name; }
var t = label(1);
var z = add(1, 2);

//--- window-read.js
function add(a, b) { return a + b; }
// A named read off the window that is not one of the host's self-aliases, and
// the value then STORED - which is the escape the old propagation answered on.
var w = window.innerWidth;
var z = add(1, 2);

//--- window-alias.js
function add(a, b) { return a + b; }
// And one that IS. `window.self` is the same proxy, so the write through it
// binds a global.
window.self.injected = 1;
var z = add(1, 2);

//--- ctor-member-prototype.js
function add(a, b) { return a + b; }
// `.prototype` off it IS, because `Function.prototype.constructor` is
// `Function`.
function grab(o) { return o.constructor.prototype; }
var t = grab(1);
var z = add(1, 2);
