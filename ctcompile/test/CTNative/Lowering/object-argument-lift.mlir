// AN ARGUMENT IS A PARAMETER TOO - part 24, as IR.
//
// The receiver lift's content is that a closed-shape literal reaches a function
// as a `ctn_x *` and its fields as `self->x`. NOTHING IN THAT CARRIER IS ABOUT
// OPERAND 0, so an argument gets it on the same proof: `read(o)` lowers to
// `fn_N(&o)` with `!emitc.ptr<!emitc.opaque<"ctn_x">>` for a parameter and
// `emitc.member_of_ptr` for every field, which is the receiver's own lowering
// with a different operand number.
//
// WHY THIS IS A `this` CHANGE AND NOT ONLY AN ARGUMENT ONE. `closedAfterLift`
// refused a literal the moment it appeared in an argument, so a method table
// that was ALSO passed to anything had an open shape - and an open shape
// refused every method on it with "a method field of an object whose shape is
// not closed". `both()` is that program: before this slice neither the method
// nor the call lifted, and the census in object-argument-refusals.mlir is what
// says how often that shape occurs.
//
// THE SIGNATURES ARE THE ASSERTION. Get the index wrong and the program still
// verifies, still compiles clean under -Werror, and prints a wrong number - so
// the parameter lists are pinned here and the answers are pinned by the
// differential gate over native-object-argument-fixture.js.
//
// ONE PROGRAM PER FILE, and the functions are appended in source order: the
// importer's `$N` suffix is its position in `program::functions`, so inserting
// a function in the middle renumbers every symbol below it. The checks below
// match the SIGNATURE rather than the number for that reason.
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

// --- THE CLASSES ------------------------------------------------------------
//
// `{x: 4, bump: <fn>}` in `both` is the SAME `ctn_x` as `{x: 4}` in `passed` -
// two sites, one class. The method field is not part of the shape key, and
// being passed to a function does not add one either. CHECK-NEXT pins each
// class to exactly its data, so a member for the method - or for a pointer, if
// a future carrier ever tried to store one - fails the line after.
//
// CHECK:      emitc.class @ctn_x
// CHECK-SAME: (2 sites)
// CHECK-NEXT:   emitc.field @x : f64
// CHECK-NEXT: }
// CHECK:      emitc.class @ctn_a
// CHECK-NEXT:   emitc.field @a : f64
// CHECK-NEXT: }
// CHECK:      emitc.class @ctn_b
// CHECK-NEXT:   emitc.field @b : f64
// CHECK-NEXT: }
// CHECK:      emitc.class @ctn_k
// CHECK-NEXT:   emitc.field @k : f64
// CHECK-NEXT: }

// --- THE ADDRESS AT THE CALL SITE, AND THE SIGNATURE IT FEEDS ---------------
//
// The literal is an `emitc.variable` - an lvalue in the caller's frame - and
// the callee takes a pointer, so the site takes its address. It is the same
// `emitc.address_of` the receiver uses, through the same lambda, which is what
// stops the two drifting into two spellings of one thing.
//
// ONE OBJECT PARAMETER, ALONE: `double fn_2(ctn_x *)`.
// CHECK:      emitc.func @passed_1
// CHECK:        address_of %{{[0-9]+}} : !emitc.lvalue<!emitc.opaque<"ctn_x">>
// CHECK:      emitc.func @fn_2(%arg0: !emitc.ptr<!emitc.opaque<"ctn_x">>) -> f64
// CHECK:        emitc.member_of_ptr

// --- THE RECEIVER AND AN ARGUMENT, ON ONE LITERAL ---------------------------
//
// `both` is the program the widening exists for. Two addresses of the SAME
// variable: `bump` takes the pointer at operand 0 because a receiver is
// `%arg0`, `read` takes it at operand 3 because that is where the parameter is.
// Two indices, one carrier, one class - and before this slice neither call
// lifted, because the argument use opened the shape and an open shape refused
// the method field by name.
//
// CHECK:      emitc.func @both_3
// CHECK:        address_of %[[OBJ:[0-9]+]] : !emitc.lvalue<!emitc.opaque<"ctn_x">>
// CHECK:        address_of %[[OBJ]] : !emitc.lvalue<!emitc.opaque<"ctn_x">>
// CHECK:      emitc.func @fn_4(%arg0: !emitc.ptr<!emitc.opaque<"ctn_x">>, %arg1: f64) -> f64
// CHECK:      emitc.func @fn_5(%arg0: !emitc.ptr<!emitc.opaque<"ctn_x">>) -> f64

// --- TWO OBJECT PARAMETERS OF TWO SHAPES ------------------------------------
//
// The lift writes a LIST of indices rather than a bit for this, and the alias
// groups keep the two apart: joining them would give both the union of the
// shapes and a class with a field neither literal has.
//
// CHECK:      emitc.func @two_arguments_6
// CHECK:        address_of %{{[0-9]+}} : !emitc.lvalue<!emitc.opaque<"ctn_a">>
// CHECK:        address_of %{{[0-9]+}} : !emitc.lvalue<!emitc.opaque<"ctn_b">>
// CHECK:      emitc.func @fn_7(%arg0: !emitc.ptr<!emitc.opaque<"ctn_a">>, %arg1: !emitc.ptr<!emitc.opaque<"ctn_b">>) -> f64

// --- AND AN OBJECT THAT IS NOT THE FIRST ARGUMENT ---------------------------
//
// Next to a number, and second. The index the lift writes is the ENTRY-BLOCK
// index, so a mixed parameter list still lines up: `fn_9(double, ctn_k *)`.
//
// CHECK:      emitc.func @mixed_8
// CHECK:        address_of %{{[0-9]+}} : !emitc.lvalue<!emitc.opaque<"ctn_k">>
// CHECK:      emitc.func @fn_9(%arg0: f64, %arg1: !emitc.ptr<!emitc.opaque<"ctn_k">>) -> f64

// --- THE COUNTS -------------------------------------------------------------
//
// Pass statistics are compiled out of the release LLVM this builds against, so
// the counter is printed and asserted rather than admired. Five object
// parameters: one in `passed`, one in `both`, two in `two_arguments`, one in
// `mixed`.
//
// REPORT: ctnative: lifted 5 closure(s) over 0 capture(s) into 5 function(s), rewrote 5 call(s), unboxed 0 cell(s), 1 method(s) of which 1 take a receiver, 5 object parameter(s)
