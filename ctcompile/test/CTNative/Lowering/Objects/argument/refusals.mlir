// What the object-argument rule refuses, one program per failed condition.
//
// The rule admits an object literal passed as an argument only when (1) the
// callee is one known function, (2) every call passes that parameter an object
// literal, and (3) the callee reads the parameter, and only through constant
// keys. Each condition has a failing program here, plus the fixpoint condition 2
// needs. Anything unproved must be a compile-time diagnostic, so each says why.
//
// The failed condition is a property of the callee (which parameter, read how,
// called from where), which the use-list walk that meets the escape cannot
// see. So `argumentCensus` writes the specific reason onto the literal as
// `ctnative.object_reason`, the same idiom as `ctnative.closure_reason` and
// `ctnative.cell_reason`. A generic "passed to a call" message would be true
// of all five programs and would not say which condition failed for this
// one.
//
// Four of the five refusals land on the ctjs.call_direct arm, not the ctjs.call
// one. A callee whose parameter this rule refused is still a closure the
// plain lift takes (its uses are all calls of it), so by the time admission
// asks, the call has already become a ctjs.call_direct. Only `opaque`, whose
// callee is a global with two stores and so never resolves, still has a
// ctjs.call at that point. Both arms are covered here for that reason.
//
// One program per file, because a refusal is contagious in both directions (a
// refused callee refuses its caller and a refused caller refuses its callee),
// so a file that pinned an acceptance and a refusal together would pin neither.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/opaque.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=OPAQUE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escapes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=ESCAPES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unread.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=UNREAD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/otheropen.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=OTHEROPEN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/otherkey.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/otherkey.mlir
// RUN: FileCheck %s --check-prefix=OTHERKEY --implicit-check-not=ctnative.not_native < %t/otherkey.mlir
// RUN: ctjs-opt %t/otherkey.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/otherkey.cpp
// RUN: %gxx -O2 %t/otherkey.cpp -o %t/otherkey-gcc
// RUN: %t/otherkey-gcc | FileCheck %s --check-prefix=OTHERKEYRUN
// RUN: %clangxx -O2 %t/otherkey.cpp -o %t/otherkey-clang
// RUN: %t/otherkey-clang | FileCheck %s --check-prefix=OTHERKEYRUN
// RUN: %native_reference %t/otherkey.js | FileCheck %s --check-prefix=OTHERKEYRUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/otherkey.js | FileCheck %s --check-prefix=OTHERKEYRUN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/compared.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=COMPARED --implicit-check-not=strict-comparison
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/presence.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=PRESENCE --implicit-check-not=strict-comparison
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/identity.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=IDENTITY
// The census is a measurement the option reports, not a rule. It is asserted
// so its numbers stay right: this program has one method-bearing literal and
// one blocking use, and the label says which.
//
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/census.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              '--ctnative-lower-to-emitc=census=1 observe-globals=true' -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CENSUS
// The same census over a call the closed-world pass already resolved.
// --ctjs-resolve-globals rewrites a call whose callee is a create_closure into
// a ctjs.call_direct; the census label must still name the reason (callee
// known vs opaque), not an undifferentiated `call_direct.argument`.
//
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/census-named.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              '--ctnative-lower-to-emitc=census=1 observe-globals=true' -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CENSUSNAMED

// --- Condition 1: the callee is not one function -----------------------------
//
// `sink` is a global with two stores, so --ctjs-resolve-globals leaves the call
// dispatching through a `load_global` and the lift has no callee to give the
// address to.
//
// This is the common shape in real programs. `--ctnative-lower-to-emitc=census=1`
// over the bootstrap, p5 and phaser workloads finds 994 method-bearing
// literals refused as open, and all 53 argument uses among their blocking uses
// have an opaque callee, with `call.argument.parameter-is-read-only` and
// `call_direct.argument` both at zero.
// 51 of those literals are blocked by nothing else.
// So this rule admits none of them there; this fixture is what shows it
// works.
//
// OPAQUE: ctjs.func {{.*}}@opaque$2
// OPAQUE-SAME: ctnative.not_native = "an object literal that escapes - it is passed to a call whose callee is not one function this rewrite can name, so there is no parameter to give the object's address to"

// --- Condition 3: the parameter is returned, not only read -------------------
//
// `return p` hands the pointer back, and the pointer names the caller's frame.
// The same lifetime question the receiver refuses when a method returns `this`,
// reached through an argument: there is no owner to introduce here (no
// unique_ptr, no shared_ptr, no allocation), so the answer is a refusal.
//
// ESCAPES: ctjs.func {{.*}}@escapes$1
// ESCAPES-SAME: ctnative.not_native = "an object literal that escapes - it is passed to a parameter that reaches it through something other than a constant key - that needs an owner, and this slice introduces none"

// --- Condition 3: the parameter is never read --------------------------------
//
// An object parameter nothing reads still has to be spelled in the signature,
// and `double take_N(ctn_x *)` with no use of it trips -Wunused-parameter;
// generated C++ is compiled with -Werror, so any warning is a ctcompile bug. The
// receiver clause makes the same decision the other way round: the lift marks
// `ctnative.receiver` only when `%arg0` is read.
//
// UNREAD: ctjs.func {{.*}}@unread$1
// UNREAD-SAME: ctnative.not_native = "an object literal that escapes - it is passed to a parameter nothing reads, and an object parameter that is never read is `-Wunused-parameter` in the generated C++"

// --- Condition 2: a literal at one call, a number at another -----------------
//
// One parameter is one C++ type. `take(o)` and `take(2)` want `ctn_x *` and
// `double` for one position, and there is no spelling for both. A template
// would need per-call-site specialisation of the callee, which this rewrite
// does not attempt.
//
// MIXED: ctjs.func {{.*}}@mixed$1
// MIXED-SAME: ctnative.not_native = "an object literal that escapes - it is passed to a parameter that is an object literal at this call and something else at another, so the parameter has no single C++ type"

// --- Condition 2, the fixpoint: the other literal is open --------------------
//
// This is the fixpoint's own test, and the reason `argumentCensus` is a loop
// rather than a single pass. `o` and `bad` are both `{x}` and both passed to the same
// read-only parameter, so on the first round the slot is a candidate and both
// of them read as closed: each one's argument use excused by the slot, the
// slot excused by the two of them. `bad` is then reached through a dynamic key,
// which nothing about the slot can fix, so the slot is dropped and `o`'s
// argument use stops being excused with it.
//
// What the loop buys, as measured. The expected failure without it was a
// crash: admit `o`, give the parameter a `ctn_x *`, then ask `shapeAt`
// for a literal admission had refused. It is not: deleting the loop leaves the
// program refused, with `field `` is not a C identifier`, because the dynamic
// key on `bad` joins an empty field name into the group the two now share. So
// the loop buys the message, exactly as the receiver lift's second ask of
// `this` does, and the correctness backstop is the call-site arm of admission,
// which asks `isClosedObject` of every object operand again.
//
// `bad` is also kept in an array, so it is not a table either (Inference/
// Dictionaries.cpp declines it, and with it `o`, which shares the parameter).
// Written under a computed key and otherwise used only as a table, the pair
// lowers to dictionaries instead (`otherkey`).
//
// OTHEROPEN: ctjs.func {{.*}}@otheropen$1
// OTHEROPEN-SAME: ctnative.not_native = "an object literal that escapes - it is passed to a parameter whose other object literal is not itself a closed shape, so the two would not agree on one class"
// OTHERKEY: emitc.func @otherkey
// OTHERKEYRUN: r=35

// --- The reason a compared literal reports -----------------------------------
//
// The object identity proof (NativeObjectIdentity.cpp) runs after this rule
// and also writes `ctnative.object_reason`, on every literal of a family that
// `===` compares. It writes its reason only where it describes the literal.
// `compared` passes `o` to a callee nobody can name and compares it with
// another object: the census's reason for the argument stays (the field-less
// `other` is a proved identity). `presence` only tests `o === undefined` (a
// read of the global) and `o === null` (a constant), which observe no
// identity, so the proof writes nothing. `identity` compares
// two objects and passes `o` to no call, so the proof's own reason is still
// the one `o` reports.
//
// COMPARED: ctjs.create_object {ctnative.object_reason = "it is passed to a call whose callee is not one function this rewrite can name, so there is no parameter to give the object's address to"
// COMPARED: ctjs.create_object {ctnative.object_identity
// PRESENCE: ctjs.func {{.*}}@presence$1
// PRESENCE-SAME: ctnative.not_native = "an object literal that escapes - it reaches `ctjs.compare`"
// PRESENCE: ctjs.create_object {ctnative.symbolic_results
// IDENTITY: ctjs.create_object {ctnative.object_reason = "strict-comparison object has an unsupported use through `ctjs.store_global`"
// IDENTITY: ctjs.create_object {ctnative.object_identity

// --- The census, as a remark -------------------------------------------------
//
// `census.js` is the shape the object-argument rule targets, with the argument
// use made unliftable so that it stays refused: a method-bearing literal whose only
// blocking use is an argument to a call this rewrite cannot name. That is the
// bucket real programs are full of, and this asserts that the census names it:
// one literal, one method field, one blocking use, `call.argument.callee-opaque`
// as the sole blocker and as the root.
//
// CENSUS: ctnative census: 1 method-bearing literal(s) refused as open, holding 1 method field(s); blocking uses: call.argument.callee-opaque=1; sole blocker: call.argument.callee-opaque=1; sole blocker by method field: call.argument.callee-opaque=1; root of the nesting: call.argument.callee-opaque=1

// The named site keeps its bucket. The IIFE's callee is a create_closure,
// so --ctjs-resolve-globals turns the call into a ctjs.call_direct and the
// literal's blocking use moves from operand 2 of a ctjs.call to operand 3 of a
// ctjs.call_direct. The label must still say why (the parameter escapes,
// because it is returned), not merely that the operation was a direct call.
// Without `blockingLabel`'s call_direct arm this reads
// `call_direct.argument=1`, and the callee-known / callee-opaque distinction
// the census exists to draw is gone for every site the resolver names.
//
// CENSUSNAMED: ctnative census: 1 method-bearing literal(s) refused as open, holding 1 method field(s); blocking uses: call.argument.parameter-escapes=1; sole blocker: call.argument.parameter-escapes=1; sole blocker by method field: call.argument.parameter-escapes=1; root of the nesting: call.argument.parameter-escapes=1

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
    var keep = [bad];
    var take = function (p) { return p.x; };
    var r = take(o) + take(bad);
    return r + bad[String(1)] + keep.length;
}
var a = otheropen();

//--- otherkey.js
function otherkey(k) {
    var o = { x: 1 };
    var bad = { x: 2 };
    var take = function (p) { return p.x; };
    var r = take(o) + take(bad);
    bad[k] = 5;
    return r * 10 + bad.y;
}
var r = otherkey("y");

//--- compared.js
function sink(p) { return p.x; }
sink = function (p) { return p.x + 1; };
function compared() {
    var o = { x: 1 };
    var other = {};
    return sink(o) + (o === other ? 1 : 0);
}
var a = compared();

//--- presence.js
var kept;
function presence() {
    var o = { x: 1 };
    kept = o;
    return o === undefined || o === null ? 0 : 1;
}
var a = presence();

//--- identity.js
var kept;
function identity() {
    var o = { x: 1 };
    var other = { x: 2 };
    kept = o;
    return o === other ? 1 : 0;
}
var a = identity();

//--- census.js
function sink(p) { return p.x; }
sink = function (p) { return p.x + 1; };
function blocked() {
    var o = { x: 1, bump: function (n) { return this.x + n; } };
    return sink(o) + o.bump(2);
}
var a = blocked();

//--- census-named.js
// The callee is the closure two tokens back, so the closed world names this
// call. `p` is returned, so the slot is not a read-only object parameter and
// the literal stays open, which is what puts it in the census at all.
function blockednamed() {
    var o = { x: 1, bump: function (n) { return this.x + n; } };
    var kept = (function (p) { return p; })(o);
    return kept.x + o.bump(2);
}
var b = blockednamed();
