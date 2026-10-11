// Data-record refusals: one program for each use of a returned object literal
// that could observe it is a copy. TypeInference::markRecordReturns
// (lib/CTNative/Analysis/Inference/Records.cpp) leaves the function unmarked,
// so its literal is refused as an escape and the refusal spreads to the
// caller. The accepted records are run end to end in
// ../../Fixtures/Objects/records.js.
//
// `===` needs no control here: an object whose reference is compared gets a
// run-time identity (`std::shared_ptr<ctnative::identity_object>`), and the
// record rule never takes an object with one, so it is not copied.
//
// Each program is its own file under split-file: one refusal anywhere refuses
// the whole call graph, so two clauses in one program would test neither.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/global.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=GLOBAL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STORED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/kept.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=KEPT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/argument.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARGUMENT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/joined.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=JOINED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic-key.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DYNAMIC

// --- A global outlives the call; the copy would be a second object ---------
// GLOBAL: ctnative.not_native = "an object literal that escapes - it is returned"

// --- Stored into another object, so two holders share it ------------------
// STORED: ctnative.not_native = "an object literal that escapes - it is returned"
// STORED: ctnative.not_native = "field `r` is stored a !ctnative.boxed, not a number or a boolean"

// --- Lent to a parameter that keeps it -------------------------------------
// The parameter is stored into a global, so it is no record parameter, and the
// call that lends to it is a use the record rule does not take.
// KEPT: ctnative.not_native = "an object literal that escapes - it is returned"

// --- Lent to a parameter that returns it ----------------------------------
// The callee hands the reference back, so caller and result would be two
// copies of one object.
// ARGUMENT: ctnative.not_native = "an object literal that escapes - it is returned"

// --- Two literals joined by control flow -----------------------------------
// One `return` per branch reaches the function's single return through an
// scf.if, so each literal is a yielded value, not a returned one. The record
// rule takes only a literal the return names directly.
// JOINED: ctnative.not_native = "an object literal that is loop-carried - more than one value reaches the variable that holds it (assigned again inside a loop, or on only one path before it)"

// --- A dynamic key could name any field, or one nothing wrote --------------
// DYNAMIC: ctnative.not_native = "an object literal that escapes - it is returned"

//--- global.js
function make() {
    return {
        v: 1
    };
}
var kept = make();
var answer = kept.v;

//--- stored.js
function make() {
    return {
        v: 1
    };
}

function store() {
    var holder = {
        r: 0
    };
    holder.r = make();
    return 1;
}
var answer = store();

//--- kept.js
function make() {
    return {
        v: 1
    };
}
var keep = 0;

function stash(o) {
    keep = o;
    return o.v;
}

function lend() {
    return stash(make());
}
var answer = lend();

//--- argument.js
function make() {
    return {
        v: 1
    };
}

function same(o) {
    return o;
}

function pass() {
    return same(make()).v;
}
var answer = pass();

//--- dynamic-key.js
function make() {
    return {
        v: 1
    };
}

function lookup(k) {
    var a = make();
    return a[k];
}
var answer = lookup("v");

//--- joined.js
function pick(left) {
    if (left) {
        return {
            v: 3
        };
    }
    return {
        v: 4
    };
}

function both() {
    return pick(true).v + pick(false).v;
}
var answer = both();
