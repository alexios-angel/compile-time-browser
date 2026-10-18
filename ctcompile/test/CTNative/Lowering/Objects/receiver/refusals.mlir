// Methods the receiver lift refuses, one program per reason, each pinned to
// its own diagnostic.
//
// The lift turns `o.m()` on an object literal into a free function taking `o`
// as a pointer parameter. It admits a method only if the four CONDITIONs in
// lib/CTNative/Lowering/ClosureLifting/Methods.cpp hold: (1) the object's shape
// is closed, (2) `o.m` is provably one function, (3) the method uses `this`
// only for constant-key access, and (4) the body is otherwise native. Native
// code has no runtime fallback, so every refusal must be a diagnostic naming
// the actual obstacle. It is on the closure, in the function that made it, and
// the nested `fn$N` target repeats it after "uses `this` - " (pinned in
// Admission/refusal/corpus-shapes.mlir).
//
// One program per file: a refused callee refuses its caller and a refused
// caller its callee, so a file pinning an acceptance and a refusal pins neither.
//
// Some pinned messages still say "this slice", "this tier" or "Phase 63".
// They come from ClosureLifting/{Methods,Captures}.cpp; change
// them there and in the FileCheck lines here together.
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
// The same program with the lowering run twice, the only way the pass can meet
// a method target it has already rewritten: after one run the method is a
// private ctjs.func named by a ctjs.call_direct, its `upvalue_count` is 0 and
// its store is gone. A second run must change nothing rather than add a second
// receiver parameter the existing call does not pass. MLIR verifies after
// every pass, so a double rewrite would fail with CallDirectOp's operand-count
// error rather than at this FileCheck.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrowthis.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:              --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARROWTHIS

// --- CONDITION 3: a method that returns `this` -----------------------------
//
// The key negative case. The receiver is a pointer into the caller's frame and
// nothing owns it, so a method that hands it back produces a value that
// outlives the object it names. There is no owner to introduce (no unique_ptr,
// no shared_ptr, no allocation), so the answer is a refusal, not a lifetime
// guess.
//
// RETURNSTHIS: ctjs.func {{.*}}@leaks$1
// RETURNSTHIS-SAME: ctnative.not_native = "a method field: it returns `this` - the receiver is the caller's frame, so returning it would outlive the object"

// --- CONDITION 2: a field reassigned to a second function ------------------
//
// `o.f = <another function>` after the literal bound one. Two stores of one key
// mean the callee of `o.f()` depends on which store ran, which is what
// "provably one function" forbids, and the message says that rather than "it
// is stored into an object", which names the mechanism and not the obstacle.
//
// REASSIGNED: ctjs.func {{.*}}@twofns$1
// REASSIGNED-SAME: ctnative.not_native = "a closure used as a value: the field `f` is written more than once, so which function a call through it reaches depends on which store ran"

// --- CONDITION 1: a method called on an object whose shape is open ---------
//
// `o[k] = 5` with a computed key can add a field, so the shape is not closed
// and there is no class for the receiver to point to. This is the same
// closed-shape proof (`hasClosedShape`) that object-to-class lowering uses.
//
// OPENSHAPE: ctjs.func {{.*}}@opened$1
// OPENSHAPE-SAME: ctnative.not_native = "a closure used as a value: it is a method field of an object whose shape is not closed, so `f` cannot become a free function taking that object"

// --- CONDITION 3: a method that stores `this` into another object ----------
//
// The same lifetime question as the return, reached the other way. The clause
// order is what makes this message the right one: `box` is a hoisted `var` and
// therefore a cell that is written, so asking the capture clause first would
// answer "capture 0 is a binding that is reassigned" for a program whose
// actual problem is the receiver.
//
// STOREDTHIS: ctjs.func {{.*}}@stores$1
// STOREDTHIS-SAME: ctnative.not_native = "a method field: it stores `this` into another object - that needs an owner, and this slice introduces none"

// --- CONDITION 3: a method that passes `this` as an argument ---------------
//
// A lift moves the receiver to the call site. A receiver has a slot there
// (operand 0 of ctjs.call_direct, which is the callee's %arg0); an ordinary
// argument position does not, because the callee would have to be
// specialized per object shape, which this lift does not do.
//
// The call is already direct by the time the lift looks, because
// --ctjs-resolve-globals has named `idof`. Without the ctjs.call_direct arm the
// message would be "it reaches `ctjs.call_direct`", naming the operation rather
// than the mistake, for the commonest way to leak a receiver.
//
// PASSEDTHIS: ctjs.func {{.*}}@passes$2
// PASSEDTHIS-SAME: ctnative.not_native = "a method field: it passes `this` as an argument to another function - a receiver moves to the CALL SITE, and an argument position has none to move to (that needs a specialised callee, Phase 63, not a lift)"

// --- An arrow method reading its lexical `this` ----------------------------
//
// `{ get: () => this.x }` reads the enclosing function's `this`, not the
// object, and every use of it is a constant-key read that CONDITION 3 admits.
// Bound to the literal, this program would print `a=1` where the interpreter
// prints `a=undefined`. closureLifter's captureLexicalThis prevents that: the
// arrow reads `arrowed`'s `this` from a capture instead of its receiver, so
// the receiver lift has no use of `this` to bind. `arrowed`'s own `this` is
// unproved, so `arrowed` keeps the receiver rule's refusal, and the arrow,
// which takes that `this` as an object pointer, is refused with it.
//
// ARROWTHIS: ctjs.func {{.*}}@arrowed$1
// ARROWTHIS-SAME: ctnative.not_native = "uses `this`"
// ARROWTHIS: ctjs.func private @fn$2
// ARROWTHIS-SAME: ctnative.not_native = "called by `arrowed$1`, which is not native"

// --- A method read as a value rather than called ---------------------------
//
// `var g = o.m;` loads the field and never calls it, so there is no call site
// for the receiver to move to. The program needs a bound function (a value
// carrying a receiver), which is an owner the lift does not build.
//
// READNOTCALLED: ctjs.func {{.*}}@grabbed$1
// READNOTCALLED-SAME: ctnative.not_native = "a method field: its field `m` is read as a value rather than called - a method used as a function value has to carry its receiver, which is a bound function and an owner this slice does not build"

// --- The field index covers the whole alias group --------------------------
//
// The receiver and the literal name the same object, so the joined field index
// keeps the Boolean write. Callable-use and initialization checks prove that
// `look` returns a Boolean, and the global boundary keeps that tag when printing.
// BOOLFIELD-NOT: ctnative.not_native
// BOOLFIELD: emitc.class @ctn_flag
// BOOLFIELD-NEXT: emitc.field @flag : !emitc.opaque<"ctnative::js_boolean_t">
// BOOLFIELD-NEXT: }
// BOOLFIELD: emitc.global static @g_shown : !emitc.opaque<"ctnative::nullable_scalar">
// BOOLFIELD: call_opaque "ctnative::global_boolean"
// BOOLFIELD-SAME: -> !emitc.opaque<"ctnative::js_boolean_t">
// BOOLFIELD: call_opaque "ctnative::print_boolean"
// BOOLFIELD: emitc.func @look_1() -> !emitc.opaque<"ctnative::js_boolean_t">
// BOOLFIELD: emitc.func @fn_2(%arg0: !emitc.ptr<!emitc.opaque<"ctn_flag">>) -> !emitc.opaque<"ctnative::js_boolean_t">
// BOOLFIELD-NOT: ctnative.not_native

// --- A `this.other()` whose callee was refused -----------------------------
//
// `area` returns `this` and is refused; `twice` calls it on the same receiver
// and was admitted by the lift, because the lift's proof runs before its own
// rewrite and `this.area()` resolved at that point. So `twice` is marked
// `ctnative.receiver` and its `%arg0` still has a `ctjs.call` on it, which is
// exactly the shape `hasClosedShape` reads as open. Admission therefore asks
// again, of the IR the lift produced.
//
// Without that second check the program is still refused (`ctjs.call` has no
// lowering), but `twice` reports "a constant that is not a number, a boolean
// or undefined", which names the `"area"` string the erased load left behind
// and tells a reader nothing. Both versions were run.
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
