// Recursive residual promises retain runtime counters and accumulators.
function modeSum(positive, count, accumulator) {
    if (count <= 0) { return accumulator; }
    if (positive) { return modeSum(positive, count - 1, accumulator + count); }
    return modeSum(positive, count - 1, accumulator - count);
}
function sums(count) {
    return modeSum(false, count, count) + modeSum(true, count, count) +
        modeSum(true, count, count + 1);
}
function alternate(mode, count, accumulator) {
    if (count <= 0) { return accumulator; }
    if (mode) { return alternate(false, count - 1, accumulator + 2); }
    return alternate(true, count - 1, accumulator + 1);
}
function alternates(count) {
    return alternate(false, count, count) + alternate(true, count, count);
}
// A changing static numeric seed triggers the whistle, retaining generic work.
function growing(seed, count) {
    if (count <= 0) { return seed; }
    return growing(seed + 1, count - 1);
}
function growth(count) {
    return growing(0, count) + growing(10, count);
}
var trace = 0;
function record(value) {
    trace = trace * 10 + value;
    return value;
}
function ordered(count) {
    return modeSum(true, record(2), count) + modeSum(true, record(3), count);
}
var sumZero = sums(0);
var sumOne = sums(1);
var sumFive = sums(5);
var sumTwenty = sums(20);
var alternateZero = alternates(0);
var alternateOne = alternates(1);
var alternateFive = alternates(5);
var alternateTwenty = alternates(20);
var growingZero = growth(0);
var growingOne = growth(1);
var growingFive = growth(5);
var orderResult = ordered(7);
var orderTrace = +trace;
var lifetime42 = 40 + modeSum(true, 1, 1);
