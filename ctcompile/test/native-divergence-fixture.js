// THE DECLARED DIVERGENCES, AS A PROGRAM THE TIER ACCEPTS - part 24 Phase 63
// Step 5, the EMITTED half.
//
// ctcompile/docs/native-divergences.md lists every place the native tier's
// answer legitimately differs, or must not differ, from the interpreter's.
// Half of that list is refusals, and a refusal is pinned by its text
// (CTNative/Lowering/divergence-refusals.mlir). The other half is EMITTED:
// the tier lowers the operation and claims the two sides agree. A claim like
// that is worth nothing until a binary and an interpreter have both answered,
// so every witness below is a global this file's differential gate compares -
// ND-4 (`**` and its guard), ND-5 (`%` is fmod), ND-6 (-0 and the printing
// convention) and ND-7 (undefined carried as NaN, in the three places where
// that is exact).
//
// EVERY GLOBAL HERE IS A NUMBER, and that is a constraint of the gate rather
// than a style: check-native-unit.cmake compares Number-valued globals line by
// line, so a boolean or a string global would be skipped by the reference,
// printed by the binary, and would fail the gate for a reason that has nothing
// to do with a divergence. Booleans are converted with `? 1 : 0` for that
// reason and no other.
//
// AND EVERY GLOBAL IS ASSIGNED BEFORE IT IS READ. A never-written global is
// `undefined` in the interpreter (skipped, not printed) and NaN natively
// (printed), which is a missing-line failure rather than a wrong answer.

// === ND-4: `**` IS NOT std::pow ===============================================
//
// Number::exponentiate answers NaN when the base has magnitude one and the
// exponent is NaN or infinite; C99 pow answers 1 for all of those. The tier
// emits the guard rather than refusing the operator, so these five must come
// back NaN from the binary as well as from the interpreter. `-1 ** x` is a
// SyntaxError in JavaScript (the unary minus may not be the base of `**`
// without parentheses), which is why the base is spelled `(0 - 1)`.
var pow_one_nan = 1 ** (0 / 0);
var pow_one_inf = 1 ** (1 / 0);
var pow_one_ninf = 1 ** (-1 / 0);
var pow_mone_inf = (0 - 1) ** (1 / 0);
var pow_mone_ninf = (0 - 1) ** (-1 / 0);
// AND THE CASES THE GUARD MUST NOT CATCH. A guard that answered NaN whenever
// an exponent was not finite, or whenever a base was one, would pass the five
// above and be wrong here - so the negative half of the guard is pinned too.
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

// === ND-5: `%` IS fmod, IN ALL FOUR SIGN QUADRANTS ============================
//
// JavaScript's remainder takes the sign of the DIVIDEND, which is C's fmod and
// is not the modulo of Python or of a mathematician. The two agree exactly,
// including on the three edge cases below, and this block is what says so.
var mod_pp = 7.5 % 2;
var mod_np = (0 - 7.5) % 2;
var mod_pn = 7.5 % (0 - 2);
var mod_nn = (0 - 7.5) % (0 - 2);
var mod_zero_divisor = 5 % 0;
var mod_inf_dividend = (1 / 0) % 2;
var mod_inf_divisor = 5 % (1 / 0);

// === ND-6: -0 IS A DISTINCT VALUE AND SURVIVES THE PRINTING CONVENTION ========
//
// `%.17g` prints -0 as `-0`, so the two sides are compared on the sign of a
// zero and not only on its magnitude. `negzero_recip` is the arithmetic
// witness that makes the distinction observable as a Number rather than only
// as a spelling: 1/-0 is -Infinity and 1/+0 is +Infinity.
var negzero = 0 * (0 - 1);
var poszero = 0 * 1;
var negzero_recip = 1 / (0 * (0 - 1));
var poszero_recip = 1 / (0 * 1);
// -0 + 0 IS +0 in IEEE-754 round-to-nearest, in both languages: the one case
// where adding zero is not the identity.
var negzero_plus_zero = 0 * (0 - 1) + 0;
var negzero_times_neg = (0 * (0 - 1)) * (0 - 1);

// === ND-7: undefined IS CARRIED AS NaN, WHERE THAT IS EXACT ===================
//
// A field that is read and never written is `undefined` in the interpreter and
// a NaN double natively. The representation is exact for ARITHMETIC,
// RELATIONAL comparison and TRUTHINESS, and those three are what this block
// exercises. It is NOT exact for equality, `typeof` or printing, and the tier
// refuses all three - divergence-refusals.mlir pins the refusals, because a
// refusal cannot be witnessed by a program that runs.
function undefined_field() {
    var o = { seen: 1 };
    return o.later;
}
var u_plus = undefined_field() + 1;
var u_minus = undefined_field() - 1;
var u_times = undefined_field() * 0;
var u_div = undefined_field() / 2;
var u_mod = undefined_field() % 2;
var u_pow = undefined_field() ** 2;
// RELATIONAL: every comparison with undefined is false, and every comparison
// with NaN is false, for the same four operators. That is the whole of the
// exactness claim, so all four are asked.
var u_lt = undefined_field() < 1 ? 1 : 0;
var u_le = undefined_field() <= 1 ? 1 : 0;
var u_gt = undefined_field() > 1 ? 1 : 0;
var u_ge = undefined_field() >= 1 ? 1 : 0;
// TRUTHINESS: undefined is falsy and NaN is falsy.
var u_truthy = 0;
if (undefined_field()) { u_truthy = 1; }
var u_not = !undefined_field() ? 1 : 0;
// AND AN UNDEFINED-OR-BOOLEAN, whose carrier is `bool` with undefined as
// false. Same claim, other carrier: exact in a branch and under `!`.
function maybe_flag(x) {
    var f = { seen: 1 };
    if (x > 0) { f.flag = true; }
    return f.flag ? 1 : 0;
}
var b_undefined_is_false = maybe_flag(0 - 1);
var b_set_is_true = maybe_flag(1);

// === ND-8: AN OUT-OF-RANGE INDEX IS undefined, WHICH IS NaN, NOT C++ UB ======
//
// `a[7]` on a three-element array is `undefined` in JavaScript and UNDEFINED
// BEHAVIOUR through `std::vector::operator[]` in C++. The tier emits
// `ctnative::vec_at`, which answers NaN - undefined's carrier - for every
// index that is negative, fractional, or not less than `size()`. That is a
// guard, so it is witnessed here rather than refused. `+ 0` turns the
// undefined into a NaN the gate can compare: a global holding undefined is
// not a Number, and the reference would skip it.
//
// NO FRACTIONAL INDEX HERE, AND THE OMISSION IS A DEFECT AND NOT A CHOICE.
// This interpreter TRUNCATES a fractional index - `[10,20,30][0.5]` is 10 -
// where `vec_at` answers NaN, so a witness of it belongs in the fixture that
// is registered to FAIL: native-index-truncation-fixture.js. ND-8 in
// ctcompile/docs/native-divergences.md is the whole story.
function element(i) {
    var a = [10, 20, 30];
    return a[i];
}
var idx_in_range = element(1);
var idx_past_end = element(7) + 0;
var idx_at_length = element(3) + 0;
var idx_negative = element(0 - 1) + 0;
var idx_nan = element(0 / 0) + 0;
var idx_infinite = element(1 / 0) + 0;
