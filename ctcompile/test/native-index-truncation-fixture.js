// Regression for ND-8, fixed while adding native Map snapshots. The reference
// truncates a numeric index toward zero before checking bounds, including
// -0.5 -> -0 -> element zero. These observations must agree with that behavior.
// The pipeline also injects an off-by-one to prove the differential gate fails.
function element(i) {
    var a = [10, 20, 30];
    return a[i];
}
var idx_fractional = element(0.5) + 0;
var idx_fractional_high = element(2.9) + 0;
var idx_fractional_negative = element(0 - 0.5) + 0;
// Truncation must still preserve the out-of-range guards.
var idx_past_end = element(3.1) + 0;
var idx_below_start = element(0 - 1.5) + 0;
