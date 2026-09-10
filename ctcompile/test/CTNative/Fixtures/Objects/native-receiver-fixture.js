// THE RECEIVER IS A PARAMETER - part 24, through the compilation-unit gate.
//
// Every object here is a method table: a closed-shape literal whose fields are
// numbers and whose methods are function expressions that reach the data
// through `this`. That is the shape the tier refused whole before this slice -
// `uses \`this\`` was 6,649 of the corpus refusals - and the reason was never
// that the callee is unknown. It is that no C++ type carried a receiver:
// `carrierOf` had a `structure` row nothing could produce, `ctjs.call_direct`
// dropped operand 0, and the one object carrier was a struct BY VALUE, refused
// the moment it was returned, stored or passed.
//
// SO THE RECEIVER LIFTS, exactly as a capture does. `ctjs.call_direct`'s
// operands ARE the callee's entry block - receiver, new.target, callee,
// parameters - so the object is already operand 0 and `%arg0` is already where
// it lands. What changed is that the lowering stops dropping it, that `%arg0`
// gets a carrier (the generated class, by pointer - see receiverType() for why
// EmitC cannot spell a `&`), and that `o.bump(2)` becomes a call_direct at all.
//
// A METHOD FIELD IS NOT A STRUCT MEMBER. The closure it holds is proved single,
// so the field is a compile-time identity and not state: the store lowers to
// nothing and `{ x: 1, bump: f }` has the shape `{x}`. `shared()` below is what
// that buys - two literals, one class, one lifted method between them.
//
// The answers, worked out by hand and NOT asserted anywhere: the interpreter is
// the reference, by definition. Eight globals, eight distinct values, so a
// wrong one cannot hide behind a right one:
//   readonly10 = 10   mutated9 = 9     chained30 = 30    shared303 = 303
//   captured14 = 14   flagged1 = 1     flagged0 = 0      accumulated15 = 15

// A READ-ONLY METHOD. The smallest receiver lift there is: `this.x` becomes
// `self->x` and `p.bump(4)` becomes `bump_1(&p, 4)`.
function read_only() {
    var p = { x: 6, bump: function (n) { return this.x + n; } };
    return p.bump(4);
}

// A MUTATING METHOD, AND THE WRITE IS VISIBLE AFTER THE CALL. This is the whole
// case for a reference: `this.n = this.n * 3` has to reach the caller's object,
// which a by-value receiver would not do, and which two calls in a row are what
// prove - by value the answer would be 3 and not 9.
function mutating() {
    var c = { n: 1, step: function () { this.n = this.n * 3; } };
    c.step();
    c.step();
    return c.n;
}

// A METHOD CALLING ANOTHER METHOD ON THE SAME RECEIVER. `this.area()` inside
// `twice` is a second lifted call whose receiver is `twice`'s own `%arg0`, so
// the pointer is passed straight through with no second address-of. It also
// makes the two functions one alias group, which is what lets `area` read a
// field `twice` never touches.
function chained() {
    var v = {
        w: 3,
        h: 5,
        area: function () { return this.w * this.h; },
        twice: function () { return this.area() * 2; }
    };
    return v.twice();
}

// TWO OBJECTS OF THE SAME SHAPE SHARING ONE LIFTED METHOD. One function
// expression, stored into two literals, called on both - which is only
// expressible BECAUSE a method field is not part of the shape key: `a` and `b`
// are both `ctn_base`, and `plus` is one free function taking that class.
//
// `add` IS NOT BOXED, and that is why this works: `compiler_impl::is_captured`
// boxes a local MENTIONED inside a nested function, and nothing mentions `add`
// inside one - so the closure value flows to both stores directly.
function shared() {
    var add = function (k) { return this.base + k; };
    var a = { base: 100, plus: add };
    var b = { base: 200, plus: add };
    return a.plus(1) + b.plus(2);
}

// A METHOD THAT IS ALSO A CLOSURE. `scale` is a parameter of the enclosing
// frame, boxed by the prologue and never written, so Phase 59 slice 1's cell
// proof unboxes it and it becomes a LEADING parameter after the receiver:
// `times_N(ctn_v * self, double scale)`. The two lifts compose because a method
// IS a closure in the IR and they share their capture clauses.
function captured(scale) {
    var o = { v: 2, times: function () { return this.v * scale; } };
    return o.times();
}

// A METHOD THAT RETURNS A BOOLEAN, so the lifted signature is `bool` and not
// `double` - the return carrier is decided by the returns, receiver or not.
function boolean_method(n) {
    var g = { limit: 50, over: function (x) { return x > this.limit; } };
    return g.over(n) ? 1 : 0;
}

// A METHOD CALLED IN A LOOP, on a literal that is one frame slot however many
// SSA values reach it (obligation O-4). --ctjs-lift-to-scf drops the trivial
// phi for a variable assigned once before the loop, so `acc` is one object and
// the call inside the loop is a call on it.
function accumulated(n) {
    var acc = { total: 0, add: function (k) { this.total = this.total + k; } };
    var i = 0;
    while (i < n) {
        acc.add(i);
        i = i + 1;
    }
    return acc.total;
}

var readonly10 = read_only();
var mutated9 = mutating();
var chained30 = chained();
var shared303 = shared();
var captured14 = captured(7);
var flagged1 = boolean_method(80);
var flagged0 = boolean_method(20);
var accumulated15 = accumulated(6);
