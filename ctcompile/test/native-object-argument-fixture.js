// AN ARGUMENT IS A PARAMETER TOO - part 24, through the compilation-unit gate.
//
// The receiver lift made a closed-shape literal reach a function as a
// `ctn_x *` and its fields as `self->x`. NOTHING IN THAT CARRIER WAS ABOUT
// OPERAND 0: `f(o)` wants the same pointer one operand along. So the same
// proof, the same `receiverType`, the same alias group and the same
// `emitc.member_of_ptr` carry an ARGUMENT, and the shape rule stops treating
// "it is passed to a call" as an escape when the callee is one function this
// rewrite makes direct and the parameter is only ever read through constant
// keys.
//
// WHY THAT MATTERS TO `this`. Until this slice `closedAfterLift` refused a
// literal the moment it appeared in an argument, so a method table that was
// ALSO passed anywhere had no closed shape and every method on it was refused
// with "a method field of an object whose shape is not closed". `both()` below
// is exactly that program: one literal, one method call and one plain call,
// and before this slice neither of them lifted.
//
// The answers, worked out by hand and NOT asserted anywhere: the interpreter is
// the reference, by definition. Seven globals, seven distinct values, so a
// wrong one cannot hide behind a right one:
//   passed12 = 12    both18 = 18      twoargs7 = 7      mutated40 = 40
//   shapes11 = 11    boolarg1 = 1     mixed26 = 26

// THE SMALLEST OBJECT ARGUMENT THERE IS. `read(o)` becomes `read_N(&o)` and
// `p.x` becomes `p->x`; no allocation, no owner, the caller's frame.
function passed() {
    var o = { x: 4 };
    var read = function (p) { return p.x * 3; };
    return read(o);
}

// ONE LITERAL, A METHOD CALL AND A PLAIN CALL. This is the program the widening
// exists for: the argument use is what made the shape open, and an open shape
// is what refused `bump`. Both lifts now apply to one object, and the method
// takes it at operand 0 while the function takes it at operand 3.
function both() {
    var o = { x: 4, bump: function (n) { return this.x + n; } };
    var read = function (p) { return p.x * 3; };
    return o.bump(2) + read(o);
}

// TWO OBJECT PARAMETERS IN ONE SIGNATURE, of two DIFFERENT shapes. The lift
// lists indices rather than a bit for this: `join_N(ctn_a * , ctn_b *)`, and
// the alias groups keep the two apart - joining them would give both the union
// of the shapes and a class with a field neither literal has.
function two_arguments() {
    var left = { a: 3 };
    var right = { b: 4 };
    var join = function (p, q) { return p.a + q.b; };
    return join(left, right);
}

// A WRITE THROUGH AN OBJECT PARAMETER, WHICH IS THE WHOLE CASE FOR A POINTER.
// `p.n = p.n * 5` has to reach the caller's object; by value the answer would
// be 8 and not 40. The mutating receiver test makes this point for `this`, and
// an argument needs it made again because it is a different operand.
function mutated() {
    var c = { n: 8 };
    var scale = function (p) { p.n = p.n * 5; };
    scale(c);
    return c.n;
}

// TWO LITERALS OF ONE SHAPE THROUGH ONE PARAMETER. Both call sites pass a
// `{v}`, so the parameter is one `ctn_v *` and the callee is one function -
// the argument form of "two objects of the same shape share one method".
function two_shapes() {
    var first = { v: 3 };
    var second = { v: 8 };
    var take = function (p) { return p.v; };
    return take(first) + take(second);
}

// AN OBJECT PARAMETER WHOSE FIELD IS A BOOLEAN, so the field's carrier is
// `bool` and the class is `ctn_ok` with a `bool` member - the field carrier
// rules are the shape census's and do not change because the object arrived
// through a parameter.
function boolean_field() {
    var g = { ok: true };
    var test = function (p) { return p.ok; };
    return test(g) ? 1 : 0;
}

// AN OBJECT AND A NUMBER IN ONE CALL, and the object is not the first
// argument. The index the lift writes is the ENTRY-BLOCK index, so a
// parameter list that mixes carriers still lines up: `mix_N(double, ctn_k *)`.
function mixed() {
    var k = { k: 6 };
    var mix = function (n, p) { return n * p.k + 2; };
    return mix(4, k);
}

var passed12 = passed();
var both18 = both();
var twoargs7 = two_arguments();
var mutated40 = mutated();
var shapes11 = two_shapes();
var boolarg1 = boolean_field();
var mixed26 = mixed();
