// Closures compiled by lambda lifting, checked end to end by the
// compilation-unit test.
//
// Every nested function here is a closure used as a value: it is stored in a
// local and called through that local, rather than being a declaration bound
// to a global. Without lifting, the enclosing function of each one would be
// refused with "uses its own closure", or with "uses `this`" because of the
// importer's unused `$enclosing_this` operand, even though the function
// mentions neither.
//
// What lets them compile natively, in the order the lowering checks it: every
// capture is a cell of this frame that is never written, so copying its value
// is exact; the target function itself compiles natively (the existing
// call-graph fixpoint decides that); and every use of the closure value is a
// call. The captured values become extra leading parameters of the lowered
// function and each call passes them, so nothing is allocated or owned, which
// is why this needs no garbage collector.
//
// Every capture here is a parameter, never a `var`, and that is forced.
// `compiler_impl::predeclare_locals` hoists every `var`/`let`/`const` of a
// function body and boxes it at the top, so the declaration becomes a
// `cell_set` into an existing cell and the binding counts as written. A
// parameter is boxed in the prologue holding its value and is never written,
// which is what the immutability check needs. The refusal for the other shape
// is pinned in test/CTNative/Lowering/Closures/refusals.mlir.
//
// Expected values, worked out by hand and not asserted here: the interpreter
// is the reference. Nine globals, nine distinct values, so a wrong one cannot
// hide behind a right one:
//   scaled42 = 42      combined15 = 15    poly72 = 72     plain9 = 9
//   accumulated = 2.5  line30 = 30        nested28 = 28   arrowed19 = 19
//   bounded101 = 101

// One capture, called once. The smallest lift: `k` becomes `apply`'s first
// parameter and `apply()` becomes `apply(k)`.
function scaled(k) {
    function apply() {
        return k * 2;
    }
    return apply();
}

// Two captures. They arrive in descriptor order, which is the order the
// capture list is written in, not the order they are read in the body.
function combine(a, b) {
    function sum() {
        return a + b;
    }
    return sum() * 3;
}

// A capture plus a parameter of its own, called twice. The captures lead: `at`
// lowers to `at(double c, double x)`, and each call site passes the same `c`
// with a different `x`.
function poly(c) {
    function at(x) {
        return c * x + 1;
    }
    return at(2) + at(5);
}


// No capture at all: a lift with an empty capture list, the commonest shape in
// real code (a nested helper that only uses its arguments). The only work is
// turning a ctjs.call through a local into a ctjs.call_direct.
function no_capture(n) {
    function twice(v) {
        return v + v;
    }
    return twice(n) + 1;
}

// Called inside a loop. The closure is built once, before the loop, and the
// call inside it passes the same captured value every iteration, so nothing
// is allocated per iteration.
function accumulate(step, n) {
    function delta() {
        return step;
    }
    var total = 0;
    var i = 0;
    while (i < n) {
        total = total + delta();
        i = i + 1;
    }
    return total;
}

// Two captures and a parameter, called three times.
function line(m, b) {
    function at(x) {
        return m * x + b;
    }
    return at(1) + at(2) + at(3);
}


// Two levels. `mid` captures nothing and is lifted for its call sites alone;
// `inner` captures `mid`'s own parameter and is lifted inside a function that
// was itself just lifted. `k` is not captured by either (only `outer_two`'s
// own body mentions it), so nothing here reaches through an enclosing
// closure. That shape is covered by nested-closure.js in this directory.
function outer_two(k) {
    function mid(j) {
        function inner() {
            return j + j;
        }
        return inner() * 2;
    }
    return mid(k) + mid(k + 1);
}

// An arrow, the shape most real closures have. An arrow's `this` is lexical,
// so lifting one is sound only when it never reads `this`. The lowering checks
// this through the `$enclosing_this` operand: it is non-undefined only for
// arrows, and is the only place the IR marks one. This arrow reads only its
// parameter and its capture, so it lifts like any other; one that reads `this`
// is refused by name in test/CTNative/Lowering/Closures/refusals.mlir.
function arrowed(k) {
    var add = (x) => x + k;
    return add(3) + add(4);
}

// A closure that returns a boolean, so the lowered function's return type is
// `bool`, not `double`, and its result is used as a condition.
function bounded(lo, hi) {
    function inside(x) {
        return x >= lo && x < hi;
    }
    var hits = 0;
    if (inside(1)) {
        hits = hits + 1;
    }
    if (inside(7)) {
        hits = hits + 10;
    }
    if (inside(4)) {
        hits = hits + 100;
    }
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
