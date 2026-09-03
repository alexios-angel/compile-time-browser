// THE CALL-GRAPH FIXPOINT'S TWO DIAGNOSTICS, WHICH NOTHING PINNED.
//
// The lowering admits each function on its own merits and then CLOSES THE SET
// OVER THE CALL GRAPH, in both directions: a native caller needs a native
// callee to emit an emitc.call to, and a native callee needs every caller
// native too, because a refused caller keeps a ctjs.call_direct that has to
// name a ctjs.func with a body - which a lowered function no longer is.
//
// THE FIXPOINT IS THE ONE PLACE THAT OVERWRITES A REASON `admission` ALREADY
// WROTE. Everywhere else in the pass the first refusal wins and is kept
// (`admission::refuse` returns early when `why` is non-empty); here a function
// that was ACCEPTED gets a reason for the first time, and a function whose
// caller is refused has its own precise reason replaced by this vague one. So
// a change in this loop can silently turn "field `class` is a C++ keyword..."
// into "called by `f$3`, which is not native" on every function in a bundle,
// and no gate in the tree would have said a word. These two strings are the
// only thing standing under that.
//
// ONE PROGRAM PER FILE, and see the header of shape-field-names.mlir for why
// that is not tidiness: a refusal is contagious in BOTH directions - which is
// the entire subject here - so the two directions cannot share a module.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/calls.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CALLS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/calledby.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CALLEDBY

// --- FORWARDS: a refused callee refuses its caller --------------------------
//
// `bad` reads `this`, which has no native carrier, and is refused STRUCTURALLY
// - no types needed, so the diagnostic does not move when inference does.
// `good` passes admission on its own: every argument it passes is a number and
// the value it gets back is one. It is the fixpoint that drops it, and the
// reason NAMES THE CALLEE. Then the same step reaches the top level, which
// calls `good`, so the reason there names `good` and not `bad` - one hop at a
// time, which is what makes the roadmap readable.
//
// CALLS: ctjs.func {{.*}}@_script_$0
// CALLS-SAME: ctnative.not_native = "calls `good$2`, which is not native"
// CALLS: ctjs.func {{.*}}@bad$1
// CALLS-SAME: ctnative.not_native = "uses `this`"
// CALLS: ctjs.func {{.*}}@good$2
// CALLS-SAME: ctnative.not_native = "calls `bad$1`, which is not native"

// --- BACKWARDS: a refused caller refuses its callee -------------------------
//
// `helper` is native by every rule the admission check has: one numeric
// parameter a caller proves, one multiplication, one numeric return. It is
// refused anyway, because the top level that calls it is not - a string
// constant it cannot carry - and a refused caller keeps a ctjs.call_direct
// that must still find a ctjs.func with a body.
//
// THIS IS THE DIRECTION THAT SURPRISES PEOPLE, and it is the one that makes a
// single unsupported construct at the top level refuse an entire program. If
// this line ever goes green with `helper` lowered, the module is inconsistent
// rather than improved.
//
// CALLEDBY: ctjs.func {{.*}}@_script_$0
// CALLEDBY-SAME: ctnative.not_native = "a constant that is not a number, a boolean or undefined"
// CALLEDBY: ctjs.func {{.*}}@helper$1
// CALLEDBY-SAME: ctnative.not_native = "called by `_script_$0`, which is not native"

//--- calls.js
function bad(x) { return this ? x * 2 : x * 4; }
function good(x) { return bad(x) + 1; }
var r = good(3);

//--- calledby.js
function helper(x) { return x * 2; }
var t = "text";
var r = helper(3);
