// Growing arguments become parameters while exact modes and weights survive.
function weighted(mode, weight, seed, count) {
    if (count <= 0) { return seed; }
    if (mode) { return weighted(mode, weight, seed + weight, count - 1); }
    return weighted(mode, weight, seed - weight, count - 1);
}
function weights(count) {
    return weighted(true, 2, 0, count) + weighted(true, 2, 10, count) +
        weighted(false, 3, 50, count) + weighted(true, 2, 20, count);
}
function append(mode, suffix, text, count) {
    if (count <= 0) { return text; }
    if (mode) { return append(mode, suffix, text + suffix, count - 1); }
    return append(mode, suffix, suffix + text, count - 1);
}
function strings(count) {
    return append(true, "x", "", count) + append(true, "ab", "s", count) +
        append(false, "!", "e", count) + append(true, "ab", "t", count);
}
// A literal reset must not recover the generalized seed's static binding.
function reset(mode, seed, count) {
    if (count <= 0) { return seed; }
    if (mode) {
        if (seed < 2) { return reset(mode, seed + 1, count - 1); }
        return reset(mode, 0, count - 1);
    }
    return reset(mode, seed - 1, count - 1);
}
function resets(count) {
    return reset(false, count, count) + reset(true, count, count) +
        reset(true, 1, count);
}
var trace = 0;
function record(value) {
    trace = trace * 10 + value;
    return value;
}
function ordered(count) {
    return weighted(true, 2, 30, record(2)) +
        weighted(false, 3, record(4), count) +
        weighted(true, 2, 40, record(3));
}
var baseWeights = weights(0);
var weightsOne = weights(1);
var weightsFive = weights(5);
var weightsTwenty = weights(20);
var stringsZero = +(strings(0) === "set");
var stringsOne = +(strings(1) === "xsab!etab");
var stringsFour = +(strings(4) === "xxxxsabababab!!!!etabababab");
var resetsZero = resets(0);
var resetsOne = resets(1);
var resetsTwo = resets(2);
var resetsFive = resets(5);
var resetsTwenty = resets(20);
var orderedResult = ordered(5);
var orderedTrace = +trace;
var lifetime42 = weights(0) - 38;
