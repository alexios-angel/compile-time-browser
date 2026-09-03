// WHAT THE RECEIVER LIFT REFUSES, AND WHY EACH ONE IS ITS OWN SENTENCE.
//
// The lift admits a method on four conditions and every one of them has a
// program here that fails it. These are the negative half of the slice: a
// carrier that admitted everything would be a guess, and a tier whose whole
// contract is "anything unproved is a compile-time diagnostic" has to be able
// to show the diagnostics.
//
// EVERY MESSAGE HERE USED TO BE `uses \`this\``, which is 6,649 of the refusals
// the compiler makes over bootstrap, p5 and phaser and says nothing at all
// about what is in the way. The nested `fn$N` in each program still says it -
// that is the target, refused because the lift did not take it - and the
// sentence a reader can act on is on the closure, in the function that made it.
//
// ONE PROGRAM PER FILE, because a refusal is contagious in both directions - a
// refused callee refuses its caller and a refused caller refuses its callee -
// so a file that pinned an acceptance and a refusal together would pin neither.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/returns_this.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RETURNSTHIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reassigned.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=REASSIGNED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/openshape.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OPENSHAPE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/storedthis.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STOREDTHIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/passedthis.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=PASSEDTHIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrowthis.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARROWTHIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/readnotcalled.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=READNOTCALLED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/boolfield.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOOLFIELD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/partial.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=PARTIAL
// AND THE SAME PROGRAM WITH THE LOWERING RUN TWICE, which is the one shape that
// could reach a method target the pass has already rewritten: after one run the
// method is a private ctjs.func that a ctjs.call_direct names, its
// `upvalue_count` is 0 and its store is gone. A second run must change nothing
// rather than insert a second receiver parameter the existing call does not
// pass - MLIR verifies after every pass, so a rewrite that ran twice would fail
// here with CallDirectOp's operand-count error rather than with this line.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrowthis.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:              --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARROWTHIS

// --- CONDITION 3: A METHOD THAT RETURNS `this` -----------------------------
//
// THE LOAD-BEARING NEGATIVE OF THE SLICE. The receiver is a pointer into the
// CALLER's frame and nothing owns it, so a method that hands it back produces a
// value that outlives the object it names. There is no owner to introduce here
// - no unique_ptr, no shared_ptr, no allocation - so the answer is a refusal
// and not a lifetime guess.
//
// RETURNSTHIS: ctjs.func {{.*}}@leaks$1
// RETURNSTHIS-SAME: ctnative.not_native = "a method field: it returns `this` - the receiver is the caller's frame, so returning it would outlive the object"

// --- CONDITION 2: A FIELD REASSIGNED TO A SECOND FUNCTION -------------------
//
// `o.f = <another function>` after the literal bound one. Two stores of one key
// means the callee of `o.f()` depends on which store ran, which is exactly what
// "provably one function" forbids - and the message says THAT rather than "it
// is stored into an object", which names the mechanism and not the obstacle.
//
// REASSIGNED: ctjs.func {{.*}}@twofns$1
// REASSIGNED-SAME: ctnative.not_native = "a closure used as a value: the field `f` is written more than once, so which function a call through it reaches depends on which store ran"

// --- CONDITION 1: A METHOD CALLED ON AN OBJECT WHOSE SHAPE IS OPEN ----------
//
// `o[k] = 5` with a computed key can add a field, so the shape is not closed
// and there is no class for the receiver to be a pointer to. Phase 56A's proof
// is the same one the receiver needs, and this is where they meet.
//
// OPENSHAPE: ctjs.func {{.*}}@opened$1
// OPENSHAPE-SAME: ctnative.not_native = "a closure used as a value: it is a method field of an object whose shape is not closed, so `f` cannot become a free function taking that object"

// --- CONDITION 3: A METHOD THAT STORES `this` INTO ANOTHER OBJECT -----------
//
// The same lifetime question as the return, reached the other way. The order of
// the clauses is what makes this message the right one: `box` is a hoisted
// `var` and therefore a cell that is written, so asking the CAPTURE clause
// first answered "capture 0 is a binding that is reassigned" for a program
// whose actual problem is the receiver.
//
// STOREDTHIS: ctjs.func {{.*}}@stores$1
// STOREDTHIS-SAME: ctnative.not_native = "a method field: it stores `this` into another object - that needs an owner, and this slice introduces none"

// --- CONDITION 3: A METHOD THAT PASSES `this` AS AN ARGUMENT ----------------
//
// A lift moves a value to the CALL SITE. A receiver has a slot there - operand
// 0 of ctjs.call_direct, which IS the callee's %arg0 - and an argument position
// does not: the callee would have to be specialised per shape, which is Phase
// 63's monomorphism proof and not this.
//
// AND THE CALL IS ALREADY DIRECT by the time the lift looks, because
// --ctjs-resolve-globals named `idof` long before. Without the ctjs.call_direct
// arm this said "it reaches `ctjs.call_direct`" - the operation, not the
// mistake - for the commonest way there is to leak a receiver.
//
// PASSEDTHIS: ctjs.func {{.*}}@passes$2
// PASSEDTHIS-SAME: ctnative.not_native = "a method field: it passes `this` as an argument to another function - a receiver moves to the CALL SITE, and an argument position has none to move to (that needs a specialised callee, Phase 63, not a lift)"

// --- AN ARROW METHOD READING ITS LEXICAL `this` -----------------------------
//
// THE OTHER GUARD WHOSE REMOVAL GIVES A WRONG ANSWER RATHER THAN A REFUSAL,
// and it was measured rather than argued. `{ get: () => this.x }` reads the
// ENCLOSING function's `this`, not the object, and every use of it is a
// constant-key read that condition 3 admits - so with the arrow clause
// disabled this program compiles with no refusal anywhere, binds `this` to the
// literal, and the binary prints `a=1` where the interpreter prints
// `a=undefined`. It is asked FIRST, before any rule that admits a `this` use,
// for exactly that reason.
//
// ARROWTHIS: ctjs.func {{.*}}@arrowed$1
// ARROWTHIS-SAME: ctnative.not_native = "a method field: it is an arrow function that reads its lexical `this` - Stage 59B"

// --- A METHOD READ AS A VALUE RATHER THAN CALLED ----------------------------
//
// `var g = o.m;` loads the field and never calls it, so there is no call site
// for the receiver to move to. What the program asked for is a bound function -
// a value carrying a receiver - which is an owner, and this slice builds none.
//
// READNOTCALLED: ctjs.func {{.*}}@grabbed$1
// READNOTCALLED-SAME: ctnative.not_native = "a method field: its field `m` is read as a value rather than called - a method used as a function value has to carry its receiver, which is a bound function and an owner this slice does not build"

// --- THE FIELD INDEX IS OVER THE ALIAS GROUP, AND THIS IS ITS PROOF ---------
//
// A METHOD'S `this.flag` IS THE SAME FIELD AS THE CALLER'S `o.flag`, and
// TypeInference has to say so: two values, `%arg0` and the literal, name one
// object, so `groupReceivers` fans every store out to every member. Drop that
// and `this.flag` finds no store, reads `undefined`, and undefined's carrier is
// a double - so `get` returns `double`, `look` returns `double`, the global
// store is admitted as a number, and the binary prints `shown=1` where the
// interpreter prints `shown=true`. MEASURED, exactly that, on the box.
//
// The refusal below is what the group buys: with it, `this.flag` is a BOOLEAN,
// so the global store is refused by name and no wrong answer is printed. A
// boolean global is Phase 62½'s own limit and not this slice's, which is why
// this is a refusal here and not a fixture.
//
// BOOLFIELD: ctjs.func {{.*}}@_script_$0
// BOOLFIELD-SAME: ctnative.not_native = "store to global `shown` operand is !ctnative.opt<!ctnative.bool>, not a number"

// --- A `this.other()` WHOSE CALLEE WAS REFUSED ------------------------------
//
// `area` returns `this` and is refused; `twice` calls it on the same receiver
// and was admitted by the LIFT, because the lift's proof runs before its own
// rewrite and `this.area()` resolved fine at that point. So `twice` is marked
// `ctnative.receiver` and its `%arg0` still has a `ctjs.call` on it, which is
// exactly the shape `hasClosedShape` reads as open. Admission asks the question
// again, of the IR that came out.
//
// WITHOUT THAT SECOND ASK the program is still refused - `ctjs.call` has no
// lowering - but `twice` reports "a constant that is not a number, a boolean or
// undefined", which names the `"area"` string the erased load left behind and
// tells a reader nothing. Measured both ways.
//
// PARTIAL: ctjs.func {{.*}}@fn$3
// PARTIAL-SAME: ctnative.not_native = "its `this` is no longer a closed-shape receiver - it calls another method on itself that this tier did not lift"

//--- returns_this.js
function leaks() {
    var o = { x: 1, self: function () { return this; } };
    var r = o.self();
    return o.x;
}
var a = leaks();

//--- reassigned.js
function twofns() {
    var o = { x: 1, f: function () { return this.x; } };
    o.f = function () { return this.x + 1; };
    return o.f();
}
var a = twofns();

//--- openshape.js
function opened(k) {
    var o = { x: 1, f: function () { return this.x; } };
    o[k] = 5;
    return o.f();
}
var a = opened(2);

//--- storedthis.js
function stores() {
    var box = { held: 0 };
    var o = { x: 1, keep: function () { box.held = this; return 0; } };
    return o.keep();
}
var a = stores();

//--- passedthis.js
function idof(v) { return 1; }
function passes() {
    var o = { x: 1, give: function () { return idof(this); } };
    return o.give();
}
var a = passes();

//--- arrowthis.js
function arrowed() {
    var o = { x: 1, get: () => this.x };
    return o.get();
}
var a = arrowed();

//--- readnotcalled.js
function grabbed() {
    var o = { x: 1, m: function () { return this.x; } };
    var g = o.m;
    return o.x + 0 * g;
}
var a = grabbed();

//--- boolfield.js
function look() {
    var o = { flag: true, get: function () { return this.flag; } };
    return o.get();
}
var shown = look();

//--- partial.js
function partial() {
    var v = {
        w: 3,
        area: function () { return this; },
        twice: function () { return this.area() * 2; }
    };
    return v.twice();
}
var a = partial();
