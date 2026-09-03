// WHAT THE OBJECT-ARGUMENT RULE REFUSES, AND WHY EACH ONE IS ITS OWN SENTENCE.
//
// The rule admits an argument on three conditions and every one of them has a
// program here that fails it, plus the fixpoint that condition 2 needs. These
// are the negative half of the slice: a carrier that admitted everything would
// be a guess, and a tier whose whole contract is "anything unproved is a
// compile-time diagnostic" has to be able to show the diagnostics.
//
// EVERY MESSAGE HERE USED TO BE "an object literal that escapes - it is passed
// to a call", which is true of all five and says nothing about which one this
// program is. The condition that failed is a property of the CALLEE - which
// parameter, read how, called from where - and the use-list walk that meets the
// escape has none of it, so `argumentCensus` writes the sentence onto the
// literal as `ctnative.object_reason`, the same idiom as
// `ctnative.closure_reason` and `ctnative.cell_reason`.
//
// AND THEY LAND ON THE ctjs.call_direct ARM, NOT THE ctjs.call ONE, in four of
// the five. A callee whose parameter this rule refused is still a closure the
// PLAIN lift takes - its uses are all calls of it - so by the time admission
// asks, the call has already become a ctjs.call_direct. Only `opaque`, whose
// callee is a global with two stores and so never resolves, still has a
// ctjs.call at that point. Both arms are covered here for that reason.
//
// ONE PROGRAM PER FILE, because a refusal is contagious in both directions - a
// refused callee refuses its caller and a refused caller refuses its callee -
// so a file that pinned an acceptance and a refusal together would pin neither.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/opaque.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OPAQUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escapes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ESCAPES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unread.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNREAD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/otheropen.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OTHEROPEN
// AND THE CENSUS, which is a MEASUREMENT the option ships rather than a rule.
// It is asserted here because a distribution nobody checks is a distribution
// that rots: this program has one method-bearing literal and one blocking use,
// and the label says which.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/census.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              '--ctnative-lower-to-emitc=census=1' -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CENSUS

// --- CONDITION 1: THE CALLEE IS NOT ONE FUNCTION ---------------------------
//
// `sink` is a global with two stores, so --ctjs-resolve-globals leaves the call
// dispatching through a `load_global` and the lift has no callee to give the
// address to.
//
// THIS IS THE SHAPE THE CORPORA ARE MADE OF, AND THE REASON THE WIDENING MOVES
// NO CORPUS NUMBER. `--ctnative-lower-to-emitc=census=1` over bootstrap, p5 and
// phaser finds 994 method-bearing literals refused as open, and every one of
// the 53 argument uses among their blocking uses has an OPAQUE callee - 53 of
// 53, with `call.argument.parameter-is-read-only` at zero and
// `call_direct.argument` at zero. 51 literals are blocked by nothing else. So
// the rule below is right and buys nothing there, which is a measurement and
// not a disappointment: the fixture is what shows the carrier works.
//
// OPAQUE: ctjs.func {{.*}}@opaque$2
// OPAQUE-SAME: ctnative.not_native = "an object literal that escapes - it is passed to a call whose callee is not one function this rewrite can name, so there is no parameter to give the object's address to"

// --- CONDITION 3: THE PARAMETER IS REACHED SOME OTHER WAY -------------------
//
// `return p` hands the pointer back, and the pointer names the CALLER's frame.
// The same lifetime question the receiver refuses when a method returns `this`,
// reached through an argument: there is no owner to introduce here - no
// unique_ptr, no shared_ptr, no allocation - so the answer is a refusal.
//
// ESCAPES: ctjs.func {{.*}}@escapes$1
// ESCAPES-SAME: ctnative.not_native = "an object literal that escapes - it is passed to a parameter that reaches it through something other than a constant key - that needs an owner, and this slice introduces none"

// --- CONDITION 3: THE PARAMETER IS NEVER READ -------------------------------
//
// An object parameter nothing reads still has to be spelled in the signature,
// and `double take_N(ctn_x *)` with no use of it is `-Wunused-parameter` under
// -Werror - which Phase 63 Step 7 makes a ctcompile bug and not a warning. The
// receiver clause makes the same decision the other way round: the lift marks
// `ctnative.receiver` only when `%arg0` is READ.
//
// UNREAD: ctjs.func {{.*}}@unread$1
// UNREAD-SAME: ctnative.not_native = "an object literal that escapes - it is passed to a parameter nothing reads, and an object parameter that is never read is `-Wunused-parameter` in the generated C++"

// --- CONDITION 2: A LITERAL AT ONE CALL, A NUMBER AT ANOTHER ----------------
//
// One parameter is one C++ type. `take(o)` and `take(2)` want `ctn_x *` and
// `double` for one position, and there is no spelling for both - a template
// would be a specialised callee, which is Phase 63's monomorphism proof and not
// a lift.
//
// MIXED: ctjs.func {{.*}}@mixed$1
// MIXED-SAME: ctnative.not_native = "an object literal that escapes - it is passed to a parameter that is an object literal at this call and something else at another, so the parameter has no single C++ type"

// --- CONDITION 2, THE FIXPOINT: THE OTHER LITERAL IS OPEN -------------------
//
// THE FIXPOINT'S OWN TEST, and the reason `argumentCensus` is a loop rather
// than a pass. `o` and `bad` are both `{x}` and both passed to the same
// read-only parameter, so on the first round the slot is a candidate and BOTH
// of them read as closed - each one's argument use excused by the slot, the
// slot excused by the two of them. `bad` is then reached through a dynamic key,
// which nothing about the slot can fix, so the slot is dropped and `o`'s
// argument use stops being excused with it.
//
// WHAT THE LOOP ACTUALLY BUYS, MEASURED RATHER THAN PREDICTED. The prediction
// was a crash: admit `o`, give the parameter a `ctn_x *`, then ask `shapeAt`
// for a literal admission had refused. It is not - deleting the loop leaves the
// program REFUSED, with `field `` is not a C identifier`, because the dynamic
// key on `bad` joins an empty field name into the group the two now share. So
// the loop buys the MESSAGE, exactly as the receiver lift's second ask of
// `this` does, and the correctness backstop is the call-site arm of admission,
// which asks `isClosedObject` of every object operand again.
//
// OTHEROPEN: ctjs.func {{.*}}@otheropen$1
// OTHEROPEN-SAME: ctnative.not_native = "an object literal that escapes - it is passed to a parameter whose other object literal is not itself a closed shape, so the two would not agree on one class"

// --- THE CENSUS, AS A REMARK ------------------------------------------------
//
// `census.js` is the shape the whole slice is about, with the argument use made
// unliftable so that it stays refused: a method-bearing literal whose ONLY
// blocking use is an argument to a call this rewrite cannot name. That is the
// bucket the corpora are full of, and this asserts that the census names it -
// one literal, one method field, one blocking use, `call.argument.callee-opaque`
// as the sole blocker and as the root.
//
// CENSUS: ctnative census: 1 method-bearing literal(s) refused as open, holding 1 method field(s); blocking uses: call.argument.callee-opaque=1; sole blocker: call.argument.callee-opaque=1; sole blocker by method field: call.argument.callee-opaque=1; root of the nesting: call.argument.callee-opaque=1

//--- opaque.js
function sink(p) { return p.x; }
sink = function (p) { return p.x + 1; };
function opaque() {
    var o = { x: 1 };
    return sink(o);
}
var a = opaque();

//--- escapes.js
function escapes() {
    var o = { x: 1 };
    var take = function (p) { return p; };
    return take(o).x;
}
var a = escapes();

//--- unread.js
function unread() {
    var o = { x: 1 };
    var take = function (p) { return 5; };
    return take(o) + o.x;
}
var a = unread();

//--- mixed.js
function mixed() {
    var o = { x: 1 };
    var take = function (p) { return p.x; };
    return take(o) + take(2);
}
var a = mixed();

//--- otheropen.js
function otheropen() {
    var o = { x: 1 };
    var bad = { x: 2 };
    var take = function (p) { return p.x; };
    var r = take(o) + take(bad);
    return r + bad[String(1)];
}
var a = otheropen();

//--- census.js
function sink(p) { return p.x; }
sink = function (p) { return p.x + 1; };
function blocked() {
    var o = { x: 1, bump: function (n) { return this.x + n; } };
    return sink(o) + o.bump(2);
}
var a = blocked();
