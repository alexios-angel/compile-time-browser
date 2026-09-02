// PHASE 54A'S FIXTURE: every row of the transfer function, EXECUTED, so the
// oracle has an observation to hold each claim against.
//
// The unit test in TypeInference.cpp asserts what the analysis SAYS. This file
// exists so the interpreter can say whether that was TRUE. It is written to
// make the analysis wrong if it can be: every operator the analysis guards
// against BigInt is called with BigInts here, so an inference that claimed
// `i32` for `a | b` would meet an observed `bigint` and the checker would name
// the register.
//
// EVERY FUNCTION IS CALLED, because an unobserved register proves nothing -
// the checker keeps "unobserved" as its own count precisely so it cannot be
// mistaken for "sound".

// --- the operand-sensitive rows, called BOTH ways ---------------------------
function bitwise(a, b) {
    var or = a | b;
    var and = a & b;
    var xor = a ^ b;
    var shl = a << b;
    var shr = a >> b;
    return [or, and, xor, shl, shr];
}
function arithmetic(a, b) {
    var sub = a - b;
    var mul = a * b;
    var div = a / b;
    var mod = a % b;
    var pow = a ** b;
    return [sub, mul, div, mod, pow];
}
function negate(a) {
    var neg = -a;
    var not = ~a;
    return [neg, not];
}
bitwise(6, 3);
bitwise(6n, 3n); // BigInt - the whole reason the guard exists
arithmetic(7, 2);
arithmetic(7n, 2n);
negate(5);
negate(5n);

// --- the rows that hold regardless of operands -----------------------------
function unconditional(a, b) {
    var ushr = a >>> b; // a BigInt throws here, so a value is always a Number
    var ty = typeof a;
    var plus = +a; // ToNumber throws on a BigInt rather than returning one
    var lt = a < b;
    var eq = a == b;
    var seq = a === b;
    var cat = "" + a + b; // concat after ToString - a string whatever a and b are
    var bang = !a;
    var v = void a;
    return [ushr, ty, plus, lt, eq, seq, cat, bang, v];
}
unconditional(1, 2);
unconditional(-1, 0); // (-1) >>> 0 is 4294967295, past int32 - f64 and not i32
unconditional("3", "4");
unconditional(1.5, true);
unconditional(null, undefined);

// --- generic `+` is a string OR a number OR a BigInt -----------------------
function plus(a, b) { return a + b; }
plus(1, 2);
plus("a", "b");
plus(1n, 2n);

// --- literals, and the numbers that are NOT int32 despite looking like one --
function literals() {
    var five = 5;
    var half = 1.5;
    var negz = -0;
    var big = 2147483648; // 2**31: one past the int32 maximum
    var s = "text";
    var t = true;
    var u = undefined;
    var n = null;
    return [five, half, negz, big, s, t, u, n];
}
literals();

// --- `++` reaches the STATIC family, and its operand can still be a BigInt --
function counter(x) {
    var i = x;
    i++;
    i += 1;
    return i;
}
counter(1);
// `i += 1` on a BigInt throws "Cannot mix BigInt and other types" - which is
// still an observation worth having for `i++` before it - but an uncaught
// throw ends the whole corpus here and nothing below it would run. Caught.
var caught;
try { counter(1n); } catch (e) { caught = e; }

// --- one register holding several types across calls: the per-register JOIN -
function mixed(x) {
    var r = x;
    if (typeof x === "number") { r = x | 0; }
    return r;
}
mixed(3);
mixed("s");
mixed(2n);

// --- a loop, so block arguments and back edges carry types -----------------
function loop(n, start) {
    var i = 0;
    var acc = start;
    while (i < n) {
        acc = acc + i;
        i = i + 1;
    }
    return acc;
}
loop(10, 0);
// THE ACCUMULATOR LEAVES INT32 IN TEN STEPS, not in two billion: this file
// once called loop(2147483648) to make the same point and the recorder wrote
// down twenty-three billion defs over six and a half minutes.
loop(10, 2147483640);
