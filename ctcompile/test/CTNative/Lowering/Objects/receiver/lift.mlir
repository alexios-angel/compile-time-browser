// Methods that use `this` lower to free functions that take the receiver as a
// pointer parameter.
//
// Three objects with methods. Lowering them needs a C++ type for the receiver:
// a closed-shape struct passed by pointer, so a method can read and write its
// fields and two objects of the same data shape can share one lifted method.
// (A struct passed by value would be refused as soon as it was returned,
// stored or passed.)
//
// The signatures are the assertion, as they are for a lifted closure. `bump`
// is written with one parameter and lowers to `fn_2(ctn_x *, double)` with the
// receiver first. With the position wrong the program still compiles and
// prints a wrong number, so the parameter lists are pinned here and the
// answers by the native differential test of ../../Fixtures/Objects/receiver.js.
//
// So is the class. `{x: 1, bump: <fn>}` has the shape `{x}`, not `{bump, x}`:
// the method is a free function, and the analysis proved the field only ever
// holds that one function, so it needs no storage. That is what lets the two
// `{base, plus}` literals in `shared` be one class (they have the same data
// shape) while `plus` is one lifted function taking a pointer to it.
//
// One program per file, with functions appended in source order: the
// importer's `$N` suffix is the function's position in `program::functions`,
// so inserting a function in the middle renumbers every symbol below it.
//
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

// --- The method field is not a member --------------------------------------
//
// CHECK-NEXT is what makes this an assertion rather than a hope: it pins the
// class to exactly one field, so a `bump` member (a field of a type the
// lowering has no C++ type for) would fail the line after.
//
// CHECK:      emitc.class @ctn_x
// CHECK-NEXT:   emitc.field @x : !emitc.opaque<"ctnative::js_num">
// CHECK-NEXT: }
// CHECK:      emitc.class @ctn_n
// CHECK-NEXT:   emitc.field @n : !emitc.opaque<"ctnative::js_num">
// CHECK-NEXT: }

// --- Two literals, one class -----------------------------------------------
//
// The provenance names both sites: the same data shape at two places is one
// class definition. If a method's identity were part of the shape key these
// would be two classes, and `plus` could not take one pointer type.
//
//
// CHECK:      emitc.class @ctn_base attributes {ctnative.provenance = "object literal at {{[^"]*}}, {{[^"]*}} (2 sites)"}
// CHECK-NEXT:   emitc.field @base : !emitc.opaque<"ctnative::js_num">
// CHECK-NEXT: }

// --- Every function is lowered ---------------------------------------------
//
// The two --implicit-check-not options above check this over the whole file: a
// `ctjs.func` in the output is a function the lowering left behind, and a
// `ctnative.not_native` is a refusal. Neither may appear.
//
// CHECK: emitc.func @main() -> i32

// --- A read-only method ----------------------------------------------------
//
// The caller takes the address of its own frame slot and passes it first. The
// method copies the pointer into one lvalue local, because
// `emitc.member_of_ptr` needs an lvalue, a parameter is not one, and
// `emitc.func` rejects an lvalue argument type outright. It reads `x` through
// that local.
//
// CHECK: emitc.func @read_only_1() -> !emitc.opaque<"ctnative::js_num">
// CHECK: %[[P:.*]] = "emitc.variable"{{.*}} -> !emitc.lvalue<!emitc.opaque<"ctn_x">>
// CHECK: %[[ADDR:.*]] = address_of %[[P]] : !emitc.lvalue<!emitc.opaque<"ctn_x">>
// CHECK: call @fn_2(%[[ADDR]], %{{.*}}) : (!emitc.ptr<!emitc.opaque<"ctn_x">>, !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">

// CHECK: emitc.func @fn_2(%arg0: !emitc.ptr<!emitc.opaque<"ctn_x">>, %arg1: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: %[[SELF:.*]] = "emitc.variable"{{.*}} -> !emitc.lvalue<!emitc.ptr<!emitc.opaque<"ctn_x">>>
// CHECK: assign %arg0 : !emitc.ptr<!emitc.opaque<"ctn_x">> to %[[SELF]]
// CHECK: %[[X:.*]] = "emitc.member_of_ptr"(%[[SELF]]) <{member = "x"}>
// CHECK: load %[[X]] : <!emitc.opaque<"ctnative::js_num">>
// CHECK: add %{{.*}} : (!emitc.opaque<"ctnative::js_num">, !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">

// --- A mutating method -----------------------------------------------------
//
// `this.n = this.n * 3` is a member_of_ptr on the left of an assign, which is
// why the receiver is a pointer rather than a value: it is EmitC's only member
// access that yields an lvalue through a parameter. The caller reads `c.n`
// after the call and gets what the method wrote.
//
// CHECK: emitc.func @mutating_3() -> !emitc.opaque<"ctnative::js_num">
// CHECK: call @fn_4(%{{.*}}) : (!emitc.ptr<!emitc.opaque<"ctn_n">>) -> !emitc.opaque<"ctnative::nullable_scalar">
// CHECK: emitc.func @fn_4(%arg0: !emitc.ptr<!emitc.opaque<"ctn_n">>) -> !emitc.opaque<"ctnative::nullable_scalar">
// CHECK: %[[NREAD:.*]] = "emitc.member_of_ptr"(%{{.*}}) <{member = "n"}>
// CHECK: load %[[NREAD]] : <!emitc.opaque<"ctnative::js_num">>
// CHECK: %[[NWRITE:.*]] = "emitc.member_of_ptr"(%{{.*}}) <{member = "n"}>
// CHECK: assign %{{.*}} : !emitc.opaque<"ctnative::js_num"> to %[[NWRITE]] : <!emitc.opaque<"ctnative::js_num">>

// --- Two objects sharing one lifted method ---------------------------------
//
// Two frame slots, two address_of, one function, whose parameter is the class
// both objects have.
//
// CHECK: emitc.func @shared_5() -> !emitc.opaque<"ctnative::js_num">
// CHECK: %[[A:.*]] = address_of %{{.*}} : !emitc.lvalue<!emitc.opaque<"ctn_base">>
// CHECK: call @fn_6(%[[A]], %{{.*}}) : (!emitc.ptr<!emitc.opaque<"ctn_base">>, !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: %[[B:.*]] = address_of %{{.*}} : !emitc.lvalue<!emitc.opaque<"ctn_base">>
// CHECK: call @fn_6(%[[B]], %{{.*}}) : (!emitc.ptr<!emitc.opaque<"ctn_base">>, !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">
// CHECK: emitc.func @fn_6(%arg0: !emitc.ptr<!emitc.opaque<"ctn_base">>, %arg1: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">

// --- The report counters (not pass statistics) -----------------------------
//
// The release LLVM package this builds against compiles pass statistics out, so
// --mlir-pass-statistics prints an empty report and a test asserting one
// asserts nothing. The `report` option, which --ctjs-resolve-globals and
// --ctjs-lift-to-scf also have, prints a remark a test can pin.
//
// REPORT: remark: ctnative: lifted 3 closure(s) over 0 capture(s) into 3 function(s), rewrote 4 call(s), unboxed 0 cell(s), 3 method(s) of which 3 take a receiver, 0 object parameter(s)
