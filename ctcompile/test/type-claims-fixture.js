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
// `i++` on a BigInt throws "Cannot mix BigInt and other types" - the compiler
// loads `1` as a Number and emits op::add, so the static family's BigInt arm
// sees one of each - and an uncaught throw would end the whole corpus here.
// Caught. The throwing instruction's def is dropped by the recorder's
// expect_pc guard, so nothing is observed for it; the point is that the rest
// of this file still runs.
// ONE PROTECTED REGION IN THE WHOLE FILE, inside `guard`: the importer
// refuses a function with more than one, and a top level with several
// try statements would leave every top-level register unclaimed.
function guard(f, a, b) { try { return f(a, b); } catch (e) { return e; } }
guard(counter, 1n);

// --- one register holding several types across calls: the per-register JOIN -
function mixed(x) {
    var r = x;
    if (typeof x === "number") { r = x | 0; }
    return r;
}
mixed(3);
mixed("s");
mixed(2n);

// --- SURPLUS ARGUMENTS, which land in the callee's LOCAL slots at entry -----
//
// context::call copies every argument into the callee's window, declared or
// not, so `callback(value, index, array)` from map puts the index in slot 1
// and the array in slot 2 of a one-parameter function - on top of `y`. The
// recorder must not report those as observations of `y`: the compiler
// initialises `y` explicitly before any read, and the surplus is reachable
// only through `arguments` and rest parameters, which read the raw window.
// Six one-parameter callbacks in p5 and phaser found this.
function surplus(x) {
    var y;
    var seen = arguments.length;
    return [y, seen];
}
[10, 20].map(surplus);
surplus(1, "two", [3]);

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

// ============================================================================
// THE POSITIVE NUMERIC ROWS, WHICH EVERYTHING ABOVE LEAVES UNOBSERVED.
//
// Every operand-sensitive row fires only when both operands are PROVED not to
// be BigInts, and a parameter is never proved anything - so `bitwise(a, b)`
// and `arithmetic(a, b)` above only ever observe the boxed fall-back. The
// functions below launder their parameters through `+a` (ToNumber throws on a
// BigInt, so its result is a proved f64) and through literals, so the i32 and
// f64 claims are actually MADE and the interpreter can hold them to account.
// Found by the adversarial review's fixture critic.
function provenBits(a, b) {
    var x = +a, y = +b;
    var or = x | y, and = x & y, xor = x ^ y, shl = x << y, shr = x >> y, not = ~x;
    var nl = ~"7", nb = ~true, nn = ~null;
    return [or, and, xor, shl, shr, not, nl, nb, nn];
}
provenBits(1, 31);           // 1 << 31 is the int32 minimum
provenBits(2147483648, 0);   // wraps to the int32 minimum
provenBits(0x7fffffff, 1);   // max | 0; max << 1 is -2
provenBits(-1, 0);
provenBits(1, 1e400);        // Infinity >>> ToUint32 is 0
provenBits(void 0, "1.9");   // NaN | 0 is 0; a fractional shift count

function provenArith(a, b) {
    var x = +a, y = +b;
    var sub = x - y, mul = x * y, div = x / y, mod = x % y, pow = x ** y, neg = -x;
    return [sub, mul, div, mod, pow, neg];
}
provenArith(1, 0);           // 1/0 is Infinity, 1%0 is NaN
provenArith(0, 0);           // 0/0 is NaN
provenArith(0, -1);          // 0 * -1 is -0, which the Mul row must call f64
provenArith(2, 31);          // 2**31 is wide
provenArith(1, 1e400);       // 1**Infinity is NaN
provenArith(-8, 1 / 3);      // a NaN pow
provenArith(2147483647, -1); // sub leaves int32

// THE STATIC FAMILY'S `+`, which only `++` and the internal counters reach.
function counters() {
    var k = 0; k++; ++k;
    var m = 2147483647; m++;   // 2147483648: the row must be f64, never i32
    var f = 0.5; f++;
    var d = 0; d--;            // op::sub on proved operands
    for (var i = 0; i < 3; i++) {}
    return [k, m, f, d, i];
}
counters();
function internalCounters() {
    var last = 0;
    for (var e of [1, 2]) { last = e; }
    var keys = [];
    for (var k in { a: 1 }) { keys.push(k); }
    var spread = [...[3, 4]];
    var [p, ...rest] = [5, 6, 7];
    return [last, keys, spread, p, rest];
}
internalCounters();

// NON-NUMBER LITERALS ARE PROVED NON-BIGINT TOO: a string, a boolean, null and
// undefined all accept the numeric rows, and the interpreter converts.
function literalOperands() {
    var s = "3" * "4", t = "a" - 1, h = "0x10" | 0, b = true << 1, n = null | 0,
        u = (void 0) - 1, sb = "3" | "1.9", sn = -"x", tn = -true;
    return [s, t, h, b, n, u, sb, sn, tn];
}
literalOperands();

// CONCAT is emitted only for template literals; `"" + a` is add_generic.
function tmpl(a, b) { return `${a}${b}`; }
tmpl(1, 2);
tmpl(1n, 2n);
tmpl(Symbol("s"), null);
tmpl({ toString: function () { return "o"; } }, [1, [2]]);
tmpl(void 0, true);

// THE UNCONDITIONAL ROWS, fed the operands their justification names.
function plusKinds(a) { return +a; }
plusKinds({ valueOf: function () { return 2.5; } });
plusKinds([]);
plusKinds("1e400");
function plusBig(a) { return +a; }
guard(plusBig, 1n);   // ToNumber throws on a BigInt: no value, no observation
function ushrBig(a, b) { return a >>> b; }
guard(ushrBig, 1n, 0n);
ushrBig({ valueOf: function () { return 7; } }, 1);
function mixBig(a, b) { return a - b; }
guard(mixBig, 1n, 1);
function divBig(a, b) { return a / b; }
guard(divBig, 1n, 0n);
function powBig(a, b) { return a ** b; }
guard(powBig, 2n, -1n);

function compares(a, b) {
    var lt = a < b, le = a <= b, gt = a > b, ge = a >= b, eq = a == b, ne = a != b,
        seq = a === b, sne = a !== b;
    return [lt, le, gt, ge, eq, ne, seq, sne];
}
compares(1n, 2);
compares(0n, 0n);
compares({ valueOf: function () { return 1; } }, 1);
compares("b", "a");
compares(NaN, NaN);
function inst(a, b) { return a instanceof b; }
inst([], Array);
inst({}, Array);
function has(k, o) { return k in o; }
has("length", [1]);
has(0, [1]);
has("x", { x: 1 });
function ty(a) { return typeof a; }
ty(1n); ty(Symbol("s")); ty(function () {}); ty({}); ty([]);
function bang(a) { return !a; }
bang(0n); bang(1n); bang({}); bang(Symbol("s")); bang([]);

// LITERAL EDGES: the int32 bounds, a hex literal, an integral wide literal, an
// Infinity literal (which the parser reads as out-of-range) and a NaN.
function literalEdges() {
    var max = 2147483647, hex = 0x7fffffff, wide = 1e10, inf = 1e400, ninf = -1e400,
        min = -2147483648, nan = 0 / 0, u = void 0;
    return [max, hex, wide, inf, ninf, min, nan, u];
}
literalEdges();

// THE NaN THAT WAS A BOOLEAN. 0x7FF4000000000003 read out of a Float64Array is
// a number whose first subtraction used to quiet it into tag_true, so the
// interpreter observed `bool` where the inference had proved f64. view_get now
// canonicalises; this is the observation that keeps it that way.
function poke(v) { var x = +v; var y = x - 1; var z = x * 1; var w = x; w++; return [y, z, w]; }
var nanBuf = new ArrayBuffer(8), nanBytes = new Uint8Array(nanBuf), nanF64 = new Float64Array(nanBuf);
nanBytes[6] = 0xF4; nanBytes[7] = 0x7F;
for (var payload = 0; payload < 4; payload++) { nanBytes[0] = payload; poke(nanF64[0]); }
