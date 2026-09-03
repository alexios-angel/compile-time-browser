// `new` BUILDS A STRUCT IN THE FRAME - part 24, through the compilation-unit
// gate.
//
// Every `new` here has a callee this pass can NAME: the constructor is a
// function expression in scope, so `ctjs.construct`'s callee operand is a
// `ctjs.create_closure` result and not a table lookup. That is the same proof
// --ctjs-resolve-globals uses for a call, asked of `new`.
//
// WHAT THE REWRITE IS. Two operations that already lower, and no third:
//
//     var p = new Point(3, 4);
//   becomes
//     %p = ctjs.create_object                       // an empty literal
//     ctjs.call_direct @Point(%p, ..., 3, 4) {ctnative.receiver}
//
// so after it there is no constructor in the IR at all - only a closed object
// literal and a free function that writes through a pointer to it. The
// instance is a frame-scope `ctn_x_y` variable and NOTHING IS ALLOCATED; the
// constructor is the receiver carrier's free function, reached by
// `address_of`. `hasClosedShape`, `groupReceivers`, `fieldsOf`, `censusShapes`
// and `replace` needed no constructor case, which is the point of doing it as
// a rewrite rather than a lowering.
//
// WHAT IS NOT HERE, DELIBERATELY: any prototype at all. The VM gives every
// instance one (`make_instance` -> `ensure_prototype`, vm/call.cpp) and Phase
// 60 owns turning that into C++ inheritance, so a program that touches a
// constructor's `prototype` is refused BY NAME rather than compiled to
// something with no chain. So is a constructor that RETURNS an object, which
// in the VM replaces the instance outright - `produced.is_object_like() ?
// produced : self`. Both are one program apiece in
// CTNative/Lowering/constructor-refusals.mlir, under split-file, because a
// refusal is contagious in both directions.
//
// THE TOP-LEVEL `function Point() {}` SHAPE IS NOT ADMITTED, and this fixture
// says so by not containing one. A declared function is stored to a global and
// `new Point(...)` loads it back, so the construct's callee is a
// `ctjs.load_global` and not a closure - measured, and the reason printed is
// "the binding is used by ctjs.construct". Naming it needs the closed world's
// per-name row, which is the next lever and not this slice.
//
// The answers, worked out by hand and NOT asserted anywhere: the interpreter is
// the reference, by definition. Six globals, six distinct values, so a wrong
// one cannot hide behind a right one:
//   built7 = 7        pair34 = 34       shared66 = 66
//   defaulted5 = 5    method25 = 25     counted6 = 6

// THE SMALLEST ONE. Two fields assigned from two arguments and read back after
// the call - which is the whole of "the write reaches the caller's object",
// and would be 0 if the receiver were passed by value.
function built() {
    var Point = function (x, y) {
        this.x = x;
        this.y = y;
    };
    var p = new Point(3, 4);
    return p.x + p.y;
}

// A CONSTRUCTOR TAKING ARGUMENTS AND DOING ARITHMETIC ON THEM, so the fields
// are not merely copied parameters and the deduced carrier has to come from
// the store rather than from the argument.
function pair() {
    var Pair = function (a, b) {
        this.lo = a * 2;
        this.hi = b * 3;
    };
    var q = new Pair(5, 8);
    return q.lo + q.hi;
}

// TWO INSTANCES OF ONE CONSTRUCTOR SHARE ONE CLASS. Both `new` sites have the
// same shape, so `censusShapes` gives them one family and one `ctn_x_y`; two
// classes here would be a template that says nothing.
function shared() {
    var Point = function (x, y) {
        this.x = x;
        this.y = y;
    };
    var a = new Point(1, 2);
    var b = new Point(30, 33);
    return a.x + a.y + b.x + b.y;
}

// A CONSTRUCTOR THAT IGNORES AN ARGUMENT IT WAS NOT GIVEN. `new Def()` is a
// short call, padded with undefined exactly as op::call pads registers - and
// the body never reads the padded slot.
function defaulted() {
    var Def = function () {
        this.v = 5;
    };
    var d = new Def();
    return d.v;
}

// AN INSTANCE PASSED TO A LIFTED FUNCTION, WHICH IS WHERE THE TWO CARRIERS
// MEET. `scale` takes the instance as an OBJECT ARGUMENT - a `ctn_w *` - so
// this only compiles if a `new` instance is, by the time anything else looks,
// an ordinary closed object literal. It is: the rewrite leaves a
// ctjs.create_object behind and nothing downstream knows a constructor was
// ever involved.
//
// NOT AS A METHOD RECEIVER, AND THE REASON IS THE SLICE'S OWN LIMIT: an
// instance here has no prototype, so it has no methods of its own, and
// `ops.scale(b)` would pass it as an argument to a call whose callee is a
// property read - which `usesCloseTheShape` refuses, correctly, as an open
// shape.
function method_on_instance() {
    var Box = function (w) {
        this.w = w;
    };
    var b = new Box(5);
    var scale = function (o, f) { return o.w * f; };
    return scale(b, 5);
}

// A CONSTRUCTOR CALLED IN A LOOP, so the instance is one frame slot however
// many SSA values reach it, and the struct is built each time round without
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
var defaulted5 = defaulted();
var method25 = method_on_instance();
var counted6 = counted(4);
