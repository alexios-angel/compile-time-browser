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
var orderResult = insertionOrder();

function zeroKey() {
    var map = new Map();
    map.set(-0, 1).set(0, 2);
    var keys = Array.from(map.keys());
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
    var values = Array.from(map.values());
    return values[0] + 0;
}
var projectionResult = projection();

//--- payloads.js
function booleanPayloads() {
    var map = new Map();
    map.set(0, false).set(1, true);
    var saved = map.get(0);
    var missing = map.get(2);
    map.delete(0);
    return +(saved === false) + +(typeof missing === "undefined") * 2 +
        +(missing !== false) * 4 + +(map.get(1) === true) * 8 +
        +(typeof map.get(0) === "undefined") * 16 + +(typeof saved === "boolean") * 32;
}
var booleanResult = booleanPayloads();

function stringPayloads() {
    var map = new Map();
    map.set("saved", "a long owning payload beyond the short-string buffer: café\0tail");
    map.set("empty", "");
    var saved = map.get("saved");
    var empty = map.get("empty");
    var missing = map.get("missing");
    map.set("saved", "a different long payload that must not change the saved string");
    map.clear();
    return +(saved === "a long owning payload beyond the short-string buffer: café\0tail") +
        +(empty === "") * 2 + +(typeof missing === "undefined") * 4 +
        +(missing !== "") * 8 + +(typeof map.get("saved") === "undefined") * 16 +
        +(typeof empty === "string") * 32;
}
var stringResult = stringPayloads();

function leafStrings() {
    var map = new Map();
    map.set(false, "a returned Map owns this long string beyond its allocating frame");
    return map;
}
function nestedStrings() {
    var outer = new Map();
    outer.set("leaf", leafStrings());
    var leaf = outer.get("leaf");
    outer.clear();
    var saved = leaf.get(false);
    leaf.clear();
    leafStrings();
    return +(saved === "a returned Map owns this long string beyond its allocating frame");
}
var nestedResult = nestedStrings();

//--- string-values.js
function valueSnapshots() {
    var map = new Map();
    map.set(3, "first long string owned by the original snapshot buffer");
    map.set(1, "").set(2, "last");
    var before = Array.from(map.values());
    map.set(1, "replacement long string copied into the second snapshot buffer");
    map.delete(3);
    map.set(3, "reinserted");
    var after = Array.from(map.values());
    map.clear();
    return +(before[0] === "first long string owned by the original snapshot buffer") +
        +(before[1] === "") * 2 + +(before[2] === "last") * 4 +
        +(after[0] === "replacement long string copied into the second snapshot buffer") * 8 +
        +(after[1] === "last") * 16 + +(after[2] === "reinserted") * 32 +
        +(typeof before[3] === "undefined") * 64 + before.length * 1000 + after.length * 100;
}
var snapshotResult = valueSnapshots();

function numericKeysOfStrings() {
    var map = new Map();
    map.set(3, "three").set(1, "one");
    var keys = Array.from(map.keys());
    return keys[0] + 0;
}
var keyResult = numericKeysOfStrings();

//--- boolean-values.js
function refusedBooleanSnapshot() {
    var map = new Map();
    map.set(1, false);
    var values = Array.from(map.values());
    return +(values[0] === false);
}
var result = refusedBooleanSnapshot();

//--- mixed-values.js
function refusedMixedPayload() {
    var map = new Map();
    map.set(1, false).set(2, "");
    return map.size;
}
var result = refusedMixedPayload();
