// `new` on a constructor the compiler can name builds a struct in the caller's
// frame instead of allocating. Run end to end as a native fixture.
//
// Every `new` here has a callee this pass can name: the constructor is a
// function expression in scope, so `ctjs.construct`'s callee operand is a
// `ctjs.create_closure` result and not a table lookup. That is the same proof
// --ctjs-resolve-globals uses for a call, applied to `new`.
//
// The rewrite turns `new` into two operations that already lower:
//
//     var p = new Point(3, 4);
//   becomes
//     %p = ctjs.create_object                       // an empty literal
//     ctjs.call_direct @Point(%p, ..., 3, 4) {ctnative.receiver}
//
// so afterwards there is no constructor in the IR at all, only a closed object
// literal and a free function that writes through a pointer to it. The
// instance is a frame-scope `ctn_x_y` variable and nothing is allocated; the
// constructor is the receiver lift's free function, reached by `address_of`.
// `hasClosedShape`, `groupReceivers`, `fieldsOf`, `censusShapes` and `replace`
// need no constructor case, which is why this is a rewrite, not a lowering.
//
// Not covered here, deliberately: prototypes. The VM gives every instance one
// (`make_instance` -> `ensure_prototype`, ctbrowser/lib/Script/vm/call/
// construct.cpp) and this lowering has no C++ equivalent yet, so a program
// that touches a constructor's `prototype` is refused by name rather than
// compiled without a prototype chain. So is a constructor that returns an
// object, which in the VM replaces the instance (`produced.is_object_like() ?
// produced : self`). Both refusals are in
// CTNative/Lowering/Objects/constructor-refusals.mlir, one program per
// split-file section, because a refusal spreads to callers and callees.
//
// A top-level `function Point() {}` is not admitted, so this fixture has none.
// A declared function is stored to a global and `new Point(...)` loads it
// back, so the construct's callee is a `ctjs.load_global`, not a closure
// (refused with "the binding is used by ctjs.construct"). Supporting it needs
// per-global knowledge of which function a global holds.
//
// The answers, worked out by hand and not asserted anywhere (the interpreter is
// the reference). Six globals with six distinct values, so a wrong one cannot
// hide behind a right one:
//   built7 = 7        pair34 = 34       shared66 = 66
//   defaulted5 = 5    method25 = 25     counted6 = 6




// The smallest case. Two fields assigned from two arguments and read back
// after the call: this checks that the constructor's writes reach the caller's
// object, and would give 0 if the receiver were passed by value.
function built() {
    var Point = function (x, y) {
        this.x = x;
        this.y = y;
    };
    var p = new Point(3, 4);
    return p.x + p.y;
}

// A constructor that does arithmetic on its arguments, so the fields are not
// merely copied parameters and the field type must come from the store, not
// from the argument.
function pair() {
    var Pair = function (a, b) {
        this.lo = a * 2;
        this.hi = b * 3;
    };
    var q = new Pair(5, 8);
    return q.lo + q.hi;
}

// Two instances of one constructor share one class. Both `new` sites have the
// same shape, so `censusShapes` gives them one family and one `ctn_x_y`; two
// classes here would mean shapes are not being merged.
function shared() {
    var Point = function (x, y) {
        this.x = x;
        this.y = y;
    };
    var a = new Point(1, 2);
    var b = new Point(30, 33);
    return a.x + a.y + b.x + b.y;
}

// A constructor that ignores an argument it was not given. `new Def()` is a
// short call, padded with undefined exactly as op::call pads registers, and the
// body never reads the padded slot.
function defaulted() {
    var Def = function () {
        this.v = 5;
    };
    var d = new Def();
    return d.v;
}


// An instance passed to a lifted function, where the constructor rewrite and
// object arguments meet. `scale` takes the instance as an object argument (a
// `ctn_w *`), so this only compiles if a `new` instance is, by the time
// anything else looks, an ordinary closed object literal. It is: the rewrite
// leaves a ctjs.create_object behind and nothing downstream knows a
// constructor was involved.
//
// Not as a method receiver: an instance here has no prototype, so it has no
// methods of its own, and `ops.scale(b)` would pass it as an argument to a
// call whose callee is a property read, which `usesCloseTheShape` correctly
// refuses as an open shape.
function method_on_instance() {
    var Box = function (w) {
        this.w = w;
    };
    var b = new Box(5);
    var scale = function (o, f) {
        return o.w * f;
    };
    return scale(b, 5);
}

// A constructor called in a loop, so the instance is one frame slot however
// many SSA values reach it, and the struct is built on each iteration without
// allocating.
function counted(n) {
    var Cell = function (k) {
        this.k = k;
    };
    var total = 0;
    var i = 0;
    while (i < n) {
        var c = new Cell(i);
        total = total + c.k;
        i = i + 1;
    }
    return total;
}

var built7 = built();
var pair34 = pair();
var shared66 = shared();


// `+ 0` makes the printed value a Number. `defaulted` returns `d.v`, and a
// closed-shape field's type starts as possibly `undefined` because nothing
// orders a read after a store, so `admission::printable` refuses to store it to
// a global. The type narrowing drops `undefined` when a `ctjs.set_property` of
// the key, on the same value, in the same `ctjs.func`, dominates the read
// (`mutated40` in argument.js relies on this). Here the only store of `v` is
// `this.v = 5` inside the constructor, a different `ctjs.func` from the read;
// requiring the same function keeps the dominance query out of
// `builtin.module`'s graph region. Removing the `+ 0` would need a callee
// summary (`Def` stores `v` unconditionally, and the receiver call dominates
// the read), which is a different proof, not a wider dominance query.
var defaulted5 = defaulted() + 0;
var method25 = method_on_instance();
var counted6 = counted(4);
