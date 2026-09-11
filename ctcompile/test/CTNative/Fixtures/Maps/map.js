// Materialize independent arrays from Map's insertion-ordered iterators.
// Every observation is numeric for the independent native gate.
function numericKeys() {
    var map = new Map();
    map.set(0 / 0, 2);
    map.set(0 / 0, 7);
    map.set(-0, 3);
    map.set(0, 5);
    return map.size * 100 + map.get(0 / 0) * 10 + map.get(-0);
}
var numeric275 = numericKeys();

function aliases() {
    var map = new Map();
    var alias = map.set("a", 1);
    alias.set("b", 2).set("a", 4);
    return map.get("a") * 10 + alias.get("b") + map.size;
}
var alias44 = aliases();

function orderedSnapshots() {
    var map = new Map();
    map.set(3, 30).set(1, 10).set(2, 20);
    var before = Array.from(map.values());
    map.set(1, 11);
    map.delete(3);
    map.set(3, 31);
    var keys = Array.from(map.keys());
    var after = Array.from(map.values());
    map.clear();
    return before[0] * 1000000 + before[1] * 10000 + before[2] * 100 +
        keys[0] * 100 + keys[1] * 10 + keys[2] +
        after[0] + after[1] + after[2] + before.length + map.size;
}
var order30102188 = orderedSnapshots();

function fractionalSnapshots(index) {
    var map = new Map();
    map.set(1, 10).set(2, 20);
    var values = Array.from(map.values());
    var keys = Array.from(map.keys());
    // As for array literals, the reference truncates before bounds checking.
    return values[0.5] + values[-0.5] + values[index] + keys[index];
}
var fractional42 = fractionalSnapshots(1.9);

function zeroKey() {
    var map = new Map();
    map.set(-0, 1).set(0, 2);
    var keys = Array.from(map.keys());
    // The current interpreter retains the first key's sign, unlike ES Map.
    return 1 / keys[0];
}
var zeroNegativeInfinity = zeroKey();

function stringKeys() {
    var map = new Map();
    map.set("a\0x", 1).set("a\0y", 2);
    map.set("__proto__", 4).set("constructor", 8).set("", 16);
    map.set("é", 32).set("😀", 64).set("\ud800", 128);
    map.set("\ud83d" + "\ude00", 256);
    return map.get("a\0x") + map.get("a\0y") + map.get("__proto__") +
        map.get("constructor") + map.get("") + map.get("é") +
        map.get("😀") + map.get("\ud800") + map.get("\ud83d" + "\ude00") +
        map.size * 1000;
}
var strings9511 = stringKeys();

function booleanKeys() {
    var map = new Map();
    map.set(true, 4).set(false, 2).set(true, 8);
    return map.get(true) * 10 + map.get(false) + (map.has(false) ? 1 : 0);
}
var boolean83 = booleanKeys();

function deletion() {
    var map = new Map();
    map.set("key", 42);
    var present = map.has("key") ? 1 : 0;
    var deleted = map.delete("key") ? 2 : 0;
    var again = map.delete("key") ? 0 : 4;
    var gone = map.has("key") ? 0 : 8;
    return present + deleted + again + gone + map.size;
}
var deleted15 = deletion();

function missing() {
    var map = new Map();
    map.set(1, 8);
    return map.get(2) + 0;
}
var missingNaN = missing();

function storedNaN() {
    var map = new Map();
    map.set("nan", 0 / 0);
    return (map.has("nan") ? 42 : 0) + (map.has("missing") ? 0 : 1);
}
var stored43 = storedNaN();

function clearReturn() {
    var map = new Map();
    map.set(1, 8);
    return map.clear() + 0;
}
var clearNaN = clearReturn();

function clearAndReuse() {
    var map = new Map();
    map.set("old", 1);
    map.clear();
    map.set("new", 42);
    return map.get("new") + map.size + (map.has("old") ? 100 : 0);
}
var reused43 = clearAndReuse();

function loop(count) {
    var map = new Map();
    map.set("sum", 0);
    for (var i = 0; i < count; i = i + 1) {
        map.set("sum", map.get("sum") + i);
    }
    return map.get("sum") + 0;
}
var loop0 = loop(0);
var loop45 = loop(10);

function loopAllocations(count) {
    var total = 0;
    for (var i = 0; i < count; i = i + 1) {
        var map = new Map();
        map.set(i, i + 1);
        total = total + map.get(i);
    }
    return total;
}
var allocation0 = loopAllocations(0);
var allocation55 = loopAllocations(10);

function evaluationOrder() {
    var count = 0;
    function key() { count = count + 1; return "key"; }
    function value() { count = count * 10; return 42; }
    var map = new Map();
    map.set(key(), value());
    return count + map.get("key");
}
var evaluation52 = evaluationOrder();

function emptyReads() {
    var map = new Map();
    return (map.has(1) ? 100 : 0) + (map.delete(1) ? 100 : 0) + map.size;
}
var empty0 = emptyReads();

function emptyMissing() {
    var map = new Map();
    return map.get("absent") + 0;
}
var emptyNaN = emptyMissing();

function emptySnapshots() {
    var map = new Map();
    var keys = Array.from(map.keys());
    var values = Array.from(map.values());
    return keys.length + values.length;
}
var emptySnapshot0 = emptySnapshots();

function emptySize() {
    var map = new Map();
    return map.size;
}
var size0 = emptySize();
