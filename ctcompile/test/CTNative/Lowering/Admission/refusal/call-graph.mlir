// Pins the two diagnostics of the call-graph fixpoint in LowerToEmitC.cpp.
//
// The lowering admits each function on its own merits and then closes the set
// over the call graph, in both directions: a native caller needs a native
// callee to emit an emitc.call to, and a native callee needs every caller
// native too, because a refused caller keeps a ctjs.call_direct that has to
// name a ctjs.func with a body, which a lowered function no longer is. The
// exception is a static member that the class statics rewrite turned into a
// direct call, when it takes no parameters and reads neither new.target nor
// its own closure: a refused caller calls it through a closure instead, so it
// stays native (STATIC and RESTORED below). Any other direct call keeps the
// rule (LEAF).
//
// The fixpoint is the one place that overwrites a reason admission already
// wrote. Everywhere else in the pass the first refusal wins and is kept
// (`admission::refuse` returns early when `why` is non-empty); here a function
// that was accepted gets a reason for the first time, and a function whose
// caller is refused has its own precise reason replaced by this vague one. So
// a change in this loop could silently turn "field `class` is a C++ keyword..."
// into "called by `f$3`, which is not native" on every function in a bundle;
// these two strings are the only test that would notice.
//
// One program per split-file input (see the header of
// Objects/shape-field-names.mlir): a refusal spreads in both directions, which
// is the subject here, so the two directions cannot share a module, or one
// direction's refusal would mask the other's.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unsupported-calledby.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=CALLEDBY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/calls.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=CALLS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/calledby.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=STRING
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/absent-calledby.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=ABSENT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/leaf-calledby.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=LEAF
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/static-leaf.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=STATIC --implicit-check-not='ctjs.call_direct @fn$3'
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/static-restored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true \
// RUN:   | FileCheck %s --check-prefix=RESTORED --implicit-check-not='ctjs.call_direct @fn$3'

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed-calledby.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc="optimize=false observe-globals=true" \
// RUN:   | FileCheck %s --check-prefix=MIXED --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func

// MIXED: emitc.global static @g_t : !emitc.opaque<"ctnative::nullable_number_string">
// MIXED: emitc.func @main
// MIXED: emitc.func @helper_1

// --- Forwards: a refused callee refuses its caller --------------------------
//
// `bad` reads `this`, which has no native carrier, and is refused structurally:
// no types are needed, so the diagnostic does not move when inference does.
// `good` passes admission on its own: every argument it passes is a number and
// the value it gets back is one. The fixpoint drops it, and the reason names
// the callee. The same step then reaches the top level, which calls `good`, so
// its reason names `good`, not `bad`: each reason points one hop closer to the
// root cause.
//
// CALLS: ctjs.func {{.*}}@_script_$0
// CALLS-SAME: ctnative.not_native = "calls `good$2`, which is not native"
// CALLS: ctjs.func {{.*}}@bad$1
// CALLS-SAME: ctnative.not_native = "uses `this`"
// CALLS: ctjs.func {{.*}}@good$2
// CALLS-SAME: ctnative.not_native = "calls `bad$1`, which is not native"

// A caller that stores a nullable global is still native, so its callee is
// too. The BigInt case (CALLEDBY) is the one that checks backwards refusal
// through the same helper call.
// ABSENT-NOT: ctnative.not_native
// ABSENT: emitc.func @main
// ABSENT: call_opaque "ctnative::print_scalar"
// ABSENT: emitc.func @helper_1
// ABSENT-NOT: ctnative.not_native

// A String global is supported: both functions lower and the global prints.
// STRING: emitc.func @main
// STRING: call_opaque "ctnative::global_string"
// STRING: emitc.func @helper_1
// STRING-NOT: ctnative.not_native

// CALLEDBY: ctjs.func {{.*}}@_script_$0
// CALLEDBY-SAME: ctnative.not_native = "store to global `t` has inconsistent global observation types"
// CALLEDBY: ctjs.func {{.*}}@helper$1
// CALLEDBY-SAME: ctnative.not_native = "called by `_script_$0`, which is not native"

// --- Backwards, except to a static member ----------------------------------
//
// The CALLEDBY program with a `leaf` that takes no parameters: the top level's
// call of it is one the closed world made, so the rule holds and `leaf` is
// refused with its caller.
//
// LEAF: ctjs.func {{.*}}@_script_$0
// LEAF-SAME: ctnative.not_native = "store to global `t` has inconsistent global observation types"
// LEAF: ctjs.func private @leaf$1
// LEAF-SAME: ctnative.not_native = "called by `_script_$0`, which is not native"

// A static getter read as `this.constructor.SIDE` in a method, which the class
// statics rewrite turns into a direct call of the getter (fn$3). `run` and
// what it calls are refused with the top level, but the getter keeps its
// native body: the refused method calls it through a closure of it, which
// names its function by index, not by symbol, as it reached the getter
// through the class before the rewrite. No direct call of it is left.
// STATIC: ctjs.func private @run$1
// STATIC-SAME: ctnative.not_native = "called by `_script_$0`, which is not native"
// STATIC: emitc.func @fn_3()
// STATIC: ctjs.func private @size$4
// STATIC-SAME: ctnative.not_native = "called by `run$1`, which is not native"
// STATIC: ctjs.create_closure %arg2[3] this

// A refused method whose body the lowering replaced (here the for-of folded
// to its index loop, ForOf.cpp) gets its own body back after the fixpoint.
// That body calls the getter by symbol again, so it is given the same call
// through a closure; otherwise a ctjs.call_direct would name a function that
// has been lowered.
// RESTORED: emitc.func @fn_3()
// RESTORED: ctjs.func private @total$4
// RESTORED: ctjs.create_closure %arg2[3] this

//--- calls.js
function bad(x) { return this ? x * 2 : x * 4; }
function good(x) { return bad(x) + 1; }
var r = good(3);

//--- calledby.js
function helper(x) { return x * 2; }
var t = "text";
var r = helper(3);

//--- absent-calledby.js
function helper(x) { return x * 2; }
var t = null;
var r = helper(3);

//--- mixed-calledby.js
function helper(x) { return x * 2; }
var t = "text";
t = 1;
var r = helper(3);

//--- unsupported-calledby.js
function helper(x) { return x * 2; }
var t = "text";
t = 1n;
var r = helper(3);

//--- leaf-calledby.js
function leaf() { return 2; }
var t = "text";
t = 1n;
var r = leaf();

//--- static-leaf.js
function run() {
    class Tag {
        constructor(v) { this.v = v; }
        static get SIDE() { return 2; }
        size() { return this.v * this.constructor.SIDE; }
    }
    return new Tag(3).size();
}
var t = "text";
t = 1n;
var r = run();

//--- static-restored.js
function run() {
    class Tag {
        constructor(xs) { this.xs = xs; }
        static get SIDE() { return 2; }
        total() {
            var s = 0;
            for (const x of this.xs) { s = s + x * this.constructor.SIDE; }
            return s;
        }
    }
    return new Tag([1, 2]).total();
}
var t = "text";
t = 1n;
var r = run();
