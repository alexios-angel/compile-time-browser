// The documented native/interpreter divergences that the native compiler
// lowers instead of refusing, written as a program it accepts.
//
// ctcompile/docs/native-divergences.md lists every place the native target's
// answer legitimately differs, or must not differ, from the interpreter's.
// Half of that list is refusals, and a refusal is checked by its text
// (test/CTNative/Lowering/Admission/divergence-refusals.mlir). The other half
// is emitted: the compiler lowers the operation and claims both sides agree.
// That claim means nothing until a binary and the interpreter have both run,
// so every witness below is a global the differential test compares:
// ND-4 (`**` and its guard), ND-5 (`%` is fmod), ND-6 (-0 and the printing
// convention) and ND-7 (undefined carried as NaN, in the three places where
// that is exact). ND-8 (an out-of-range index) closes the file.
//
// Every global here is a Number, so each comparison is between two doubles
// printed with Number::toString (-0 kept); booleans are converted with
// `? 1 : 0` to keep it so.
// The differential test (test/CTNative/Checks/compilation-unit.py) also
// prints other kinds, but a Number comparison is what each divergence claim
// is about.
//
// Every global is assigned before it is read. A never-written global is
// `undefined` in the interpreter (not printed) and NaN natively (printed),
// which would be a missing-line failure rather than a wrong answer, and would
// say nothing about the divergence under test.

// === ND-4: `**` is not std::pow ===============================================
//
// Number::exponentiate answers NaN when the base has magnitude one and the
// exponent is NaN or infinite; C99 pow answers 1 for all of those. The compiler
// emits the guard rather than refusing the operator, so these five must come
// back NaN from the binary as well as from the interpreter. `-1 ** x` is a
// SyntaxError in JavaScript (the unary minus may not be the base of `**`
// without parentheses), which is why the base is spelled `(0 - 1)`.
var pow_one_nan = 1 ** (0 / 0);
var pow_one_inf = 1 ** (1 / 0);
var pow_one_ninf = 1 ** (-1 / 0);
var pow_mone_inf = (0 - 1) ** (1 / 0);
var pow_mone_ninf = (0 - 1) ** (-1 / 0);
// The cases the guard must not catch. A guard that answered NaN whenever an
// exponent was not finite, or whenever a base was one, would pass the five
// above and be wrong here, so the negative half of the guard is checked too.
// `pow_nan_zero` is the C99 rule JavaScript agrees with: anything to the zero
// is 1, NaN included.
var pow_nan_zero = (0 / 0) ** 0;
var pow_two_31 = 2 ** 31;
var pow_two_inf = 2 ** (1 / 0);
var pow_half_inf = 0.5 ** (1 / 0);
var pow_zero_ninf = 0 ** (-1 / 0);
var pow_root = 4 ** 0.5;
var pow_neg_odd = (0 - 2) ** 3;
var pow_two_nan = 2 ** (0 / 0);

// === ND-5: `%` is fmod, in all four sign quadrants ============================
//
// JavaScript's remainder takes the sign of the dividend, which is C's fmod and
// not the modulo of Python or of a mathematician. The two agree exactly,
// including on the three edge cases below, and this block shows it.
var mod_pp = 7.5 % 2;
var mod_np = (0 - 7.5) % 2;
var mod_pn = 7.5 % (0 - 2);
var mod_nn = (0 - 7.5) % (0 - 2);
var mod_zero_divisor = 5 % 0;
var mod_inf_dividend = (1 / 0) % 2;
var mod_inf_divisor = 5 % (1 / 0);

// === ND-6: -0 is a distinct value and survives the printing convention ========
//
// The convention prints -0 as `-0`, so the two sides are compared on the sign
// of a zero and not only on its magnitude. `negzero_recip` is the arithmetic
// witness that makes the distinction observable as a Number rather than only
// as a spelling: 1/-0 is -Infinity and 1/+0 is +Infinity.
var negzero = 0 * (0 - 1);
var poszero = 0 * 1;
var negzero_recip = 1 / (0 * (0 - 1));
var poszero_recip = 1 / (0 * 1);
// -0 + 0 is +0 in IEEE-754 round-to-nearest, in both languages: the one case
// where adding zero is not the identity.
var negzero_plus_zero = 0 * (0 - 1) + 0;
var negzero_times_neg = (0 * (0 - 1)) * (0 - 1);

// === ND-7: undefined is carried as NaN, where that is exact ===================
//
// A field that is read and never written is `undefined` in the interpreter and
// a NaN double natively. The representation is exact for arithmetic,
// relational comparison and truthiness, and those three are what this block
// exercises. It is not exact for equality, `typeof` or printing, and the
// compiler refuses all three; divergence-refusals.mlir checks the refusals,
// because a refusal cannot be witnessed by a program that runs.
function undefined_field() {
    var o = {
        seen: 1
    };
    return o.later;
}
var u_plus = undefined_field() + 1;
var u_minus = undefined_field() - 1;
var u_times = undefined_field() * 0;
var u_div = undefined_field() / 2;
var u_mod = undefined_field() % 2;
var u_pow = undefined_field() ** 2;
// Relational: every comparison with undefined is false, and every comparison
// with NaN is false, for the same four operators. That is the whole of the
// exactness claim, so all four are tested.
var u_lt = undefined_field() < 1 ? 1 : 0;
var u_le = undefined_field() <= 1 ? 1 : 0;
var u_gt = undefined_field() > 1 ? 1 : 0;
var u_ge = undefined_field() >= 1 ? 1 : 0;
// Truthiness: undefined is falsy and NaN is falsy.
var u_truthy = 0;
if (undefined_field()) {
    u_truthy = 1;
}
var u_not = !undefined_field() ? 1 : 0;
// An undefined-or-boolean, whose carrier is `bool` with undefined as false.
// Same claim, other carrier: exact in a branch and under `!`.
function maybe_flag(x) {
    var f = {
        seen: 1
    };
    if (x > 0) {
        f.flag = true;
    }
    return f.flag ? 1 : 0;
}
var b_undefined_is_false = maybe_flag(0 - 1);
var b_set_is_true = maybe_flag(1);

// === ND-8: an out-of-range index is undefined, which is NaN, not C++ UB ======
//
// `a[7]` on a three-element array is `undefined` in JavaScript and undefined
// behaviour through `std::vector::operator[]` in C++. The compiler emits
// `ctnative::vec_at`, which answers NaN (undefined's carrier) for every
// index that is negative, fractional, or not less than `size()`. That is a
// guard, so it is witnessed here rather than refused.
//
// The calls below cover an index in range, past the end, exactly at the
// length, negative, NaN and Infinity; each takes a different path through
// the guard.
//
// Fractional indices are not tested here. They have their own fixture,
// test/CTNative/Fixtures/Objects/index-truncation.js; see ND-8 in
// ctcompile/docs/native-divergences.md.
function element(i) {
    var a = [10, 20, 30];
    return a[i];
}
// Each result gets `+ 0` so every global is a plain Number. `a[i]` has type
// opt<num> however this function is called: `i` is a parameter nothing bounds,
// and an index past the end, negative or fractional reads undefined (the ND-8
// guard above). That is a real possibility, not imprecision, so even the
// in-range call needs the `+ 0`.
//
// Expected: idx_in_range is 20, and the other five are NaN (undefined's
// carrier) on both sides. The test does not hard-code these values: it runs
// this file through the interpreter and compares, so the interpreter is the
// oracle.
var idx_in_range = element(1) + 0;
var idx_past_end = element(7) + 0;
var idx_at_length = element(3) + 0;
var idx_negative = element(0 - 1) + 0;
var idx_nan = element(0 / 0) + 0;
var idx_infinite = element(1 / 0) + 0;
