// Divergence ND-8 (ctcompile/docs/native-divergences.md). A Number key selects
// an element only when it is an integral index; a fractional key names a
// property ("0.5", "-0.5") the array does not have, so the read is undefined,
// as in Node and the interpreter. -0 names "0" and selects element zero.
// index-truncation.test also checks that an off-by-one global is caught.
function element(i) {
    var a = [10, 20, 30];
    return a[i];
}
var idx_fractional = element(0.5) + 0;
var idx_fractional_high = element(2.9) + 0;
var idx_fractional_negative = element(0 - 0.5) + 0;
// Integral keys, -0 included, still select their elements.
var idx_integral = element(2) + 0;
var idx_negative_zero = element(-0) + 0;
// The out-of-range guards stay.
var idx_past_end = element(3.1) + 0;
var idx_below_start = element(0 - 1.5) + 0;
