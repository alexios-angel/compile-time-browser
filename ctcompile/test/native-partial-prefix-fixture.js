// These callers deliberately supply different arguments. Only each factory's
// initialization prefix is static; its boundary and suffix execute at runtime.
function mixedSeed(input) {
    const scratch = { amount: 10 };
    scratch.amount = 40;
    const discarded = new Map();
    discarded.set("temporary", 99);
    const child = new Map();
    child.set("value", scratch.amount + 2);
    const root = new Map();
    root.set("left", child);
    root.set("right", child);
    const before = child.get("value");
    child.set("value", input);
    child.set("before", before);
    return root;
}
function observeMixed(first, second) {
    if (!first.has("left")) { return -1; }
    if (!first.has("right")) { return -2; }
    if (!second.has("right")) { return -3; }
    const before = first.get("left").get("before");
    first.get("left").set("value", 77);
    return before * 1000000 + first.get("right").get("value") * 1000 +
        second.get("right").get("value");
}
function scalarSnapshot(input) {
    const map = new Map();
    map.set("value", 10);
    map.set("value", 40);
    const before = map.get("value");
    map.set("value", input);
    return before * 100 + map.get("value");
}
function branchPrefix(choose) {
    const object = { value: 10 };
    object.value = 40;
    const before = object.value + 2;
    if (choose) { object.value = 3; }
    else { object.value = 5; }
    return before * 100 + object.value;
}
function loopPrefix(count) {
    const map = new Map();
    map.set("value", 10);
    map.set("value", 42);
    for (var i = 0; i < count; ++i) {
        map.set("value", map.get("value") + 1);
    }
    return +map.get("value");
}
function effectPrefix(input) {
    const map = new Map();
    map.set("value", 10);
    map.set("value", 40);
    const before = map.get("value");
    published = input;
    return before;
}
function mutateOnce(map, input) {
    map.set("value", map.get("value") + input);
    return map.get("value");
}
function callPrefix(input) {
    const map = new Map();
    map.set("value", 10);
    map.set("value", 40);
    const before = map.get("value");
    const after = mutateOnce(map, input);
    return before * 100 + after;
}
function keyPrefix(input) {
    const first = {};
    const second = {};
    const map = new Map();
    map.set(first, 10);
    map.set(second, 22);
    map.set(first, 20);
    const before = map.get(first);
    map.set(first, input);
    return before * 10000 + map.get(first) * 100 + map.size;
}
var mixedAliases = observeMixed(mixedSeed(5), mixedSeed(8));
var scalarFirst = scalarSnapshot(1);
var scalarSecond = scalarSnapshot(2);
var branchTrue = branchPrefix(true);
var branchFalse = branchPrefix(false);
var loopZero = loopPrefix(0);
var loopThree = loopPrefix(3);
var published = 0;
var effectFirst = effectPrefix(11);
var effectSecond = effectPrefix(12);
var callFirst = callPrefix(2);
var callSecond = callPrefix(3);
var keyFirst = keyPrefix(1);
var keySecond = keyPrefix(2);
var lifetime42 = scalarSnapshot(42);
