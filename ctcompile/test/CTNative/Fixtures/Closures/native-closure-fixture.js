// A CLOSURE THAT CARRIES BY LIFTING - part 24 Phase 59, slice 1, through the
// compilation-unit gate.
//
// Every nested function here is a closure USED AS A VALUE: it is put in a
// local, and called through that local. None of them is a declaration bound to
// a global, which is the only closure shape the tier handled before - so before
// this file the enclosing function of every one of them was refused, and the
// message was "uses its own closure" or (through the importer's dead
// $enclosing_this operand) "uses `this`", on functions that mention neither.
//
// WHAT MAKES THEM ADMISSIBLE, and the four conditions in the order the lowering
// checks them: every capture is a cell of THIS frame that nothing ever writes,
// so copying its value is exact; the target is native by the existing
// call-graph fixpoint; and every use of the closure value is a call. The
// captured values become extra LEADING parameters of the lowered function and
// each call passes them, so the program allocates nothing and owns nothing -
// which is the only closure a tier with no collector can have for free.
//
// A CAPTURED PARAMETER, NOT A CAPTURED `var`, IN EVERY CASE HERE, and that is
// forced rather than chosen. `compiler_impl::predeclare_locals` hoists every
// `var`/`let`/`const` of a function body and boxes it on the spot, so the
// declaration is a `cell_set` into an already-built cell and the binding reads
// as one that is written. A parameter is boxed in the prologue holding its
// value and is written nowhere, which is what the immutability proof needs. The
// refusal for the other shape is pinned in closure-refusals.mlir.
//
// The answers, worked out by hand and NOT asserted anywhere - the interpreter
// is the reference, by definition. Eight globals, eight distinct values, so a
// wrong one cannot hide behind a right one:
//   scaled42 = 42      combined15 = 15    poly72 = 72     plain9 = 9
//   accumulated = 2.5  line30 = 30        nested28 = 28   arrowed19 = 19
//   bounded101 = 101

// ONE CAPTURE, CALLED ONCE. The smallest lift there is: `k` becomes `apply`'s
// first parameter and `apply()` becomes `apply(k)`.
function scaled(k) {
    function apply() { return k * 2; }
    return apply();
}

// TWO CAPTURES. They arrive in descriptor order, which is the order the
// capture list is written in - not the order they are read in the body.
function combine(a, b) {
    function sum() { return a + b; }
    return sum() * 3;
}

// A CAPTURE AND A PARAMETER OF ITS OWN, called twice. The captures lead: `at`
// lowers to `at(double c, double x)`, and each call site passes the same `c`
// with a different `x`.
function poly(c) {
    function at(x) { return c * x + 1; }
    return at(2) + at(5);
}

// NO CAPTURE AT ALL, which is a lift with an empty capture list and is the
// commonest shape in real code: a nested helper that only uses its arguments.
// The whole of the work is turning a ctjs.call through a local into a
// ctjs.call_direct.
function no_capture(n) {
    function twice(v) { return v + v; }
    return twice(n) + 1;
}

// CALLED INSIDE A LOOP. The closure is built once, before the loop, and the
// call inside it passes the same captured value every iteration - so nothing
// is allocated per iteration, which is the property the lift buys.
function accumulate(step, n) {
    function delta() { return step; }
    var total = 0;
    var i = 0;
    while (i < n) {
        total = total + delta();
        i = i + 1;
    }
    return total;
}

// TWO CAPTURES AND A PARAMETER, called three times.
function line(m, b) {
    function at(x) { return m * x + b; }
    return at(1) + at(2) + at(3);
}

// TWO LEVELS. `mid` captures nothing and is lifted for its call sites alone;
// `inner` captures `mid`'s own parameter and is lifted inside a function that
// was itself just lifted. `k` is NOT captured by either - it is mentioned only
// in `outer_two`'s own body - so nothing here reaches through an enclosing
// closure. That shape is slice 1b's, and native-nested-closure-fixture.js is
// built out of it.
function outer_two(k) {
    function mid(j) {
        function inner() { return j + j; }
        return inner() * 2;
    }
    return mid(k) + mid(k + 1);
}

// AN ARROW, which is the shape most real closures have. An arrow's `this` is
// lexical, so lifting one is sound only when it never reads `this` - which the
// lowering checks, because after the importer's correction a non-undefined
// `$enclosing_this` operand is the only place the IR says a target is an arrow.
// This one reads only its parameter and its capture, so it lifts like any
// other; one that reads `this` is refused by name in closure-refusals.mlir.
function arrowed(k) {
    var add = (x) => x + k;
    return add(3) + add(4);
}

// A CLOSURE THAT RETURNS A BOOLEAN, so the lowered function's return type is
// `bool` and not `double`, and its result is used as a condition.
function bounded(lo, hi) {
    function inside(x) { return x >= lo && x < hi; }
    var hits = 0;
    if (inside(1)) { hits = hits + 1; }
    if (inside(7)) { hits = hits + 10; }
    if (inside(4)) { hits = hits + 100; }
    return hits;
}

var scaled42 = scaled(21);
var combined15 = combine(2, 3);
var poly72 = poly(10);
var plain9 = no_capture(4);
var accumulated = accumulate(0.5, 5);
var line30 = line(4, 2);
var nested28 = outer_two(3);
var arrowed19 = arrowed(6);
var bounded101 = bounded(0, 5);
