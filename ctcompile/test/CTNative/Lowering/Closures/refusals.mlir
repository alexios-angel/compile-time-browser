// Closure-lifting refusals in the native lowering, one program per refusal.
//
// The native target has no GC, so a closure is lowered only when its captures
// can be passed as extra arguments at each call site ("lifted"). Anything the
// lowering cannot prove gets a `ctnative.not_native` diagnostic, and each
// program here pins one message. The conditions whyNotLiftable
// (ClosureLifting/Captures.cpp) checks are: (1) every capture is filled from a
// binding that reaches the call, (2) a written binding's write reaches every
// read, (3) the call site is in the function that owns the captured binding,
// and (4) the closure does not escape (it is not stored, returned or passed).
// Local bindings that hold a function have their own five-clause rule, stated
// beside `bindingClosures` in ClosureLifter.h; its sections are marked below.
//
// Each message names what is actually in the way. They all used to read `uses
// its own closure`, which covered two different things: a function that really
// reads its own closure value (a named function expression calling itself),
// and a function that merely declares a nested one, because a
// ctjs.create_closure takes the enclosing closure as an operand.
//
// One program per split-file input, because a refusal spreads in both
// directions (a refused callee refuses its caller and vice versa), so a file
// that pinned an acceptance and a refusal together would pin neither.
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/sharedreturn.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SHAREDRETURN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/sharedstring.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SHAREDSTRING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/methodcap.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=METHODCAP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STORED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/returned.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RETURNED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/passed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=PASSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enclosing.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ENCLOSING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enclosing3.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CHAIN3
// The same program with the lowering run twice. A returned closure with an
// immutable environment is lowered after its enclosing factory lifts; both
// runs must verify, so a repeated pass cannot insert another capture argument.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/relift.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RELIFT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/relift.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:              --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RELIFT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrowthis.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARROWTHIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/decl.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DECL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boundtwice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDTWICE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boundearly.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDEARLY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boundvalue.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDVALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/bounddata.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDDATA --implicit-check-not=ctnative.not_native
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/bounddeep.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDDEEP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/deepwrite.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DEEPWRITE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/deepvalue.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DEEPVALUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boundwrite.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDWRITE

// --- Shared bindings that now compile ---------------------------------------
//
// A binding a closure writes (`var n = 0; function tick() { n = n + 1; }`),
// one written twice, one whose write does not dominate a read, one written on
// a single path and one written inside a loop before the call have no single
// value a copy could carry. They are lowered to an ordinary frame-local
// variable, and the closure gets a pointer to it, so the callee reads and
// writes the caller's storage. Those programs are checked against the
// interpreter in Fixtures/Closures/shared-cell.js (the counter answers 1, then
// 2). Their split-file inputs further down (counter.js, twice.js, earlyread.js,
// loopwrite.js, outerstore.js) are kept for reference and not run by this file.
//
// The variable is assigned the binding's initial value, so a read on a path
// that stored nothing loads NaN, which is also what the interpreter answers.
// That is why `earlyread`, `outerstore` and `loopwrite` are right rather than
// merely admitted.
//
// The pointer cannot dangle (ClosureLifter.h, beside carriedCells, has the
// full argument): the closure cannot outlive the frame (condition 4), every
// call is in the frame that owns the variable (condition 3), and the pointer
// is used for nothing but the replaced upvalue reads and writes.
//
// What this file keeps are the escape refusals: the two programs below each
// fail one of the clauses that keep the closure inside the frame, and without
// them a pointer to a dead stack slot could be emitted.

// --- Condition 4: a closure over a shared binding that escapes --------------
//
// This refusal is the one whose absence would be undefined behaviour rather
// than a wrong number. `tick` captures a binding the lowering would carry by
// pointer, and it is returned, so a call of it can happen after `maker`'s frame
// is gone, when `&n` would name a dead stack slot. Condition 4 of
// whyNotLiftable refuses the closure ("returned closure escapes or is inspected
// through `ctjs.store_global`"), which stops the pointer from being taken at
// all.
//
// If this refusal is relaxed, no C++ is emitted at all: lift() walks every
// user of the closure result expecting a call, meets the `ctjs.return`, which
// has no call arguments, and stops with a named fatal error that says
// condition 4 admitted a use it cannot rewrite (Rewrite.cpp). That is a
// diagnosis, not a second line of defence: this refusal is the only thing
// between the program and a dangling `&n`.
//
// SHAREDRETURN pins both the function's reason and the cell's reason below.
//
// SHAREDRETURN: ctjs.func {{.*}}@maker$1
// SHAREDRETURN-SAME: ctnative.not_native = "a closure used as a value: returned closure escapes or is inspected through `ctjs.store_global`"
// The cell carries a reason too, written by whyCarriedCellStaysABox: a cell is
// admitted on its use list alone, and whether every closure that shares it was
// actually lifted is only known after the fixpoint. A binding that an unlifted
// closure still holds needs a real box, not a variable in this frame, and the
// reason names the closure's own refusal. (It is on the cell rather than in
// `ctnative.not_native` because the walk in admission::function meets `%arg2`'s
// create_closure use before the body.)
// SHAREDRETURN: ctjs.create_cell
// SHAREDRETURN-SAME: ctnative.cell_reason = "the closure that shares it is not lifted, so the binding needs a real box and not a variable in this frame - returned closure escapes or is inspected through `ctjs.store_global`"

// A mutable string binding has an owning string value and a lifted pointer.
// Mixed primitive cell storage is covered by Scalars/strings.mlir's SHARED-MIXED.
// SHAREDSTRING: emitc.func @tag_1() -> !emitc.opaque<"ctnative::js_num">
// SHAREDSTRING: emitc.func @grow_2({{.*}}!emitc.ptr<!emitc.opaque<"ctnative::js_string">>) -> !emitc.opaque<"ctnative::js_num">
// SHAREDSTRING-NOT: ctnative.not_native

// --- Condition 3 across functions: a method called from another method -----
//
// `inner` captures `k`, a binding of `make`'s frame; `outer` calls
// `this.inner()`, and the receiver lift resolves that call because `%arg0` of
// `outer` names the same literal. So the site where lift() would prepend the
// capture is inside `outer`, a different ctjs.func, where `k` does not exist.
// Nothing in SSA forbids writing it: builtin.module's body is a graph region,
// in which every operation dominates every other, so a bare dominance question
// answers "yes" here. whyCapturesDoNotReach compares the enclosing function
// first, and that comparison is the only thing between this program and an
// emitc.func referring to a value defined in another one.
//
// METHODCAP: ctjs.func {{.*}}@make$1
// METHODCAP-SAME: ctnative.not_native = "a method field: capture 0 is a binding of the frame that built the closure, and this call of it is in another function - lifting prepends the captured value at the CALL, and there is nothing to prepend here"

// --- Condition 4: a closure stored to a global ------------------------------
//
// Two uses, so this is not a function declaration (a declaration's
// create_closure / store_global pair lowers to nothing). The binding outlives
// the frame and there is no call site to move a capture to, so this would
// need a generated functor struct, which the lowering does not emit here.
//
// STORED: ctjs.func {{.*}}@make$1
// STORED-SAME: ctnative.not_native = "a closure used as a value: it is stored to a global - Phase 59 slice 2"

// --- Condition 4: a closure returned ----------------------------------------
//
// RETURNED: ctjs.func {{.*}}@maker$1
// RETURNED-SAME: ctnative.not_native = "a closure used as a value: returned closure escapes or is inspected through `ctjs.store_global`"

// --- Condition 4: a closure passed as an argument ---------------------------
//
// Lifting moves a capture to the call site, but a callee that receives a
// function value has no call site to move it to. Lowering this would need
// `apply_it` specialised per closure (a monomorphism proof), which is a
// different mechanism, so the closure is refused here. The message's
// "(Phase 63)" is the lowering's own wording, emitted from
// ClosureLifting, and is pinned verbatim.
//
// PASSED: ctjs.func {{.*}}@run$2
// PASSED-SAME: ctnative.not_native = "a closure used as a value: it is passed as an argument - lifting has no call site to move the captures to, so this needs a specialised callee (Phase 63), not a lift"

// --- Condition 1: a capture filled from an enclosing closure that did not
// --- lift -------------------------------------------------------------------
//
// `deep` names `k`, which belongs to `outer` and not to `mid`, so the compiler
// marks `deep`'s descriptor as not from_parent_local and the VM fills that slot
// from the enclosing closure. The importer leaves an `undefined` placeholder in
// that operand and puts the descriptor's index on `enclosing_indices`
// (lib/CTJS/Import/BytecodeImport.cpp, op::closure), and the lift carries the
// slot only once `mid` is lifted and that upvalue has become `mid`'s capture
// parameter, which nested-closure-lift.mlir and
// Fixtures/Closures/nested-closure.js exercise.
//
// Here `mid` does not lift, so `deep`'s capture is named on a closure the
// lowering does not carry (`mid` has no `ctnative.captures`). The refusal says
// so and also says why `mid` did not lift. That chained sentence is the one a
// reader can act on, and it is only knowable after the fixpoint has settled
// `mid`'s verdict: write the reasons inside the loop instead of after it and
// the suffix is gone. This check pins that.
//
// The chain must end in a refusal the lowering still makes, or this check
// would pass on a file where the chained suffix was never written. A write to
// `k` after the closure is built (`k = k + 1`) does not end it: the binding
// becomes a frame variable whose address is passed, and an early read loads
// the NaN the interpreter also answers. So `sink.m = mid` plus `mid()` makes
// `mid` a method field that is also used as a value, which is an escape and is
// always refused.
//
// ENCLOSING checks three functions: `outer` (the escape itself), `mid`
// (refused because its capture chain ends in that escape) and `deep` (which
// really does read its own closure).
//
// The plan labels at the end of some messages ("Phase 59 slice 2" here,
// and "Phase 63" elsewhere) are the lowering's own wording,
// emitted from ClosureLifting/Captures.cpp and Methods.cpp, and are pinned
// verbatim.
//
// ENCLOSING: ctjs.func {{.*}}@outer$1
// ENCLOSING-SAME: ctnative.not_native = "a method field: it is a method field that is also used as a value elsewhere - Phase 59 slice 2"
// ENCLOSING: ctjs.func {{.*}}@mid$2
// ENCLOSING-SAME: ctnative.not_native = "a closure used as a value: capture 0 is filled from the enclosing closure, which did not lift: it is a method field that is also used as a value elsewhere - Phase 59 slice 2"
// `deep` keeps the plain sentence, which is the right one for it: it really
// does read its own closure, with ctjs.load_upvalue, because nothing lifted it.
// ENCLOSING: ctjs.func {{.*}}@deep$3
// ENCLOSING-SAME: ctnative.not_native = "uses its own closure"

// --- The same, one level deeper: the chain is spelled all the way out -------
//
// `inner`'s capture is filled from `mid`'s closure, whose own capture is filled
// from `outer`'s, and `mid` escapes as a method field exactly as in ENCLOSING.
// Each level appends the next, so `inner`'s sentence walks the whole chain.
//
// Three levels, same terminator. What this adds over ENCLOSING is that the
// suffix nests: `inner` carries two "which did not lift" clauses before the
// reason, so a fixpoint that settled only one level out would print a shorter
// sentence here and this check would catch it.
//
// CHAIN3: ctjs.func {{.*}}@mid$2
// CHAIN3-SAME: ctnative.not_native = "a closure used as a value: capture 0 is filled from the enclosing closure, which did not lift: it is a method field that is also used as a value elsewhere - Phase 59 slice 2"
// CHAIN3: ctjs.func {{.*}}@inner$3
// CHAIN3-SAME: ctnative.not_native = "a closure used as a value: capture 0 is filled from the enclosing closure, which did not lift: capture 0 is filled from the enclosing closure, which did not lift: it is a method field that is also used as a value elsewhere - Phase 59 slice 2"

// --- An enclosing function that lifts, and a nested closure that is returned
//
// `mid` lifts first, making its inherited capture a value parameter. The
// returned-closure analysis then sees a closed factory and carries that value
// into `deep`'s owning environment. This checks how the two rewrites interact,
// and that repeating the pass does not change the capture count.
//
// RELIFT: emitc.func @mid_2({{.*}}!emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::ctn_env_deep_3">
// RELIFT: emitc.func @deep_3({{.*}}!emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// RELIFT-NOT: ctnative.not_native

// --- An arrow function that reads its lexical `this` ------------------------
//
// A lifted call passes the call's receiver as %arg0. For an arrow that is not
// what the interpreter reads: `context::effective_this` answers the
// captured_this recorded where the arrow was written (make_closure in
// ctbrowser/lib/Script/vm/call/coroutines.cpp). closureLifter's
// captureLexicalThis therefore makes that value a capture before any rule
// runs: the arrow reads it with ctjs.load_upvalue and no longer reads %arg0,
// so it lifts like any other closure. Here the captured value is `holder`'s
// own `this`, which no caller proves, so `holder` keeps the receiver rule's
// refusal and the lifted arrow the capture rule's.
//
// ARROWTHIS: ctjs.func {{.*}}@holder$1
// ARROWTHIS-SAME: ctnative.not_native = "uses `this`"
// ARROWTHIS: ctjs.create_closure %arg2[2] this %{{.*}} captures %{{.*}} {ctnative.lifted
// ARROWTHIS: ctjs.func private @fn$2
// ARROWTHIS-SAME: ctnative.not_native = "capture 0 is !ctnative.boxed - no caller proves it

// --- A refusal that lands on the box ----------------------------------------
//
// `kept = function () { return k; }` is a create_closure whose only use is a
// store_global, which is the shape isDeclarationClosure calls a declaration,
// so it is exempt from the closure rules above and never gets a reason. What
// is left is the cell `k` was boxed into, which no lift unboxed, and
// `op::new_cell` runs in the prologue, before the `closure` opcode, so an
// admission walk in program order meets the box first. Without a reason on the
// cell this would read "`ctjs.create_cell` is not native yet", which names
// neither the binding nor what is wrong with it.
//
// DECL: ctjs.func {{.*}}@outerdecl$1
// DECL-SAME: ctnative.not_native = "a captured binding that stays a cell: the closure that captures it is not lifted"

// --- Local function bindings that cannot become direct calls -----------------
//
// `var f = function () { ... };` in a function body is a hoisted local, boxed
// when a nested function mentions the name. The lowering turns every call
// through such a name into a `ctjs.call_direct` of the function it holds, so
// the box, the store and the reads lower to nothing.
// Fixtures/Closures/local-function.js checks the accepted shapes against the
// interpreter. The programs below are the shapes it refuses, one clause of the
// binding rule each, and each fails only the clause it is named for: an
// earlier clause firing first would make the check measure nothing.
//
// The reason is the binding's, not the value's, and `whyNotLiftable` asks for
// it before it walks the use list. A refused binding keeps its box; the
// capture of that box is then read as a constant cell and lifted by value, and
// the closure acquires a second use (a `ctjs.call_direct` argument) whose own
// refusal is "it is passed as an argument". Asked in the other order, several
// programs below would report that true but unhelpful consequence instead of
// the obstacle.

// --- Binding clause 1: a name assigned twice --------------------------------
//
// Which function `use()` reaches depends on the path taken to it, so there is
// no one function the name is. If this is relaxed, the rewrite takes the first
// closure and the second `ctjs.cell_set` is left naming a box nothing else
// uses, which hits the named fatal error in `bindLocalFunctions` rather than
// calling the wrong function.
//
// BOUNDTWICE: ctjs.func {{.*}}@twice_bound$1
// BOUNDTWICE-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: it is assigned more than once"

// --- Binding clause 3: a name called before it is assigned -------------------
//
// `use` is built and called above the assignment, so there is a path to the
// read on which the box still holds the `undefined` the hoist put in it. That
// is a TypeError in the interpreter and would be a direct call to `fn$2` here.
// `writeReachesEveryRead` is the same check the shared-cell rules use, and
// this program fails its first clause: the store must dominate every use of
// every closure that captured the box.
//
// The call is guarded so that only the dominance clause can fail. `early(-1)`
// never enters the `if`, so nothing about this program is a throw, an arity
// mismatch or an escape; the only thing wrong with it is that a call site is
// not dominated by the store.
//
// BOUNDEARLY: ctjs.func {{.*}}@early$1
// BOUNDEARLY-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: its one assignment does not reach every read of it, and a read before it yields the undefined the binding was hoisted with"

// --- Binding clause 4: a name that is returned rather than called -----------
//
// `use` hands the binding out. A name that the direct-call rewrite erases has
// no value to hand out (no closure object, no functor, no pointer), so a read
// used for anything but a call is refused. The message names the operation the
// value reached (`ctjs.cell_set`).
//
// BOUNDVALUE: ctjs.func {{.*}}@passed_name$1
// BOUNDVALUE-SAME: ctnative.not_native = "a closure used as a value: returned closure escapes or is inspected through `ctjs.cell_set`"

// --- Binding clause 5: a name whose function closes over a data binding -----
//
// `f` closes over `step`, a number in `data_capture`'s frame, and the call of
// `f` is inside `use`, a different ctjs.func. Lifting prepends the captured
// value at the call, so `use`, made in the frame `step` belongs to, captures
// `step` as well (canAddSiblingCapture) and passes it on: `use` takes `step`
// and calls `f` with it. A capture that is itself a binding this rewrite
// erases needs nothing (`chain27` and `recur15` in
// Fixtures/Closures/local-function.js).
//
// BOUNDDATA: emitc.func @data_capture_{{[0-9]+}}(
// BOUNDDATA: call @fn_[[USE:[0-9]+]](%{{[0-9]+}}, %{{[0-9]+}})
// BOUNDDATA: emitc.func @fn_[[F:[0-9]+]](%arg0: !emitc.opaque<"ctnative::js_num">, %arg1: !emitc.opaque<"ctnative::js_num">)
// BOUNDDATA: emitc.func @fn_[[USE]](%arg0: !emitc.opaque<"ctnative::js_num">, %arg1: !emitc.opaque<"ctnative::js_num">)
// BOUNDDATA: call @fn_[[F]](%arg0, %arg1)
//
// When the call is a frame further in, inside `use` made by `mid`, `step` is
// not in scope where `use` is made, so there is still nothing to prepend it
// at.
//
// BOUNDDEEP: ctjs.func {{.*}}@data_deep$1
// BOUNDDEEP-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: it is read from inside another function, and capture 0 of the function it holds is not a binding this step erases - a call out there has nothing to prepend the capture at"

// --- The slot clause inward: an inner level that fails ----------------------
//
// A name read two frames in is accepted: `two_frames_in` in
// Fixtures/Closures/deep-binding.js is checked against the interpreter. The
// lowering follows the binding inward through `enclosing_indices` and removes
// the capture slot at every level, deepest first. What is left to refuse is an
// inner level that fails one of the checks the owning frame already makes, and
// then the whole chain is refused, because a box is one name.
//
// The sentence is chained, which is the point of both checks below: the outer
// level says the binding travelled further in, and the inner level's own
// reason follows it. A refusal that stopped at "a function one frame further
// in names the binding" would name the mechanism and hide the obstacle, which
// every message in this file is written to avoid.
//
// `deep` assigns the name, two frames in. The box still has exactly one
// `ctjs.cell_set` in the frame that owns it, so clause 1 sees nothing wrong;
// the `ctjs.store_upvalue` is inside a function the owner never looks at, and
// only the walk that follows the slot inward reaches it.
//
// Relaxing this check (measured, not reasoned) gives a named fatal error from
// `removeCaptureSlots`. Dropping the propagation leaves the inner slot in place
// and removes the outer one, so the outer renumbering meets `deep`'s
// `enclosing_indices` entry naming a slot that no longer exists and `move()`
// aborts with `LLVM ERROR: ctnative lowering: ... still names upvalue 0, which
// the local-function rule removed`. It is an abort and not a wrong number,
// which a reader could not tell without running it; either way this refusal
// is what keeps the program from reaching the rewrite.
//
// DEEPWRITE: ctjs.func {{.*}}@deep_assigns$1
// DEEPWRITE-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: a function one frame further in names the binding through its enclosing closure, which did not lift: a function that reads it ASSIGNS the binding, so the name holds a variable and not one function"

// --- The slot clause inward: an inner level that holds the name as a value --
//
// `deep` returns the binding rather than calling it, two frames in. The read
// is a `ctjs.load_upvalue` whose use is a `ctjs.return`, so clause 4 refuses it
// exactly as it does BOUNDVALUE, one frame further in. The message carries no
// depth prefix, so it is the same text as BOUNDVALUE's; the check proves the
// inward walk asks clause 4 at all.
//
// DEEPVALUE: ctjs.func {{.*}}@deep_value$1
// DEEPVALUE-SAME: ctnative.not_native = "a closure used as a value: returned closure escapes or is inspected through `ctjs.cell_set`"



// --- The slot clause: a closure that assigns the name -----------------------
//
// `flip` writes the binding with a `ctjs.store_upvalue`, so the box has one
// `ctjs.cell_set` and is still a variable: `use()` answers 2 after `flip()` has
// run and 1 before it. Clause 1 counts stores in this frame and cannot see
// that one, so the slot walk asks it.
//
// Relaxing this clause (measured) makes removeCaptureSlots abort with
// `LLVM ERROR: ctnative lowering: \`fn$3\` still names upvalue 0, which the
// local-function rule removed`. Two further guards stand behind that: erase
// the surplus store and the replacement closure goes dead ("nothing calls
// it"), and only with that relaxed too does a wrong value appear (`bw2` prints
// 12 where the interpreter prints 22). The clause is still the one that
// refuses the program by name. How a guard fails cannot be seen without
// running it, which is why the measured outcome is recorded here.
//
// BOUNDWRITE: ctjs.func {{.*}}@reassigns$1
// BOUNDWRITE-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: a function that reads it ASSIGNS the binding, so the name holds a variable and not one function"

//--- sharedreturn.js
function maker() {
    var n = 0;
    function tick() { n = n + 1; return n; }
    return tick;
}
var made = maker();

//--- sharedstring.js
function tag() {
    var s = "a";
    function grow() { s = s + "b"; return 1; }
    grow();
    grow();
    return s === "abb" ? 2 : 0;
}
var t = tag();

//--- counter.js
function counter(start) {
    function tick() { start = start + 1; return start; }
    return tick() + tick();
}
var r = counter(0);

//--- twice.js
function twice() {
    var n = 1;
    function get() { return n; }
    n = 2;
    return get();
}
var r = twice();

//--- earlyread.js
function earlyread() {
    var n;
    var first = n;
    n = 5;
    function get() { return n; }
    return first + get();
}
var r = earlyread();

//--- loopwrite.js
function loopwrite(n) {
    var v;
    var i = 0;
    while (i < n) {
        v = i;
        i = i + 1;
    }
    function get() { return v; }
    return get();
}
var r = loopwrite(3);

//--- outerstore.js
function outerstore(k) {
    var v;
    var t = k * 2;
    if (k > 0) { v = t; }
    function get() { return v; }
    return get();
}
var r = outerstore(-1);

//--- methodcap.js
function make(k) {
    var o = {
        inner: function () { return k; },
        outer: function () { return this.inner() + 1; }
    };
    return o.outer();
}
var r = make(5);

//--- stored.js
function make() {
    function inner() { return 1; }
    kept = inner;
    return inner();
}
var r = make();

//--- returned.js
function maker() {
    function inner() { return 1; }
    return inner;
}
var r = maker();

//--- passed.js
function apply_it(f) { return f(); }
function run() {
    function inner() { return 1; }
    return apply_it(inner);
}
var r = run();

//--- enclosing.js
function outer(k) {
    var sink = {};
    function mid() {
        function deep() { return k; }
        return deep();
    }
    k = k + 1;
    sink.m = mid;
    return mid();
}
var r = outer(5);

//--- enclosing3.js
function outer(k) {
    var sink = {};
    function mid() {
        function inner() {
            function deep() { return k; }
            return deep();
        }
        return inner();
    }
    k = k + 1;
    sink.m = mid;
    return mid();
}
var r = outer(5);

//--- relift.js
function outer(k) {
    function mid() {
        function deep() { return k; }
        return deep;
    }
    return mid()();
}
var r = outer(5);

//--- arrowthis.js
function holder() {
    return (() => this)();
}
var r = holder();

//--- decl.js
function outerdecl(k) {
    kept = function () { return k; };
    return 1;
}
var r = outerdecl(2);

//--- boundtwice.js
function twice_bound() {
    var f = function () { return 1; };
    f = function () { return 2; };
    var use = function () { return f(); };
    return use();
}
var r = twice_bound();

//--- boundearly.js
function early(k) {
    var use = function () { return f(); };
    var hit = 0;
    if (k > 0) { hit = use(); }
    var f = function () { return 7; };
    return hit;
}
var r = early(-1);

//--- boundvalue.js
function passed_name() {
    var f = function () { return 1; };
    var use = function () { return f; };
    return use()();
}
var r = passed_name();

//--- bounddata.js
function data_capture(k) {
    var step = k * 2;
    var f = function (n) { return n + step; };
    var use = function (n) { return f(n); };
    return use(1);
}
var r = data_capture(3);

//--- bounddeep.js
function data_deep(k) {
    var step = k * 2;
    var f = function (n) { return n + step; };
    var mid = function () {
        var use = function (n) { return f(n); };
        return use(1);
    };
    return mid();
}
var r = data_deep(3);

//--- deepwrite.js
function deep_assigns() {
    var f = function () { return 1; };
    var mid = function () {
        var deep = function () { f = function () { return 2; }; return 0; };
        return deep() + f();
    };
    return mid();
}
var r = deep_assigns();

//--- deepvalue.js
function deep_value() {
    var g = function () { return 3; };
    var mid = function () {
        var deep = function () { return g; };
        return deep()();
    };
    return mid();
}
var r = deep_value();

//--- boundwrite.js
function reassigns() {
    var f = function () { return 1; };
    var flip = function () { f = function () { return 2; }; return 0; };
    var use = function () { return f(); };
    return flip() + use();
}
var r = reassigns();
