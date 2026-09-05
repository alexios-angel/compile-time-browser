// Primitive precomputation keeps every runtime producer and selected effect.
var trace = 0;
function recordBoolean(value) {
    trace = trace * 10 + value;
    return value > 0;
}
function symbolicBoolean(value) {
    const flag = recordBoolean(value);
    return flag === flag ? 42 : -1;
}
function recordForty(value) {
    trace = trace * 10 + value;
    return 40;
}
function resultOfEffect(value) {
    const answer = recordForty(value);
    return answer + 2;
}
function voidOfEffect(value) {
    return (void recordForty(value)) === (void 0) ? 1 : 0;
}
function selectedEffects(value) {
    if ("4" + 2 === "42") { recordForty(5); }
    else { recordForty(9); }
    recordForty(value);
    return 42;
}
function symbolicTypes(value) {
    const flag = value < 0;
    const kind = typeof value;
    return (flag === flag ? 1 : 0) + (kind === kind ? 2 : 0);
}
function literalEdges() {
    var score = 0;
    if (!(0 / 0 === 0 / 0)) { score += 1; }
    if (1 / (-0 + 0) > 0) { score += 2; }
    if (1 / (-0 * 1) < 0) { score += 4; }
    if ((1 / 0) * 0 !== (1 / 0) * 0) { score += 8; }
    if ((0 / 0) ** 0 === 1) { score += 16; }
    if (+"0x2a" === 42) { score += 32; }
    if (null == (void 0)) { score += 64; }
    if (typeof null === "object") { score += 128; }
    if ("" + null === "null") { score += 256; }
    if ("40" + 2 === "402") { score += 512; }
    return score;
}
function residualArithmetic(value) {
    const number = +value;
    const sum = number + 0;
    const product = number * 0;
    return (number === number ? 1 : 0) + (sum === 0 ? 2 : 0) +
        (1 / sum < 0 ? 4 : 0) + (1 / product < 0 ? 8 : 0) +
        (product === product ? 16 : 0);
}
function runtimeBranch(value) {
    if (value < 0) { return value - 1; }
    return value + 1;
}
var booleanFirst = symbolicBoolean(2);
var booleanSecond = symbolicBoolean(3);
var traceBoolean = +trace;
var effectResult = resultOfEffect(4);
var traceResult = +trace;
var effectVoid = voidOfEffect(6);
var traceVoid = +trace;
var selected = selectedEffects(7);
var traceSelected = +trace;
var symbolicNegative = symbolicTypes(-1);
var symbolicPositive = symbolicTypes(1);
var literalScore = literalEdges();
var arithmeticZero = residualArithmetic(-0);
var arithmeticNan = residualArithmetic(0 / 0);
var arithmeticInfinity = residualArithmetic(1 / 0);
var arithmeticNumber = residualArithmetic(2);
var branchNegative = runtimeBranch(-4);
var branchPositive = runtimeBranch(4);
var lifetime42 = resultOfEffect(8);
