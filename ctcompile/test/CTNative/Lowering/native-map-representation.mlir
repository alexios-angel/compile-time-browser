// RUN: split-file %s %t
// RUN: python3 %S/check-map-representation.py --fixtures %t --work %t/run --translate ctjs-translate --opt ctjs-opt

//--- associative.js
function numericKeys() {
    var map = new Map();
    map.set(0 / 0, 2).set(-0, 3).set(1 / 0, 11).set(-1 / 0, 13).set(1, 15);
    map.set(0 / 0, 7).set(0, 9);
    return map.size * 1000 + map.get(0 / 0) * 100 + map.get(-0) * 10 +
        map.get(1 / 0) + map.get(-1 / 0) + map.get(1);
}
var sameValueZeroResult = numericKeys();

function mutation() {
    var map = new Map();
    map.set(0 / 0, 1).set(-0, 2).set(1, 3);
    var removed = map.delete(0 / 0) ? 1 : 0;
    var missing = map.has(0 / 0) ? 1 : 0;
    map.set(0 / 0, 7).set(0, 4);
    var alias = map;
    alias.clear();
    alias.set(-0, 42);
    return removed * 1000 + missing * 100 + map.get(0);
}
var mutationResult = mutation();

function leaf() {
    var map = new Map();
    map.set("a\0x", 40).set("a\0y", 2);
    return map;
}
function retained() {
    var outer = new Map();
    outer.set("child", leaf());
    var child = outer.get("child");
    outer.clear();
    return child;
}
function lifetime() {
    var map = retained();
    retained();
    return map.get("a\0x") + map.get("a\0y");
}
var lifetimeResult = lifetime();

function identities() {
    var first = {};
    var second = {};
    var map = new Map();
    map.set(first, 40).set(second, 2);
    var saved = map.get(first);
    map.delete(first);
    return saved + map.get(second);
}
var identityResult = identities();

//--- ordered.js
function insertionOrder() {
    var map = new Map();
    map.set(3, 30).set(1, 10).set(2, 20);
    var before = map.values();
    map.set(1, 11);
    map.delete(3);
    map.set(3, 31);
    var keys = map.keys();
    var after = map.values();
    map.clear();
    return before[0] * 1000000 + before[1] * 10000 + before[2] * 100 +
        keys[0] * 100 + keys[1] * 10 + keys[2] +
        after[0] + after[1] + after[2] + before.length + map.size;
}
var orderResult = insertionOrder();

function zeroKey() {
    var map = new Map();
    map.set(-0, 1).set(0, 2);
    var keys = map.keys();
    return 1 / keys[0] < 0 ? 1 : 0;
}
var zeroResult = zeroKey();

function stringOrder() {
    var map = new Map();
    map.set("z", 1).set("a", 2).set("m", 3).set("z", 4);
    map.delete("a");
    map.set("a", 5);
    var keys = Array.from(map.keys());
    map.clear();
    return +(keys[0] === "z") + +(keys[1] === "m") * 2 +
        +(keys[2] === "a") * 4 + keys.length * 10;
}
var stringOrderResult = stringOrder();

function projection() {
    var map = new Map();
    map.set("z", 30).set("a", 10);
    var values = map.values();
    return values[0] + 0;
}
var projectionResult = projection();
