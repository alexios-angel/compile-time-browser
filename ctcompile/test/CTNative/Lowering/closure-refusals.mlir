// WHAT PHASE 59 SLICE 1 REFUSES, AND WHY EACH ONE IS ITS OWN SENTENCE.
//
// The lift admits a closure on four conditions, and every one of them has a
// program here that fails it. These are the negative half of the slice: a
// carrier that admitted everything would be a guess, and a tier whose whole
// contract is "anything unproved is a compile-time diagnostic" has to be able
// to show the diagnostics.
//
// EVERY MESSAGE HERE USED TO BE `uses its own closure`. That refusal is 2,426
// of the 12,916 the compiler makes over bootstrap, p5 and phaser, and it was
// reported for two completely different things: a function that genuinely
// reads its own closure value (a named function expression calling itself),
// and a function that merely DECLARES a nested one - because a
// ctjs.create_closure takes the enclosing closure as an operand and a use is a
// use. The second is the whole population of this file, and every line of it
// now names what is actually in the way.
//
// ONE PROGRAM PER FILE, because a refusal is contagious in both directions -
// a refused callee refuses its caller and a refused caller refuses its callee
// - so a file that pinned an acceptance and a refusal together would pin
// neither. split-file is what the other refusal lits use for the same reason.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/counter.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=COUNTER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/hoisted.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=HOISTED
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
// AND THE SAME PROGRAM WITH THE LOWERING RUN TWICE, which is the only way to
// reach the "its target is already called by symbol" guard: after one run,
// `mid` is a lifted target that a ctjs.call_direct names, and lifting it again
// would insert a second capture parameter that the existing call does not
// pass. MLIR verifies after every pass, so without the guard this line fails
// with CallDirectOp's operand-count error instead of the refusal below.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enclosing.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:              --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ENCLOSING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrowthis.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARROWTHIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/decl.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DECL

// --- CONDITION 2, AND THE LOAD-BEARING NEGATIVE OF THE WHOLE SLICE ----------
//
// `start` is a parameter, so the enclosing frame boxes it once holding its
// value and never writes it - which is exactly the shape the immutability
// proof admits, and the shape native-closure-fixture.js is built out of. What
// refuses this program is the CLOSURE: `tick` writes the binding with
// ctjs.store_upvalue, so the cell is shared mutable state and copying its
// value would give each call its own counter and print 1 twice. Delete the
// store_upvalue arm of the mutation fixpoint and this program is admitted and
// answers 2 where the interpreter answers 3, which is why the REFUSAL is
// pinned and not only the acceptance.
//
// COUNTER: ctjs.func {{.*}}@counter$1
// COUNTER-SAME: ctnative.not_native = "a closure used as a value: capture 0 is a binding that is reassigned - a shared cell is Phase 59 slice 2"
// And `tick` itself keeps the OLD message, which is the right one for it: it
// really does read its own closure, with ctjs.load_upvalue, because nothing
// lifted it.
// COUNTER: ctjs.func {{.*}}@tick$2
// COUNTER-SAME: ctnative.not_native = "uses its own closure"

// --- CONDITION 2 AGAIN, FROM THE OTHER SIDE: A CAPTURED `var` --------------
//
// Nothing here reassigns anything a reader would call a variable, and the
// program is still refused - because `compiler_impl::predeclare_locals` hoists
// `n` to the top of the body, boxes the hoisted `undefined` on the spot, and
// compiles `var n = 5` as a ctjs.cell_set into that box. The cell therefore IS
// written after it was built and the tier says so. This is why every capture
// in native-closure-fixture.js is a parameter, and it is a slice-2 work item
// rather than a defect.
//
// HOISTED: ctjs.func {{.*}}@localvar$1
// HOISTED-SAME: ctnative.not_native = "a closure used as a value: capture 0 is a binding that is reassigned - a shared cell is Phase 59 slice 2"

// --- CONDITION 4: A CLOSURE STORED TO A GLOBAL ------------------------------
//
// TWO uses, so it is not a function declaration - whose create_closure /
// store_global pair already lowered to nothing before this slice existed. The
// binding outlives the frame and there is no call site to move a capture to.
// Slice 2's generated functor struct is what carries this.
//
// STORED: ctjs.func {{.*}}@make$1
// STORED-SAME: ctnative.not_native = "a closure used as a value: it is stored to a global - Phase 59 slice 2"

// --- CONDITION 4: A CLOSURE RETURNED ---------------------------------------
//
// RETURNED: ctjs.func {{.*}}@maker$1
// RETURNED-SAME: ctnative.not_native = "a closure used as a value: it is returned - Phase 59 slice 2"

// --- CONDITION 4: A CLOSURE PASSED AS AN ARGUMENT ---------------------------
//
// THE BRIEF FOR THIS WORK ASKED FOR THIS ONE TO BE ADMITTED, and the mechanism
// cannot give it. Lifting moves a capture to the CALL SITE; a callee that
// receives a function value has no call site to move it to, and lowering it
// means specialising `apply_it` per closure - Phase 63's monomorphism proof,
// which is a different mechanism and a different slice. Refused by name, and
// the name says which phase owns it.
//
// PASSED: ctjs.func {{.*}}@run$2
// PASSED-SAME: ctnative.not_native = "a closure used as a value: it is passed as an argument - lifting has no call site to move the captures to, so this needs a specialised callee (Phase 63), not a lift"

// --- CONDITION 1: A CAPTURE THAT IS NOT A CELL OF THIS FRAME ---------------
//
// `deep` names `k`, which belongs to `outer` and not to `mid`, so the compiler
// marks `deep`'s descriptor NOT from_parent_local and the VM fills that slot
// from the enclosing closure instead of from the operand. The importer pushes
// `undefined` as a placeholder for exactly those (BytecodeImport.cpp,
// op::closure), so lifting one would capture `undefined` where the program
// captured a binding. Requiring every capture to be a cell of THIS frame is
// what makes the rewrite sound, not a convenience.
//
// `mid` itself IS lifted - it captures `k` legally - so the refusal lands in
// `mid`'s body, where `deep` is made, and reaches `outer` through the
// call-graph fixpoint.
//
// ENCLOSING: ctjs.func {{.*}}@mid$2
// ENCLOSING-SAME: ctnative.not_native = "a closure used as a value: capture 0 is not a cell of this frame - it is filled from the enclosing closure, which slice 1 does not carry"

// --- STAGE 59B: AN ARROW THAT READS ITS LEXICAL `this` ----------------------
//
// A lifted call passes the CALL's receiver as %arg0. For an arrow that is not
// what the interpreter reads - `context::effective_this` answers the
// captured_this recorded where the arrow was WRITTEN (call.cpp:924) - so an
// arrow may be lifted only when it never looks. native-closure-fixture.js has
// one that does not and it lifts like any other function.
//
// AND THE RULE READS THE IMPORTER'S CORRECTION. `$enclosing_this` is now
// `undefined` for every non-arrow target, so an operand that is NOT undefined
// is the only place the IR says a target is an arrow. Revert that half of the
// importer change and this rule has nothing to test.
//
// ARROWTHIS: ctjs.func {{.*}}@holder$1
// ARROWTHIS-SAME: ctnative.not_native = "a closure used as a value: it is an arrow function that reads its lexical `this` - Stage 59B"

// --- AND THE ONE THAT LANDS ON THE BOX -------------------------------------
//
// `kept = function () { return k; }` is a create_closure whose ONLY use is a
// store_global, which is the shape isDeclarationClosure calls a declaration -
// so it is exempt from the closure rules above and never gets a reason. What
// is left is the cell `k` was boxed into, which no lift unboxed, and
// `op::new_cell` runs in the prologue - before the `closure` opcode - so an
// admission walk in program order meets the BOX first. Without a reason on the
// cell this reads "`ctjs.create_cell` is not native yet", which names neither
// the binding nor what is wrong with it.
//
// DECL: ctjs.func {{.*}}@outerdecl$1
// DECL-SAME: ctnative.not_native = "a captured binding that stays a cell: the closure that captures it is not lifted"

//--- counter.js
function counter(start) {
    function tick() { start = start + 1; return start; }
    return tick() + tick();
}
var r = counter(0);

//--- hoisted.js
function localvar() {
    var n = 5;
    function get() { return n; }
    return get();
}
var r = localvar();

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
    function mid() {
        function deep() { return k; }
        return deep();
    }
    return mid();
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
