// Factory initialization is evaluated; observers still execute at runtime.
function sharedSeed() {
    const scratch = { amount: 10 };
    scratch.amount = 40;
    const discarded = new Map();
    discarded.set("temporary", 99);
    const child = new Map();
    child.set("value", scratch.amount + 2);
    child.set("deleted", -1);
    child.delete("deleted");
    const root = new Map();
    root.set("left", child);
    root.set("right", child);
    return root;
}
function observeSharing(first, second) {
    if (!first.has("left")) { return -1; }
    if (!first.has("right")) { return -2; }
    if (!second.has("right")) { return -3; }
    first.get("left").set("value", 43);
    return first.get("right").get("value") * 1000 + second.get("right").get("value");
}
function orderedSeed() {
    const map = new Map();
    map.set(9, 1);
    map.set(4, 2);
    map.set(9, 3);
    map.delete(4);
    map.set(4, 4);
    return map;
}
function observeOrder(map) {
    return map.keys()[0] * 1000 + map.keys()[1] * 100 +
        map.values()[0] * 10 + map.values()[1];
}
function keyedSeed() {
    const first = {};
    const second = {};
    const map = new Map();
    map.set(first, 20);
    map.set(second, 22);
    map.set(first, 21);
    return map;
}
function observeKeys(map) {
    return map.size * 10000 + map.values()[0] * 100 + map.values()[1];
}
function clearedSeed() {
    const map = new Map();
    map.set("discarded", 99);
    map.clear();
    map.set("answer", 42);
    return map;
}
function observeCleared(map) { return map.size * 100 + map.get("answer"); }
function constantLoop(count) {
    var total = 0;
    for (var i = 0; i < count; ++i) { total += i; }
    return total;
}
function branchSeed(choose) {
    const map = new Map();
    if (choose) { map.set("answer", 42); }
    else { map.set("answer", -1); }
    return map;
}
// ctbrowser's primitive helpers supply these conversion and comparison rules.
function primitiveSeed(text) {
    var score = 0;
    if (text + 2 === "402") { score += 1; }
    if (+text === 40) { score += 2; }
    if (text == 40) { score += 4; }
    if (text < "5") { score += 8; }
    if (typeof null === "object") { score += 16; }
    if (typeof (void 0) === "undefined") { score += 32; }
    if (!(0 / 0 === 0 / 0)) { score += 64; }
    if (1 / -0 < 0) { score += 128; }
    if (+"0x2a" === 42) { score += 256; }
    if (null == (void 0)) { score += 512; }
    if ((0 / 0) ** 0 === 1) { score += 1024; }
    return score;
}
function numericKeySeed() {
    const map = new Map();
    map.set(0 / 0, 20);
    map.set(-0, 22);
    map.set(0 / 0, 21);
    map.set(0, 23);
    return map;
}
function observeNumericKeys(map) {
    return map.size * 10000 + map.get(0 / 0) * 100 + map.get(0);
}
var primitiveSemantics = primitiveSeed("40");
var numericKeySemantics = observeNumericKeys(numericKeySeed());
var aliases = observeSharing(sharedSeed(), sharedSeed());
var order = observeOrder(orderedSeed());
var identity = observeKeys(keyedSeed());
var cleared = observeCleared(clearedSeed());
var loop = constantLoop(7);
var lifetime42 = observeCleared(branchSeed(true));
