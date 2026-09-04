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

// --- WHAT SLICE 2 STEP 2 TOOK, AND WHAT IS LEFT OF THIS SECTION ------------
//
// FIVE REFUSALS WERE PINNED HERE AND ALL FIVE ARE NOW PROGRAMS THAT COMPILE.
// A binding written by the closure (`var n = 0; function tick() { n = n + 1; }`
// - the COUNTER program, which was the load-bearing negative of slice 1), one
// written twice, one whose write does not dominate a read, one written on a
// single path and one written inside a loop whose call is after it: none has a
// single value a copy could carry, and slice 2 step 2 stops copying. The box
// becomes an ordinary frame-local variable and the capture becomes a POINTER to
// it, so the callee reads and writes the caller's storage and the shared
// binding is shared. All five are in native-shared-cell-fixture.js now, run
// against the interpreter through the compilation-unit gate, which is a
// stronger statement than a refusal: the counter answers 1 then 2.
//
// THE INITIAL IS WHY THE THREE DOMINANCE PROGRAMS CAME WITH THEM. Step 1 had
// to prove no read could see the hoisted `undefined`, because it REPLACED
// every read with the stored value; step 2 emits the variable and assigns the
// initial to it, so a read on a path that stored nothing loads NaN - which is
// what the interpreter answers, and what makes `earlyread`, `outerstore` and
// `loopwrite` right rather than merely admitted.
//
// WHAT IS LEFT IN THIS FILE IS THE ESCAPE. A pointer into a frame is safe
// exactly as long as the closure holding it cannot outlive the frame, and the
// two programs below are the clauses that guarantee that, each failing on its
// own.

// --- SLICE 2 STEP 2, CONDITION 1: A CLOSURE OVER A SHARED BINDING THAT
// --- ESCAPES ---------------------------------------------------------------
//
// THIS IS THE LOAD-BEARING NEGATIVE OF STEP 2, and the one whose absence is
// not a wrong number but undefined behaviour. `tick` captures a binding this
// tier would carry by pointer, and it is RETURNED - so a call of it can happen
// after `maker`'s frame is gone, and `&n` would then name a dead stack slot.
// Condition 4 of whyNotLiftable refuses the closure ("it is returned"), which
// is what stops the pointer being taken at all; measured by relaxing it, the
// emitted C++ returns a function pointer and there is nothing to return it as,
// so the refusal is what keeps the address in the frame that owns it.
//
// SHAREDRETURN: ctjs.func {{.*}}@maker$1
// SHAREDRETURN-SAME: ctnative.not_native = "a closure used as a value: it is returned - Phase 59 slice 2"
// AND THE BOX SAYS SO TOO, in the sentence whyCarriedCellStaysABox writes: the
// census admits a cell on its use list alone, and whether every closure that
// SHARES it was actually lifted is only knowable after the fixpoint. A binding
// one unlifted closure still holds needs a real box, not a variable in this
// frame, and the reason names the closure's own refusal. (It is on the cell
// rather than in `ctnative.not_native` because the walk in
// admission::function meets `%arg2`'s create_closure use before the body.)
// SHAREDRETURN: ctjs.create_cell
// SHAREDRETURN-SAME: ctnative.cell_reason = "the closure that shares it is not lifted, so the binding needs a real box and not a variable in this frame - it is returned - Phase 59 slice 2"

// --- SLICE 2 STEP 2, CONDITION 2: A SHARED BINDING WITH NO NATIVE CARRIER --
//
// The lift runs BEFORE the type solve and cannot ask what a binding holds, so
// the carrier is admission's question - asked twice, once of the box and once
// of the pointer parameter, because the two are in different functions and
// either can be reached first. `s` is a string here, which this tier has no
// C++ carrier for, and a pointer to a value it cannot spell is not a pointer
// it may take.
//
// THE LIFT STILL RAN, and `ctnative.cell_args` on `grow` is the proof: the
// closure was carried and the REFUSAL is the carrier clause, not a capture
// clause standing in front of it. That is what makes this a witness for this
// clause and for no other.
//
// SHAREDSTRING: ctjs.func {{.*}}@tag$1
// SHAREDSTRING-SAME: ctnative.not_native = "a shared binding of type !ctnative.opt<!ctnative.str<utf8>>, which has no native carrier - a variable this tier cannot spell is not one it may point at"
// SHAREDSTRING: ctjs.func {{.*}}@grow$2
// SHAREDSTRING-SAME: ctnative.cell_args = array<i32: 3>
// SHAREDSTRING-SAME: ctnative.not_native = "shared capture 0 is !ctnative.opt<!ctnative.str<utf8>>, which has no native carrier yet"

// --- CONDITION 3 ACROSS FUNCTIONS: A METHOD CALLED FROM ANOTHER METHOD -----
//
// `inner` captures `k`, a binding of `make`'s frame; `outer` calls
// `this.inner()`, and the receiver lift resolves that call because `%arg0` of
// `outer` names the same literal. So the site lift() would prepend the capture
// at is inside `outer` - a DIFFERENT ctjs.func, where `k` does not exist.
// Nothing in SSA forbids writing it: builtin.module's body is a GRAPH region,
// in which every operation dominates every other, so a bare dominance question
// answers "yes" here. whyCapturesDoNotReach compares the enclosing function
// first, and that comparison is the only thing between this program and an
// emitc.func referring to a value defined in another one.
//
// METHODCAP: ctjs.func {{.*}}@make$1
// METHODCAP-SAME: ctnative.not_native = "a method field: capture 0 is a binding of the frame that built the closure, and this call of it is in another function - lifting prepends the captured value at the CALL, and there is nothing to prepend here"

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
// Here `mid` does NOT lift: `outer` writes `k` once, with `k = k + 1`, whose
// own operand READS the box before that write - so the single write does not
// dominate every read and slice 2 step 1 refuses it too. (Before that step the
// sentence was "a binding that is reassigned"; the clause that fails is now
// named, and it is condition 2 rather than the count of writes.) `deep`'s
// capture is then named on a closure this tier does not carry - `mid` has no
// `ctnative.captures` - and the refusal says so AND says why `mid` did not
// lift - the chained sentence is the one a
// reader can act on, and it is only knowable after the fixpoint has settled
// `mid`'s verdict. Write the reasons inside the loop instead of after it and
// the suffix is gone: this line pins that.
//
// THE ROOT IS AN ESCAPE NOW, NOT A DOMINANCE FAILURE, and it had to change:
// slice 2 step 2 ADMITS the shape this chain used to end in. `k` is written by
// the frame after a closure over it was built, which step 1 refused because it
// copied the stored value and a read before the write would have seen the
// hoisted `undefined`; step 2 emits the variable and takes its address, so the
// early read loads the NaN the interpreter also answers. The chain still has
// to terminate in something, and the terminator has to be a rule step 2 does
// NOT lift, or this pin would go green on a file where the suffix was never
// written. `sink.m = mid` plus `mid()` makes `mid` a method field that is also
// used as a value - an escape, and one of the three that keep a pointer inside
// its frame.
//
// ENCLOSING: ctjs.func {{.*}}@outer$1
// ENCLOSING-SAME: ctnative.not_native = "a method field: it is a method field that is also used as a value elsewhere - Phase 59 slice 2"
// ENCLOSING: ctjs.func {{.*}}@mid$2
// ENCLOSING-SAME: ctnative.not_native = "a closure used as a value: capture 0 is filled from the enclosing closure, which did not lift: it is a method field that is also used as a value elsewhere - Phase 59 slice 2"
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
// THREE LEVELS, SAME TERMINATOR. What this adds over ENCLOSING is that the
// suffix nests: `inner` carries TWO "which did not lift" clauses before the
// reason, so a fixpoint that settled only one level out would print a shorter
// sentence here and this line would catch it.
//
// CHAIN3: ctjs.func {{.*}}@mid$2
// CHAIN3-SAME: ctnative.not_native = "a closure used as a value: capture 0 is filled from the enclosing closure, which did not lift: it is a method field that is also used as a value elsewhere - Phase 59 slice 2"
// CHAIN3: ctjs.func {{.*}}@inner$3
// CHAIN3-SAME: ctnative.not_native = "a closure used as a value: capture 0 is filled from the enclosing closure, which did not lift: capture 0 is filled from the enclosing closure, which did not lift: it is a method field that is also used as a value elsewhere - Phase 59 slice 2"

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
    return 2;
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
