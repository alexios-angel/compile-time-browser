// Object literals passed as call arguments lower to pointer parameters.
//
// The receiver lift lowers a closed-shape literal to a `ctn_x *` parameter
// and its fields to `self->x`. Nothing in that lowering depends on operand 0,
// so an argument gets it on the same proof: `read(o)` lowers to
// `fn_N(&o)` with `!emitc.ptr<!emitc.opaque<"ctn_x">>` for a parameter and
// `emitc.member_of_ptr` for every field, which is the receiver's own lowering
// with a different operand number.
//
// This also matters for `this`. If an argument use made a literal's shape
// open (`closedAfterLift` in ClosureLifting/Methods.cpp decides), a method
// table that was also passed anywhere would have every method refused with
// "a method field of an object whose shape is not closed". `both()` is that
// program, and both its method and its call must lift. The census in
// refusals.mlir counts how often that shape occurs in real programs.
//
//
// The signatures are the assertion. With a wrong index the program still
// verifies, compiles cleanly under -Werror and prints a wrong number, so the
// parameter lists are pinned here and the answers are pinned by the native
// fixture test over test/CTNative/Fixtures/Objects/argument.js.
//
// One program per file, and the functions are appended in source order: the
// importer's `$N` suffix is its position in `program::functions`, so inserting
// a function in the middle renumbers every symbol below it. The checks below
// match the signature rather than the number for that reason.
//
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --implicit-check-not=ctnative.not_native \
// RUN:                  --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              '--ctnative-lower-to-emitc=report=1' -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REPORT

function passed() {
    var o = { x: 4 };
    var read = function (p) { return p.x * 3; };
    return read(o);
}
function both() {
    var o = { x: 4, bump: function (n) { return this.x + n; } };
    var read = function (p) { return p.x * 3; };
    return o.bump(2) + read(o);
}
function two_arguments() {
    var left = { a: 3 };
    var right = { b: 4 };
    var join = function (p, q) { return p.a + q.b; };
    return join(left, right);
}
function mixed() {
    var k = { k: 6 };
    var mix = function (n, p) { return n * p.k + 2; };
    return mix(4, k);
}
var p1 = passed();
var b1 = both();
var t1 = two_arguments();
var m1 = mixed();

// --- The classes -------------------------------------------------------------
//
// `{x: 4, bump: <fn>}` in `both` shares the exact `ctn_x` class with
// `{x: 4}` in `passed`. Complete callable-use and initialization checks prove
// double storage for both. The erased method adds no runtime member.
// CHECK:      emitc.class @ctn_x
// CHECK-SAME: (2 sites)
// CHECK-NEXT:   emitc.field @x : !emitc.opaque<"ctnative::js_num">
// CHECK-NEXT: }
// CHECK:      emitc.class @ctn_a
// CHECK-NEXT:   emitc.field @a : !emitc.opaque<"ctnative::js_num">
// CHECK-NEXT: }
// CHECK:      emitc.class @ctn_b
// CHECK-NEXT:   emitc.field @b : !emitc.opaque<"ctnative::js_num">
// CHECK-NEXT: }
// CHECK:      emitc.class @ctn_k
// CHECK-NEXT:   emitc.field @k : !emitc.opaque<"ctnative::js_num">
// CHECK-NEXT: }

// --- The address at the call site, and the signature it feeds ----------------
//
// The literal is an `emitc.variable` - an lvalue in the caller's frame - and
// the callee takes a pointer, so the site takes its address. It is the same
// `emitc.address_of` the receiver uses, through the same lambda, which is what
// stops the two drifting into two spellings of one thing.
//
// One object parameter, alone: `double fn_2(ctn_x *)`.
// CHECK:      emitc.func @passed_1
// CHECK:        address_of %{{[0-9]+}} : !emitc.lvalue<!emitc.opaque<"ctn_x">>
// CHECK:      emitc.func @fn_2(%arg0: !emitc.ptr<!emitc.opaque<"ctn_x">>) -> !emitc.opaque<"ctnative::js_num">
// CHECK:        emitc.member_of_ptr

// --- The receiver and an argument, on one literal ----------------------------
//
// `both` is the program this rule is for. Two addresses of the same
// variable: `bump` takes the pointer at operand 0 because a receiver is
// `%arg0`, `read` takes it at operand 3 because that is where the parameter is.
// Two indices, one carrier, one class. The argument use must not open the
// shape, or the method field would be refused by name.
//
//
// CHECK:      emitc.func @both_3
// CHECK:        address_of %[[OBJ:[0-9]+]] : !emitc.lvalue<!emitc.opaque<"ctn_x">>
// CHECK:        address_of %[[OBJ]] : !emitc.lvalue<!emitc.opaque<"ctn_x">>
// CHECK:      emitc.func @fn_4(%arg0: !emitc.ptr<!emitc.opaque<"ctn_x">>, %arg1: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK:      emitc.func @fn_5(%arg0: !emitc.ptr<!emitc.opaque<"ctn_x">>) -> !emitc.opaque<"ctnative::js_num">

// --- Two object parameters of two shapes -------------------------------------
//
// The lift writes a list of indices rather than a bit for this, and the alias
// groups keep the two apart: joining them would give both the union of the
// shapes and a class with a field neither literal has.
//
// CHECK:      emitc.func @two_arguments_6
// CHECK:        address_of %{{[0-9]+}} : !emitc.lvalue<!emitc.opaque<"ctn_a">>
// CHECK:        address_of %{{[0-9]+}} : !emitc.lvalue<!emitc.opaque<"ctn_b">>
// CHECK:      emitc.func @fn_7(%arg0: !emitc.ptr<!emitc.opaque<"ctn_a">>, %arg1: !emitc.ptr<!emitc.opaque<"ctn_b">>) -> !emitc.opaque<"ctnative::js_num">

// --- An object that is not the first argument --------------------------------
//
// Next to a number, and second. The index the lift writes is the entry-block
// index, so a mixed parameter list still lines up: `fn_9(double, ctn_k *)`.
//
// CHECK:      emitc.func @mixed_8
// CHECK:        address_of %{{[0-9]+}} : !emitc.lvalue<!emitc.opaque<"ctn_k">>
// CHECK:      emitc.func @fn_9(%arg0: !emitc.opaque<"ctnative::js_num">, %arg1: !emitc.ptr<!emitc.opaque<"ctn_k">>) -> !emitc.opaque<"ctnative::js_num">

// --- The counts --------------------------------------------------------------
//
// Pass statistics are compiled out of the release LLVM this builds against, so
// the counter is printed and asserted. Five object
// parameters: one in `passed`, one in `both`, two in `two_arguments`, one in
// `mixed`.
//
// REPORT: ctnative: lifted 5 closure(s) over 0 capture(s) into 5 function(s), rewrote 5 call(s), unboxed 0 cell(s), 1 method(s) of which 1 take a receiver, 5 object parameter(s)
