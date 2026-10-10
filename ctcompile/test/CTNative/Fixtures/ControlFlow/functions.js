// Fixture for the native (GC-free) C++ target; see functions.emitc.mlir.
//
// Numbers and booleans only: functions with parameters, recursion, a counted
// `for`, a pre-tested `while`, `if`/`else`, every arithmetic operator, the
// comparisons, unary minus, and answers that are integral, fractional, NaN,
// +Infinity, -Infinity and -0. Nothing here allocates: no string, no object,
// no array, no closure over mutable state. This is the smallest subset the
// native lowering handles, and functions.emitc.mlir is the expected lowering
// of this file.
//
// Ten numeric globals with ten distinct answers, so a wrong one cannot hide
// behind a right one. The eight functions are globals too; the reference
// counts them and skips them.
//
// Names are valid C++ at global scope. A JavaScript global called `nan` or
// `inf`, or a function called `remainder`, would collide with <cmath> once it
// becomes a C++ global. This file avoids those names so that a failure here
// means the lowering is wrong, not that the names collided.
//
// The answers, worked out by hand and not asserted anywhere (the interpreter is
// the reference). They are here so a reader can tell a wrong fixture from a
// wrong compiler:
//   fib20 = 6765            sum100 = 5050          collatz27 = 111
//   third = 0.33333333333333331                    negzero = -0
//   inf_pos = inf           inf_neg = -inf         not_a_number = nan
//   mod_neg = -1.5          clamped = 1.25

function fib(n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

function sum_to(n) {
    var s = 0;
    for (var i = 1; i <= n; i = i + 1) {
        s = s + i;
    }
    return s;
}

function collatz_steps(n) {
    var steps = 0;
    while (n !== 1) {
        if (n % 2 === 0) {
            n = n / 2;
        } else {
            n = 3 * n + 1;
        }
        steps = steps + 1;
    }
    return steps;
}

function ratio(a, b) {
    return a / b;
}

function negate(x) {
    return -x;
}

function modulo(a, b) {
    return a % b;
}

function is_between(x, lo, hi) {
    return x >= lo && x < hi;
}

function clamp01(x) {
    if (is_between(x, 0, 1)) {
        return x;
    }
    if (x < 0) {
        return 0;
    }
    return 1;
}

var fib20 = fib(20);
var sum100 = sum_to(100);
var collatz27 = collatz_steps(27);
var third = ratio(1, 3);
var negzero = negate(0);
var inf_pos = ratio(1, 0);
var inf_neg = ratio(-1, 0);
var not_a_number = ratio(0, 0);
var mod_neg = modulo(-7.5, 2);
var clamped = clamp01(negate(third)) + clamp01(2) + clamp01(0.25);
