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
// RUN:   | FileCheck %s --check-prefix=BOUNDDATA
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/bounddeep.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDDEEP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boundwrite.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDWRITE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boundmutual.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDMUTUAL

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
// is what stops the pointer being taken at all.
//
// WHAT RELAXING IT ACTUALLY DOES, corrected: ctjs-opt SEGFAULTS. It does not
// emit C++ that fails to compile, which this comment claimed and which would
// have been a second line of defence; no C++ is emitted at all. lift() walked
// every user of the closure result and read `call ? call.getArgs() :
// named.getArgs()` with both null for a ctjs.return. That is now a named fatal
// saying which claim failed, so a regression here reports condition 4 rather
// than dying in a debugger - but the refusal below is still the whole of the
// defence, and this pin is what holds it.
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
// THE TYPE IN THE MESSAGE LOST ITS `opt` AT SLICE 2 STEP 3, and the refusal did
// not move. `var s = "a"` is a write that dominates every read of the binding,
// so the box's hoisted `undefined` is unobservable and the narrowing drops it
// from the join - `opt<str<utf8>>` becomes `str<utf8>`. Neither has a C++
// carrier, which is what this program is about; the change is the narrowing
// showing through a diagnostic, not a change of verdict.
//
// SHAREDSTRING: ctjs.func {{.*}}@tag$1
// SHAREDSTRING-SAME: ctnative.not_native = "a shared binding of type !ctnative.str<utf8>, which has no native carrier - a variable this tier cannot spell is not one it may point at"
// SHAREDSTRING: ctjs.func {{.*}}@grow$2
// SHAREDSTRING-SAME: ctnative.cell_args = array<i32: 3>
// SHAREDSTRING-SAME: ctnative.not_native = "shared capture 0 is !ctnative.str<utf8>, which has no native carrier yet"

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

// --- SLICE 2 STEP 4: WHAT A LOCAL BINDING THAT HOLDS A FUNCTION IS NOT ------
//
// `var f = function () { ... };` in a function body is a hoisted local, boxed
// when a nested function mentions the name, and step 4 makes every call through
// that name a `ctjs.call_direct` of the function it holds - so the box, the
// store and the reads lower to nothing. `native-local-function-fixture.js` runs
// eight of them against the interpreter. These are the seven shapes it refuses,
// one clause apiece, and each program fails ONLY the clause it is named for:
// an earlier clause firing first would make the pin measure nothing, which is
// what the `loopwrite` / `outerstore` note further up this file is about.
//
// THE REASON IS THE BINDING'S AND NOT THE VALUE'S, and `whyNotLiftable` asks
// for it before it walks the use list. A binding step 4 refused keeps its box;
// the capture of that box is then read as a constant cell and lifted BY VALUE,
// and the closure acquires a second use - a `ctjs.call_direct` argument - whose
// own refusal is "it is passed as an argument". Four of the seven below said
// exactly that until the question moved in front of the loop: a true sentence
// about a consequence, and no help at all about the obstacle.

// --- CONDITION 1: A NAME ASSIGNED TWICE ------------------------------------
//
// Which function `use()` reaches depends on the path taken to it, so there is
// no one function the name IS. Relax this and the rewrite takes the first
// closure and the second `ctjs.cell_set` is left naming a box nothing else
// uses - the named fatal in `bindLocalFunctions`, which is what the pass says
// rather than calling the wrong function.
//
// BOUNDTWICE: ctjs.func {{.*}}@twice_bound$1
// BOUNDTWICE-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: it is assigned more than once"

// --- CONDITION 3: A NAME CALLED BEFORE IT IS ASSIGNED -----------------------
//
// `use` is built and called ABOVE the assignment, so there is a path to the
// read on which the box still holds the `undefined` the hoist put in it -
// which is a TypeError in the interpreter and would be a direct call to `fn$2`
// here. `writeReachesEveryRead` is the same function slice 2 steps 1 and 3 ask,
// and this is its first clause: the store dominates every use of every closure
// that captured the box.
//
// THE CALL IS GUARDED SO THAT ONLY THE DOMINANCE CLAUSE CAN FAIL. `early(-1)`
// never enters the `if`, so nothing about this program is a throw or an arity
// or an escape; the only thing wrong with it is that a call site is not
// dominated by the store.
//
// BOUNDEARLY: ctjs.func {{.*}}@early$1
// BOUNDEARLY-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: its one assignment does not reach every read of it, and a read before it yields the undefined the binding was hoisted with"

// --- CONDITION 4: A NAME THAT IS RETURNED RATHER THAN CALLED ----------------
//
// `use` hands the binding OUT. A name this step erases has no value to hand
// out - there is no closure object, no functor and no pointer - so a read used
// for anything but a call is refused, and the sentence says that the binding
// holds a function VALUE rather than naming the operation it reached.
//
// BOUNDVALUE: ctjs.func {{.*}}@passed_name$1
// BOUNDVALUE-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: the name reaches `ctjs.return`, so the binding holds a function VALUE and this step makes none"

// --- CONDITION 5: A NAME WHOSE FUNCTION CLOSES OVER A DATA BINDING ----------
//
// `f` closes over `step`, a number of `data_capture`'s frame, and the call of
// `f` is inside `use` - a DIFFERENT ctjs.func, where `step` does not exist.
// Lifting prepends the captured value at the CALL and there is nothing to
// prepend it at, which is the METHODCAP argument one binding along. A capture
// that is itself a binding this step erases is fine, and that is `chain27` and
// `recur15` in the fixture; a capture that carries a VALUE is not.
//
// BOUNDDATA: ctjs.func {{.*}}@data_capture$1
// BOUNDDATA-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: it is read from inside another function, and capture 0 of the function it holds is not a binding this step erases - a call out there has nothing to prepend the capture at"

// --- THE SLOT CLAUSE: A NAME READ TWO FRAMES IN -----------------------------
//
// `deep` names `f`, which belongs to `two_frames`; the compiler marks `deep`'s
// descriptor NOT from_parent_local and the VM fills that slot from `mid`'s
// closure, with the index on `enclosing_indices` (slice 1b). Step 4 removes the
// slot `mid` holds the box at, and removing it renumbers every capture past it
// - including the one `deep` names through the attribute. Refused rather than
// renumbered: the binding is travelling further inward and there is no call
// site out here for it. This is the largest bucket left over bootstrap's 19
// reachable callees, at 5 of them.
//
// BOUNDDEEP: ctjs.func {{.*}}@two_frames$1
// BOUNDDEEP-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: a function two frames in names the binding through its enclosing closure, and this step has no call site out here for it"

// --- THE SLOT CLAUSE: A CLOSURE THAT ASSIGNS THE NAME -----------------------
//
// `flip` writes the binding with a `ctjs.store_upvalue`, so the box has ONE
// `ctjs.cell_set` and is still a variable: `use()` answers 2 after `flip()` has
// run and 1 before it. Condition 1 counts stores in this frame and cannot see
// that one, so the slot walk asks it.
//
// WHAT ITS RELAXATION ACTUALLY GIVES, measured rather than reasoned: a named
// fatal. `LLVM ERROR: ctnative lowering: \`fn$3\` still names upvalue 0, which
// the local-function rule removed`, out of removeCaptureSlots. This comment
// used to say "a WRONG ANSWER rather than a refusal", and that is wrong twice
// over: it is an abort, and TWO further defences stand behind it - erase the
// surplus store and the replacement closure goes dead ("nothing calls it"),
// and only with that relaxed too does a wrong number appear (`bw2` prints 12
// where the interpreter prints 22). The clause IS load-bearing; the tier is
// safer than this comment claimed, and how a guard fails is the one thing a
// reader cannot check without running it.
//
// BOUNDWRITE: ctjs.func {{.*}}@reassigns$1
// BOUNDWRITE-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: a function that reads it ASSIGNS the binding, so the name holds a variable and not one function"

// --- CONDITION 3 ACROSS TWO NAMES: MUTUAL RECURSION -------------------------
//
// `even_ish` calls `odd_ish` and `odd_ish` calls `even_ish`, so one of the two
// stores has to come second - and `writeReachesEveryRead` asks that the store
// dominate every USE of every closure that captured the box, which for a name
// includes the `ctjs.cell_set` that puts the other closure in ITS box. It
// cannot ask about calls instead: a closure whose value goes into a box of its
// own has no call site at all until this very rewrite makes one. So the second
// name is refused for dominance and the first for condition 5, because the
// capture it needs is the name that was just refused.
//
// THIS IS THE RULE'S CONSERVATISM AND NOT A SOUNDNESS CLAUSE, and it is the
// same conservatism that refuses `function g() { f(); } function f() {}`
// written in that order. Following a closure's value through its own box, one
// hop out, is what would admit both - and is the next lever after the slot
// clause above.
//
// BOUNDMUTUAL: ctjs.func {{.*}}@mutual$1
// BOUNDMUTUAL-SAME: ctnative.not_native = "a closure used as a value: it is the value of a local binding this tier cannot call directly: its one assignment does not reach every read of it, and a read before it yields the undefined the binding was hoisted with"

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
function two_frames() {
    var f = function () { return 1; };
    var mid = function () {
        var deep = function () { return f(); };
        return deep();
    };
    return mid();
}
var r = two_frames();

//--- boundwrite.js
function reassigns() {
    var f = function () { return 1; };
    var flip = function () { f = function () { return 2; }; return 0; };
    var use = function () { return f(); };
    return flip() + use();
}
var r = reassigns();

//--- boundmutual.js
function mutual(k) {
    var even_ish = function (n) { if (n <= 0) { return 1; } return odd_ish(n - 1); };
    var odd_ish = function (n) { if (n <= 0) { return 0; } return even_ish(n - 1); };
    return even_ish(k) * 10 + odd_ish(k);
}
var r = mutual(3);
