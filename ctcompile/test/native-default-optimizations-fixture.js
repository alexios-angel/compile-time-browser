// The same program must agree with the interpreter with native defaults on
// and off. Known return values never authorize removing the effectful calls.
var trace = 0;
function record(value) {
    trace = trace * 10 + value;
    return 40;
}
function compute(input) {
    const first = record(input);
    const answer = first + (6 * 7 - 40);
    if (2 * 3 === 6) { record(2); }
    else { record(9); }
    record(input + 1);
    return answer;
}
function numeric(value) {
    const product = value * 0;
    return product === product ? 1 : 0;
}
var answer = compute(1);
var observed = +trace;
var negativeInfinity = 1 / (-0 * 1);
var literalNaNEqual = (0 / 0) === (0 / 0) ? 1 : 0;
var runtimeNaNEqual = numeric(0 / 0);
var runtimeNumberEqual = numeric(3);
var literalStringEqual = "known" + " text" === "known text" ? 1 : 0;
