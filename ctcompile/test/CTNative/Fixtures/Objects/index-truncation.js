// Regression for ND-8. A Number key selects an element only when it is an
// integral index; a fractional one names a property ("0.5", "-0.5") the array
// does not have, so the read is undefined, as in Node and in the reference
// since it stopped truncating. -0 names "0" and selects element zero.
// The pipeline also injects an off-by-one to prove the differential gate fails.
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
