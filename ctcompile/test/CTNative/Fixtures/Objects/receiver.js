// Methods that use `this` compile with the receiver as a parameter. Run end to
// end as a native fixture.
//
// Every object here is a method table: a closed-shape literal whose fields are
// numbers and whose methods are function expressions that reach the data
// through `this`. Compiling them needs a C++ type for the receiver, not a
// known callee (the callee is already known). In real bundles, "uses `this`"
// is one of the most common reasons a function would otherwise be refused.
//
// The receiver is lifted exactly as a capture is. `ctjs.call_direct`'s
// operands are the callee's entry block arguments (receiver, new.target,
// callee, parameters), so the object is operand 0 and lands in `%arg0`. The
// lowering keeps that operand, gives `%arg0` the generated class by pointer
// (see receiverType() for why EmitC cannot spell a `&`), and turns `o.bump(2)`
// into a call_direct.
//
// A method field is not a struct member. The closure it holds is proved to be
// the only one stored there, so the field is a compile-time identity, not
// state: the store lowers to nothing and `{ x: 1, bump: f }` has the shape
// `{x}`. `shared()` below depends on this: two literals, one class, one lifted
// method between them.
//
// The answers, worked out by hand and not asserted anywhere (the interpreter is
// the reference). Eight globals with eight distinct values, so a wrong one
// cannot hide behind a right one:
//   readonly10 = 10   mutated9 = 9     chained30 = 30    shared303 = 303
//   captured14 = 14   flagged1 = 1     flagged0 = 0      accumulated15 = 15


// A read-only method, the smallest receiver lift: `this.x` becomes `self->x`
// and `p.bump(4)` becomes `bump_1(&p, 4)`.
function read_only() {
    var p = {
        x: 6,
        bump: function (n) {
            return this.x + n;
        }
    };
    return p.bump(4);
}

// A mutating method whose write is visible after the call. This is why the
// receiver is passed by reference: `this.n = this.n * 3` has to reach the
// caller's object, which a by-value receiver would not do, and two calls in a
// row show it; by value the answer would be 3, not 9.
function mutating() {
    var c = {
        n: 1,
        step: function () {
            this.n = this.n * 3;
        }
    };
    c.step();
    c.step();
    return c.n;
}

// A method calling another method on the same receiver. `this.area()` inside
// `twice` is a second lifted call whose receiver is `twice`'s own `%arg0`, so
// the pointer is passed straight through with no second address-of. It also
// makes the two functions one alias group, which lets `area` read a field
// `twice` never touches.
function chained() {
    var v = {
        w: 3,
        h: 5,
        area: function () {
            return this.w * this.h;
        },
        twice: function () {
            return this.area() * 2;
        }
    };
    return v.twice();
}

// Two objects of the same shape sharing one lifted method. One function
// expression, stored into two literals, called on both. This works only
// because a method field is not part of the shape key: `a` and `b` are both
// `ctn_base`, and `plus` is one free function taking that class.
//
// `add` is not boxed, which is also required: `compiler_impl::is_captured`
// boxes a local mentioned inside a nested function, and nothing mentions `add`
// inside one, so the closure value flows to both stores directly.
function shared() {
    var add = function (k) {
        return this.base + k;
    };
    var a = {
        base: 100,
        plus: add
    };
    var b = {
        base: 200,
        plus: add
    };
    return a.plus(1) + b.plus(2);
}

// A method that is also a closure. `scale` is a parameter of the enclosing
// frame, boxed by the prologue and never written, so the closure lift's
// never-written-cell proof unboxes it and it becomes a parameter right after
// the receiver: `times_N(ctn_v * self, double scale)`. The two lifts compose
// because a method is a closure in the IR and they share their capture checks.
function captured(scale) {
    var o = {
        v: 2,
        times: function () {
            return this.v * scale;
        }
    };
    return o.times();
}

// A method that returns a boolean, so the lifted function returns `bool`, not
// `double`: the return type comes from the returns, receiver or not.
function boolean_method(n) {
    var g = {
        limit: 50,
        over: function (x) {
            return x > this.limit;
        }
    };
    return g.over(n) ? 1 : 0;
}

// A method called in a loop, on a literal that stays one frame slot however
// many SSA values reach it. --ctjs-lift-to-scf drops the trivial phi for a
// variable assigned once before the loop, so `acc` is one object and the call
// inside the loop is a call on it.
function accumulated(n) {
    var acc = {
        total: 0,
        add: function (k) {
            this.total = this.total + k;
        }
    };
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
