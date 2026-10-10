// Object literals passed as call arguments, run end to end as a native fixture.
//
// The receiver lift makes a closed-shape literal reach a method as a `ctn_x *`
// and its fields as `self->x`. Nothing in that representation depends on the
// object being operand 0: `f(o)` wants the same pointer one operand along. So
// the same proof, the same `receiverType`, the same alias group and the same
// `emitc.member_of_ptr` carry an argument, and the shape rule does not treat
// "it is passed to a call" as an escape when the callee is one function this
// rewrite makes direct and the parameter is only ever read through constant
// keys.
//
// Why this matters for `this`: if passing a literal as an argument made its
// shape open, `closedAfterLift` would refuse it, and a method table that was
// also passed anywhere would have every method refused with "a method field of
// an object whose shape is not closed". `both()` below is that program: one
// literal, one method call and one plain call, and both must lift.
//
// The answers, worked out by hand and not asserted anywhere (the interpreter is
// the reference). Seven globals with seven distinct values, so a wrong one
// cannot hide behind a right one:

//   passed12 = 12    both18 = 18      twoargs7 = 7      mutated40 = 40
//   shapes11 = 11    boolarg1 = 1     mixed26 = 26

// The smallest object argument. `read(o)` becomes `read_N(&o)` and `p.x`
// becomes `p->x`: no allocation, no owner, the object stays in the caller's frame.
function passed() {
    var o = {
        x: 4
    };
    var read = function (p) {
        return p.x * 3;
    };
    return read(o);
}

// One literal, a method call and a plain call. If the argument use made the
// shape open, `bump` would be refused. Both lifts apply to one object: the
// method takes it at operand 0 (the receiver) and the function at operand 3
// (its first parameter).
function both() {
    var o = {
        x: 4,
        bump: function (n) {
            return this.x + n;
        }
    };
    var read = function (p) {
        return p.x * 3;
    };
    return o.bump(2) + read(o);
}

// Two object parameters in one signature, of two different shapes. The lift
// records parameter indices rather than a single flag for this:
// `join_N(ctn_a * , ctn_b *)`, and the alias groups keep the two apart; joining
// them would give a class with the union of the shapes' fields.
function two_arguments() {
    var left = {
        a: 3
    };
    var right = {
        b: 4
    };
    var join = function (p, q) {
        return p.a + q.b;
    };
    return join(left, right);
}

// A write through an object parameter, which is why it is a pointer.
// `p.n = p.n * 5` has to reach the caller's object; by value the answer would
// be 8, not 40. The mutating receiver test makes this point for `this`; an
// argument needs it checked separately because it is a different operand.
function mutated() {
    var c = {
        n: 8
    };
    var scale = function (p) {
        p.n = p.n * 5;
    };
    scale(c);
    return c.n;
}

// Two literals of one shape through one parameter. Both call sites pass a
// `{v}`, so the parameter is one `ctn_v *` and the callee is one function: the
// argument form of "two objects of the same shape share one method".
function two_shapes() {
    var first = {
        v: 3
    };
    var second = {
        v: 8
    };
    var take = function (p) {
        return p.v;
    };
    return take(first) + take(second);
}


// An object parameter whose field is a boolean, so the field is a `bool` and
// the class is `ctn_ok` with a `bool` member. Field types come from the shape
// census and do not change because the object arrived through a parameter.
function boolean_field() {
    var g = {
        ok: true
    };
    var test = function (p) {
        return p.ok;
    };
    return test(g) ? 1 : 0;
}

// An object and a number in one call, with the object second. The index the
// lift records is the entry-block index, so a parameter list that mixes C++
// types still lines up: `mix_N(double, ctn_k *)`.
function mixed() {
    var k = {
        k: 6
    };
    var mix = function (n, p) {
        return n * p.k + 2;
    };
    return mix(4, k);
}

var passed12 = passed();
var both18 = both();
var twoargs7 = two_arguments();
var mutated40 = mutated();
var shapes11 = two_shapes();
var boolarg1 = boolean_field();
var mixed26 = mixed();
