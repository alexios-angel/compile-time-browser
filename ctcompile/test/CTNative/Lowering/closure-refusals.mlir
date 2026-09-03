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
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/enclosing3.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CHAIN3
// AND A PROGRAM WITH THE LOWERING RUN TWICE. After one run `mid` is a lifted
// target that a ctjs.call_direct names, with `ctnative.captures` on it and an
// extra entry-block argument; a second run meets that closure with ONE capture
// operand against a target whose upvalue_count is now 0 and refuses it by the
// descriptor mismatch - it never inserts a second capture parameter the
// existing call does not pass. MLIR verifies after every pass, so a lift that
// ran again here would fail this line with CallDirectOp's operand-count error
// rather than the refusal below. `deep` is refused for a reason that does not
// change between runs, so the SAME pins hold for both.
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

// --- CONDITION 1, SLICE 1b: A CAPTURE FILLED FROM AN ENCLOSING CLOSURE THAT
// --- DID NOT LIFT ----------------------------------------------------------
//
// `deep` names `k`, which belongs to `outer` and not to `mid`, so the compiler
// marks `deep`'s descriptor NOT from_parent_local and the VM fills that slot
// from the enclosing closure. The importer leaves an `undefined` placeholder in
// that operand and puts the descriptor's index on `enclosing_indices`
// (BytecodeImport.cpp, op::closure), and the lift carries the slot ONLY once
// `mid` is lifted and that upvalue has become `mid`'s capture parameter - which
// is the whole of slice 1b, and what nested-closure-lift.mlir and
// native-nested-closure-fixture.js exercise.
//
// Here `mid` does NOT lift: `outer` writes `k` after making `mid`, so the cell
// is shared mutable state and `mid`'s capture is refused as reassigned. `deep`'s
// capture is then named on a closure this tier does not carry - `mid` has no
// `ctnative.captures` - and the refusal says so AND says why `mid` did not
// lift - the chained sentence is the one a
// reader can act on, and it is only knowable after the fixpoint has settled
// `mid`'s verdict. Write the reasons inside the loop instead of after it and
// the suffix is gone: this line pins that.
//
// ENCLOSING: ctjs.func {{.*}}@outer$1
// ENCLOSING-SAME: ctnative.not_native = "a closure used as a value: capture 0 is a binding that is reassigned - a shared cell is Phase 59 slice 2"
// ENCLOSING: ctjs.func {{.*}}@mid$2
// ENCLOSING-SAME: ctnative.not_native = "a closure used as a value: capture 0 is filled from the enclosing closure, which did not lift: capture 0 is a binding that is reassigned - a shared cell is Phase 59 slice 2"
// And `deep` keeps the old sentence, which is the right one for it: it really
// does read its own closure, with ctjs.load_upvalue, because nothing lifted it.
// ENCLOSING: ctjs.func {{.*}}@deep$3
// ENCLOSING-SAME: ctnative.not_native = "uses its own closure"

// --- THE SAME, ONE LEVEL DEEPER: THE CHAIN IS SPELLED ALL THE WAY OUT --------
//
// `inner`'s capture is filled from `mid`'s closure, whose own capture is filled
// from `outer`'s, whose binding is reassigned. Each level appends the next, so
// the sentence on `inner`'s closure walks the whole chain to the obstacle.
//
// CHAIN3: ctjs.func {{.*}}@mid$2
// CHAIN3-SAME: ctnative.not_native = "a closure used as a value: capture 0 is filled from the enclosing closure, which did not lift: capture 0 is a binding that is reassigned - a shared cell is Phase 59 slice 2"
// CHAIN3: ctjs.func {{.*}}@inner$3
// CHAIN3-SAME: ctnative.not_native = "a closure used as a value: capture 0 is filled from the enclosing closure, which did not lift: capture 0 is filled from the enclosing closure, which did not lift: capture 0 is a binding that is reassigned - a shared cell is Phase 59 slice 2"

// --- AN ENCLOSING FUNCTION THAT LIFTS, AND A NESTED CLOSURE THAT STILL DOES NOT
//
// `mid` lifts - `k` is a constant cell of `outer`'s frame - so `deep`'s capture
// IS `mid`'s parameter after round one, and condition 1 is satisfied. What
// refuses `deep` is condition 4: it is returned. The refusal names THAT and not
// the capture, which is what shows the slice-1b clause admitted the operand.
//
// RELIFT: ctjs.func {{.*}}@mid$2
// RELIFT-SAME: ctnative.not_native = "a closure used as a value: it is returned - Phase 59 slice 2"

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
    k = k + 1;
    return mid();
}
var r = outer(5);

//--- enclosing3.js
function outer(k) {
    function mid() {
        function inner() {
            function deep() { return k; }
            return deep();
        }
        return inner();
    }
    k = k + 1;
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
