// THE RECEIVER IS A PARAMETER - part 24, as IR.
//
// Three method tables, which is the shape the tier refused whole before this
// slice: `uses \`this\`` was 6,649 of the refusals it makes over bootstrap, p5
// and phaser, and the blocker was never that the callee is unknown. It is that
// no C++ type carried a receiver - `carrierOf` had a `structure` row nothing
// could produce, `ctjs.call_direct` dropped operand 0, and the one object
// carrier was a closed-shape struct BY VALUE, refused the moment it was
// returned, stored or passed.
//
// THE SIGNATURES ARE THE ASSERTION, exactly as they are for a lifted closure.
// `bump` is written with one parameter and lowers to `fn_2(ctn_x *, double)`
// with the receiver FIRST; get the position wrong and the program still
// compiles and prints a wrong number, which is why the parameter lists are
// pinned here and the answers are pinned by the differential gate.
//
// AND SO IS THE CLASS. `{x: 1, bump: <fn>}` has the shape `{x}` and NOT
// `{bump, x}`: the method is a free function, the field holds an identity
// condition 2 proved single, and it takes no storage. That is what lets the two
// `{base, plus}` literals in `shared` be ONE class - they are the same DATA
// shape - while `plus` is one lifted function taking a pointer to it.
//
// ONE PROGRAM PER FILE, and the functions are appended in source order: the
// importer's `$N` suffix is its position in `program::functions`, so inserting
// a function in the middle renumbers every symbol below it.
//
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --implicit-check-not=ctnative.not_native \
// RUN:                  --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              '--ctnative-lower-to-emitc=report=1' -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REPORT

function read_only() {
    var p = { x: 6, bump: function (n) { return this.x + n; } };
    return p.bump(4);
}
function mutating() {
    var c = { n: 1, step: function () { this.n = this.n * 3; } };
    c.step();
    return c.n;
}
function shared() {
    var add = function (k) { return this.base + k; };
    var a = { base: 100, plus: add };
    var b = { base: 200, plus: add };
    return a.plus(1) + b.plus(2);
}
var r = read_only();
var m = mutating();
var s = shared();

// --- THE METHOD FIELD IS NOT A MEMBER --------------------------------------
//
// CHECK-NEXT is what makes this an assertion rather than a hope: it pins the
// class to exactly one field, so a `bump` member - a field of a type this tier
// has no carrier for - would fail the line after.
//
// CHECK:      emitc.class @ctn_x
// CHECK-NEXT:   emitc.field @x : f64
// CHECK-NEXT: }
// CHECK:      emitc.class @ctn_n
// CHECK-NEXT:   emitc.field @n : f64
// CHECK-NEXT: }

// --- TWO LITERALS, ONE CLASS ------------------------------------------------
//
// The provenance names both sites, which is Phase 56C's rule reaching the case
// it was written for: the same DATA shape at two places is one definition. If a
// method identity were part of the shape key these would be two classes, and
// `plus` could not take one pointer type.
//
// CHECK:      emitc.class @ctn_base attributes {ctnative.provenance = "object literal at {{[^"]*}}, {{[^"]*}} (2 sites)"}
// CHECK-NEXT:   emitc.field @base : f64
// CHECK-NEXT: }

// --- EVERY FUNCTION IS CLAIMED ----------------------------------------------
//
// The two --implicit-check-not options above are the whole-file half of this: a
// `ctjs.func` anywhere in the output is a function the lowering left behind,
// and a `ctnative.not_native` anywhere is a refusal. Neither may appear.
//
// CHECK: emitc.func @main() -> i32

// --- A READ-ONLY METHOD -----------------------------------------------------
//
// The caller takes the address of its own frame slot and passes it first; the
// method copies the pointer into one lvalue local - which is what
// `emitc.member_of_ptr` needs, because a parameter is not an lvalue and
// `emitc.func` rejects an lvalue argument type outright - and reads `x`
// through it.
//
// CHECK: emitc.func @read_only_1() -> f64
// CHECK: %[[P:.*]] = "emitc.variable"{{.*}} -> !emitc.lvalue<!emitc.opaque<"ctn_x">>
// CHECK: %[[ADDR:.*]] = address_of %[[P]] : !emitc.lvalue<!emitc.opaque<"ctn_x">>
// CHECK: call @fn_2(%[[ADDR]], %{{.*}}) : (!emitc.ptr<!emitc.opaque<"ctn_x">>, f64) -> f64

// CHECK: emitc.func @fn_2(%arg0: !emitc.ptr<!emitc.opaque<"ctn_x">>, %arg1: f64) -> f64
// CHECK: %[[SELF:.*]] = "emitc.variable"{{.*}} -> !emitc.lvalue<!emitc.ptr<!emitc.opaque<"ctn_x">>>
// CHECK: assign %arg0 : !emitc.ptr<!emitc.opaque<"ctn_x">> to %[[SELF]]
// CHECK: %[[X:.*]] = "emitc.member_of_ptr"(%[[SELF]]) <{member = "x"}>
// CHECK: load %[[X]] : <f64>

// --- A MUTATING METHOD ------------------------------------------------------
//
// `this.n = this.n * 3` is a member_of_ptr on the LEFT of an assign, which is
// the whole reason the receiver is a pointer rather than a value: EmitC's only
// member access that yields an lvalue through a parameter is this one. The
// caller reads `c.n` after the call and gets what the method wrote.
//
// CHECK: emitc.func @mutating_3() -> f64
// CHECK: call @fn_4(%{{.*}}) : (!emitc.ptr<!emitc.opaque<"ctn_n">>) -> f64
// CHECK: emitc.func @fn_4(%arg0: !emitc.ptr<!emitc.opaque<"ctn_n">>) -> f64
// CHECK: %[[NREAD:.*]] = "emitc.member_of_ptr"(%{{.*}}) <{member = "n"}>
// CHECK: load %[[NREAD]] : <f64>
// CHECK: %[[NWRITE:.*]] = "emitc.member_of_ptr"(%{{.*}}) <{member = "n"}>
// CHECK: assign %{{.*}} : f64 to %[[NWRITE]] : <f64>

// --- TWO OBJECTS SHARING ONE LIFTED METHOD ----------------------------------
//
// Two frame slots, two address_of, ONE function - and its parameter is the
// class both of them are.
//
// CHECK: emitc.func @shared_5() -> f64
// CHECK: %[[A:.*]] = address_of %{{.*}} : !emitc.lvalue<!emitc.opaque<"ctn_base">>
// CHECK: call @fn_6(%[[A]], %{{.*}}) : (!emitc.ptr<!emitc.opaque<"ctn_base">>, f64) -> f64
// CHECK: %[[B:.*]] = address_of %{{.*}} : !emitc.lvalue<!emitc.opaque<"ctn_base">>
// CHECK: call @fn_6(%[[B]], %{{.*}}) : (!emitc.ptr<!emitc.opaque<"ctn_base">>, f64) -> f64
// CHECK: emitc.func @fn_6(%arg0: !emitc.ptr<!emitc.opaque<"ctn_base">>, %arg1: f64) -> f64

// --- THE COUNTERS, WHICH ARE NOT PASS STATISTICS ----------------------------
//
// The release LLVM package this builds against compiles pass statistics out, so
// --mlir-pass-statistics prints an empty report and a test asserting one
// asserts nothing. The `report` option is the escape hatch --ctjs-resolve-globals
// and --ctjs-lift-to-scf already ship, and this is a number a test can pin.
//
// REPORT: remark: ctnative: lifted 3 closure(s) over 0 capture(s) into 3 function(s), rewrote 4 call(s), unboxed 0 cell(s), 3 method(s) of which 3 take a receiver
