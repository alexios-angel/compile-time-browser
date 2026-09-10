function placements(direction) {
    var isRTL = function () { return direction === "rtl"; };
    var topStart = isRTL() ? "top-end" : "top-start";
    var topEnd = isRTL() ? "top-start" : "top-end";
    var bottomStart = isRTL() ? "bottom-end" : "bottom-start";
    var bottomEnd = isRTL() ? "bottom-start" : "bottom-end";
    var rightStart = isRTL() ? "left-start" : "right-start";
    var leftStart = isRTL() ? "right-start" : "left-start";
    return topStart + "," + topEnd + "," + bottomStart + "," + bottomEnd + "," + rightStart + "," + leftStart;
}
var rtl63 = placements("rtl") === "top-end,top-start,bottom-end,bottom-start,left-start,right-start" ? 63 : 0;
var ltr63 = placements("ltr") === "top-start,top-end,bottom-start,bottom-end,right-start,left-start" ? 63 : 0;

var startup42 = (function initialize(factory) {
    return factory("rtl") === "top-end" ? 42 : 0;
})(function factory(direction) {
    return direction === "rtl" ? "top-end" : "top-start";
});

function decorate(value) {
    return "pre-" + value + "-post";
}
var returned7 = decorate("rtl") === "pre-rtl-post" ? 7 : 0;

function choose(mode, left, right) {
    if (mode > 0) { return left; }
    return right;
}
var branches11 = choose(1, "rtl", "ltr") === "rtl" && choose(-1, "rtl", "ltr") === "ltr" ? 11 : 0;

function repeated(count) {
    var value = "";
    for (var index = 0; index < count; index = index + 1) {
        value = value + "ab";
    }
    return value;
}
var loop13 = repeated(3) === "ababab" && repeated(0) === "" ? 13 : 0;

function truth(value) {
    return value ? 1 : 0;
}
function negate(value) {
    return !value ? 10 : 0;
}
var truth11 = truth("\0") + truth("") + negate("") + negate("false");

function join(left, right) {
    return left + right;
}
var nul17 = join("a\0", "b") === "a\0b" && join("a\0", "b") !== "a\0c" ? 17 : 0;
var unicode19 = join("caf\u00e9", "\u{1f600}") === "caf\u00e9\u{1f600}" ? 19 : 0;
var surrogate23 = join("\ud800", "x") === "\ud800x" && join("\ud800", "x") !== "\ud801x" ? 23 : 0;
var split41 = join("\ud83d", "\ude00") === "\ud83d\ude00" ? 0 : 41;
var escaping29 = join("\"\\\n", "9A") === "\"\\\n9A" ? 29 : 0;
var loose31 = join("r", "tl") == "rtl" ? 31 : 0;

function shared(seed) {
    var value = seed;
    var append = function (suffix) { value = value + suffix; return value; };
    var first = append("x");
    var second = append("y");
    return first === "0123456789abcdef0123456789abcdefx" && second === "0123456789abcdef0123456789abcdefxy" && value === "0123456789abcdef0123456789abcdefxy" ? 37 : 0;
}
// Exceed small-string storage so the snapshot survives a heap-backed mutation.
var shared37 = shared("0123456789abcdef0123456789abcdef");

function copied() {
    var first = "a";
    var snapshot = first;
    first = first + "b";
    return snapshot + ":" + first;
}
var copied43 = copied() === "a:ab" ? 43 : 0;

function keyAndValue() {
    var key = "tag";
    var record = {tag: 42};
    return key + (record[key] === 42 ? "!" : "?");
}
var key47 = keyAndValue() === "tag!" ? 47 : 0;

function discardedTruth(value) {
    !value;
    return value;
}
var discarded53 = discardedTruth("x") === "x" ? 53 : 0;

function lengthKeyAndValue() {
    var key = "length";
    var values = [10, 20];
    return key + (values[key] === 2 ? "!" : "?");
}
var lengthKey49 = lengthKeyAndValue() === "length!" ? 49 : 0;
