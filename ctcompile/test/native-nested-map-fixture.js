// Nested Maps retain child identities and own their storage without cycles.
function leaf(value) {
    var map = new Map();
    map.set("value", value);
    return map;
}
function aliasMutation() {
    var inner = leaf(1);
    var outer = new Map();
    outer.set("first", inner).set("second", inner);
    var read = outer.get("first");
    read.set("value", 21);
    return inner.get("value") + outer.get("second").get("value");
}
var alias42 = aliasMutation();

function retainedChild(value) {
    var outer = new Map();
    outer.set(1, leaf(value));
    return outer.get(1);
}
function lifetime() {
    var retained = retainedChild(42);
    for (var i = 0; i < 100; i = i + 1) { retainedChild(i); }
    return retained.get("value") + 0;
}
var lifetime42 = lifetime();

function replacedEntry() {
    var outer = new Map();
    outer.set(1, leaf(41));
    var old = outer.get(1);
    outer.set(1, leaf(1));
    return old.get("value") + outer.get(1).get("value");
}
var replaced42 = replacedEntry();

function threeLevels() {
    var middle = new Map();
    var top = new Map();
    top.set(true, middle);
    var readMiddle = top.get(true);
    readMiddle.set("leaf", leaf(42));
    return readMiddle.get("leaf").get("value") + 0;
}
var depth42 = threeLevels();

function keyCarriers() {
    var inner = new Map();
    inner.set(false, 42);
    var outer = new Map();
    var key = 7;
    outer.set(key, inner);
    return outer.get(key).get(false) + 0;
}
var keys42 = keyCarriers();

function independentInstances() {
    var a = retainedChild(19);
    var b = retainedChild(23);
    return a.get("value") + b.get("value");
}
var independent42 = independentInstances();

function nestedSnapshots() {
    var outer = new Map();
    outer.set(2, leaf(20));
    outer.set(4, leaf(22));
    var keys = outer.keys();
    return outer.get(2).get("value") + outer.get(4).get("value") + keys[0] + keys[1];
}
var snapshot48 = nestedSnapshots();

function makeNestedClosure(value) {
    var outer = new Map();
    var inner = leaf(value);
    return function read() {
        outer.set("value", inner);
        return outer.get("value").get("value") + 0;
    };
}
function nestedClosureLifetime() {
    var read = makeNestedClosure(42);
    makeNestedClosure(17);
    return read();
}
var closure42 = nestedClosureLifetime();

function noReads() {
    var outer = new Map();
    outer.set(1, leaf(20));
    var before = outer.size;
    var removed = outer.delete(1);
    outer.set(2, leaf(22));
    outer.clear();
    if (removed) { return before + outer.size; }
    return 0;
}
var erased1 = noReads();
